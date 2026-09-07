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
- 131,520 complete OpenCL hashes across four AMD cards and four kernel
  variants, including nonce boundaries, tails, candidate sets and overflow.
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

## AMD live acceptance

The combined CUDA/OpenCL/CPU executable ran unchanged on both AMD-only rigs.
The initial 90-second acceptance runs used B2Pool port 5555 to collect enough
shares from every card and exercise difficulty changes:

| Rig configuration | Accepted | Rejected | Stale | Pending |
|---|---:|---:|---:|---:|
| RX 7600 XT + two Vega 20 cards | 127 | 0 | 0 | 0 |
| RX 7900 XTX | 124 | 0 | 0 | 0 |

Independent Python `hashlib` replay verified every submitted hash and assigned
target. The three-card connection contributed 32, 46 and 49 shares from its
respective device nonce partitions. Assigned difficulty changed 1 → 32 → 1
and 1 → 16 → 1 in these runs without invalid submissions.

Combined Release, OpenCL-only Release and CPU-only Release configurations are
covered by CTest; sanitizer checks also cover the host code. Local OpenCL
Stratum regression on NVIDIA validated 1,278 shares with one deliberately
in-flight stale; the unchanged CUDA path validated 1,395 with one such stale.
AMD control fixtures cover prevalidation, units, resets and restoring the fan
policy after a failed PWM write. Real RDNA power/clock writes and resets passed;
manual fan writes are unsupported on the tested drivers, as documented in
[operations](OPERATIONS.md).

The subsequent runs on the regular GPU port (4444, difficulty 128) produced:

| Rig configuration | Duration | Scanned rate | Accepted | Rejected / stale / pending |
|---|---:|---:|---:|---:|
| RX 7900 XTX | 300.100 s | 5.783 GH/s | 4 | 0 / 0 / 0 |
| RX 7600 XT + two Vega 20 cards | 282.327 s | 6.450 GH/s combined | 3 | 0 / 0 / 0 |

The three-card run was stopped early when one Vega 20 reached 85°C and its
rate dropped to about 1.9 GH/s. This is a cooling/performance limitation of the
observed run, not a rejected-hash result. Its shorter 2.75 GH/s benchmark
must not be presented as a sustained rate. No clock, fan or power changes
were applied during either live mining run.

Across both ports, **258/258 AMD shares were accepted and independently
verified**, with no rejects, stale responses or unanswered submissions. Every
card submitted accepted shares in the low-difficulty run. No network block
was found, so block acceptance remains outside these observations.

## Remaining coverage

Long-duration operation, pool restarts, broader hardware, and block submission
need further testing. NVML capabilities and permissions vary by card and driver.
CPU AVX-512, FPGA and direct-node RPC backends are not implemented. AMD support
is validated on the listed rigs; broader hardware and driver coverage is pending.
CUDA defaults to architecture 120; other GPUs require an appropriate build
and their own correctness/performance validation. There is no production-support
claim or released binary package yet.

## v0.1.0 release packaging checks

The Linux release build uses CUDA 12.8.1 and an Ubuntu 22.04 baseline, with
static OpenSSL and GCC runtimes. Eight CTest checks pass, including loopback
HTTP API bounds, read-only routes, share accounting and HiveOS/mmpOS adapter
fixtures. Launcher tests cover placeholder refusal, device selection, CPU
pool selection and treating configuration as data rather than shell code.

The release executable passed 57,344 CUDA full-hash comparisons on RTX 5090
and 8,220 OpenCL comparisons on RX 7900 XTX. New live B2Pool samples accepted
5/5 NVIDIA shares (90 seconds, GPU port) and 90/90 AMD shares (60 seconds,
low-difficulty port), with no rejected, stale or pending shares. Independent
replay verified every submitted hash. The NVIDIA live rate was 17.22 GH/s.
The Docker image also passed a local Stratum test with independently checked
GPU shares and a GPU benchmark around 17.35 GH/s. Short benchmarks do not
replace sustained thermal measurements.

HiveOS/mmpOS callbacks are tested against their documented interface formats;
no complete deployment on those installed operating systems has been observed.
Windows CI provides native build, CPU protocol and monitoring checks, but no
physical Windows GPU validation. These limits are stated in the download guide.

## Version 0.2.0

The packaged Linux build passed 12 CTest cases and the CUDA full-hash oracle
(57,344 hashes). Live SV2 checks on that build accepted 7/7 RTX 5090 shares,
7/7 AVX2 CPU shares and 4/4 RX 7900 XTX shares, with no rejects, stale shares or
pending replies at completion. The shared scheduler allocates disjoint ranges
across threads. Noise authentication, job replacement and batched reply handling
also have local protocol tests. These are share acceptance results; no network
block was found during the tests.

Windows uses OpenCL and CPU; physical Windows GPU validation and installed
HiveOS/mmpOS deployments remain outside these checks. The rig integrations are
covered by launcher/API fixtures. SV2 uses Standard Channels only.

Post-publication SV2 solo checks on 2026-09-07 used the unchanged release
binary. After the pool update, all three regions accepted GPU and CPU work:

| Region | GPU accepted | CPU accepted | Rejected | Unconfirmed at shutdown |
|---|---:|---:|---:|---:|
| DE | 3 | 4 | 0 | 0 |
| HEL | 6 | 6 | 0 | 0 |
| ORD | 4 | 5 | 0 | 1 (GPU) |

DE GPU ran for 120 seconds; the other runs mined for 150 seconds. The miner
waits up to five additional seconds for outstanding replies. The unconfirmed
ORD share is not counted as accepted. All regions passed authenticated
connections on the six shared/solo ports. These short checks did not find a
network block. Earlier Helsinki rejections occurred before the pool update;
the miner binary was unchanged for the successful retests.
