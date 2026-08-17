# H2D standalone

Self-contained Host-to-Device copy check. **Not** the L3 Orchestrator. Five sources plus this README.

| File | Role |
| --- | --- |
| `h2d_copy.h` | memcpy + `dlopen` ACL (`Init` / `SetDevice` / `CreateContext` / `Malloc` / `Memcpy` / teardown) |
| `h2d_bench.cpp` | size sweep |
| `test_h2d.cpp` | integrity tests |
| `CMakeLists.txt` | independent project, does not link `libhierarchical_orch` |

## Build

```bash
cmake -B build -S .
cmake --build build --parallel 4
```

Needs `g++` (C++17), `cmake >= 3.15`. gtest is fetched if missing (needs network once).

## Run

```bash
ctest --test-dir build --output-on-failure
./build/h2d_bench
```

Without CANN: host-memcpy tests run; ACL tests `GTEST_SKIP` (ctest still passes).  
With CANN on SSH:

```bash
export ASCEND_HOME_PATH=...
export LD_LIBRARY_PATH=$ASCEND_HOME_PATH/lib64:${LD_LIBRARY_PATH:-}
export ASCEND_DEVICE_ID=0
ctest --test-dir build --output-on-failure
./build/h2d_bench --device 0
```

ACL tests then do real H2D + D2H and check the payload.
