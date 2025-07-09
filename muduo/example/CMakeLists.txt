cmake_minimum_required(VERSION 3.14)
project(example)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# add_compile_options(-Wall -Wextra -pedantic -Werror)  # 全局生效

find_library(Tcp_LIB
  NAMES tcp
  HINTS "${CMAKE_PREFIX_PATH}/lib"
  REQUIRED
)

# find_library(Spd_LIB
#   NAMES spdlog
#   HINTS "${CMAKE_PREFIX_PATH}/lib"
#   REQUIRED
# )

# find_package(Boost REQUIRED COMPONENTS program_options)
# 创建可执行文件
add_executable(main main.cpp)


# 链接库
target_link_libraries(main PRIVATE ${Tcp_LIB})
# target_link_libraries(main PRIVATE Boost::program_options)


# 包含头文件
target_include_directories(main PRIVATE
    ${CMAKE_INSTALL_PREFIX}/include
)

# target_link_libraries(main PRIVATE ${Spd_LIB})

install(TARGETS main
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)