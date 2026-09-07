Authenticated BTCB2 Stratum V2 mining is now available alongside Stratum V1.

- Linux: NVIDIA CUDA, AMD OpenCL and AVX2/scalar CPU in one executable.
- Windows: OpenCL/CPU package built and tested by GitHub Actions.
- HiveOS and mmpOS packages include the monitoring API and launchers.
- Docker: `ocminersupr/supryolo:0.2.0`, plus an offline image archive.
- The same device selection, GPU controls and Matrix terminal work with SV2.

**Start with SV2:** edit the address in `start.sh`, then run `PROTOCOL=sv2 ./start.sh`. For CPU-only mining use `PROTOCOL=sv2 MODE=cpu ./start.sh --cpu-threads 8`.

GPU endpoint: `stratum2+tcp://de.b2pool.io:14444`. CPU endpoint: `stratum2+tcp://de.b2pool.io:15555`. Direct commands require `--sv2-authority` with the pool's key; see the examples below. The regional endpoints use the same pin.

[Installation and platform requirements](https://github.com/ocminer/supryolo/blob/main/docs/RELEASES.md) · [SV2 commands and pool key](https://github.com/ocminer/supryolo/blob/main/README.md#stratum-v2-encrypted-mining)

**SV2 solo update (2026-09-07):** GPU and CPU solo share acceptance now works on DE, HEL and ORD with the unchanged v0.2.0 binary following the pool update. Use port **14445 for GPUs** or **15556 for CPUs**, with the same authority key. Follow-up checks recorded **28 accepted shares, zero rejects**, and one additional ORD GPU share unconfirmed at test shutdown. Bundled documentation contains the earlier, conservative solo status; the linked documentation is current.

Measured live rates: **RTX 5090 17.14 GH/s**, **RX 7900 XTX 5.713 GH/s**. The final Linux build passed 12 tests, the GPU hash oracle and 18/18 live NVIDIA/AMD/CPU shares. The Docker image accepted another 2/2 shares. Windows passed its native tests and standalone executable checks.

Linux requires x86-64/glibc 2.35+ and an appropriate GPU driver. Docker supports NVIDIA/CPU; use native Linux for AMD. Physical Windows GPUs and complete installed HiveOS/mmpOS deployments have not been validated. SV2 uses Standard Channels; Extended Channels and Job Declaration are not included. Accepted shares are not confirmed network blocks.

User agent: **`supryolo/0.2.0`**. Private mining with profits is permitted; commercial use requires prior written permission from ocminer. See LICENSE; dependencies retain their original terms.

Verify downloads against `SHA256SUMS`. Import the offline Docker archive with `gzip -dc supryolo-0.2.0-docker.tar.gz | docker load`.
