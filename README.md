# Kernel Driver for AR0234

This guide provides detailed instructions on how to install the AR0234 kernel driver on a Linux system, specifically Raspbian.

## Prerequisites

Before you begin the installation process, please ensure the following prerequisites are met:

- **Kernel version**: You should be running on a Linux kernel version 6.1 or newer. You can verify your kernel version by executing `uname -r` in your terminal.

- **Development tools**: Essential tools such as `gcc`, `dkms`, and `linux-headers` are required for compiling a kernel module. If not already installed, these can be installed using the package manager with the following command:
  
   ```bash 
   sudo apt install linux-headers dkms git
   ```
   
## Installation Steps

### Fetching the Source Code

Clone the repository to your local machine and navigate to the cloned directory:

```bash
git clone https://github.com/Kurokesu/ar0234-v4l2-driver.git
cd ar0234-v4l2-driver/
```

### Compiling and Installing the Kernel Driver

To compile and install the kernel driver, execute the provided installation script:

```bash 
sudo sh setup.sh
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

### cam0

If the camera is attached to cam0 port, append the dtoverlay with `,cam0` like this:  
```
camera_auto_detect=0
dtoverlay=ar0234,cam0
```

## Special Thanks

Special thanks to:
- [6by9](https://github.com/6by9) for sharing modded [ar0234 driver](https://github.com/6by9/linux/tree/rpi-6.12.y-ar0234) and [libcamera](https://github.com/6by9/libcamera/tree/ar0234) code.
- [Will Whang](https://github.com/will127534) for [imx585-v4l2-driver](https://github.com/will127534/imx585-v4l2-driver) repository which was used as the basis for structuring this driver.
- Sasha Shturma's Raspberry Pi CM4 Сarrier with Hi-Res MIPI Display project, the install script is adapted from the github project page: https://github.com/renetec-io/cm4-panel-jdi-lt070me05000
