add_executable(main main.cpp)

target_link_libraries(main PRIVATE orderbook_reconstructor)

target_include_directories(main PRIVATE 
    ${PROJECT_SOURCE_DIR}/include
)