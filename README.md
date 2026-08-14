# L3 Orchestrator standalone package

Self-contained extract of **one** engine: the host-side L3 `Orchestrator` from simpler (`src/common/hierarchical`).

This is **not** the L2 `PTO2OrchestratorState` (host_build_graph / tensormap_and_ringbuffer). No CANN, no Ascend SDK, no NPU.

## Requirements

- Linux (recommended) or macOS
- `g++` with C++17, `cmake >= 3.15`, `pthread`
- GoogleTest optional — CMake fetches v1.14.0 if missing
- First cmake configure needs network if gtest is not installed

Windows cannot build this (`fork` / `shm_open`).

## Build and run

```bash
cmake -B build -S .
cmake --build build --parallel 4
```

**Gold standard (do this first):**

```bash
ctest --test-dir build -j4 --output-on-failure \
      -R 'test_orchestrator|test_scheduler|test_ring|test_scope|test_tensormap'
```

**Smoke only (not a substitute for the tests):**

```bash
./build/orch_main
```

Expect `orch_main: ok` and `fake_completed=2`. That path uses an in-process `FakeEndpoint` — no device, no H2D.

## What is in the box

| Path | Role |
| --- | --- |
| `src/common/hierarchical/` | L3 engine (Orchestrator / Scheduler / WorkerManager / Ring / TensorMap / Scope) |
| `src/common/task_interface/` | POD wire types used by submit |
| `src/common/worker/` | Link-time deps of the 13-TU set (`chip_worker` is compiled, not exercised by `orch_main`) |
| `libhierarchical_orch.a` | Build product |
| `tests/` | Original-repo hierarchical gtests |
| `apps/orch_main.cpp` | 2-node DAG + Scheduler + FakeEndpoint |

## Out of scope

- Host-to-device (H2D) copy benchmark — do not mix into `orch_main` timing
- L2 device-side / host-side `pto_orchestrator`

## Check there is no CANN

```bash
ldd build/orch_main
# must not list libascendcl, libhcom, or libruntime
```
