#! /usr/bin/env bash

#Copyright 2022 NXP
#
#Licensed under the Apache License, Version 2.0 (the "License");
#you may not use this file except in compliance with the License.
#You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
#Unless required by applicable law or agreed to in writing, software
#distributed under the License is distributed on an "AS IS" BASIS,
#WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#See the License for the specific language governing permissions and
#limitations under the License.


VTEC_DIR=$(dirname  "$(realpath "$0")")
ROOT_DIR=$(dirname  "${VTEC_DIR}")
BUILD_DIR=${ROOT_DIR}/build
CORE_DIR=${ROOT_DIR}/core
EXTRA_DIR=${ROOT_DIR}/extra
CONTRIB_DIR=${ROOT_DIR}/contrib

TAG=4.5.5
ROOTFS_DIR=
SDK_DIR=${SDK_DIR:-/opt/sdk}
GIT_OPENCV_CORE=https://github.com/opencv/opencv.git
GIT_OPENCV_CONTRIB=https://github.com/opencv/opencv_contrib.git
GIT_OPENCV_EXTRA=https://github.com/opencv/opencv_extra.git

OPTS_SHORT=s:r:t:h
OPTS_LONG=sdk:,rootfs:,tag:,git-opencv-core:,git-opencv-contrib:,git-opencv-extra:,help

options=$(getopt -o ${OPTS_SHORT} -l ${OPTS_LONG} -- "$@")
# shellcheck disable=SC2181
[ $? == 0 ] || {
    echo "Incorrect options provided"
    exit 1
}

eval set -- "$options"

do_usage() {
cat << EOF
    $(basename "$0") [OPTIONS] CMD
        -h, --help
            This help message
        -t, --tag TAG
            Specify the tag to checkout on opencv core (except vtec)
            default: 4.5.5
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
EOF
}

eval set -- "$options"

while true; do
    case "$1" in
        --git-opencv-core)
            shift
            GIT_OPENCV_CORE=$1
            ;;
        --git-opencv-contrib)
            shift
            GIT_OPENCV_CONTRIB=$1
            ;;
        --git-opencv-extra)
            shift
            GIT_OPENCV_EXTRA=$1
            ;;
        --sdk | s)
            shift
            SDK_DIR=$1
            ;;
        --tag | t)
            shift
            TAG=$1
            ;;
        --rootfs | r)
            shift
            ROOTFS_DIR=$1
            ;;
        --help | h)
            do_usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
    esac
    shift
done

declare -a COMMANDS
COMMANDS+=("${@:-all}")

fatal()
{
    echo "$@"
    exit 1
}

sudomize()
{
    destdir=$1
    shift

    if [ "$(stat --printf=%u "$destdir")" !=  "$(id -u)" ];
    then
        sudo "$@"
    else
        exec "$@"
    fi
}

source_SDK () {
    [ -d "${SDK_DIR}" ] || fatal "SDK folder does not exist"
    unset LD_LIBRARY_PATH
    ENV_SETUP_SCRIPT=$(find ${SDK_DIR} -type f -name environment-setup* 2> /dev/null)
    if [ -n "${ENV_SETUP_SCRIPT}" ];
    then
        # shellcheck disable=SC1091
        . "${ENV_SETUP_SCRIPT}"
        # shellcheck disable=SC2181
        if [ $? != 0 ];then
            fatal "Error while sourcing SDK file"
        fi
        if [ -z "${SDKTARGETSYSROOT}" ];
        then
            echo "Buildroot toolchain detected"
            SDKTARGETSYSROOT=$(find "${SDK_DIR}" -type d -name sysroot)
            OECORE_NATIVE_SYSROOT=${SDK_DIR}
            [ -n ${SDKTARGETSYSROOT} ] || fatal "Buildroot SDK target sysroot not found"
            [ -n ${OECORE_NATIVE_SYSROOT} ] || fatal "Buildroot SDK native sysroot not found"

            EXTRA_OPTS="-DCMAKE_TOOLCHAIN_FILE=${OECORE_NATIVE_SYSROOT}/share/buildroot/toolchainfile.cmake"
        else
            echo "Poky toolchain detected"
        fi
    else
        fatal "SDK environment setup file not found !"
    fi
}

