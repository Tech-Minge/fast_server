# include(FetchContent)
# FetchContent_Declare(
#     spdlog
#     GIT_REPOSITORY https://github.com/gabime/spdlog.git
#     GIT_TAG v1.11.0  # 指定稳定版本
# )
# FetchContent_MakeAvailable(spdlog)


add_executable(main fix_main.cpp)

target_include_directories(main PRIVATE ${PROJECT_SOURCE_DIR}/include)

# target_link_libraries(main PRIVATE snappy)

# target_link_libraries(main PRIVATE spdlog)
# target_compile_options(main PRIVATE -Wall -Wextra -pedantic -Werror)


install(TARGETS main
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)