# supryolo

A modular, open-source BTCB2 BLAKE2b miner by **ocminer**.

**Development preview.** NVIDIA CUDA, AMD OpenCL, runtime-dispatched AVX2 CPU mining,
BTCB2 Stratum, GPU monitoring/control and a Matrix-style terminal dashboard
are implemented. GPU and CPU shares have been accepted by B2Pool and checked
independently. This is not yet a production release or a complete HiveOS package.

The same binary runs on NVIDIA and AMD rigs. OpenCL hashing and live shares
have been validated on RX 7600 XT, RX 7900 XTX and Vega 20 hardware.
Direct-node RPC solo mining and FPGA backends are planned.
Pool solo ports already use the same Stratum client.

![Matrix terminal demo; all displayed mining values are simulated](docs/assets/tui-demo.png)

## Build

Requirements: Linux, CMake 3.24+, a C++20 compiler, OpenSSL development files,
a CUDA toolkit for NVIDIA builds, and OpenCL headers for OpenCL builds. Python 3 runs integration tests.
OpenCL headers (`CL/cl.h` and `CL/cl_ext.h`) are needed when `YOLO_OPENCL=ON`
(the default). OpenCL is loaded dynamically; its kernels are embedded in the
executable. The JSON dependency is included with its license. NVIDIA telemetry uses
the driver's NVML library at runtime; no separate NVML development package is needed.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=120
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/yolo_tests 0
python3 tests/stratum_mock.py ./build/supryolo
```

CPU-only build, without a CUDA toolkit:

```sh
cmake -S . -B build-cpu -DCMAKE_BUILD_TYPE=Release -DYOLO_CUDA=OFF -DYOLO_OPENCL=OFF
cmake --build build-cpu -j
ctest --test-dir build-cpu --output-on-failure
```

An OpenCL/CPU build without the CUDA toolkit uses the same executable name:

```sh
cmake -S . -B build-opencl -DCMAKE_BUILD_TYPE=Release -DYOLO_CUDA=OFF
cmake --build build-opencl -j
```

The combined build can contain CUDA, OpenCL and CPU support together. Automatic
selection uses CUDA for NVIDIA and OpenCL for AMD. `--gpu-backend cuda` or
`--gpu-backend opencl` restricts discovery to one API; the latter can also be
used for OpenCL diagnostics on NVIDIA. Use `--list-devices` with the same
backend option to see the corresponding device indices.

## Run

Replace `YOUR_BTCB2_ADDRESS` with your BTCB2 payout address. GPU 0 only:

```sh
./build/supryolo --url stratum+tcp://de.b2pool.io:4444 \
  --user YOUR_BTCB2_ADDRESS.rig1 --gpu-device 0 --no-cpu
```

`--gpu-device 0,1` selects GPUs 0 and 1; omitted means **all visible GPUs**.
CPU mining is also enabled by default. `--no-cpu` disables CPU hashing;
`--no-gpu` disables GPUs. Giving both flags reports that no devices are enabled.
Selected devices share a connection with separate nonce ranges.

CPU-only mining on the low-difficulty port:

```sh
./build/supryolo --no-gpu --cpu-threads 15 \
  --url stratum+tcp://de.b2pool.io:5555 --user YOUR_BTCB2_ADDRESS.cpu1
```

AVX2 is selected automatically when available; other CPUs use the scalar kernel.
Choose a thread count suitable for your CPU, or omit it for automatic selection.
B2Pool GPU pool-solo uses **4445**, CPU pool-solo **5556**. Direct-node RPC
`getblocktemplate` / `submitblock` is a separate planned job source.

The TUI starts in an interactive terminal. Use `--no-tui` for plain output,
`--list-devices` to inspect hardware, or `--tui-demo` for a display-only preview.
GPU controls include `--gpu-core-clock`, `--gpu-mem-clock`, `--powerlimit` and
`--gpu-fan-speed`. Settings change only when explicitly supplied.
Read [device selection, controls and TUI operation](docs/OPERATIONS.md) for
units, per-device lists, reset behavior and temperature alarms.

`--seconds 120` limits a run; `--password x` is the default. TLS is available
with `stratum+tls://` or `stratum+ssl://` when supported by the endpoint, with
certificate and hostname verification. B2Pool's listed ports use plain TCP.

User agent: `supryolo/0.1.0-dev`.

## Performance

Measured on the supported hardware; these are scanned hashes per elapsed
second, not estimates from the arrival times of a few shares.

| Device | Configuration | Measured rate | Measurement |
|---|---|---|---|
| RTX 5090 32 GB | Default CUDA settings | 17.21 GH/s | Live B2Pool, 240 seconds |
| RX 7900 XTX 24 GB | Default OpenCL settings | 5.783 GH/s | Live B2Pool, 300 seconds |
| RX 7600 XT 16 GB | OpenCL, variant 3, group 64 | 2.09 GH/s | Local benchmark, 4 seconds |
| Instinct MI50/MI60, Vega 20 16 GB | OpenCL, variant 3, group 64 | 2.75 GH/s | Local benchmark, 4 seconds |
| Ryzen 9 3950X | AVX2, 15 threads | 366.7 MH/s | Local benchmark, 10 seconds |
| Ryzen 9 3950X | AVX2, 1 thread | 27.1 MH/s | Local benchmark, 5 seconds |
| Ryzen 9 3950X | Scalar, 1 thread | 4.0 MH/s | Local benchmark, 5 seconds |

The RX 7600 XT and Vega 20 local results are short tuning samples, not sustained
guarantees. One Vega 20 card reached 85°C and about 1.9 GH/s in the multi-card
run, which was stopped early; cooling needs attention for sustained operation. AMD driver versions
3581.0 (Navi 31) and 3649.0 (Navi 33/Vega 20) were used.
CPU thread scaling depends on other workloads and cooling. The RTX 5090 live rate
was measured on one card; it is not an isolated dual-GPU result. CUDA 13.3,
architecture 120 was used. The local GPU benchmark measured 17.31 GH/s over
30 seconds. See [validation and live acceptance](docs/VALIDATION.md).

```sh
./build/supryolo --benchmark --no-cpu --gpu-device 0 --seconds 30
./build/supryolo --benchmark --no-gpu --cpu-threads 15 --seconds 30
```

Default CUDA tuning is `--variant 3 --block 256 --batch 67108864`.
Automatic OpenCL tuning uses variant 3 on AMD drivers exposing
`cl_amd_media_ops`, otherwise the native variant 0, with 64 threads per
workgroup. `--block 0` selects these per-backend defaults.
Use `--variant` for CUDA or `--opencl-variant` for OpenCL; see `--help`.
Larger batches can increase job-switch latency. Every GPU/AVX2 candidate is
independently rehashed and checked against the full 256-bit target before
submission. GPU candidate-buffer overflow is an explicit error.

See [architecture](docs/ARCHITECTURE.md) and [third-party notices](THIRD_PARTY.md).

## License

MIT. Copyright (c) 2026 ocminer. Dependencies retain their own notices.
