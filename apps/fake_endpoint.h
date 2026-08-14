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

#include <atomic>
#include <deque>
#include <mutex>

#include "worker_manager.h"

// In-process WorkerEndpoint: submit_progress immediately queues COMPLETED.
// Same contract as tests/ut/cpp/hierarchical/test_scheduler.cpp FakeEndpoint.
class FakeEndpoint final : public WorkerEndpoint {
public:
    explicit FakeEndpoint(int32_t worker_id) {
        caps_.kind = WorkerEndpointKind::REMOTE_L3;
        caps_.worker_id = worker_id;
        caps_.remote = true;
        caps_.transport = "l3-orch-main";
    }

    const WorkerEndpointCaps &caps() const override { return caps_; }

    void submit_progress(Ring *, const WorkerDispatch &dispatch) override {
        std::lock_guard<std::mutex> lk(mu_);
        WorkerEndpointProgress progress;
        progress.kind = WorkerProgressKind::COMPLETED;
        progress.dispatch = dispatch;
        progress.completion.task_slot = dispatch.task_slot;
        progress.completion.group_index = dispatch.group_index;
        progress.completion.outcome = EndpointOutcome::SUCCESS;
        events_.push_back(progress);
        completed_.fetch_add(1, std::memory_order_relaxed);
    }

    bool poll_progress(WorkerEndpointProgress &progress) override {
        std::lock_guard<std::mutex> lk(mu_);
        if (events_.empty()) return false;
        progress = events_.front();
        events_.pop_front();
        return true;
    }

    bool activate_progress(RunId) override { return true; }

    int completed() const { return completed_.load(std::memory_order_relaxed); }

private:
    WorkerEndpointCaps caps_;
    std::mutex mu_;
    std::deque<WorkerEndpointProgress> events_;
    std::atomic<int> completed_{0};
};
