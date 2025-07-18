# CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(orderbook_reconstructor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# add_compile_options(-Wall -Wextra -pedantic -Werror)  # 全局生效

add_subdirectory(src)

# add_subdirectory(third_party/snappy)