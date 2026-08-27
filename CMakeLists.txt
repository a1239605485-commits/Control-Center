cmake_minimum_required(VERSION 3.22.1)
project("Terraria-Mod-Control-Center" LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# Pin Dear ImGui so later builds are reproducible.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

message(STATUS "=== SYSTEM INFO ===")
message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_SIZEOF_VOID_P: ${CMAKE_SIZEOF_VOID_P}")
message(STATUS "Dear ImGui source: ${imgui_SOURCE_DIR}")
message(STATUS "===================")

include_directories(${CMAKE_SOURCE_DIR}/mod-api)

macro(add_terraria_imgui_mod MOD_NAME)
    add_library(${MOD_NAME} SHARED
        ${CMAKE_SOURCE_DIR}/mod-api/tefkernel/tef_api_imp.c
        ${ARGN}
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )

    target_include_directories(${MOD_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/mod-api
        ${CMAKE_CURRENT_SOURCE_DIR}/ModControlCenter
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )

    if(ANDROID)
        # Terraria Android 1.4.5.6.4 renders through GLES2.
        # Force Dear ImGui's OpenGL backend to compile for GLES2.
        target_compile_definitions(${MOD_NAME} PRIVATE IMGUI_IMPL_OPENGL_ES2)
        target_link_libraries(${MOD_NAME} PRIVATE GLESv2 log android)
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
            add_custom_command(TARGET ${MOD_NAME} POST_BUILD
                COMMAND ${CMAKE_STRIP} --strip-all $<TARGET_FILE:${MOD_NAME}>
                COMMENT "Stripping symbols from ${MOD_NAME}"
                VERBATIM
            )
        endif()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Android")
        set(LOCAL_SYSTEM "android")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(LOCAL_SYSTEM "linux")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(LOCAL_SYSTEM "darwin")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(LOCAL_SYSTEM "windows")
    else()
        set(LOCAL_SYSTEM "unknown")
    endif()

    if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
        set(LOCAL_ARCH "arm64")
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a" OR CMAKE_SYSTEM_PROCESSOR MATCHES "armv7|arm")
        set(LOCAL_ARCH "arm")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        set(LOCAL_ARCH "x64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i686|i386|x86")
        set(LOCAL_ARCH "x86")
    else()
        set(LOCAL_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    if(WIN32)
        set_target_properties(${MOD_NAME} PROPERTIES
            OUTPUT_NAME "${MOD_NAME}.${LOCAL_SYSTEM}.${LOCAL_ARCH}"
            SUFFIX ".dll"
            PREFIX ""
        )
    elseif(APPLE)
        set_target_properties(${MOD_NAME} PROPERTIES
            OUTPUT_NAME "${MOD_NAME}.${LOCAL_SYSTEM}.${LOCAL_ARCH}"
            SUFFIX ".dylib"
        )
    else()
        set_target_properties(${MOD_NAME} PROPERTIES
            OUTPUT_NAME "${MOD_NAME}.${LOCAL_SYSTEM}.${LOCAL_ARCH}"
            SUFFIX ".so"
        )
    endif()
endmacro()

add_subdirectory(ModControlCenter)
