# Kernel Driver for AR0234

[![code formatting](https://github.com/Kurokesu/ar0234-v4l2-driver/actions/workflows/clang-format.yml/badge.svg)](https://github.com/Kurokesu/ar0234-v4l2-driver/actions/workflows/clang-format.yml)

This guide provides detailed instructions on how to install the AR0234 kernel driver on a Linux system, specifically Raspbian.

## Prerequisites

**Kernel version**: You should be running on a Linux kernel version 6.1 or newer. You can verify your kernel version by executing `uname -r` in your terminal.
   
## Installation Steps

### Development tools

Required tools: `gcc`, `dkms`, `linux-headers`. If not already installed, install with:

```bash 
sudo apt install -y linux-headers dkms git
```

### Fetching the Source Code

Clone the repository to your local machine and navigate to the cloned directory:

```bash
cd ~
git clone https://github.com/Kurokesu/ar0234-v4l2-driver.git
cd ar0234-v4l2-driver/
```

### Compiling and Installing the Kernel Driver

To compile and install the kernel driver, execute the provided installation script:

```bash 
sudo ./setup.sh
```

### Updating the Boot Configuration

Edit the boot configuration file using the following command:

```bash
sudo nano /boot/firmware/config.txt
```

In the opened editor, locate the line containing `camera_auto_detect` and change its value to `0`. Then, add the line `dtoverlay=ar0234`. So, it will look like this:

```
camera_auto_detect=0
dtoverlay=ar0234
```

After making these changes, save the file and exit the editor.

Remember to reboot your system for the changes to take effect.

## dtoverlay options

The `ar0234` overlay supports comma-separated options to override defaults:

| option | description | default |
|--------|-------------|----------|
| `cam0` | Use cam0 port instead of cam1 | cam1 |
| `4lane` | Use 4-lane MIPI CSI-2 (if wired) | 2 lanes |
| `link-frequency=<Hz>` | Set MIPI CSI-2 link frequency (Hz) | 450000000 |

### cam0

If the camera is connected to the cam0 port, append `,cam0`:

```
dtoverlay=ar0234,cam0
```

### 4lane

To enable 4-lane MIPI CSI-2, append `,4lane`:

```
dtoverlay=ar0234,4lane
```

> [!WARNING]
> Before using `4lane`, confirm your selected camera port (cam0 or cam1) actually has 4 lanes wired on your Raspberry Pi and carrier board. Not all carrier boards provide 4-lane CSI on both ports.

### link-frequency

This driver supports link frequencies of 450 MHz (default) and 360 MHz. Due to the sensor PLL scheme, you must use 360 MHz to enable 8-bit output modes; otherwise output stays at 10-bit.

To set the link frequency to 360 MHz, append `,link-frequency=360000000`:

```
dtoverlay=ar0234,link-frequency=360000000
```

### Sensor output formats

| Link frequency | Data rate / lane | Lanes | Bit depth | Width | Height | Max FPS |
|---|---|---|---|---|---|---|
| **960×600 (2×2 binned)** | | | | | | |
| 360MHz | 720 Mbps | 2 | 8 | 960 | 600 | 119 fps |
| 450MHz | 900 Mbps | 2 | 10 | 960 | 600 | 119 fps |
| 360MHz | 720 Mbps | 4 | 8 | 960 | 600 | 238 fps |
| 450MHz | 900 Mbps | 4 | 10 | 960 | 600 | 238 fps |
| **HD 720p (sensor crop)** | | | | | | |
| 360MHz | 720 Mbps | 2 | 8 | 1280 | 720 | 100 fps |
| 450MHz | 900 Mbps | 2 | 10 | 1280 | 720 | 100 fps |
| 360MHz | 720 Mbps | 4 | 8 | 1280 | 720 | 200 fps |
| 450MHz | 900 Mbps | 4 | 10 | 1280 | 720 | 200 fps |
| **Full HD 1080p (sensor crop)** | | | | | | |
| 360MHz | 720 Mbps | 2 | 8 | 1920 | 1080 | 67 fps |
| 450MHz | 900 Mbps | 2 | 10 | 1920 | 1080 | 67 fps |
| 360MHz | 720 Mbps | 4 | 8 | 1920 | 1080 | 134 fps |
| 450MHz | 900 Mbps | 4 | 10 | 1920 | 1080 | 134 fps |
| **1920×1200 (full sensor resolution)** | | | | | | |
| 360MHz | 720 Mbps | 2 | 8 | 1920 | 1200 | 60 fps |
| 450MHz | 900 Mbps | 2 | 10 | 1920 | 1200 | 60 fps |
| 360MHz | 720 Mbps | 4 | 8 | 1920 | 1200 | 120 fps |
| 450MHz | 900 Mbps | 4 | 10 | 1920 | 1200 | 120 fps |

> [!TIP]
> You can combine options. Example: cam0 + 4 lanes + 360 MHz:
> ```
> dtoverlay=ar0234,cam0,4lane,link-frequency=360000000
> ```

## libcamera Support

Currently, the main `libcamera` repository does not support the `ar0234` sensor. To enable support, a fork has been created with the necessary modifications.

On Raspberry Pi devices, `libcamera` and `rpicam-apps` must be rebuilt together. Detailed instructions can be found [here](https://www.raspberrypi.com/documentation/computers/camera_software.html#advanced-rpicam-apps), but for convenience, this is the shorter version:

### Build libcamera and rpicam-apps

#### Remove Pre-installed rpicam-apps

```bash
sudo apt remove --purge rpicam-apps
```

#### Install rpicam-apps Dependencies

```bash
sudo apt install -y libepoxy-dev libjpeg-dev libtiff5-dev libpng-dev
```

```bash
sudo apt install -y libavcodec-dev libavdevice-dev
```

```bash
sudo apt install -y cmake libboost-program-options-dev libdrm-dev libexif-dev
sudo apt install -y meson ninja-build
```

#### Install libcamera Dependencies

```bash
sudo apt install -y libboost-dev
sudo apt install -y libgnutls28-dev openssl libtiff5-dev pybind11-dev
sudo apt install -y qtbase5-dev libqt5core5a libqt5gui5 libqt5widgets5
sudo apt install -y meson cmake
sudo apt install -y python3-yaml python3-ply
sudo apt install -y libglib2.0-dev libgstreamer-plugins-base1.0-dev
```

#### Clone the Forked libcamera Repository

Download a local copy of Kurokesu's fork of `libcamera` with `ar0234` modifications from GitHub:

```bash
cd ~
git clone https://github.com/Kurokesu/libcamera.git --branch ar0234
cd libcamera/
```

#### Configure the Build Environment

Run `meson` to configure the build environment:

```bash
meson setup build --buildtype=release -Dpipelines=rpi/vc4,rpi/pisp -Dipas=rpi/vc4,rpi/pisp -Dv4l2=enabled -Dgstreamer=enabled -Dtest=false -Dlc-compliance=disabled -Dcam=disabled -Dqcam=disabled -Ddocumentation=disabled -Dpycamera=enabled
```

#### Build and Install libcamera

Finally, run the following command to build and install `libcamera`:

```bash
sudo ninja -C build install
```

> [!TIP]
> On devices with 1GB of memory or less, the build may exceed available memory. Append the `-j 1` flag to meson commands to limit the build to a single process.

> [!WARNING]
> `libcamera` does not yet have a stable binary interface. Always build `rpicam-apps` after you build `libcamera`.

#### Clone the rpicam-apps Repository

Download a local copy of Raspberry Pi’s `rpicam-apps` GitHub repository:

```bash
cd ~
git clone https://github.com/raspberrypi/rpicam-apps.git
cd rpicam-apps
```

#### Configure the rpicam-apps Build

Run the following `meson` command to configure the build (libav enabled by default):

```bash
meson setup build -Denable_libav=enabled -Denable_drm=enabled -Denable_egl=enabled -Denable_qt=enabled -Denable_opencv=disabled -Denable_tflite=disabled -Denable_hailo=disabled
```

> [!IMPORTANT]
> `-Denable_libav` enables optional video encode/decode support (FFmpeg / `libavcodec`).
> 
> - **Debian Bookworm**: the packaged `libav*` version is **too old** to build `rpicam-apps` releases **newer than v1.9.0** with libav enabled.
>   - On Bookworm this typically shows up as build errors like “libavcodec API version is too old”, because Bookworm ships `libavcodec` **59.x** while newer `rpicam-apps` expects **libavcodec >= 60** (see [Raspberry Pi forum thread](https://forums.raspberrypi.com/viewtopic.php?t=392649)).
>   - If you want libav support on Bookworm, check out the `rpicam-apps` **v1.9.0** before running `meson setup`:
> 
>     ```bash
>     git checkout v1.9.0
>     ```
>   - If you are building **`rpicam-apps` > v1.9.0** on Bookworm, you must disable libav:
>
>     ```bash
>     meson setup build -Denable_libav=disabled -Denable_drm=enabled -Denable_egl=enabled -Denable_qt=enabled -Denable_opencv=disabled -Denable_tflite=disabled -Denable_hailo=disabled
>     ```
> - **Debian Trixie**: build `rpicam-apps` as usual with `-Denable_libav=enabled` (no need to check out an older version).

#### Build rpicam-apps

Run the following command to build:

```bash
meson compile -C build
```

#### Install rpicam-apps

Run the following command to install `rpicam-apps`:

```bash
sudo meson install -C build
```

> [!TIP]  
> The command above should automatically update the `ldconfig` cache. If you have trouble accessing your new `rpicam-apps` build, run the following command to update the cache:
> 
> ```bash
> sudo ldconfig
> ```

#### Verify the rpicam-apps Build

Verify that `rpicam-apps` was rebuilt correctly by checking the version:

```bash
rpicam-hello --version
```

You should get output similar to this, with your build date:

```
rpicam-apps build: v1.6.0 000000000000-invalid 08-05-2025 (16:08:14)
rpicam-apps capabilites: egl:1 qt:1 drm:1 libav:1
libcamera build: v0.4.0
```

### Verify that `ar0234` is detected

Do not forget to reboot!

```bash
sudo reboot
```

Run the following command to list available cameras:

```bash
rpicam-hello --list-cameras
```

You should see output similar to this (depending on your link-frequency and lane count):

```
Available cameras
-----------------
0 : ar0234 [1920x1200 10-bit GRBG] (/base/axi/pcie@1000120000/rp1/i2c@88000/ar0234@10)
    Modes: 'SGRBG10_CSI2P' : 960x600 [238.78 fps - (0, 0)/1920x1200 crop]
                             1280x720 [199.84 fps - (320, 240)/1280x720 crop]
                             1920x1080 [134.19 fps - (60, 0)/1920x1080 crop]
                             1920x1200 [120.95 fps - (0, 0)/1920x1200 crop]
```

## Special Thanks

Special thanks to:
- [6by9](https://github.com/6by9) for sharing modded [ar0234 driver](https://github.com/6by9/linux/tree/rpi-6.12.y-ar0234) and [libcamera](https://github.com/6by9/libcamera/tree/ar0234) code.
- [Will Whang](https://github.com/will127534) for [imx585-v4l2-driver](https://github.com/will127534/imx585-v4l2-driver) repository which was used as the basis for structuring this driver.
- Sasha Shturma's Raspberry Pi CM4 carrier with Hi-Res MIPI Display project. The install script is adapted from [cm4-panel-jdi-lt070me05000](https://github.com/renetec-io/cm4-panel-jdi-lt070me05000).
