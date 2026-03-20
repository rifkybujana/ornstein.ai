cmake_minimum_required(VERSION 3.20)
project(ornstein C CXX OBJC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

# GLAD
add_library(glad STATIC lib/glad/src/gl.c)
target_include_directories(glad PUBLIC lib/glad/include)

# GLFW
find_package(glfw3 3.3 REQUIRED)

# llama.cpp — build server with Metal
set(GGML_METAL    ON  CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON  ON  CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER  ON  CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS   ON  CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/llama.cpp)

# libcurl (for future HTTP communication with llama-server)
find_package(CURL REQUIRED)

# Main executable
add_executable(ornstein src/main.c src/face.c src/mood.c src/platform_macos.m
                        src/text.c src/llm.c src/chat.c src/sentiment.c
                        src/stats.c src/ambient.c src/gl_util.c)
target_include_directories(ornstein PRIVATE include src)
target_link_libraries(ornstein PRIVATE glad glfw CURL::libcurl)

# macOS frameworks
if(APPLE)
    target_link_libraries(ornstein PRIVATE
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework CoreWLAN"
        "-framework Carbon"
    )
endif()

# Tests
add_executable(ornstein_tests tests/test_main.c src/sentiment.c src/face.c src/gl_util.c)
target_include_directories(ornstein_tests PRIVATE include src)
target_link_libraries(ornstein_tests PRIVATE glad "-framework OpenGL")
target_compile_definitions(ornstein_tests PRIVATE ORNSTEIN_TESTING=1)
enable_testing()
add_test(NAME unit_tests COMMAND ornstein_tests)

# Install
install(TARGETS ornstein RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/ornstein/assets)
