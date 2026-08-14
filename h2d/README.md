# H2D standalone

Independent Host-to-Device copy check. **Not** the L3 Orchestrator.

Does not compile or link `libhierarchical_orch`. Do not mix these numbers with `orch_main`.

## Build

```bash
cmake -B build -S .
cmake --build build --parallel 4
```

Needs `g++` (C++17), `cmake >= 3.15`. gtest optional (fetched if missing). No CANN required to build.

## Run

```bash
ctest --test-dir build --output-on-failure
./build/h2d_bench
```

Without NPU: host memcpy only; ACL case is skipped (that is a pass).

With CANN on SSH:

```bash
export ASCEND_HOME_PATH=...
./build/h2d_bench --device 0
```
