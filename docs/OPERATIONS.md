# Devices, controls and terminal display

CPU and every visible CUDA GPU are enabled by default. Always select devices
explicitly on a machine shared with other workloads.

| Option | Meaning |
|---|---|
| `--gpu-device 0` | Use CUDA GPU 0 |
| `--gpu-device 0,1` | Use CUDA GPUs 0 and 1 |
| No `--gpu-device` | Use all visible CUDA GPUs |
| `--no-cpu` | Disable CPU hashing |
| `--no-gpu` | Disable GPU hashing |
| Both disable flags | Exit with an explicit no-devices error |
| `--cpu-threads N` | Use N CPU threads; 0 chooses automatically |
| `--cpu-variant auto` | AVX2 when supported, otherwise scalar |
| `--cpu-variant scalar` / `avx2` | Force a kernel; unsupported AVX2 fails explicitly |
| `--list-devices` | List available devices and current GPU sensors, then exit |

`-d` aliases `--gpu-device`. `--cpu` is a CPU-only compatibility alias.
Indices follow CUDA's visible ordering, including `CUDA_VISIBLE_DEVICES`.
GPU management resolves each selected CUDA device by its PCI bus address;
it does not assume NVML and CUDA enumerate devices in the same order.
Each GPU and CPU worker receives a separate nonce partition.

Mining's automatic CPU thread count is half the available logical threads,
minus the selected GPU count and one feeder thread, with a minimum of one.
Set `--cpu-threads` explicitly when tuning a rig. A CPU-only build needs neither
the CUDA toolkit nor the NVML library. The current CPU implementation supports
runtime AVX2 detection on x86-64 GCC/Clang, with a portable scalar fallback.

## GPU settings

These flags are **absolute settings**, not clock offsets. Clocks use the MHz
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
