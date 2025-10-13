cmake_minimum_required(VERSION 3.16)
project(SkeletonOpenGL3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options to keep GLFW small
option(GLFW_BUILD_EXAMPLES "GLFW examples" OFF)
option(GLFW_BUILD_TESTS "GLFW tests" OFF)
option(GLFW_BUILD_DOCS "GLFW docs" OFF)
option(GLFW_INSTALL "GLFW install" OFF)
option(BUILD_SHARED_LIBS "Build shared libs" OFF)

# ---- Dependencies ----
# GLM: header-only
add_subdirectory(external/glm)

# GLFW: build from source
add_subdirectory(external/glfw)

# GLAD: build a small static lib from generated sources
add_library(glad STATIC external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)

# ---- App ----
add_executable(skeleton
    src/main.cpp
)

target_include_directories(skeleton
    PRIVATE
        external/glad/include
        external/glm        # glm headers
)

# Link order: glad first, then glfw; system libs/frameworks are pulled via glfw
target_link_libraries(skeleton
    PRIVATE
        glad
        glfw
)

# macOS frameworks (GLFW usually handles these when added via add_subdirectory,
# but the block below is harmless if already linked transitively)
if(APPLE)
    target_link_libraries(skeleton PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
        "-framework OpenGL"
    )
endif()

# Linux extras (again, GLFW tends to add what it needs, this is a safe fallback)
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(skeleton PRIVATE Threads::Threads dl)
endif()

# Nice warnings
if(MSVC)
    target_compile_options(skeleton PRIVATE /W4 /permissive-)
else()
    target_compile_options(skeleton PRIVATE -Wall -Wextra -Wpedantic)
endif()

# If you’re on Apple Silicon and want a universal build, uncomment this:
# set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
