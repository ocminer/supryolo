# Architecture and implementation choices

## Decision

Use a small new C++20 core, with CUDA as the first compute backend. Existing
miners are references rather than the inherited application framework.
The objective is to add job sources, coins and hardware without embedding
network state inside hash kernels or board drivers.

| Reference | Useful material | Reason not to fork the whole application |
|---|---|---|
| [JayDDee/cpuminer-opt](https://github.com/JayDDee/cpuminer-opt) | Stratum, extranonce subscription, GBT, CPU SIMD organization | CPU-oriented scheduler and substantial unrelated algorithm code; BTCB2 needs new work construction anyway |
| [tpruvot/ccminer](https://github.com/tpruvot/ccminer) | Specialized Sia BLAKE2b CUDA and rotation techniques | Historical Sia protocol differs from BTCB2; old CUDA/build assumptions and shared result writes should not be inherited |
| [ocminer/suprminer-fpga](https://github.com/ocminer/suprminer-fpga) | ZTEX discovery, firmware/configuration and libusb transactions | Board-specific state and algorithms need separation from a new scheduler |
| [NebulousLabs/Sia-GPU-Miner](https://github.com/NebulousLabs/Sia-GPU-Miner) | Compact specialized 80-byte OpenCL kernel | Historical Sia RPC and single-result design do not provide the needed framework |
| [Knots consensus source](https://github.com/bitcoinknots/bitcoin/blob/v29.4.1.knots20260508/src/primitives/block.cpp) | Authoritative block-to-work transformation | A node is a correctness reference, not a miner framework |
| [CONVOY DATUM](https://github.com/CONVOYMining/datum_gateway) | Knots/Sia job construction and Stratum interoperability reference | Pool/gateway responsibilities differ from miner responsibilities |

The current first-party code implements the BLAKE2b specification independently;
it does not incorporate the legacy miners' application code. Any future reuse
must retain source notices and satisfy the specific component's license. In
particular, GPL miner code cannot simply be copied into this MIT codebase and
have its notices removed. Re-evaluate the project license before such reuse.

## Modules

- `yolo_core`: byte encoding, scalar hash oracle, target conversion, BTCB2
  work construction, scalar/AVX2 CPU backends, and the `Backend` interface.
- `yolo_protocol`: deterministic Stratum session state; no device or socket I/O.
- `yolo_cuda`: specialized BLAKE2b-256 scan and full-hash diagnostic kernels;
  compiled out for CPU-only builds.
- `yolo_network`: connection lifecycle, verified TLS, worker scheduling,
  share submission and result accounting.
- `yolo_hardware`: optional NVML telemetry and explicit controls, mapped by PCI bus.
- `yolo_ui`: independent dashboard renderer, terminal lifecycle and plain logging.
- Executable: argument parsing, device inspection and benchmarks.

The first `Work` type intentionally represents the supported 80-byte Sia work
format. Before adding a second algorithm, introduce explicit algorithm IDs,
work-size/capability negotiation and a backend factory; do not reinterpret this
buffer through undocumented casts. A future `JobSource` interface will let
Stratum and direct-node RPC feed the same scheduler and solution verifier.
These extension interfaces are design work, not claims of shipped backends.

## BTCB2 correctness contract

The supported Stratum profile uses:

1. `work_root = BLAKE2b-256(0x00 || coinb1 || extranonce1 || extranonce2)`.
2. `header = hidden_prevhash || nonce64 || ntime64 || work_root`.
3. `hash = BLAKE2b-256(header)`, compared as a big-endian integer to the share target.

The leaf is 52 bytes and the work header is 80 bytes. These fit in one BLAKE2b
compression block each. Digest length is **32 bytes in the parameter block**;
truncating BLAKE2b-512 is not equivalent. Wire byte strings are not word-swapped.
The `prevhash` field has already been transformed by the pool. Version and
nbits are not substituted into the device header.

[The B2Pool protocol](https://b2pool.io/stratum-protocol.md) specifies four-byte
extranonce1 and eight-byte extranonce2, nonce and ntime fields. Difficulty and
extranonce updates are staged for the next notify. Clean jobs invalidate older
work. A reused job ID replaces its previous generation, including when a new
extranonce arrives. Each notification gets a fresh extranonce2, preventing
nonce reuse when otherwise identical work is repeated.

The client requests `mining.extranonce.subscribe`. A pool that reports the method
unsupported may continue with its assigned extranonce. It does not negotiate
version rolling: the version is already committed inside coinb1, so rolling it
as if this were ordinary SHA256d mining is incorrect.

Knots' serialized v2 block header is 164 bytes. Direct RPC support must construct
its commitments, witness-aware coinbase/merkle data, transactions and complete
block serialization, request the `segwit` and `blake2b` GBT rules, and pass node
validation. It cannot be implemented by merely sending the 80-byte device work
header to `submitblock`.

## CUDA and optimization

Each thread evaluates a complete nonce. The 12 rounds are unrolled; zero message
words and unused final outputs can be eliminated by the compiler. Precomputation
moves three nonce-independent first-round mixing operations out of each thread.
Alternative rotation forms allow direct comparison of compiler-generated and
explicit PTX funnel shifts. Kernels return bounded candidate lists using atomics;
the host independently recomputes candidates and performs the full target test.

Tune measured throughput, not source instruction count: block size, register
pressure, launch overhead and generated SASS all matter. Current inspected
kernels do not spill registers. A short benchmark is not proof of sustained
pool performance. Uniform nonce-word and two/four-nonce thread variants are
available for measured comparison; they did not improve the default on the
initial RTX 5090. The measured winner remains the default.

## CPU and device orchestration

The x86-64 AVX2 backend evaluates four independent nonces per vector and dispatches
only after a runtime capability check. The scalar implementation stays separate
as an oracle and fallback. Full-hash diagnostics cover vector tails and nonce
boundaries; candidate filtering is followed by scalar target verification.
No global AVX2 compiler requirement is imposed on the executable.

Each CPU worker and GPU owns a distinct nonce partition. Workers share immutable
job snapshots, while the connection thread owns acknowledgements and device
attribution. GPU telemetry and terminal rendering have their own threads;
rendering does not hold the mining queue lock. The display reports network-target
block candidates, not confirmed blocks inferred from ordinary share responses.
Hardware writes occur only for explicit control flags.

## Subsequent backends

1. Extend GPU/CPU live acceptance samples and sustained measurements across more
   hardware, job changes and reconnects.
2. Measure additional CPU kernels on supported hardware; AVX2 is implemented,
   while AVX-512 and architecture-specific alternatives need separate validation.
3. Add AMD OpenCL first if the installed driver/hardware offers a reliable
   64-bit integer path; evaluate Vulkan against the same workload. Choose using
   hardware measurements rather than assuming Vulkan is faster.
4. Add FPGA transports separately: ZTEX USB/libusb, serial framed links where
   actually supported, and PCIe via the relevant board runtime or DMA driver.
   USB ZTEX access is not synonymous with a generic serial-port protocol.

FPGA hashing also requires a compatible bitstream and a defined work/result
contract: algorithm/version, nonce partitions, endian layout, job IDs, queue
limits, reset semantics and error reporting. A host USB/PCIe driver alone does
not implement BLAKE2b hardware. Board model, FPGA part, available bitstreams,
clocks and interface documentation are needed before that phase. A Sia FPGA
reference worth evaluating is [SiaFpgaMiner](https://github.com/pedrorivera/SiaFpgaMiner);
its resource/timing results cannot be assumed to apply to a different board.
