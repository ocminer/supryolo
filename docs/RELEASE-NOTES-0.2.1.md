supryolo v0.2.1 supports BTCB2 mining through Stratum V1, authenticated Stratum V2 and B2Pool's hosted DATUM gateway. SV2 block-candidate tracking is corrected.

- **DATUM GPU:** `stratum+tcp://de.b2pool.io:24444`
- **DATUM CPU:** `stratum+tcp://de.b2pool.io:25555`
- **SV2 GPU:** `stratum2+tcp://de.b2pool.io:14444`
- **SV2 CPU:** `stratum2+tcp://de.b2pool.io:15555`

Edit your address in `start.sh`, then use `PROTOCOL=datum ./start.sh` or `PROTOCOL=sv2 ./start.sh`. Add `MODE=cpu` before `./start.sh` for CPU-only mining. SV2 uses the pool authority key shown in the README. Hosted DATUM requires no local node and currently offers DE shared mining only. SV2 shared and solo endpoints are documented for DE, HEL and ORD.

[Connection and device examples](https://github.com/ocminer/supryolo/blob/v0.2.1/README.md) · [Installation guide](https://github.com/ocminer/supryolo/blob/v0.2.1/docs/RELEASES.md)

Packages: Linux NVIDIA/AMD/CPU, Windows OpenCL/CPU, HiveOS, mmpOS and Docker. Docker supports NVIDIA/CPU; use native Linux for AMD. Image: `ocminersupr/supryolo:0.2.1`.

Linux requires x86-64 and glibc 2.35+. Physical Windows GPUs and complete installed HiveOS/mmpOS deployments have not been validated. Job Declaration and direct-node RPC mining remain deferred. A block-candidate message or accepted share is not a confirmed block reward.

User agent: **`supryolo/0.2.1`**. Verify downloads against `SHA256SUMS`. Private mining with profits is permitted; commercial use requires prior written permission from ocminer. Dependencies retain their original license terms.
