project(genesis_converter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(genesis_converter
    src/main.cpp
)

target_compile_options(genesis_converter PRIVATE
    -Wall
    -Wextra
    -Wno-missing-field-initializers
)

target_include_directories(genesis_converter PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor/stb
)
