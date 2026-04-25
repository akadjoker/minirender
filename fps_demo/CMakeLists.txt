project(fps_demo)
cmake_policy(SET CMP0072 NEW)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(glm REQUIRED)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_compile_options(
    -Wextra
    -Wall
    -Wno-sign-compare
    -Wno-missing-field-initializers
    -Wundef
    -Wuninitialized
    -Wunused
    -Wno-unused-parameter
    -Wwrite-strings
    -Wchar-subscripts
    -funsigned-char
)

add_executable(fps_demo
    src/main.cpp
    src/GenesisBspLoader.cpp
    src/GenesisBspCollider.cpp
    src/FpsPlayerController.cpp
    src/genesis/GenesisUtils.cpp
    src/genesis/GenesisGbspFile.cpp
    src/genesis/Genesis3dtFile.cpp
    src/genesis/GenesisEntities.cpp
    src/genesis/GenesisBrushMeshBuilder.cpp
    src/genesis/GenesisPortalSystem.cpp
    src/genesis/GenesisMoverSystem.cpp
)

target_include_directories(fps_demo PUBLIC
    include
    src
    ${CMAKE_SOURCE_DIR}/vendor/imgui
)

if(CMAKE_BUILD_TYPE MATCHES Debug)
    if (UNIX)
        target_compile_options(fps_demo PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g -D_DEBUG)
        target_link_options(fps_demo PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g -D_DEBUG)
    endif()
elseif(CMAKE_BUILD_TYPE MATCHES Release)
    target_compile_options(fps_demo PRIVATE -O3 -DNDEBUG)
    target_link_options(fps_demo PRIVATE -O3 -DNDEBUG)
endif()

if (UNIX)
    target_link_libraries(fps_demo core imgui m SDL2 GL)
endif()
