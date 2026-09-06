# Validation

Development preview, tested on Linux with RTX 5090 and Ryzen 9 3950X.
Independent hash/protocol checks and short live B2Pool acceptance runs pass.
These samples do not establish a production failure rate or block acceptance.

## Automated checks

- BLAKE2b-256 known-answer vectors and B2Pool's work-root vector.
- Exact rational difficulty conversion and protocol state transitions, including
  staged difficulty/extranonce updates, reused job IDs and clean jobs.
- 57,344 complete GPU hashes across seven kernels, randomized headers, the
  32-bit nonce boundary and the end of the 64-bit range.
- AVX2 complete hashes compared with the scalar oracle, including incomplete
  four-lane groups, nonce boundaries, candidate sets and exact target equality.
- Full 256-bit target checks, including rejection based on lower target limbs.
- Explicit GPU candidate-buffer overflow handling.
- Local Stratum verification using Python `hashlib`, with GPU, CPU and mixed
  workers, including extranonce changes with a reused job ID.
- NVML test driver covering selected-device PCI mapping, no implicit writes,
  setters/resets, unavailable sensors, permission errors and limit validation.
- TUI tests for 2 Hz alarm timing, terminal text sanitization and frame bounds;
  real PTY tests for resize, keyboard exit, SIGINT and terminal restoration.

CUDA Release and CPU-only Release builds pass their checks. CPU-only Debug
checks also pass with AddressSanitizer and UndefinedBehaviorSanitizer.
The final mixed CPU/GPU mock run independently validated 1,405 shares; one
in-flight share became stale during the deliberately forced job transition.

```sh
ctest --test-dir build --output-on-failure
./build/yolo_tests 0
python3 tests/stratum_mock.py ./build/supryolo
python3 tests/stratum_mock.py ./build/supryolo --mixed
```

The mock uses very easy targets only on localhost. Mock submissions are never
sent to a live pool. An in-flight stale is different from an invalid hash.
GPU diagnostics must be run separately on each device being qualified.
The original four CUDA kernels passed full-hash checks on both test GPUs;
the extended seven-kernel suite was run on GPU 0.

The fake NVML shared library is a test fixture, not a runtime dependency.
Do not package `build/test-libs` with a miner distribution.

## Live B2Pool results — 2026-09-06

| Run | Duration | Accepted | Rejected | Stale | Pending at exit |
|---|---:|---:|---:|---:|---:|
| Current CUDA GPU 0, port 4444 | 240 s | 8 | 0 | 0 | 0 |
| Current AVX2 CPU, 15 threads, port 5555 | 240 s | 14 | 0 | 0 | 0 |
| Earlier CUDA GPU 0, port 4444 | 180 s | 8 | 0 | 0 | 0 |
| Earlier shared two-GPU connection, port 4444 | 180 s | 9 | 0 | 0 | 0 |

All 22 submitted shares in the current GPU/CPU runs were independently
reconstructed from captured jobs and checked using Python `hashlib`; their
full hashes met the assigned targets and the pool acknowledged every share.
The earlier two-GPU sample was also independently checked and contained shares
from both devices. That run shared the GPUs with other workloads and is not
an isolated dual-GPU performance measurement.

Current GPU throughput was **17.21 GH/s** from 4,131,758,538,752 scanned hashes
over 240.051 seconds. CPU throughput in the simultaneous acceptance run was
258.7 MH/s; compilation and other workloads were running on the same host.
Use the separately labeled local CPU benchmarks in the README for their exact
thread counts and durations; neither measurement is a universal sustained rate.

Worker-specific responses are the acceptance evidence. Pool-wide or wallet-wide
statistics can include other miners and must not be attributed to these runs.
A successful handshake alone does not validate mining. No network block was
found in these samples, so confirmed block acceptance remains untested.

Real NVML writes were also exercised on the RTX 5090: a 500 W power limit,
2400 MHz locked core, 10001 MHz locked memory, and 70% fan command all succeeded.
Each test was followed by a successful reset; the initial 600 W power limit
was restored. The other GPU was not targeted by these control tests.

## Remaining coverage

Long-duration operation, pool restarts, broader hardware, and block submission
need further testing. NVML capabilities and permissions vary by card and driver.
CPU AVX-512, AMD, FPGA and direct-node RPC backends are not implemented.
CUDA defaults to architecture 120; other GPUs require an appropriate build
and their own correctness/performance validation. There is no production-support
claim or released binary package yet.
