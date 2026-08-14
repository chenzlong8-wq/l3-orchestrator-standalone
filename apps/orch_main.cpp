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

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "call_config.h"
#include "fake_endpoint.h"
#include "orchestrator.h"
#include "ring.h"
#include "scheduler.h"
#include "scope.h"
#include "task_args.h"
#include "tensormap.h"
#include "types.h"
#include "worker_manager.h"

namespace {

CallableIdentity callable(uint8_t seed) {
    CallableIdentity id;
    id.digest.fill(seed);
    return id;
}

CanonicalIdentity identity_for(uint64_t buffer_id) {
    CanonicalIdentity id{};
    id.buffer_id = buffer_id;
    return id;
}

TaskArgs single_tensor_args(uint64_t buffer_id, TensorArgType tag) {
    TaskArgs args;
    Tensor tensor{};
    tensor.buffer.backend_kind = static_cast<uint8_t>(BackendKind::POSIX_SHM);
    tensor.buffer.access = static_cast<uint8_t>(AccessMode::READWRITE);
    tensor.buffer.nbytes = 1;
    tensor.buffer.identity = identity_for(buffer_id);
    tensor.ndims = 1;
    tensor.shapes[0] = 1;
    tensor.strides[0] = 1;
    tensor.dtype = DataType::UINT8;
    args.add_tensor(tensor, tag);
    return args;
}

}  // namespace

int main() {
    TensorMap tensormap;
    Ring allocator;
    Scope scope;
    ReadyQueue ready_sub;
    NextLevelReadyQueues ready_next;
    WorkerManager manager;
    Orchestrator orch;
    Scheduler sched;
    CallConfig cfg;

    allocator.init(/*heap_bytes=*/1ULL << 20);

    auto endpoint = std::make_unique<FakeEndpoint>(0);
    FakeEndpoint *endpoint_ptr = endpoint.get();
    manager.add_next_level_endpoint(std::move(endpoint));
    manager.start(
        &allocator,
        [&sched](WorkerCompletion completion) { sched.worker_done(std::move(completion)); },
        [&orch](WorkerDispatch dispatch) { orch.mark_task_accepted(dispatch.task_slot); }
    );
    ready_next.reset(manager.next_level_worker_ids());

    orch.init(&tensormap, &allocator, &scope, &ready_sub, &ready_next, &manager, [&sched] {
        sched.notify_ready();
    });

    Scheduler::Config sched_cfg;
    sched_cfg.ring = &allocator;
    sched_cfg.ready_sub_queue = &ready_sub;
    sched_cfg.ready_next_level_queues = &ready_next;
    sched_cfg.manager = &manager;
    sched_cfg.enqueue_ready_cb = [&orch](TaskSlot slot) { orch.enqueue_ready(slot); };
    sched_cfg.active_run_cb = [&orch] { return orch.dispatchable_run_id(); };
    sched_cfg.on_consumed_cb = [&orch](TaskSlot slot) { orch.on_consumed(slot); };
    sched_cfg.on_task_failed_cb = [&orch](TaskSlot slot, const std::string &message) {
        orch.report_task_error(slot, message);
    };
    sched.start(sched_cfg);

    const auto t0 = std::chrono::steady_clock::now();
    int exit_code = 0;
    try {
        RunId run = orch.begin_run();
        orch.scope_begin();
        // Producer then consumer on the same TensorMap key: two-node DAG.
        (void)orch.submit_next_level(
            callable(1), single_tensor_args(0xBEEF, TensorArgType::OUTPUT), cfg, /*worker_id=*/0
        );
        (void)orch.submit_next_level(
            callable(2), single_tensor_args(0xBEEF, TensorArgType::INPUT), cfg, /*worker_id=*/0
        );
        orch.scope_end();
        orch.close_run_submission(run);

        if (!orch.wait_run_for(run, /*timeout_seconds=*/5.0)) {
            throw std::runtime_error("wait_run timed out (scheduler did not drain the DAG)");
        }
        if (orch.run_failed(run)) {
            throw std::runtime_error("run failed");
        }
        if (endpoint_ptr->completed() != 2) {
            throw std::runtime_error(
                "expected FakeEndpoint to complete 2 tasks, got " + std::to_string(endpoint_ptr->completed())
            );
        }

        const auto ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::cout << "orch_main: ok  run_id=" << run << "  fake_completed=" << endpoint_ptr->completed()
                  << "  elapsed_ms=" << ms << "\n";
        std::cout << "orch_main: this is schedule-only (FakeEndpoint). H2D is a separate bench.\n";
    } catch (const std::exception &e) {
        std::cerr << "orch_main: failed: " << e.what() << "\n";
        exit_code = 1;
    }

    sched.stop();
    manager.stop();
    allocator.shutdown();
    return exit_code;
}
