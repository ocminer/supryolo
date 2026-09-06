# Validation

This is a development preview. External pool acceptance is a separate gate
from local correctness tests and has not yet been completed.

## Checks available in the repository

- BLAKE2b-256 known-answer vectors and B2Pool's work-root vector.
- Exact rational conversion of a decimal difficulty string to the Bitcoin
  difficulty-one target divided by difficulty.
- Pure protocol tests for staged difficulty/extranonce changes, repeated job
  IDs, clean/non-clean jobs and invalid field lengths.
- 32,768 complete GPU hashes compared with the scalar implementation across
  four variants, randomized headers and nonce ranges crossing the 32-bit
  boundary and approaching the 64-bit limit.
- GPU candidate sets compared with CPU scans, including all target limbs.
- Local TCP integration tests independently verifying submitted hashes with
  Python `hashlib`, including extranonce replacement with the same job ID.

Run:

```sh
ctest --test-dir build --output-on-failure
./build/yolo_tests 0
python3 tests/stratum_mock.py ./build/supryolo
```

Mock difficulty is deliberately easier than live-chain difficulty so the test
can verify many solutions quickly. Those submissions stay on localhost and
must never be sent to a real pool. In-flight stale results during a simulated
job transition are distinct from an invalid hash.

## Live acceptance gate

Use an explicitly configured BTCB2 payout worker. On B2Pool, GPU development
uses port 4444 and CPU development uses 5555. Record the executable revision,
GPU configuration, elapsed time, actual scanned hashes, accepted responses,
stale responses, non-stale rejects and connection gaps. Do not infer hashrate
solely from a handful of randomly timed shares.

Check both fresh work and transitions: difficulty, extranonce, clean jobs,
reused IDs and reconnects. Compare sampled submitted work with an independent
implementation and, when available, the pool's block-validation path. A pool
handshake alone is not a completed acceptance test.

## Current limits

Linux is the initial host target. CPU is a scalar reference backend, not tuned.
CUDA defaults to architecture 120; other architectures need explicit builds
and correctness/performance validation. Direct-node RPC, AMD and FPGA are not
implemented. Multi-GPU scheduling uses separate nonce partitions. Full hash tests passed on
both test GPUs, and a shared two-GPU connection passed the local mock test; sustained
multi-GPU live-pool validation is pending. There is no released binary package
or production-support claim yet.