do_clone () {
    if [ ! -d "${CORE_DIR}/.git" ];
    then
        git clone "${GIT_OPENCV_CORE}" "${CORE_DIR}"
    fi

    if [ ! -d "${CONTRIB_DIR}/.git" ];
    then
        git clone "${GIT_OPENCV_CONTRIB}" "${CONTRIB_DIR}"
    fi

    if [ ! -d "${EXTRA_DIR}/.git" ];
    then
        git clone "${GIT_OPENCV_EXTRA}" "${EXTRA_DIR}"
    fi
}

do_checkout () {
    SRCS_DIR=("${CORE_DIR}" "${EXTRA_DIR}" "${CONTRIB_DIR}")
    for dir in "${SRCS_DIR[@]}";
    do
        git -C "$dir" fetch
        git -C "$dir" checkout "${TAG}"
    done
}

do_configure () {
    EXTRA_OPTS=
    source_SDK
    rm -Rf "${BUILD_DIR}"
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}" || fatal "Can chdir to ${BUILD_DIR}"
    cmake \
            -GNinja \
            -DENABLE_CCACHE=ON \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            -DCMAKE_C_COMPILER="${CROSS_COMPILE}gcc" \
            -DCMAKE_CXX_COMPILER="${CROSS_COMPILE}g++" \
            -DKERNEL_HEADER_INCLUDE_DIR:PATH="${SDKTARGETSYSROOT}/usr/src/kernels/include" \
            -DOPENCV_EXTRA_MODULES_PATH="${CONTRIB_DIR}/modules;${VTEC_DIR}/modules" \
            -DOPENCV_TEST_DATA_PATH="${EXTRA_DIR}/testdata" \
            -DINSTALL_C_EXAMPLES=ON \
            -DINSTALL_PYTHON_EXAMPLES=ON \
            -DINSTALL_TESTS=ON \
            -DPYTHON3_EXECUTABLE="${OECORE_NATIVE_SYSROOT}/usr/bin/python3" \
            -DPYTHON3_NUMPY_INCLUDE_DIRS:PATH="${SDKTARGETSYSROOT}/usr/lib/python3.9/site-packages/numpy/core/include" \
            -DBUILD_opencv_python3=ON \
            -DBUILD_OPENJPEG=ON \
            -DWITH_TBB=ON \
            -DWITH_QT=OFF \
            -DWITH_OPENGL=ON \
            -DWITH_JPEG=ON \
            -DWITH_OPENCL=ON \
            ${EXTRA_OPTS} \
            "${CORE_DIR}"
}

do_build() {
    source_SDK
    ninja -C "${BUILD_DIR}" || fatal "OpenCV build error"
    ninja -C "${BUILD_DIR}" install &> /dev/null
}

do_install() {
    if [ -z "${ROOTFS_DIR}" ] || [ ! -d "${ROOTFS_DIR}" ];
    then
        fatal "Can not execute install without a valid rootfs directory."
    fi

    sudomize ${ROOTFS_DIR} rm -Rf \
        "${ROOTFS_DIR}/usr/lib/libopencv_*" \
        "${ROOTFS_DIR}/usr/lib/python3.9/site-packages/cv2"

    sudomize ${ROOTFS_DIR} rsync -arz "${BUILD_DIR}/install/" "${ROOTFS_DIR}/usr/"
    sudomize ${ROOTFS_DIR} rsync -arz "${BUILD_DIR}/install/" "${SDKTARGETSYSROOT}/usr/"

    cd "${ROOTFS_DIR}/usr/lib/python3.9/site-packages/cv2/python-3.9" || fatal "cv2 python library folder does not exist"
    if [ -e cv2.cpython-39-x86_64-linux-gnu.so ];
    then
        sudomize $(pwd) mv -f cv2.cpython-39-x86_64-linux-gnu.so cv2.so
    fi
}

for CMD in "${COMMANDS[@]}";
do
    case "$CMD" in
        "clone")
            echo Cloning ...
            do_clone
            ;;
        "checkout")
            echo Checkouting ...
            do_checkout
            ;;
        "configure")
            echo Configuring ...
            do_configure
            ;;
        "build")
            echo Building ...
            do_build
            ;;
        "install")
            echo Installing
            do_install
            ;;
        "all")
            do_clone
            do_checkout
            do_configure
            do_build
            do_install
            ;;
        *)
            echo "$CMD not supported"
            ;;
    esac
done
