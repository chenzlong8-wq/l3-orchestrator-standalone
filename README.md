# L3 extract (two packages)

Two independent projects. Download the zip you need.

| Zip | What |
| --- | --- |
| [l3-orchestrator-standalone.zip](https://github.com/chenzlong8-wq/l3-orchestrator-standalone/releases/download/v0.4.0/l3-orchestrator-standalone.zip) | L3 lib + original-repo mocks + `orch_main` |
| [l3-h2d-standalone.zip](https://github.com/chenzlong8-wq/l3-orchestrator-standalone/releases/download/v0.4.0/l3-h2d-standalone.zip) | `h2d_bench` + `test_h2d` only |

Or clone and build each directory by itself:

```bash
cmake -B orchestrator/build -S orchestrator && cmake --build orchestrator/build --parallel 4
cmake -B h2d/build -S h2d && cmake --build h2d/build --parallel 4
```

They do not share a CMake target. H2D does not link the orchestrator library.
