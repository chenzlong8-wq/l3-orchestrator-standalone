/*
 * Copyright (c) PyPTO Contributors.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 * -----------------------------------------------------------------------------------------------------------
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>

// Host memcpy always. Real H2D/D2H via dlopen(libascendcl.so) when CANN is present.
// Never link CANN — Ubuntu / WSL without NPU must still build and run.

namespace h2d {

inline double ns_to_ms(int64_t ns) { return static_cast<double>(ns) / 1e6; }

inline double gbps(size_t bytes, int64_t ns) {
    if (ns <= 0) return 0.0;
    return (static_cast<double>(bytes) * 1e9 / static_cast<double>(ns)) / 1e9;
}

inline void fill_pattern(uint8_t *p, size_t n, uint8_t seed) {
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(seed + static_cast<uint8_t>(i));
}

inline bool buffers_equal(const uint8_t *a, const uint8_t *b, size_t n) { return std::memcmp(a, b, n) == 0; }

struct TimedCopy {
    int64_t ns{0};
    bool ok{false};
};

inline TimedCopy host_memcpy(const uint8_t *src, uint8_t *dst, size_t n) {
    const auto t0 = std::chrono::steady_clock::now();
    if (n > 0) std::memcpy(dst, src, n);
    const auto t1 = std::chrono::steady_clock::now();
    TimedCopy r;
    r.ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    r.ok = n == 0 || buffers_equal(src, dst, n);
    return r;
}

enum {
    kAclSuccess = 0,
    kAclErrorRepeatInitialize = 100002,
    kAclMemMallocHugeFirst = 0,
    kAclMemcpyHostToDevice = 1,
    kAclMemcpyDeviceToHost = 2,
};

struct AclApi {
    void *handle{nullptr};
    int (*init)(const char *){nullptr};
    int (*finalize)(){nullptr};
    int (*set_device)(int32_t){nullptr};
    int (*get_device)(int32_t *){nullptr};
    int (*reset_device)(int32_t){nullptr};
    int (*create_context)(void **, int32_t){nullptr};
    int (*destroy_context)(void *){nullptr};
    int (*set_current_context)(void *){nullptr};
    int (*malloc_dev)(void **, size_t, int){nullptr};
    int (*free_dev)(void *){nullptr};
    int (*memcpy)(void *, size_t, const void *, size_t, int){nullptr};
    int (*sync)(){nullptr};
    const char *(*recent_err)(){nullptr};
    void *context{nullptr};
    int32_t device{-1};
    bool inited{false};
    bool ready{false};
    std::string error;
};

inline std::string acl_msg(const AclApi &api, const char *what, int rc) {
    std::string s = std::string(what) + " failed rc=" + std::to_string(rc);
    if (api.recent_err != nullptr) {
        const char *e = api.recent_err();
        if (e != nullptr && e[0] != '\0') s += std::string(" ") + e;
    }
    return s;
}

inline void *try_dlopen() {
    const char *home = std::getenv("ASCEND_HOME_PATH");
    if (home != nullptr && home[0] != '\0') {
        const char *suffixes[] = {"/lib64/libascendcl.so", "/lib64/libascendcl.so.1", "/lib/libascendcl.so"};
        for (const char *suf : suffixes) {
            const std::string p = std::string(home) + suf;
            if (void *h = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL)) return h;
        }
    }
    if (void *h = dlopen("libascendcl.so", RTLD_NOW | RTLD_LOCAL)) return h;
    if (void *h = dlopen("libascendcl.so.1", RTLD_NOW | RTLD_LOCAL)) return h;
    return nullptr;
}

inline AclApi load_acl(int32_t device) {
    AclApi api;
    api.handle = try_dlopen();
    if (api.handle == nullptr) {
        api.error = "libascendcl.so not found (no CANN). Host memcpy only.";
        return api;
    }
    auto load = [&](const char *name, bool required) -> void * {
        void *s = dlsym(api.handle, name);
        if (s == nullptr && required) api.error = std::string("dlsym failed: ") + name;
        return s;
    };
    api.init = reinterpret_cast<int (*)(const char *)>(load("aclInit", true));
    api.finalize = reinterpret_cast<int (*)()>(load("aclFinalize", false));
    api.set_device = reinterpret_cast<int (*)(int32_t)>(load("aclrtSetDevice", true));
    api.get_device = reinterpret_cast<int (*)(int32_t *)>(load("aclrtGetDevice", false));
    api.reset_device = reinterpret_cast<int (*)(int32_t)>(load("aclrtResetDevice", false));
    api.create_context = reinterpret_cast<int (*)(void **, int32_t)>(load("aclrtCreateContext", true));
    api.destroy_context = reinterpret_cast<int (*)(void *)>(load("aclrtDestroyContext", false));
    api.set_current_context = reinterpret_cast<int (*)(void *)>(load("aclrtSetCurrentContext", false));
    api.malloc_dev = reinterpret_cast<int (*)(void **, size_t, int)>(load("aclrtMalloc", true));
    api.free_dev = reinterpret_cast<int (*)(void *)>(load("aclrtFree", true));
    api.memcpy = reinterpret_cast<int (*)(void *, size_t, const void *, size_t, int)>(load("aclrtMemcpy", true));
    api.sync = reinterpret_cast<int (*)()>(load("aclrtSynchronizeDevice", false));
    api.recent_err = reinterpret_cast<const char *(*)()>(load("aclGetRecentErrMsg", false));
    if (!api.error.empty()) return api;

    int rc = api.init(nullptr);
    if (rc != kAclSuccess && rc != kAclErrorRepeatInitialize) {
        api.error = acl_msg(api, "aclInit", rc);
        return api;
    }
    api.inited = rc == kAclSuccess;

    rc = api.set_device(device);
    if (rc != kAclSuccess) {
        api.error = acl_msg(api, "aclrtSetDevice", rc);
        return api;
    }
    api.device = device;

    rc = api.create_context(&api.context, device);
    if (rc != kAclSuccess) {
        api.error = acl_msg(api, "aclrtCreateContext", rc);
        return api;
    }
    if (api.set_current_context != nullptr) {
        rc = api.set_current_context(api.context);
        if (rc != kAclSuccess) {
            api.error = acl_msg(api, "aclrtSetCurrentContext", rc);
            return api;
        }
    }
    api.ready = true;
    return api;
}

inline void close_acl(AclApi &api) {
    if (api.context != nullptr && api.destroy_context != nullptr) api.destroy_context(api.context);
    if (api.device >= 0 && api.reset_device != nullptr) api.reset_device(api.device);
    if (api.inited && api.finalize != nullptr) api.finalize();
    if (api.handle != nullptr) dlclose(api.handle);
    api = {};
}

struct DeviceCopy {
    TimedCopy h2d;
    TimedCopy d2h;
    std::string error;
};

inline DeviceCopy acl_roundtrip(AclApi &api, const uint8_t *src, uint8_t *back, size_t n) {
    DeviceCopy out;
    if (!api.ready) {
        out.error = api.error.empty() ? "ACL not ready" : api.error;
        return out;
    }
    void *dev = nullptr;
    int rc = api.malloc_dev(&dev, n == 0 ? 1 : n, kAclMemMallocHugeFirst);
    if (rc != kAclSuccess) {
        out.error = acl_msg(api, "aclrtMalloc", rc);
        return out;
    }
    {
        const auto t0 = std::chrono::steady_clock::now();
        rc = api.memcpy(dev, n, src, n, kAclMemcpyHostToDevice);
        if (api.sync) api.sync();
        const auto t1 = std::chrono::steady_clock::now();
        out.h2d.ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        out.h2d.ok = rc == kAclSuccess;
        if (rc != kAclSuccess) out.error = acl_msg(api, "aclrtMemcpy H2D", rc);
    }
    if (out.error.empty()) {
        const auto t0 = std::chrono::steady_clock::now();
        rc = api.memcpy(back, n, dev, n, kAclMemcpyDeviceToHost);
        if (api.sync) api.sync();
        const auto t1 = std::chrono::steady_clock::now();
        out.d2h.ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        out.d2h.ok = rc == kAclSuccess && (n == 0 || buffers_equal(src, back, n));
        if (rc != kAclSuccess) out.error = acl_msg(api, "aclrtMemcpy D2H", rc);
        else if (!out.d2h.ok) out.error = "D2H payload mismatch";
    }
    api.free_dev(dev);
    return out;
}

}  // namespace h2d
