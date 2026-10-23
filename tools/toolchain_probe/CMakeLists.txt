cmake_minimum_required(VERSION 3.22)
project(IronstackToolchainProbe LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "This readiness probe requires the Windows MSVC toolchain.")
endif()

add_executable(toolchain_probe main.cpp)
target_compile_features(toolchain_probe PRIVATE cxx_std_17)
target_compile_options(toolchain_probe PRIVATE /W4 /WX /permissive-)
set_target_properties(toolchain_probe PROPERTIES CXX_EXTENSIONS OFF)

message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Windows SDK: $ENV{WindowsSDKVersion}")
message(STATUS "Visual Studio instance: $ENV{VSINSTALLDIR}")

enable_testing()
add_test(NAME windows_cpp17_probe COMMAND toolchain_probe)
