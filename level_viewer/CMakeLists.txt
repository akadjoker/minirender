project(level_viewer)
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

add_executable(level_viewer src/main.cpp)

target_include_directories(level_viewer PUBLIC
    include
    src
    ${CMAKE_SOURCE_DIR}/vendor/imgui
)

target_compile_options(level_viewer PRIVATE -O3 -DNDEBUG)
target_link_options(level_viewer PRIVATE -O3 -DNDEBUG)

target_link_libraries(level_viewer core imgui m SDL2 GL)
