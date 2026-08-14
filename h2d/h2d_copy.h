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
#include <stdexcept>
#include <string>
#include <vector>

// Host memcpy always. Real H2D/D2H via dlopen(libascendcl.so) when CANN is present.
// Never link CANN — local Ubuntu / WSL must build and run without it.

namespace h2d {

inline double ns_to_ms(int64_t ns) { return static_cast<double>(ns) / 1e6; }

inline double gbps(size_t bytes, int64_t ns) {
    if (ns <= 0) return 0.0;
    return (static_cast<double>(bytes) * 1e9 / static_cast<double>(ns)) / 1e9;
}

inline void fill_pattern(uint8_t *p, size_t n, uint8_t seed) {
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(seed + i);
}

inline bool buffers_equal(const uint8_t *a, const uint8_t *b, size_t n) { return std::memcmp(a, b, n) == 0; }

struct TimedCopy {
    int64_t ns{0};
    bool ok{false};
};

inline TimedCopy host_memcpy(const uint8_t *src, uint8_t *dst, size_t n) {
    const auto t0 = std::chrono::steady_clock::now();
    std::memcpy(dst, src, n);
    const auto t1 = std::chrono::steady_clock::now();
    TimedCopy r;
    r.ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    r.ok = buffers_equal(src, dst, n);
    return r;
}

// Minimal ACL surface. Values match CANN aclrt headers.
enum {
    kAclMemMallocHugeFirst = 0,
    kAclMemcpyHostToDevice = 1,
    kAclMemcpyDeviceToHost = 2,
};

struct AclApi {
    void *handle{nullptr};
    int (*init)(const char *){nullptr};
    int (*set_device)(int32_t){nullptr};
    int (*malloc)(void **, size_t, int){nullptr};
    int (*free)(void *){nullptr};
    int (*memcpy)(void *, size_t, const void *, size_t, int){nullptr};
    int (*sync)(){nullptr};
    int (*finalize)(){nullptr};
    std::string error;
    bool ready{false};
};

inline void *try_dlopen() {
    const char *home = std::getenv("ASCEND_HOME_PATH");
    if (home != nullptr && home[0] != '\0') {
        const std::string p = std::string(home) + "/lib64/libascendcl.so";
        if (void *h = dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL)) return h;
    }
    if (void *h = dlopen("libascendcl.so", RTLD_NOW | RTLD_LOCAL)) return h;
    return nullptr;
}

inline AclApi load_acl() {
    AclApi api;
    api.handle = try_dlopen();
    if (api.handle == nullptr) {
        api.error = "libascendcl.so not found (no CANN). Host memcpy only.";
        return api;
    }
    auto load = [&](const char *name) -> void * {
        void *s = dlsym(api.handle, name);
        if (s == nullptr) api.error = std::string("dlsym failed: ") + name;
        return s;
    };
    api.init = reinterpret_cast<int (*)(const char *)>(load("aclInit"));
    api.set_device = reinterpret_cast<int (*)(int32_t)>(load("aclrtSetDevice"));
    api.malloc = reinterpret_cast<int (*)(void **, size_t, int)>(load("aclrtMalloc"));
    api.free = reinterpret_cast<int (*)(void *)>(load("aclrtFree"));
    api.memcpy = reinterpret_cast<int (*)(void *, size_t, const void *, size_t, int)>(load("aclrtMemcpy"));
    api.sync = reinterpret_cast<int (*)()>(load("aclrtSynchronizeDevice"));
    api.finalize = reinterpret_cast<int (*)()>(load("aclFinalize"));
    if (!api.error.empty()) return api;
    if (api.init(nullptr) != 0) {
        api.error = "aclInit failed";
        return api;
    }
    api.ready = true;
    return api;
}

inline void close_acl(AclApi &api) {
    if (api.ready && api.finalize) api.finalize();
    if (api.handle) dlclose(api.handle);
    api = {};
}

struct DeviceCopy {
    TimedCopy h2d;
    TimedCopy d2h;
    std::string error;
};

inline DeviceCopy acl_roundtrip(AclApi &api, int32_t device, const uint8_t *src, uint8_t *back, size_t n) {
    DeviceCopy out;
    if (!api.ready) {
        out.error = api.error.empty() ? "ACL not ready" : api.error;
        return out;
    }
    if (api.set_device(device) != 0) {
        out.error = "aclrtSetDevice failed";
        return out;
    }
    void *dev = nullptr;
    if (api.malloc(&dev, n, kAclMemMallocHugeFirst) != 0) {
        out.error = "aclrtMalloc failed";
        return out;
    }
    {
        const auto t0 = std::chrono::steady_clock::now();
        const int rc = api.memcpy(dev, n, src, n, kAclMemcpyHostToDevice);
        if (api.sync) api.sync();
        const auto t1 = std::chrono::steady_clock::now();
        out.h2d.ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        out.h2d.ok = rc == 0;
        if (rc != 0) out.error = "aclrtMemcpy H2D failed";
    }
    if (out.error.empty()) {
        const auto t0 = std::chrono::steady_clock::now();
        const int rc = api.memcpy(back, n, dev, n, kAclMemcpyDeviceToHost);
        if (api.sync) api.sync();
        const auto t1 = std::chrono::steady_clock::now();
        out.d2h.ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        out.d2h.ok = rc == 0 && buffers_equal(src, back, n);
        if (rc != 0) out.error = "aclrtMemcpy D2H failed";
        else if (!out.d2h.ok) out.error = "D2H payload mismatch";
    }
    api.free(dev);
    return out;
}

}  // namespace h2d
