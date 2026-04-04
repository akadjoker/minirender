cmake_minimum_required(VERSION 3.20)
project(exporter)
cmake_policy(SET CMP0072 NEW)


set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ")

set(BUGL_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(BUGL_ASSIMP_INCLUDE_DIRS
    "${BUGL_ROOT_DIR}/vendor/assimp/include"
    "${BUGL_ROOT_DIR}/build-vendor-cache/Release/assimp/include"
    "${BUGL_ROOT_DIR}/build-vendor-cache/Debug/assimp/include"
)

set(BUGL_ASSIMP_LIB "")
set(BUGL_ASSIMP_ZLIB "")
if(CMAKE_BUILD_TYPE MATCHES Debug)
    if(EXISTS "${BUGL_ROOT_DIR}/build-vendor-cache/Debug/assimp/lib/libassimpd.a")
        set(BUGL_ASSIMP_LIB "${BUGL_ROOT_DIR}/build-vendor-cache/Debug/assimp/lib/libassimpd.a")
    endif()
    if(EXISTS "${BUGL_ROOT_DIR}/build-vendor-cache/Debug/assimp/contrib/zlib/libzlibstaticd.a")
        set(BUGL_ASSIMP_ZLIB "${BUGL_ROOT_DIR}/build-vendor-cache/Debug/assimp/contrib/zlib/libzlibstaticd.a")
    endif()
else()
    if(EXISTS "${BUGL_ROOT_DIR}/build-vendor-cache/Release/assimp/lib/libassimp.a")
        set(BUGL_ASSIMP_LIB "${BUGL_ROOT_DIR}/build-vendor-cache/Release/assimp/lib/libassimp.a")
    endif()
    if(EXISTS "${BUGL_ROOT_DIR}/build-vendor-cache/Release/assimp/contrib/zlib/libzlibstatic.a")
        set(BUGL_ASSIMP_ZLIB "${BUGL_ROOT_DIR}/build-vendor-cache/Release/assimp/contrib/zlib/libzlibstatic.a")
    endif()
endif()

if(BUGL_ASSIMP_LIB)
    add_library(assimp::assimp STATIC IMPORTED GLOBAL)
    set_target_properties(assimp::assimp PROPERTIES
        IMPORTED_LOCATION "${BUGL_ASSIMP_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${BUGL_ROOT_DIR}/vendor/assimp/include;${BUGL_ROOT_DIR}/build-vendor-cache/Release/assimp/include;${BUGL_ROOT_DIR}/build-vendor-cache/Debug/assimp/include"
    )
else()
    find_package(assimp REQUIRED)
endif()

if (WIN32)
    set(LIBS_DIR "E:/windows/libs")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}   -D_CRT_SECURE_NO_WARNINGS")
    if (MSVC)
        if(CMAKE_BUILD_TYPE MATCHES Debug)
            add_compile_options(/RTC1 /Od /Zi)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fsanitize=address")
        endif()     
    endif()

endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_compile_options(
    -Wall 
)


file(GLOB SOURCES "src/*.cpp")
add_executable(exporter   ${SOURCES})

if (WIN32)
    target_include_directories(exporter PUBLIC "${LIBS_DIR}/include" include src)
else() 
    target_include_directories(exporter PUBLIC  include src)
endif()



if(CMAKE_BUILD_TYPE MATCHES Debug)

    if (UNIX)
        target_compile_options(exporter PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g  -D_DEBUG )
        target_link_options(exporter PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g  -D_DEBUG) 
    endif()


elseif(CMAKE_BUILD_TYPE MATCHES Release)
    target_compile_options(exporter PRIVATE -O3   -DNDEBUG )
    target_link_options(exporter PRIVATE -O3   -DNDEBUG )
endif()



if (WIN32)
    target_link_libraries(exporter assimp::assimp  Winmm.lib opengl32.lib)
endif()


if (UNIX)
    if(BUGL_ASSIMP_ZLIB)
        target_link_libraries(exporter m assimp::assimp "${BUGL_ASSIMP_ZLIB}")
    else()
        target_link_libraries(exporter m assimp::assimp)
    endif()
endif()

#message(STATUS "SDL2 Library Dir: ${LIB_DIR}/")
