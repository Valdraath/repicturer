# RePicturer (gui): dear imgui + glfw (built from source, x11 only on linux: libX11 etc. are dlopen'ed at
# run time by glfw, so only headers are needed at build time) + opengl3

set(GLFW_DIR ${TP}/glfw/src)
set(GLFW_SOURCES
    ${GLFW_DIR}/context.c ${GLFW_DIR}/init.c ${GLFW_DIR}/input.c ${GLFW_DIR}/monitor.c ${GLFW_DIR}/platform.c
    ${GLFW_DIR}/vulkan.c ${GLFW_DIR}/window.c ${GLFW_DIR}/egl_context.c ${GLFW_DIR}/osmesa_context.c
    ${GLFW_DIR}/null_init.c ${GLFW_DIR}/null_monitor.c ${GLFW_DIR}/null_window.c ${GLFW_DIR}/null_joystick.c)

if(WIN32)
    list(APPEND GLFW_SOURCES
        ${GLFW_DIR}/win32_module.c ${GLFW_DIR}/win32_time.c ${GLFW_DIR}/win32_thread.c
        ${GLFW_DIR}/win32_init.c ${GLFW_DIR}/win32_joystick.c ${GLFW_DIR}/win32_monitor.c ${GLFW_DIR}/win32_window.c
        ${GLFW_DIR}/wgl_context.c)
    set(GLFW_DEFS _GLFW_WIN32 UNICODE _UNICODE)
else()
    list(APPEND GLFW_SOURCES
        ${GLFW_DIR}/posix_module.c ${GLFW_DIR}/posix_time.c ${GLFW_DIR}/posix_thread.c
        ${GLFW_DIR}/x11_init.c ${GLFW_DIR}/x11_monitor.c ${GLFW_DIR}/x11_window.c ${GLFW_DIR}/xkb_unicode.c
        ${GLFW_DIR}/glx_context.c ${GLFW_DIR}/linux_joystick.c ${GLFW_DIR}/posix_poll.c)
    set(GLFW_DEFS _GLFW_X11 _DEFAULT_SOURCE)
endif()

add_library(w2x_glfw STATIC ${GLFW_SOURCES})
target_compile_definitions(w2x_glfw PRIVATE ${GLFW_DEFS})
target_include_directories(w2x_glfw PUBLIC ${TP}/glfw/include)
if(NOT WIN32)
    target_include_directories(w2x_glfw PRIVATE ${TP}/x11-include)
endif()

set(IMGUI_DIR ${TP}/imgui)
add_library(w2x_imgui STATIC
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_glfw.cpp ${IMGUI_DIR}/imgui_impl_opengl3.cpp)
target_include_directories(w2x_imgui PUBLIC ${IMGUI_DIR})
target_compile_definitions(w2x_imgui PUBLIC GLFW_INCLUDE_NONE)
target_link_libraries(w2x_imgui PUBLIC w2x_glfw)

set(GUI_SOURCES
    gui/AppMain.cpp gui/MainDialog.cpp gui/FileBrowser.cpp gui/Subprocess.cpp gui/Util.cpp gui/ImageProbe.cpp)
if(WIN32)
    # windres cannot cope with include paths containing spaces: build the resource from a copy in the binary dir
    foreach(f app.rc app.manifest app.ico)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/resources/${f} ${CMAKE_BINARY_DIR}/res/${f} COPYONLY)
    endforeach()
    add_library(w2x_res OBJECT ${CMAKE_BINARY_DIR}/res/app.rc)
    list(APPEND GUI_SOURCES $<TARGET_OBJECTS:w2x_res>)
endif()

add_executable(repicturer WIN32 ${GUI_SOURCES})
set_target_properties(repicturer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin OUTPUT_NAME "RePicturer")
target_include_directories(repicturer PRIVATE ${TP}/stb gui)
target_compile_definitions(repicturer PRIVATE W2X_VERSION="${W2X_VERSION}")
target_link_libraries(repicturer PRIVATE w2x_imgui w2x_glfw Threads::Threads)
if(WIN32)
    target_link_libraries(repicturer PRIVATE opengl32 gdi32 dwmapi shell32 ole32 uuid)
    if(MINGW)
        target_link_options(repicturer PRIVATE -municode)
    endif()
else()
    target_link_libraries(repicturer PRIVATE ${CMAKE_DL_LIBS} m)
endif()
