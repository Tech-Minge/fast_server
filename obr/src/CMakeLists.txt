# src/CMakeLists.txt
aux_source_directory(. SRC_FILES)
add_library(orderbook_reconstructor SHARED
    ${SRC_FILES}
)

target_include_directories(orderbook_reconstructor PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
)