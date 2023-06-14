## Repository for NXP Edge Processing OpenCV's extra modules

This repository is delivering openCV extra modules offering hardware
acceleration on NXP i.MX8 System On Chip.

## Getting started

The helper script build.sh eases various openCV build tasks:
 - Cloning and checkouting the various openCV git repositories.
 - Configuring the build
 - Building and deploying the artifacts to a root file system.

```console
foo@bar:vtec$ ./build.sh --help
    build.sh [OPTIONS] CMD
        -h, --help
            This help message
        -t, --tag TAG
            Specify the tag to checkout on opencv core (except vtec)
            default: 4.7.0
        -r, --rootfs ROOTFS
            specify the target rootfs directory where to install build artifacts
        -s, --sdk SDK_DIR
            Specify the SDK (poky or buildroot) installation path
            default: /opt/sdk
        --git-opencv-core OPENCV_CORE
            Set the opencv core git remote repository
            default: https://github.com/opencv/opencv.git
        --git-opencv-contrib OPENCV_CONTRIB
            Set the opencv contrib git remote repository
            default: https://github.com/opencv/opencv_contrib.git
        --git-opencv-extra OPENCV_EXTRA
            Set the opencv contrib git remote repository
            default: https://github.com/opencv/opencv_extra.git

        Possible CMD choice:
            - clone
            - checkout
            - configure
            - build
            - install
            - all (default)
```

Assuming that SDK_DIR and ROOTFS shell variables have been set to point
respectively on a poky or buildroot SDK and the target root file system
accessible on your host, you could start with:
```console
foo@bar:vtec$ ./build.sh --sdk ${SDK_DIR} --rootfs {ROOTFS} all
```

`build.sh` script behavior can also be amended with environment variables:
```
MODULES=[<space separated list of modules names>]
```
where modules names correspond to directories in [modules](modules)\
Default: all modules are enabled
```
VERBOSE=[ON|OFF]
```
Configures build output verbosity\
Default: OFF
```
TINY=[ON|OFF]
```
Restricts to the minimum the list of modules included in the build

### Exporting the linux kernel headers

You might have to export the linux kernel headers to use latest features of some
modules.

```console
make -C ${KDIR} ARCH=arm64 INSTALL_HDR_PATH=$(find "${SDK_DIR}" -type d -name sysroot*)/usr/src/kernels headers_install
```

## License

This project is released under Apache 2.0 license terms.
