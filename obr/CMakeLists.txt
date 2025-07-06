# CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(orderbook_reconstructor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# option(BUILD_TESTS "Build unit tests" OFF)
option(BUILD_CLI "Build command line tool" ON)

# include_directories(include)

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

add_subdirectory(src)

if(BUILD_CLI)
    add_subdirectory(example)
endif()

# if(BUILD_TESTS)
#     enable_testing()
#     add_subdirectory(tests)
# endif()