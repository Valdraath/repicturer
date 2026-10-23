# cross compile the linux build with zig (works from windows, macos or linux hosts)
#   cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/zig-linux.cmake ..
# glibc 2.27 baseline (ubuntu 18.04 / debian 10 / rhel 8 and newer), libc++ linked statically
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(CMAKE_HOST_WIN32)
    set(_ext cmd)
else()
    set(_ext sh)
endif()
set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig/zig-cc.${_ext})
set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig/zig-c++.${_ext})
set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/zig/zig-ar.${_ext} CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/zig/zig-ranlib.${_ext} CACHE FILEPATH "" FORCE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# zig ships no libgomp: use ncnn's built-in openmp runtime
set(W2X_NCNN_SIMPLEOMP ON CACHE BOOL "" FORCE)
# lto through zig is slow and gains little here
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
# strip release binaries
set(CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT "-s")
