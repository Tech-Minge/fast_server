#!/bin/bash


set -e  # 出错立即退出

# 变量定义
SOURCE_DIR=$(pwd)
BUILD_DIR=${BUILD_DIR:-build}
INSTALL_DIR=${INSTALL_DIR:-install}


mkdir -p ${BUILD_DIR}
rm -rf ${BUILD_DIR}/*

cmake \
    -S ${SOURCE_DIR} \
    -B ${BUILD_DIR} \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
    -DCMAKE_BUILD_TYPE=Release # 设置编译类型为 Release

cmake --build ${BUILD_DIR} \
    --parallel 4 \
    --target install


./install/bin/main


exit 0



