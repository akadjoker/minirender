cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0072 NEW)

project(minirender)

 

set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BU3D_PORTABLE_RELEASE "Build release binaries without CPU-specific native flags" ON)
option(BU3D_STRIP_RELEASE "Strip symbols from release binaries when supported" ON)
option(BU3D_IMGUI_DEMO "Build Dear ImGui demo window support" OFF)
option(BU3D_ENABLE_COMPILER_CACHE "Use compiler cache launcher (sccache/ccache) when available" ON)
option(BU3D_VENDOR_EXCLUDE_FROM_ALL "Configure optional vendor libs with EXCLUDE_FROM_ALL (build only on demand)" ON)
option(BU3D_WITH_MINIZ "Configure vendor/miniz" ON)
option(BU3D_WITH_POLY2TRI "Configure vendor/poly2tri" ON)
option(BU3D_WITH_BOX2D "Configure vendor/box2d" ON)
option(BU3D_WITH_AUDIO "Configure vendor/audio" ON)
option(BU3D_WITH_OPENSTEER "Configure vendor/OpenSteer" ON)
option(BU3D_WITH_JOLT "Configure vendor/Jolt" ON)
option(BU3D_WITH_ASSIMP "Configure vendor/assimp" ON)
 

if(BU3D_ENABLE_COMPILER_CACHE)
    if(NOT CMAKE_C_COMPILER_LAUNCHER AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
        find_program(BU3D_COMPILER_CACHE_PROGRAM NAMES sccache ccache)
        if(BU3D_COMPILER_CACHE_PROGRAM)
            message(STATUS "BU3D: enabling compiler cache launcher: ${BU3D_COMPILER_CACHE_PROGRAM}")
            set(CMAKE_C_COMPILER_LAUNCHER "${BU3D_COMPILER_CACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
            set(CMAKE_CXX_COMPILER_LAUNCHER "${BU3D_COMPILER_CACHE_PROGRAM}" CACHE STRING "CXX compiler launcher" FORCE)
        else()
            message(STATUS "BU3D: compiler cache launcher not found (sccache/ccache)")
        endif()
    endif()
endif()

 

find_program(CLANG_TIDY_EXE NAMES clang-tidy)

if(CLANG_TIDY_EXE)
    add_custom_target(
        tidy
        COMMAND ${CLANG_TIDY_EXE}
        -p=${CMAKE_BINARY_DIR}
        ${CMAKE_SOURCE_DIR}/core/src/*.cpp
        ${CMAKE_SOURCE_DIR}/demos/src/*.cpp
        COMMENT "Running clang-tidy"
    )
endif()

function(bu3d_add_optional_vendor dir)
    if(BU3D_VENDOR_EXCLUDE_FROM_ALL)
        add_subdirectory(${dir} EXCLUDE_FROM_ALL)
    else()
        add_subdirectory(${dir})
    endif()
endfunction()

add_compile_definitions(IMGUI_IMPL_OPENGL_ES3)
add_subdirectory(vendor/imgui)
# Forçar Position Independent Code para a library poder ser linkada num plugin partilhado
if(TARGET imgui)
    set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(BU3D_WITH_MINIZ)
    bu3d_add_optional_vendor(vendor/miniz)
    if(TARGET miniz)
        set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endif()

if(BU3D_WITH_POLY2TRI)
    bu3d_add_optional_vendor(vendor/poly2tri)
endif()

if(BU3D_WITH_BOX2D)
    bu3d_add_optional_vendor(vendor/box2d)
    if(TARGET box2d)
        set_target_properties(box2d PROPERTIES POSITION_INDEPENDENT_CODE ON)

        set(_BU3D_BOX2D_USER_SETTINGS_CANDIDATES
            "${CMAKE_SOURCE_DIR}/BU3DBox2d/include/b2_user_settings.h"
            "${CMAKE_SOURCE_DIR}/BU3DBox2d/include/box2d/b2_user_settings.h"
            "${CMAKE_SOURCE_DIR}/BuBox2d/include/b2_user_settings.h"
            "${CMAKE_SOURCE_DIR}/BuBox2d/include/box2d/b2_user_settings.h"
            "${CMAKE_SOURCE_DIR}/vendor/box2d/include/b2_user_settings.h"
            "${CMAKE_SOURCE_DIR}/vendor/box2d/include/box2d/b2_user_settings.h"
        )
        set(_BU3D_BOX2D_USER_SETTINGS_FOUND FALSE)
        foreach(_candidate IN LISTS _BU3D_BOX2D_USER_SETTINGS_CANDIDATES)
            if(EXISTS "${_candidate}")
                get_filename_component(_settings_dir "${_candidate}" DIRECTORY)
                if(_settings_dir MATCHES "/box2d$")
                    get_filename_component(_box2d_include_root "${_settings_dir}" DIRECTORY)
                else()
                    set(_box2d_include_root "${_settings_dir}")
                endif()

                target_compile_definitions(box2d PUBLIC B2_USER_SETTINGS)
                target_include_directories(box2d PUBLIC "${_box2d_include_root}")
                set(_BU3D_BOX2D_USER_SETTINGS_FOUND TRUE)
                message(STATUS "BU3D: Box2D custom settings enabled from ${_candidate}")
                break()
            endif()
        endforeach()
        if(NOT _BU3D_BOX2D_USER_SETTINGS_FOUND)
            message(STATUS "BU3D: Box2D custom settings disabled (b2_user_settings.h not found); using upstream defaults")
        endif()
    endif()
endif()

if(BU3D_WITH_AUDIO)
    bu3d_add_optional_vendor(vendor/audio)
endif()

if(BU3D_WITH_OPENSTEER)
    set(OPENSTEER_BUILD_DEMO OFF CACHE BOOL "Disable upstream OpenSteer demo in BU3D build" FORCE)
    bu3d_add_optional_vendor(vendor/OpenSteer)
endif()

if(BU3D_WITH_JOLT)
    bu3d_add_optional_vendor(vendor/Jolt)
endif()
 

# Assimp - import only, no tools/tests/exporter
# if(BU3D_WITH_ASSIMP)
#     set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
#     set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
#     set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
#     set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
#     set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
#     set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
#     set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
#     bu3d_add_optional_vendor(vendor/assimp)
# endif()

#Configurar o SDL2 para compilar apenas de forma estática
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "Disable SDL2 install rules in BU3D build" FORCE)
set(SDL_SHARED OFF CACHE BOOL "Disable SDL shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Enable SDL static library" FORCE)
set(SDL_TEST OFF CACHE BOOL "Disable SDL test library in BU3D build" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Disable SDL test programs in BU3D build" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "Disable SDL audio subsystem in BU3D build" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "Disable SDL haptic subsystem in BU3D build" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "Disable SDL sensor subsystem in BU3D build" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "Disable SDL Vulkan support in BU3D build" FORCE)
set(SDL_KMSDRM OFF CACHE BOOL "Disable SDL KMSDRM video backend in BU3D build" FORCE)
set(SDL_KMSDRM_SHARED OFF CACHE BOOL "Disable SDL KMSDRM shared loading in BU3D build" FORCE)
set(SDL_RPI OFF CACHE BOOL "Disable SDL Raspberry Pi backend in BU3D build" FORCE)
set(SDL_VIVANTE OFF CACHE BOOL "Disable SDL Vivante backend in BU3D build" FORCE)
set(SDL_OFFSCREEN OFF CACHE BOOL "Disable SDL offscreen backend in BU3D build" FORCE)
add_subdirectory(vendor/SDL)



 
add_subdirectory(core)
add_subdirectory(demos)

 
 
