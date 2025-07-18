


add_executable(main main.cpp)

target_link_libraries(main PRIVATE snappy)


target_compile_options(main PRIVATE -Wall -Wextra -pedantic -Werror)


install(TARGETS main
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)