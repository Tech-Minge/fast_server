

# find_library(Spd_LIB
#   NAMES spdlog
#   HINTS "${CMAKE_PREFIX_PATH}/lib"
#   REQUIRED
# )

# find_package(Boost REQUIRED COMPONENTS program_options)
# 创建可执行文件
add_executable(main main.cpp)

add_executable(client client.cpp)
# 链接库
target_link_libraries(main PRIVATE tcp)
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