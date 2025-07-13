#!/bin/bash


set -e  # 出错立即退出

# 变量定义
SOURCE_DIR=$(pwd)
BUILD_DIR=${BUILD_DIR:-build}
INSTALL_DIR=${INSTALL_DIR:-install}
LIB_BUILD_DIR=${BUILD_DIR}/lib

EXAMPLE_SOURCE_DIR=${SOURCE_DIR}/example
EXAMPLE_BUILD_DIR=${BUILD_DIR}/example



# 定义逻辑1函数（库相关操作）
logic1() {
   mkdir -p ${LIB_BUILD_DIR}
    rm -rf ${LIB_BUILD_DIR}/*
    cmake \
        -S ${SOURCE_DIR} \
        -B ${LIB_BUILD_DIR} \
        -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
        -DCMAKE_BUILD_TYPE=Release # 设置编译类型为 Release

    cmake --build ${LIB_BUILD_DIR} \
        --parallel 4 \
        --target install
}

# 定义逻辑2函数（应用相关操作）
logic2() {
    mkdir -p ${EXAMPLE_BUILD_DIR}
    rm -rf ${EXAMPLE_BUILD_DIR}/*
    cmake \
        -S ${EXAMPLE_SOURCE_DIR} \
        -B ${EXAMPLE_BUILD_DIR} \
        -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
        -DCMAKE_BUILD_TYPE=Release

    cmake --build ${EXAMPLE_BUILD_DIR} \
        --parallel 4 \
        --target install

    export LD_LIBRARY_PATH=./install/lib:$LD_LIBRARY_PATH
    ./install/bin/main

}

# 主参数处理逻辑
if [ $# -eq 0 ]; then
    # 无参数：执行两个逻辑
    logic1
    logic2
elif [ $# -eq 1 ]; then
    # 单参数分支
    case "$1" in
        "lib")
            logic1
            ;;
        "app")
            logic2
            ;;
        *)
            # 非法参数处理
            echo "错误：无效参数 '$1'！只允许 'lib' 或 'app'" >&2
            echo "用法: $0 [lib|app]"
            exit 1
            ;;
    esac
else
    # 参数过多处理
    echo "错误：最多只允许一个参数！" >&2
    echo "用法: $0 [lib|app]"
    exit 1
fi

exit 0



