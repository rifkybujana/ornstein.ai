cmake_minimum_required(VERSION 3.20)
project(ornstein C CXX OBJC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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
add_executable(ornstein src/main.c src/face.c src/mood.c src/platform_macos.m)
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
    )
endif()
