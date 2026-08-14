# L3 Orchestrator standalone

Self-contained extract of the host-side L3 `Orchestrator` (`src/common/hierarchical`).

This zip has **no H2D**. Copy measurement is a separate package: `l3-h2d-standalone.zip`.

## Build

```bash
cmake -B build -S .
cmake --build build --parallel 4
```

Linux or macOS. `g++` C++17, `cmake >= 3.15`. gtest optional (fetched if missing). No CANN.

## Run

```bash
ctest --test-dir build -j4 --output-on-failure \
      -R 'test_orchestrator|test_scheduler|test_ring|test_scope|test_tensormap'
./build/orch_main
```

Expect `orch_main: ok` and `fake_completed=2`. That path is FakeEndpoint only — no device copy.

```bash
ldd build/orch_main
# must not list libascendcl, libhcom, or libruntime
```
