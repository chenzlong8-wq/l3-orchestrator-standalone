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

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "h2d_copy.h"

namespace {

void print_row(size_t bytes, const h2d::TimedCopy &h2h, const h2d::DeviceCopy *dev) {
    std::cout << std::setw(10) << bytes << "  " << std::setw(10) << std::fixed << std::setprecision(3)
              << h2d::ns_to_ms(h2h.ns) << "  " << std::setw(8) << std::setprecision(2) << h2d::gbps(bytes, h2h.ns);
    if (dev != nullptr && dev->error.empty()) {
        std::cout << "  " << std::setw(10) << h2d::ns_to_ms(dev->h2d.ns) << "  " << std::setw(8)
                  << h2d::gbps(bytes, dev->h2d.ns) << "  " << std::setw(10) << h2d::ns_to_ms(dev->d2h.ns) << "  "
                  << std::setw(8) << h2d::gbps(bytes, dev->d2h.ns);
    } else {
        std::cout << "  " << std::setw(10) << "-" << "  " << std::setw(8) << "-" << "  " << std::setw(10) << "-"
                  << "  " << std::setw(8) << "-";
    }
    std::cout << "\n";
}

}  // namespace

int main(int argc, char **argv) {
    int32_t device = 0;
    int repeats = 3;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--device" && i + 1 < argc) device = std::atoi(argv[++i]);
        else if (a == "--repeat" && i + 1 < argc) repeats = std::max(1, std::atoi(argv[++i]));
        else if (a == "--help") {
            std::cout << "h2d_bench [--device N] [--repeat N]\n"
                      << "  Host memcpy always. Real H2D/D2H only if libascendcl.so is loadable.\n"
                      << "  Do not compare these numbers with orch_main (schedule-only).\n";
            return 0;
        }
    }

    h2d::AclApi acl = h2d::load_acl();
    bool have_acl = acl.ready;
    std::cout << "h2d_bench: host memcpy always";
    if (have_acl) std::cout << "; ACL H2D/D2H on device " << device << "\n";
    else std::cout << "; " << acl.error << "\n";

    const std::vector<size_t> sizes = {4 * 1024, 64 * 1024, 1024 * 1024, 16 * 1024 * 1024, 64 * 1024 * 1024};

    std::cout << std::setw(10) << "bytes" << "  " << std::setw(10) << "h2h_ms" << "  " << std::setw(8) << "h2h_GBs"
              << "  " << std::setw(10) << "h2d_ms" << "  " << std::setw(8) << "h2d_GBs" << "  " << std::setw(10)
              << "d2h_ms" << "  " << std::setw(8) << "d2h_GBs" << "\n";

    int failures = 0;
    for (size_t n : sizes) {
        std::vector<uint8_t> src(n), dst(n), back(n);
        h2d::fill_pattern(src.data(), n, 0xA5);

        h2d::TimedCopy best_h2h{};
        h2d::DeviceCopy best_dev{};
        bool have_dev = false;
        for (int r = 0; r < repeats; ++r) {
            std::fill(dst.begin(), dst.end(), 0);
            auto h2h = h2d::host_memcpy(src.data(), dst.data(), n);
            if (!h2h.ok) {
                std::cerr << "h2d_bench: host memcpy mismatch at " << n << " bytes\n";
                ++failures;
                break;
            }
            if (r == 0 || h2h.ns < best_h2h.ns) best_h2h = h2h;

            if (have_acl) {
                std::fill(back.begin(), back.end(), 0);
                auto dev = h2d::acl_roundtrip(acl, device, src.data(), back.data(), n);
                if (!dev.error.empty()) {
                    std::cerr << "h2d_bench: ACL copy failed at " << n << " bytes: " << dev.error << "\n";
                    ++failures;
                    have_acl = false;
                    break;
                }
                if (!have_dev || dev.h2d.ns < best_dev.h2d.ns) best_dev = dev;
                have_dev = true;
            }
        }
        print_row(n, best_h2h, have_dev ? &best_dev : nullptr);
    }

    h2d::close_acl(acl);
    std::cout << "h2d_bench: " << (failures == 0 ? "ok" : "failed") << "  (not mixed into orch_main)\n";
    return failures == 0 ? 0 : 1;
}
