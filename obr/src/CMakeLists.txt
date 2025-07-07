# src/CMakeLists.txt
aux_source_directory(. SRC_FILES)
add_library(Obr SHARED
    ${SRC_FILES}
)

target_include_directories(Obr PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
)

install(TARGETS Obr
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY  ${PROJECT_SOURCE_DIR}/include/ DESTINATION include)
