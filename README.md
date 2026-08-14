# L3 Orchestrator standalone package

Self-contained extract of **one** engine: the host-side L3 `Orchestrator` from simpler (`src/common/hierarchical`).

This is **not** the L2 `PTO2OrchestratorState` (host_build_graph / tensormap_and_ringbuffer).

## Requirements

- Linux (recommended) or macOS
- `g++` with C++17, `cmake >= 3.15`, `pthread`
- GoogleTest optional — CMake fetches v1.14.0 if missing
- First cmake configure needs network if gtest is not installed
- CANN / NPU **optional**. Without it, H2D tests still pass (host memcpy). With `libascendcl.so` on `LD_LIBRARY_PATH` or `ASCEND_HOME_PATH`, the same binaries also measure real H2D/D2H.

Windows cannot build this (`fork` / `shm_open`).

## Build and run

```bash
cmake -B build -S .
cmake --build build --parallel 4
```

**1. Orchestrator mocks (gold standard):**

```bash
ctest --test-dir build -j4 --output-on-failure \
      -R 'test_orchestrator|test_scheduler|test_ring|test_scope|test_tensormap'
```

**2. H2D (separate from scheduling):**

```bash
ctest --test-dir build --output-on-failure -R test_h2d
./build/h2d_bench
# on a machine with CANN:
#   export ASCEND_HOME_PATH=...
#   ./build/h2d_bench --device 0
```

`test_h2d` always checks host-memcpy integrity. The ACL case is `GTEST_SKIP` when there is no CANN — that is a pass, not a failure.

**3. Scheduler smoke (not a substitute for the tests):**

```bash
./build/orch_main
```

Expect `orch_main: ok` and `fake_completed=2`. That path has **no H2D**. Do not compare `orch_main` elapsed_ms with `h2d_bench`.

## What is in the box

| Path | Role |
| --- | --- |
| `src/common/hierarchical/` | L3 engine |
| `libhierarchical_orch.a` | Build product |
| `tests/test_orchestrator.cpp` etc. | Original-repo hierarchical gtests |
| `tests/test_h2d.cpp` | H2D integrity (memcpy always; ACL if present) |
| `apps/orch_main.cpp` | 2-node DAG + Scheduler + FakeEndpoint |
| `apps/h2d_bench.cpp` | Size sweep: h2h always; h2d/d2h when CANN loads |

## Check orch_main still has no CANN

```bash
ldd build/orch_main
# must not list libascendcl, libhcom, or libruntime
# h2d_bench also does not *link* CANN; it dlopens at runtime if present
```
