# Download and run

Use the assets on the [GitHub release page](https://github.com/ocminer/supryolo/releases).
Version 0.1.0 is the first binary release. User agent: `supryolo/0.1.0`.

## Which file?

| Asset | Use |
|---|---|
| `supryolo-0.1.0-linux-x86_64.tar.gz` | Linux: combined NVIDIA CUDA, AMD OpenCL and CPU |
| `supryolo-0.1.0.hiveos.tar.gz` | HiveOS Custom miner, with callbacks and API adapter |
| `supryolo-0.1.0-mmpos.tar.gz` | mmpOS Custom miner, with launcher and stats adapter |
| `supryolo-0.1.0-windows-x86_64.zip` | Windows: OpenCL GPU and AVX2/scalar CPU; built by GitHub Actions |
| `supryolo-0.1.0-docker.tar.gz` | Offline Docker image; import with `gzip -dc FILE | docker load` |
| `SHA256SUMS` | SHA-256 checksums for release assets |

Linux binaries require **x86-64 and glibc 2.35 or newer** (Ubuntu 22.04/24.04).
Check with `ldd --version`. Older HiveOS/MMPOS images must be upgraded first.
This is not an ARM build. No CUDA toolkit or compiler is needed on the rig.
NVIDIA requires a CUDA 12.8-capable driver; use **570.124.06 or newer**, and a
driver that supports your GPU. The CUDA package includes architectures
75, 80, 86, 89, 90 and 120. Only the hardware listed in the README was measured.
AMD requires a working OpenCL ICD/runtime supplied by its driver.
A kernel driver alone is insufficient; `--list-devices` must show your card.

## Linux: change your address and start

```sh
tar -xzf supryolo-0.1.0-linux-x86_64.tar.gz
cd supryolo
nano start.sh
./start.sh
```

Replace `YOUR_BTCB2_ADDRESS` on the `WALLET` line with your own BTCB2 address.
The script refuses to mine with the placeholder. Defaults: B2Pool shared GPU
port 4444, all GPUs, no CPU. It applies no clock, power or fan changes.

```sh
./start.sh --gpu-device 0,2
./start.sh --gpu-device 0 --powerlimit 450
MODE=cpu ./start.sh --cpu-threads 4
MODE=mixed ./start.sh --gpu-device 0 --cpu-threads 4
```

CPU mode selects B2Pool's low-difficulty port 5555. Use the README's
hardware-specific tuning guidance before applying power or clock settings.
Environment variables `WALLET`, `WORKER` and `POOL` can override the script.

## HiveOS

Create a flight sheet with your BTCB2 wallet and choose **Custom** miner:

| Field | Value |
|---|---|
| Miner name | `supryolo` |
| Installation URL | `https://github.com/ocminer/supryolo/releases/download/v0.1.0/supryolo-0.1.0.hiveos.tar.gz` |
| Wallet and worker template | `%WAL%.%WORKER_NAME%` |
| Pool URL | `stratum+tcp://de.b2pool.io:4444` |
| Pass | `x` |
| Extra config arguments | Optional, e.g. `--gpu-device 0,2` |

The wrapper enables all GPUs and disables CPU mining. Use the standalone
Linux package for CPU/mixed mining, or mmpOS with explicit CPU arguments.
Supply exactly one pool URL. Extra arguments are parsed as data: shell
commands and substitutions are not executed. Do not override `--api-port`;
the adapter uses **4068 on loopback**. Only one instance may use this port.

The miner keeps its TUI for an interactive console and writes events to
`/var/log/miner/supryolo/supryolo.log`. HiveOS rotates this log using the manifest.
`h-stats.sh` reads the API and supplies `$khs` plus `$stats` to the Hive agent.
Hashrates are converted correctly from H/s to total kH/s; per-device rates
retain H/s units and GPU PCI bus IDs. Stale shares are included in Hive's
rejected count; pending responses are never counted as accepted.

The scripts follow the [HiveOS custom miner interface](https://github.com/minershive/hiveos-linux/blob/master/hive/miners/custom/README.md).
Their configuration and stats mapping are covered by automated fixtures.
A complete flight-sheet deployment on an installed HiveOS rig has not been
validated; report integration issues with your OS/agent version.

## mmpOS

Create a **Custom miner** profile. Download URL:

```text
https://github.com/ocminer/supryolo/releases/download/v0.1.0/supryolo-0.1.0-mmpos.tar.gz
```

Select your BTCB2 wallet and B2Pool GPU port 4444. In advanced arguments use:

```text
./mmp-launch.sh --coin BTCB2 --pool-protocol %pool_protocol% --pool %pool_server%:%pool_port% --user %user% --password %password% --api-port 4068
```

Append `--gpu-device 0,2` to choose cards. Default is all GPUs, no CPU.
For CPU-only mining select pool port 5555 and append `--no-gpu --cpu-threads 4`.
The custom adapter requires API port 4068; do not run two instances on that port.
`mmp-stats.sh` maps GPU PCI bus IDs, places the CPU row first when enabled,
and reports H/s and acknowledged share totals. Requires Bash and Python 3.

The package follows the [mmpOS custom miner interface](https://github.com/ddobreff/mmpos/blob/main/CUSTOM_MINER.MD).
Launcher and stats transformations are fixture-tested; a complete installed
mmpOS-agent deployment has not yet been validated.

## Docker

Image: `ocminersupr/supryolo:0.1.0`. The versioned tag is recommended for rigs.
The default command prints help; it never mines to a built-in wallet.
For NVIDIA, install NVIDIA Container Toolkit on the host first.

```sh
docker run --rm -it --runtime=nvidia -e NVIDIA_VISIBLE_DEVICES=all ocminersupr/supryolo:0.1.0 \
  --url stratum+tcp://de.b2pool.io:4444 --user YOUR_BTCB2_ADDRESS.rig1 --no-cpu
```

Expose just host GPU 0 to the container:

```sh
docker run --rm -it --runtime=nvidia -e NVIDIA_VISIBLE_DEVICES=0 ocminersupr/supryolo:0.1.0 \
  --url stratum+tcp://de.b2pool.io:4444 --user YOUR_BTCB2_ADDRESS.rig1 \
  --no-cpu --gpu-device 0
```

Container GPU numbering follows the devices visible inside the container.
For CPU-only mining omit `--runtime=nvidia`, set `-e NVIDIA_VISIBLE_DEVICES=void`, and use `--no-gpu --cpu-threads 4` with port 5555.
To use the simple starter script instead:

```sh
docker run --rm -it --runtime=nvidia -e NVIDIA_VISIBLE_DEVICES=all -e WALLET=YOUR_BTCB2_ADDRESS \
  --entrypoint ./start.sh ocminersupr/supryolo:0.1.0
```

This image supplies NVIDIA/CPU runtime dependencies. AMD rigs should use the
native Linux package; a host OpenCL installation is not automatically visible
inside this NVIDIA container. GPU controls depend on the driver's container
permissions; use host rig management for tuning. No privileged container is
required for ordinary NVIDIA mining. The loopback API is not published outside
the container; query it from inside when enabled.

## Windows

Extract the entire ZIP; keep the DLLs and certificate bundle beside the EXE.
Edit `start.cmd`, replace `YOUR_BTCB2_ADDRESS`, then double-click it. Windows
10/11 x86-64 is the target. Install the NVIDIA/AMD graphics driver with OpenCL.

```powershell
.\supryolo.exe --list-devices
.\supryolo.exe --gpu-device 0 --no-cpu --url stratum+tcp://de.b2pool.io:4444 --user YOUR_BTCB2_ADDRESS.rig1
.\supryolo.exe --no-gpu --cpu-threads 4 --url stratum+tcp://de.b2pool.io:5555 --user YOUR_BTCB2_ADDRESS.cpu
```

The initial Windows package uses **OpenCL for both NVIDIA and AMD**, with
AVX2/scalar CPU support. It does not include CUDA; Linux CUDA benchmarks do
not describe its performance. Windows CI checks the native executable, CPU
Stratum shares and API; no physical Windows GPU is available for validation.
AMD temperature/control support currently uses Linux sysfs and is unavailable
on Windows. NVIDIA telemetry depends on NVML availability and driver support.
For TLS when launching directly, set `$env:SSL_CERT_FILE` to the bundled
`ca-bundle.crt`; `start.cmd` sets this automatically.

## Monitoring API

```sh
./supryolo --api-port 4068 --no-cpu --gpu-device 0 \
  --url stratum+tcp://de.b2pool.io:4444 --user YOUR_BTCB2_ADDRESS.rig1
curl --fail http://127.0.0.1:4068/summary
```

Disabled by default (`--api-port 0`). Only loopback HTTP GET `/summary` and `/`
are supported. Responses include version, state, uptime, hashes, acknowledged
accepted/rejected/stale shares, pending shares, block candidates, and per-device
H/s, PCI address, temperature and fan. Unknown sensors are JSON null. The API
excludes wallet credentials and has no configuration or control endpoints.
A displayed block candidate is not a confirmed network block.

## Building release packages

Build in the pinned CUDA 12.8 / Ubuntu 22.04 builder from
`packaging/linux/Dockerfile.build`, with Release optimization, static OpenSSL
and GCC runtimes, and `CMAKE_CUDA_ARCHITECTURES=75;80;86;89;90;120`.
Run CTest and device hash-oracle checks on the final binary. Use
`scripts/package-linux.sh` for native, HiveOS and mmpOS archives; include runtime
license notices. Use the clean extracted package as Docker build context.
The Windows workflow builds and tests on GitHub Actions, then uploads an
artifact for release publication. Never package the worktree wholesale: build
outputs, local configuration, handoff and research stay private.
