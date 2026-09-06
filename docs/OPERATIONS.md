# Devices, controls and terminal display

CPU and every discovered GPU are enabled by default. Always select devices
explicitly on a machine shared with other workloads.

| Option | Meaning |
|---|---|
| `--gpu-device 0` | Use GPU 0 in the selected backend list |
| `--gpu-device 0,1` | Use GPUs 0 and 1 in that list |
| No `--gpu-device` | Use all discovered GPUs |
| `--no-cpu` | Disable CPU hashing |
| `--no-gpu` | Disable GPU hashing |
| Both disable flags | Exit with an explicit no-devices error |
| `--cpu-threads N` | Use N CPU threads; 0 chooses automatically |
| `--cpu-variant auto` | AVX2 when supported, otherwise scalar |
| `--cpu-variant scalar` / `avx2` | Force a kernel; unsupported AVX2 fails explicitly |
| `--list-devices` | List available devices and current GPU sensors, then exit |

`-d` aliases `--gpu-device`. `--cpu` is a CPU-only compatibility alias.
In automatic mode, CUDA devices come first, followed by AMD OpenCL devices.
`CUDA_VISIBLE_DEVICES` affects CUDA ordering. Use `--list-devices` with your
chosen `--gpu-backend auto|cuda|opencl` option before selecting indices.
GPU management resolves each selected CUDA device by its PCI bus address;
it does not assume NVML and CUDA enumerate devices in the same order.
Each GPU and CPU worker receives a separate nonce partition.

Mining's automatic CPU thread count is half the available logical threads,
minus the selected GPU count and one feeder thread, with a minimum of one.
Set `--cpu-threads` explicitly when tuning a rig. A CPU-only build needs neither
the CUDA toolkit nor the NVML library. The current CPU implementation supports
runtime AVX2 detection on x86-64 GCC/Clang, with a portable scalar fallback.

## GPU settings

These flags use **absolute values**, not clock offsets. AMD clock selection
and reset semantics are described below. Clocks use the MHz
reported by NVML; memory marketing data rates can use different units.

| Flag | Unit | Value `0` |
|---|---|---|
| `--gpu-core-clock` | MHz | Reset locked core clocks |
| `--gpu-mem-clock` | MHz | Reset locked memory clocks |
| `--powerlimit` | Watts | Restore the card's default power limit |
| `--gpu-fan-speed` | Percent, 1–100 | Restore automatic fan control |

One value applies to all selected GPUs. A comma-separated list must contain
one value per selected GPU, in the **selection order**. For example,
`--gpu-device 1,0 --powerlimit 450,500` sets GPU 1 to 450 W and GPU 0 to 500 W,
provided both cards support those limits. These are syntax examples, not
recommended settings for every card.

No flag means no write for that setting. Explicit settings persist after the
miner exits; reset them with `0` or your rig's management tool. Avoid having
HiveOS and the miner continually override each other's settings. Fan settings
apply to all controllable fans on a selected card.

The driver must support the requested operation and may require root privileges.
Unsupported operations and permission failures are errors, not success messages.
Power limits are checked against driver-reported bounds before writes. If a
later operation fails after an earlier one succeeded, the error says that the
earlier settings remain active. Changes are not an atomic transaction.

## Matrix terminal

The TUI starts automatically on an interactive terminal. Use `--tui` to request
it or `--no-tui` for plain logs. Redirected output always uses plain logs.

- Top left: coin, connection state, endpoint, worker, elapsed time, accepted,
  rejected, stale and pending shares, difficulty, and block candidates.
- Top right: ASCII logo and animated binary background.
- Middle: active GPU/CPU devices, individual rates, GPU temperature, fan,
  core/memory clocks, watts, and accepted/rejected counts.
- Bottom third: share responses with device and rejection reason, difficulty
  changes, connection events and thermal warnings.

Press `q` to stop, `j`/`k` to scroll devices. Resize is handled while running;
small terminals use a compact layout. `Ctrl+C` restores the cursor and terminal.
Try the display without mining or touching hardware:

```sh
./build/supryolo --tui-demo --seconds 10
```

Temperatures are green below **75°C**, yellow from 75°C, and red from **85°C**.
At the alarm threshold, `ALARM` blinks at **2 Hz**. Configure these thresholds
with `--gpu-temp-warn` and `--gpu-temp-alarm`. Threshold crossings also produce
log events. These are visual alarms; they do not automatically stop or throttle
the device. Hardware thermal protection remains the driver's responsibility.
Unavailable sensor readings are `--`, not zero. CPU sensors are not implemented.

A qualifying network-target hash is a **block candidate**, not a confirmed
block. Ordinary Stratum share acceptance does not prove that the network
accepted a block; the display deliberately preserves that distinction.

Rendering and sensor polling run separately from mining and Stratum I/O.

## AMD specifics

AMD uses OpenCL 1.2 kernels compiled by the installed driver. One executable
can contain CUDA, OpenCL and CPU support, or be built with either GPU backend
disabled. OpenCL libraries are loaded on demand; kernels are embedded in the
binary. No external kernel directory is needed.

The automatic OpenCL kernel uses AMD bit-alignment rotations and precomputation
when `cl_amd_media_ops` is available, otherwise portable native rotations.
`--opencl-variant 0` is native, `1` adds precomputation, `2` uses AMD rotations,
and `3` combines both. Variants 2/3 fail explicitly without the required driver
extension. Workgroup size defaults to 64 for OpenCL and 256 for CUDA;
`--block` overrides it for the selected devices.

GPU identity uses PCI information from OpenCL. Multiple ICDs exposing the same
PCI device are deduplicated. Use `--list-devices` to see the indices for your
chosen backend. AMD temperature, fan duty, clocks, utilization and power are
read through Linux amdgpu sysfs/hwmon; missing readings stay unavailable.

AMD power limits use the driver's supported range; `0` restores its default.
AMD clock settings select **existing driver-advertised DPM levels in MHz**.
Arbitrary overclock frequencies, voltage changes and enabling overdrive are not
implemented. See the card's `pp_dpm_sclk` / `pp_dpm_mclk` files for available
levels. Actual operating clocks still depend on load, power and temperature.
Setting either AMD clock flag to `0` restores **both clock domains** to the
automatic policy. A reset cannot be combined with a nonzero clock setting in
the same invocation. NVIDIA clock resets remain independent.

Power limits and DPM clock selection/reset were exercised on RX 7600 XT and
RX 7900 XTX. The tested kernel/driver combinations rejected manual fan PWM
writes even as root. On those systems, use automatic fan control: a nonzero
`--gpu-fan-speed` fails explicitly. If switching to manual mode succeeded but
the subsequent PWM write fails, the miner restores the previous fan policy;
it reports a restoration failure if the driver also refuses that. Automatic
fan reset (`0`) succeeded on both rigs. Fan percentage is PWM duty, not RPM.

No drivers, firmware, kernel parameters or voltage tables are modified by the
miner. The underlying interfaces are described in the
[Linux amdgpu power and thermal documentation](https://docs.kernel.org/6.12/gpu/amdgpu/thermal.html).
