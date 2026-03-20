cmake_minimum_required(VERSION 3.20)
project(ornstein C OBJC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# GLAD
add_library(glad STATIC lib/glad/src/gl.c)
target_include_directories(glad PUBLIC lib/glad/include)

# GLFW
find_package(glfw3 3.3 REQUIRED)

# Main executable
add_executable(ornstein src/main.c src/face.c src/mood.c src/platform_macos.m)
target_include_directories(ornstein PRIVATE include src)
target_link_libraries(ornstein PRIVATE glad glfw)

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
