# supryolo

A modular, open-source BTCB2 BLAKE2b miner by **ocminer**.

**Development preview — not a production release.** CUDA hashing, CPU reference
hashing and BTCB2 Stratum are implemented. Local independent hash and protocol
tests pass. Live-pool share acceptance has not yet been validated.

The initial backend targets NVIDIA RTX 5090. AMD OpenCL/Vulkan, optimized CPU,
direct-node RPC solo mining and FPGA transports are planned; they are not
implemented yet. Pool solo ports already use the same Stratum client.

## Build

Requirements: Linux, CMake 3.24+, a C++20 compiler, OpenSSL development files,
and a CUDA toolkit supporting your GPU. Python 3 runs the protocol tests.
The JSON dependency is included with its license.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=120
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/yolo_tests 0
python3 tests/stratum_mock.py ./build/supryolo
```

CPU-only reference build, without a CUDA toolkit:

```sh
cmake -S . -B build-cpu -DCMAKE_BUILD_TYPE=Release -DYOLO_CUDA=OFF
cmake --build build-cpu -j
ctest --test-dir build-cpu --output-on-failure
```

## Run

Replace `YOUR_BTCB2_ADDRESS` with your payout address on the BTCB2 chain.

```sh
./build/supryolo --url stratum+tcp://de.b2pool.io:4444 \
  --user YOUR_BTCB2_ADDRESS.rig1 -d 0
```

Select both GPUs with `-d 0,1`. They share one pool connection and receive
disjoint nonce ranges. GPU indices follow CUDA's visible device ordering.
The miner does not stop other applications or alter GPU clocks.

The scalar CPU reference can mine on the low-difficulty port:

```sh
./build-cpu/supryolo --cpu --url stratum+tcp://de.b2pool.io:5555 \
  --user YOUR_BTCB2_ADDRESS.cpu1
```

B2Pool GPU pool-solo uses port **4445**, and CPU pool-solo uses **5556**.
This is pool-mediated solo mining; direct `getblocktemplate` / `submitblock`
mining is a separate planned job source.

`--seconds 120` limits a run. `--password x` is the default.
TLS is available with `stratum+tls://` or `stratum+ssl://` when the endpoint
supports it; certificate and hostname verification are enabled. B2Pool's
listed Stratum ports use plain TCP.

User agent: `supryolo/0.1.0-dev`.

## Performance

Initial **local hashing benchmark**, not a pool-estimated hashrate:
**17.31 GH/s on one RTX 5090**, measured over 30 seconds with the default
configuration (CUDA 13.3, architecture 120). Results depend on batch size and
kernel variant. Sustained live-pool measurements are pending.

```sh
./build/supryolo --benchmark -d 0 --seconds 30 \
  --block 256 --batch 67108864 --variant 3
```

`--variant 0` uses native rotations, `1` explicit PTX rotations, `2` native
rotations with precomputed nonce-independent operations, and `3` combines
precomputation with PTX. The default is `3`, 256 threads per block, and
67,108,864 hashes per batch. Larger batches may increase job-switch latency.

Each GPU candidate is checked against the full 256-bit target on the CPU
before submission. Candidate-buffer overflow is an error, never silent loss.

See [architecture and implementation choices](docs/ARCHITECTURE.md),
[validation](docs/VALIDATION.md), and [third-party notices](THIRD_PARTY.md).

## License

MIT. Copyright (c) 2026 ocminer. Dependencies retain their own notices.
