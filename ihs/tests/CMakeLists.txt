include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(googletest)  # 自动下载、编译并导入目标[1,2](@ref)



# 添加单元测试
add_subdirectory(unit)

# 添加集成测试（如果有）
add_subdirectory(integration)