cmake_minimum_required(VERSION 3.20)
project(zerus_engine VERSION 1.0.0 LANGUAGES C)

# Set C standard to C23 (latest)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Enable compile_commands.json for clangd/VS Code
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif ()

# Compiler flags for memory safety and best practices
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Common compiler flags
add_compile_options(
        -Wall                   # Enable all warnings
        -Wextra                 # Enable extra warnings
        -Werror                 # Treat warnings as errors
        -Wpedantic              # Strict ISO C compliance
        -Wformat=2              # Format string vulnerabilities
        -Wformat-security       # Format security
        -Wnull-dereference      # Null pointer dereference
        -Wstack-protector       # Stack protection
        -Wtrampolines           # Trampolines
        -Walloca                # Alloca usage
        -Wvla                   # Variable length arrays
        -Warray-bounds=2        # Array bounds checking
        -Wimplicit-fallthrough  # Switch fallthrough
        -Wlogical-op            # Logical operations
        -Wduplicated-cond       # Duplicated conditions
        -Wduplicated-branches   # Duplicated branches
        -Wformat-overflow=2     # Format overflow
        -Wformat-truncation=2   # Format truncation
        -Wstringop-overflow=4   # String operation overflow
        -Wcast-qual             # Cast qualifiers
        -Wcast-align=strict     # Cast alignment
        -Wdate-time             # Date/time macros
        -Winit-self             # Self initialization
        -Wuninitialized         # Uninitialized variables
        -Wmaybe-uninitialized   # Maybe uninitialized
        -Wunused                # Unused variables/functions
        -Wundef                 # Undefined macros
)

# Memory safety flags (GCC/Clang specific)
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
            -fstack-protector-strong    # Stack protection
            -fstack-clash-protection    # Stack clash protection
            -fcf-protection=full        # Control flow protection (x86)
            -fPIE                       # Position independent executable
    )

    # Address Sanitizer for debug builds
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address -fsanitize=undefined)
    endif ()
endif ()

# Link flags
add_link_options(-pie)  # Position independent executable

# Include directories
include_directories(
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

# Source files (header-only engine, only main.c needed)
set(SOURCES
        "src/main.c"
        include/engine/prelude.h
        include/engine/device.h
        include/engine/surface.h
        include/engine/frame.h
        include/engine/renderer.h
        include/engine/shaders.h
)

# Executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(glfw3 REQUIRED)

# cglm is a C-compatible math library (C version of GLM)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CGLM REQUIRED cglm)

# Find shaderc for shader compilation
find_package(PkgConfig REQUIRED)
pkg_check_modules(SHADERC REQUIRED shaderc)

# Link libraries
target_link_libraries(${PROJECT_NAME}
        glfw
        ${CGLM_LIBRARIES}
        ${SHADERC_LIBRARIES}
        vulkan
        m  # Math library
)

# Include cglm headers
target_include_directories(${PROJECT_NAME} PRIVATE ${CGLM_INCLUDE_DIRS})
target_compile_options(${PROJECT_NAME} PRIVATE ${CGLM_CFLAGS_OTHER})

# Include shaderc headers
target_include_directories(${PROJECT_NAME} PRIVATE ${SHADERC_INCLUDE_DIRS})
target_compile_options(${PROJECT_NAME} PRIVATE ${SHADERC_CFLAGS_OTHER})

# Install target
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# Testing
enable_testing()
add_subdirectory(tests)


