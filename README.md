# Kernel driver for AR0234

[![code formatting](https://github.com/Kurokesu/ar0234-v4l2-driver/actions/workflows/clang-format.yml/badge.svg)](https://github.com/Kurokesu/ar0234-v4l2-driver/actions/workflows/clang-format.yml)
[![Raspberry Pi OS Bookworm](https://img.shields.io/badge/Raspberry_Pi_OS-Bookworm-blue?logo=raspberrypi)](https://www.debian.org/releases/bookworm/)
[![Raspberry Pi OS Trixie](https://img.shields.io/badge/Raspberry_Pi_OS-Trixie-blue?logo=raspberrypi)](https://www.debian.org/releases/trixie/)

Raspberry Pi kernel driver for Onsemi AR0234CS — a 2.3 MP global shutter 1/2.6" CMOS sensor.

- 2-lane and 4-lane MIPI CSI-2 (up to 900 Mbps/lane)
- 8-bit and 10-bit RAW output
- 1920×1200 @ 120 fps (full resolution)
- 960×600 @ 237 fps (2×2 binning)
- External trigger modes (pulsed, automatic, sync-sink)
- Flash output with programmable lead/lag delay

## Prerequisites

**Kernel version**: Linux kernel 6.1 or newer. Verify with `uname -r`.
   
## Installation steps

### Development tools

Required tools: `git`, `dkms`. If not already installed, install with:

```bash
sudo apt install -y git
sudo apt install -y --no-install-recommends dkms
```

### Fetching the source code

Clone this repository:

```bash
cd ~
git clone https://github.com/Kurokesu/ar0234-v4l2-driver.git
cd ar0234-v4l2-driver/
```

### Compiling and installing

Compile and install by running the setup script:

```bash
sudo ./setup.sh
```

### Boot configuration

Edit boot configuration:

```bash
sudo nano /boot/firmware/config.txt
```

Make two changes:

1. Find `camera_auto_detect` near the top and set it to `0`:

```ini
camera_auto_detect=0
```

2. Add `dtoverlay=ar0234` under the `[all]` section at the bottom of the file:

```ini
[all]
dtoverlay=ar0234
```

Save and exit. Reboot for changes to take effect.

> [!IMPORTANT]
> Stock `libcamera` does not support AR0234 — you must build a patched version for camera to function. See [libcamera](#libcamera) below.

## dtoverlay options

`ar0234` overlay supports comma-separated options to override defaults:

| option | description | default |
|--------|-------------|----------|
| `cam0` | Use cam0 port instead of cam1 | cam1 |
| `4lane` | Use 4-lane MIPI CSI-2 (if wired) | 2 lanes |
| `link-frequency=<Hz>` | Set MIPI CSI-2 link frequency (Hz) | 450000000 |
| `external-trigger` | Pulse/automatic trigger mode via TRIG pin | off |
| `sync-sink` | Multi-sensor sync mode (frame timing locked to TRIG pin) | off |
| `always-on` | Keep regulator powered (prevents runtime PM power-off) | off |
| `flash` | Enable FLASH output pin (HIGH during exposure) | off |
| `flash-lead=<n>` | Flash lead delay (~3.4 µs/unit 4-lane, ~6.8 µs/unit 2-lane) | 0 |
| `flash-lag=<n>` | Flash lag delay (~3.4 µs/unit 4-lane, ~6.8 µs/unit 2-lane) | 0 |

### cam0

If camera is connected to cam0 port, append `,cam0`:

```ini
dtoverlay=ar0234,cam0
```

### 4lane

To enable 4-lane MIPI CSI-2, append `,4lane`:

```ini
dtoverlay=ar0234,4lane
```

> [!WARNING]
> Before using `4lane`, confirm your camera port actually supports 4-lane MIPI CSI. Not all Raspberry Pi models and carrier boards provide 4-lane MIPI CSI on both ports.

### link-frequency

Supported link frequencies: 450 MHz (default, 10-bit only) and 360 MHz (8-bit only). Sensor PLL constraints tie bit depth to link frequency — switch to 360 MHz if you need 8-bit output.

To set link frequency to 360 MHz, append `,link-frequency=360000000`:

```ini
dtoverlay=ar0234,link-frequency=360000000
```

#### Output formats

| Link frequency | Data rate / lane | Lanes | Bit depth | Width | Height | Max FPS |
|---|---|---|---|---|---|---|
| **960×600 (2×2 binned)** | | | | | | |
| 360 MHz | 720 Mbps | 2 | 8 | 960 | 600 | 119 fps |
| 450 MHz | 900 Mbps | 2 | 10 | 960 | 600 | 119 fps |
| 360 MHz | 720 Mbps | 4 | 8 | 960 | 600 | 237 fps |
| 450 MHz | 900 Mbps | 4 | 10 | 960 | 600 | 237 fps |
| **HD 720p (sensor crop)** | | | | | | |
| 360 MHz | 720 Mbps | 2 | 8 | 1280 | 720 | 100 fps |
| 450 MHz | 900 Mbps | 2 | 10 | 1280 | 720 | 100 fps |
| 360 MHz | 720 Mbps | 4 | 8 | 1280 | 720 | 200 fps |
| 450 MHz | 900 Mbps | 4 | 10 | 1280 | 720 | 200 fps |
| **Full HD 1080p (sensor crop)** | | | | | | |
| 360 MHz | 720 Mbps | 2 | 8 | 1920 | 1080 | 67 fps |
| 450 MHz | 900 Mbps | 2 | 10 | 1920 | 1080 | 67 fps |
| 360 MHz | 720 Mbps | 4 | 8 | 1920 | 1080 | 134 fps |
| 450 MHz | 900 Mbps | 4 | 10 | 1920 | 1080 | 134 fps |
| **1920×1200 (full resolution)** | | | | | | |
| 360 MHz | 720 Mbps | 2 | 8 | 1920 | 1200 | 60 fps |
| 450 MHz | 900 Mbps | 2 | 10 | 1920 | 1200 | 60 fps |
| 360 MHz | 720 Mbps | 4 | 8 | 1920 | 1200 | 120 fps |
| 450 MHz | 900 Mbps | 4 | 10 | 1920 | 1200 | 120 fps |

> [!NOTE]
> These framerates do not apply to pulsed trigger mode — see [external-trigger](#external-trigger).

> [!TIP]
> Options can be combined. Example — cam0, 4-lane, 360 MHz:
> ```ini
> dtoverlay=ar0234,cam0,4lane,link-frequency=360000000
> ```

### Trigger modes

AR0234 supports two external trigger modes. Both use `TRIG` pin on camera module as external signal input. `TRIG` is a **1.8V logic level** input wired directly to sensor. Trigger pulse only initiates capture — exposure time remains controlled by sensor's integration time register.

#### external-trigger

Sensor stays in standby and waits for activity on `TRIG` pin. Exposure and readout happen sequentially — readout does not begin until exposure is complete. Two sub-modes are available:

- **Pulsed** — each high pulse on `TRIG` pin captures a single frame (minimum pulse width 125 ns — 3 EXTCLK cycles at 24 MHz). Framerate is determined by pulse frequency.
- **Automatic** — if `TRIG` signal stays high, sensor outputs frames continuously at configured framerate.

```ini
dtoverlay=ar0234,external-trigger
```

---

When using `rpicam-apps` in **pulsed** trigger mode, start with a fixed shutter duration and gain:

```bash
rpicam-hello -t 0 --qt-preview --shutter 10000 --gain 2.0
```

> [!IMPORTANT]
> Always specify a fixed shutter duration and gain, to ensure the AGC does not try to adjust them automatically. With external trigger the AGC tends to go unstable.

Shutter value directly controls sensor exposure time and must satisfy:

`exposure_time < trigger_period - (1 / max_fps) - ~1.6 ms`

Where `max_fps` is maximum framerate for your mode from [output formats](#output-formats) table, and ~1.6 ms accounts for MIPI wakeup and internal sensor overhead. For example, at full resolution 4-lane 10-bit (max 120 fps) triggered at 30 Hz: `1/30 - 1/120 - 1.6 ms ≈ 23.7 ms` maximum exposure time.

Maximum trigger frequency for pulsed mode:

| Resolution | Lanes | Max trigger frequency |
|---|---|---|
| **1920×1200 (full resolution)** | 2 | 30 Hz |
| | 4 | 60 Hz |
| **960×600 (2×2 binned)** | 2 | 60 Hz |
| | 4 | 120 Hz |

#### sync-sink

Sensor streams continuously but locks frame timing to external `TRIG` signal. Unlike `external-trigger`, exposure and readout overlap (pipelined), so higher framerates are possible. Trigger period must not be shorter than configured frame length.

```ini
dtoverlay=ar0234,sync-sink
```

---

Trigger modes can also be set at runtime via the module parameter:

```bash
# 0=off, 1=external-trigger, 2=sync-sink
echo 1 | sudo tee /sys/module/ar0234/parameters/trigger_mode
```

> [!NOTE]
> Module parameter is global — in a multi-camera setup it applies to all AR0234 sensors. To configure each sensor independently, use device tree overlay options instead. Device tree setting takes precedence over module parameter.

### always-on

`always-on` keeps camera regulator permanently enabled, preventing kernel from powering off sensor during runtime PM suspend.

```ini
dtoverlay=ar0234,always-on
```

### Flash output

AR0234 has a `FLASH` output pin (1.8V logic level) that goes HIGH during sensor exposure, useful for synchronizing external illumination such as strobes or LEDs.

To enable flash output:

```ini
dtoverlay=ar0234,flash
```

By default, flash pulse closely follows exposure period (longer by ~8 µs on 4-lane or ~16 µs on 2-lane due to sensor overhead). The flash signal start can be shifted relative to exposure using `flash-lead` or `flash-lag`:

- **`flash-lead`** — flash starts *before* exposure, extending total flash time
- **`flash-lag`** — flash starts *after* exposure begins, shortening total flash time

Both accept values in the range 0 to 127, where each unit is approximately **3.4 µs** (4-lane) or **6.8 µs** (2-lane).

```ini
# Flash starts ~34 µs before exposure (4-lane)
dtoverlay=ar0234,4lane,flash,flash-lead=10

# Flash starts ~34 µs after exposure begins (4-lane)
dtoverlay=ar0234,4lane,flash,flash-lag=10
```

For most use cases, small delay values (single digits) are sufficient. Large delay values combined with short exposure times are not recommended. For short exposures (below ~450 µs on 4-lane or ~900 µs on 2-lane), `flash-lead` should not be used.

> [!NOTE]
> In trigger mode, flash output is suppressed when the trigger pulse is shorter than ~1.5 ms.

## libcamera

Main `libcamera` repository does not support AR0234. A fork with necessary modifications is available.

On Raspberry Pi, `libcamera` and `rpicam-apps` must be rebuilt together. Detailed instructions are available [here](https://www.raspberrypi.com/documentation/computers/camera_software.html#advanced-rpicam-apps), but for convenience, here is a shorter version:

### Build libcamera and rpicam-apps

#### Remove pre-installed rpicam-apps

```bash
sudo apt remove --purge rpicam-apps
```

#### Install libcamera dependencies

```bash
sudo apt install -y libboost-dev
sudo apt install -y libgnutls28-dev openssl libtiff5-dev pybind11-dev
sudo apt install -y qtbase5-dev libqt5core5a libqt5gui5 libqt5widgets5
sudo apt install -y meson cmake
sudo apt install -y python3-yaml python3-ply
sudo apt install -y libglib2.0-dev libgstreamer-plugins-base1.0-dev
```

#### Clone libcamera fork

Clone Kurokesu's `libcamera` fork with AR0234 support:

```bash
cd ~
git clone https://github.com/Kurokesu/libcamera.git --branch ar0234
cd libcamera/
```

#### Configure build environment

Configure with `meson`:

```bash
meson setup build --buildtype=release -Dpipelines=rpi/vc4,rpi/pisp -Dipas=rpi/vc4,rpi/pisp -Dv4l2=enabled -Dgstreamer=enabled -Dtest=false -Dlc-compliance=disabled -Dcam=disabled -Dqcam=disabled -Ddocumentation=disabled -Dpycamera=enabled
```

#### Build and install libcamera

Build:

```bash
ninja -C build
```

Install:

```bash
sudo ninja -C build install
```

> [!TIP]
> On devices with 1GB of memory or less, build may exceed available memory. Append `-j 1` to limit to a single process.

> [!WARNING]
> `libcamera` does not yet have a stable binary interface. Always build `rpicam-apps` after you build `libcamera`.

#### Install rpicam-apps dependencies

```bash
sudo apt install -y cmake libboost-program-options-dev libdrm-dev libexif-dev
sudo apt install -y libavcodec-dev libavdevice-dev libavformat-dev libswresample-dev
sudo apt install -y libepoxy-dev libpng-dev
```

#### Clone rpicam-apps

Clone Raspberry Pi's `rpicam-apps` repository:

```bash
cd ~
git clone https://github.com/raspberrypi/rpicam-apps.git
cd rpicam-apps
```

#### Configure rpicam-apps build

Configure with `meson` (libav enabled by default):

```bash
meson setup build -Denable_libav=enabled -Denable_drm=enabled -Denable_egl=enabled -Denable_qt=enabled -Denable_opencv=disabled -Denable_tflite=disabled -Denable_hailo=disabled
```

> [!IMPORTANT]
> On Raspberry Pi OS **Bookworm**, packaged `libav*` is **too old** for `rpicam-apps` newer than v1.9.0.

<details>
<summary>Bookworm libav workaround</summary>

Bookworm ships `libavcodec` **59.x** while newer `rpicam-apps` expects **libavcodec >= 60**, causing build errors like "libavcodec API version is too old" (see [Raspberry Pi forum thread](https://forums.raspberrypi.com/viewtopic.php?t=392649)).

- **Keep libav** — check out `rpicam-apps` **v1.9.0** before running `meson setup`:
  ```bash
  git checkout v1.9.0
  ```
- **Disable libav** — if building `rpicam-apps` > v1.9.0:
  ```bash
  meson setup build -Denable_libav=disabled -Denable_drm=enabled -Denable_egl=enabled -Denable_qt=enabled -Denable_opencv=disabled -Denable_tflite=disabled -Denable_hailo=disabled
  ```

</details>

#### Build rpicam-apps

Build:

```bash
meson compile -C build
```

#### Install rpicam-apps

Install:

```bash
sudo meson install -C build
```

> [!TIP]  
> This should automatically update `ldconfig` cache. If you have trouble accessing your new build, update manually:
> 
> ```bash
> sudo ldconfig
> ```

#### Verify rpicam-apps build

Verify `rpicam-apps` was rebuilt correctly:

```bash
rpicam-hello --version
```

Expected output (build date will differ):

```
rpicam-apps build: v1.11.1 8b7be4ebfe18 24-02-2026 (19:30:43)
rpicam-apps capabilites: egl:1 qt:1 drm:1 libav:1
libcamera build: v0.0.0+6157-91924454
```

### Verify that `ar0234` is detected

Do not forget to reboot!

```bash
sudo reboot
```

List available cameras:

```bash
rpicam-hello --list-cameras
```

Expected output (varies by link frequency and lane configuration):

```
Available cameras
-----------------
0 : ar0234 [1920x1200 10-bit GRBG] (/base/axi/pcie@1000120000/rp1/i2c@80000/ar0234@10)
    Modes: 'SGRBG10_CSI2P' : 960x600 [236.85 fps - (0, 0)/1920x1200 crop]
                             1280x720 [198.49 fps - (320, 240)/1280x720 crop]
                             1920x1080 [133.58 fps - (60, 0)/1920x1080 crop]
                             1920x1200 [120.45 fps - (0, 0)/1920x1200 crop]
```

## Special thanks

- [6by9](https://github.com/6by9) for sharing modded [ar0234 driver](https://github.com/6by9/linux/tree/rpi-6.12.y-ar0234) and [libcamera](https://github.com/6by9/libcamera/tree/ar0234) code.
- [Will Whang](https://github.com/will127534) for [imx585-v4l2-driver](https://github.com/will127534/imx585-v4l2-driver) repository which was used as the basis for structuring this driver.
- Sasha Shturma's Raspberry Pi CM4 carrier with Hi-Res MIPI Display project. The install script is adapted from [cm4-panel-jdi-lt070me05000](https://github.com/renetec-io/cm4-panel-jdi-lt070me05000).
