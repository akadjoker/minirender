cmake_minimum_required(VERSION 3.16)
project(mini_pipeline)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(glm REQUIRED)


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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(mini_pipeline ${SOURCES})

if (WIN32)
    target_include_directories(mini_pipeline PUBLIC "${LIBS_DIR}/include" include src)
else() 
    target_include_directories(mini_pipeline PUBLIC  include src)
endif()

target_include_directories(mini_pipeline PRIVATE 
    src
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_IMAGE_INCLUDE_DIRS}
)

target_link_libraries(mini_pipeline 
    OpenGL::GL
    ${SDL2_LIBRARIES}
    SDL2_image
    glm::glm
)

if(CMAKE_BUILD_TYPE MATCHES Debug)
     target_compile_options(mini_pipeline PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g -Winvalid-pch)
     target_link_options(mini_pipeline PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g -Winvalid-pch) 
endif()