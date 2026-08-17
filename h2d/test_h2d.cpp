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

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "h2d_copy.h"

namespace {

int32_t bench_device() {
    const char *env = std::getenv("ASCEND_DEVICE_ID");
    if (env == nullptr || env[0] == '\0') return 0;
    return std::atoi(env);
}

}  // namespace

TEST(H2D, HostMemcpyIntegrity) {
    const size_t sizes[] = {1, 4096, 64 * 1024, 1024 * 1024};
    for (size_t n : sizes) {
        std::vector<uint8_t> src(n), dst(n, 0);
        h2d::fill_pattern(src.data(), n, 0x3C);
        auto r = h2d::host_memcpy(src.data(), dst.data(), n);
        EXPECT_TRUE(r.ok) << "mismatch at " << n << " bytes";
        EXPECT_GE(r.ns, 0);
    }
}

TEST(H2D, HostMemcpyDoesNotAlias) {
    std::vector<uint8_t> src(256), dst(256, 0);
    h2d::fill_pattern(src.data(), src.size(), 0x11);
    ASSERT_TRUE(h2d::host_memcpy(src.data(), dst.data(), src.size()).ok);
    dst[0] ^= 0xFF;
    EXPECT_FALSE(h2d::buffers_equal(src.data(), dst.data(), src.size()));
}

TEST(H2D, AclInitAndRoundtrip) {
    h2d::AclApi acl = h2d::load_acl(bench_device());
    if (!acl.ready) {
        GTEST_SKIP() << acl.error;
    }

    const size_t sizes[] = {4096, 64 * 1024, 1024 * 1024};
    for (size_t n : sizes) {
        std::vector<uint8_t> src(n), back(n, 0);
        h2d::fill_pattern(src.data(), n, 0x5A);
        auto r = h2d::acl_roundtrip(acl, src.data(), back.data(), n);
        EXPECT_TRUE(r.error.empty()) << r.error << " size=" << n;
        EXPECT_TRUE(r.h2d.ok);
        EXPECT_TRUE(r.d2h.ok);
        EXPECT_TRUE(h2d::buffers_equal(src.data(), back.data(), n));
    }
    h2d::close_acl(acl);
}

TEST(H2D, AclDetectsCorruption) {
    h2d::AclApi acl = h2d::load_acl(bench_device());
    if (!acl.ready) {
        GTEST_SKIP() << acl.error;
    }
    const size_t n = 4096;
    std::vector<uint8_t> src(n), back(n, 0);
    h2d::fill_pattern(src.data(), n, 0x22);
    auto r = h2d::acl_roundtrip(acl, src.data(), back.data(), n);
    ASSERT_TRUE(r.error.empty()) << r.error;
    back[10] ^= 0xFF;
    EXPECT_FALSE(h2d::buffers_equal(src.data(), back.data(), n));
    h2d::close_acl(acl);
}
