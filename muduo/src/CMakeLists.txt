
aux_source_directory(. SRC_FILES)
add_library(tcp SHARED
    ${SRC_FILES}
)

target_include_directories(tcp PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
)

target_include_directories(tcp PUBLIC 
    ${PROJECT_SOURCE_DIR}/third_party/spdlog/include
)

target_link_libraries(tcp PUBLIC 
    spdlog::spdlog
)
install(TARGETS tcp
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY  ${PROJECT_SOURCE_DIR}/include/ DESTINATION include)

install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/spdlog/include/spdlog/
    DESTINATION include/spdlog
)
