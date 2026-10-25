cmake_minimum_required(VERSION 3.16)

cmake_policy(SET CMP0091 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(repicturer C CXX)
if(WIN32)
    enable_language(RC)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build" FORCE)
endif()

set(W2X_VERSION "20250915-gui1" CACHE STRING "version string")

option(W2X_BUILD_CLI "build repicturer-cli, the converter (enhanced waifu2x-ncnn-vulkan)" ON)
option(W2X_BUILD_GUI "build RePicturer, the gui" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# everything is linked statically so the result is portable
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(MINGW)
    # gcc on windows does not realign the stack for avx / avx512 spills; let the assembler emit unaligned moves
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wa,-muse-unaligned-vector-move")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wa,-muse-unaligned-vector-move")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s")
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------- shared image codec dependencies

if(W2X_BUILD_CLI OR W2X_BUILD_GUI)
    # zlib-ng (zlib api)
    set(ZLIB_COMPAT ON CACHE BOOL "" FORCE)
    set(ZLIB_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(ZLIBNG_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(WITH_GTEST OFF CACHE BOOL "" FORCE)
    set(WITH_GZFILEOP OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    add_subdirectory(${TP}/zlib-ng ${CMAKE_BINARY_DIR}/zlib-ng EXCLUDE_FROM_ALL)

    # libwebp
    foreach(opt ANIM_UTILS CWEBP DWEBP GIF2WEBP IMG2WEBP VWEBP WEBPINFO WEBPMUX EXTRAS)
        set(WEBP_BUILD_${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
    set(WEBP_ENABLE_SWAP_16BIT_CSP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_LIBWEBPMUX ON CACHE BOOL "" FORCE) # animated webp encoder (WebPAnimEncoder)
    add_subdirectory(${TP}/libwebp ${CMAKE_BINARY_DIR}/libwebp EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------- enhanced cli

if(W2X_BUILD_CLI)
    find_package(OpenMP)

    # openjpeg (jpeg 2000)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(BUILD_CODEC OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_DOC OFF CACHE BOOL "" FORCE)
    add_subdirectory(${TP}/openjpeg ${CMAKE_BINARY_DIR}/openjpeg EXCLUDE_FROM_ALL)

    # ncnn
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ncnn_options.cmake)
    if(W2X_NCNN_SIMPLEOMP)
        set(NCNN_SIMPLEOMP ON CACHE BOOL "" FORCE)
    endif()
    add_subdirectory(${TP}/ncnn ${CMAKE_BINARY_DIR}/ncnn EXCLUDE_FROM_ALL)

    # shaders -> hex headers (same as upstream)
    set(SHADER_SPV_HEX_FILES)
    macro(waifu2x_add_shader SHADER_SRC)
        get_filename_component(SHADER_SRC_NAME_WE ${SHADER_SRC} NAME_WE)
        set(SHADER_COMP_HEADER ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_SRC_NAME_WE}.comp.hex.h)
        add_custom_command(
            OUTPUT ${SHADER_COMP_HEADER}
            COMMAND ${CMAKE_COMMAND} -DSHADER_SRC=${CMAKE_CURRENT_SOURCE_DIR}/cli/${SHADER_SRC} -DSHADER_COMP_HEADER=${SHADER_COMP_HEADER} -P "${CMAKE_CURRENT_SOURCE_DIR}/cli/generate_shader_comp_header.cmake"
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cli/${SHADER_SRC}
            COMMENT "Preprocessing shader source ${SHADER_SRC_NAME_WE}.comp"
            VERBATIM)
        set_source_files_properties(${SHADER_COMP_HEADER} PROPERTIES GENERATED TRUE)
        list(APPEND SHADER_SPV_HEX_FILES ${SHADER_COMP_HEADER})
    endmacro()
    waifu2x_add_shader(waifu2x_preproc.comp)
    waifu2x_add_shader(waifu2x_postproc.comp)
    foreach(s realcugan_preproc realcugan_postproc realcugan_4x_postproc
            realesrgan_preproc realesrgan_postproc)
        waifu2x_add_shader(${s}.comp)
    endforeach()
    add_custom_target(generate-spirv DEPENDS ${SHADER_SPV_HEX_FILES})

    add_executable(repicturer-cli cli/main.cpp cli/waifu2x.cpp cli/realcugan.cpp cli/realesrgan.cpp cli/image_io.cpp cli/anim_io.cpp)
    set_target_properties(repicturer-cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    add_dependencies(repicturer-cli generate-spirv)
    target_compile_definitions(repicturer-cli PRIVATE W2X_VERSION="${W2X_VERSION}" OPJ_STATIC)
    target_include_directories(repicturer-cli PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${TP}/stb
        ${TP}/libwebp/src
        ${TP}/openjpeg/src/lib/openjp2
        ${CMAKE_BINARY_DIR}/openjpeg/src/lib/openjp2)
    target_link_libraries(repicturer-cli PRIVATE ncnn webpdemux libwebpmux webp sharpyuv openjp2 zlib Threads::Threads)
    if(OPENMP_FOUND AND NOT W2X_NCNN_SIMPLEOMP)
        target_link_libraries(repicturer-cli PRIVATE OpenMP::OpenMP_CXX)
    endif()
    if(WIN32)
        target_link_libraries(repicturer-cli PRIVATE ole32)
        if(MINGW)
            target_link_options(repicturer-cli PRIVATE -municode)
        endif()
    else()
        target_link_libraries(repicturer-cli PRIVATE ${CMAKE_DL_LIBS} m)
    endif()
endif()

# ---------------------------------------------------------------- gui

if(W2X_BUILD_GUI)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/gui.cmake)
endif()
