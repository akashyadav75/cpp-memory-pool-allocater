# ============================================================================
# CMakeLists.txt - Build Configuration for Custom C++ Memory Pool Allocator
# ============================================================================
# Why CMake?
# - CMake is the industry-standard cross-platform build system generator for C++.
# - It provides seamless integration with IDEs (Visual Studio, CLion, VS Code).
# - It allows easy target-based dependency management.
#
# Alternatives Considered & Why Rejected:
# 1. Raw Makefiles: Rejected because they are platform-specific (mostly Unix-centric)
#    and maintaining them for Windows (nmake/mingw) is error-prone.
# 2. MSBuild/vcxproj: Rejected because they are Windows-only, making it hard
#    to run CI/CD on other platforms or integrate with non-Windows developers.
# 3. Bazel: Rejected as it introduces heavy setup overhead and a steep learning
#    curve for a lightweight library project.
# ============================================================================

cmake_minimum_required(VERSION 3.15 FATAL_ERROR)
project(CppMemoryPoolAllocator
    VERSION 1.0.0
    LANGUAGES CXX
)

# Force C++17 standard to leverage modern language features (std::mutex, std::align, etc.)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable compiler warnings to maintain high code quality
if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(-Wall -Wextra -Werror -pedantic)
endif()

# Define the core library
add_library(memory_pool
    src/MemoryPool.cpp
    src/Block.cpp
)

# Target include directories
target_include_directories(memory_pool PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link Windows system libraries if compiling on Windows (needed for VirtualAlloc)
if(WIN32)
    target_link_libraries(memory_pool PRIVATE Kernel32)
endif()

# Enable testing
enable_testing()

# Unit Tests Target
add_executable(test_allocations tests/test_allocations.cpp)
target_include_directories(test_allocations PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_allocations PRIVATE memory_pool)
add_test(NAME UnitTests COMMAND test_allocations)

# Benchmarks Target
add_executable(throughput_test benchmarks/throughput_test.cpp)
target_include_directories(throughput_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(throughput_test PRIVATE memory_pool)
