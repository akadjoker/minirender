project(spritegen)

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

# Reuse the original SpritePreviewRenderer (no ImGui deps)
set(ORIG_SRC ${CMAKE_SOURCE_DIR}/_spritegen/src)

add_executable(spritegen
    src/main.cpp
    ${ORIG_SRC}/SpritePreviewRenderer.cpp
)

target_include_directories(spritegen PRIVATE
    src
    ${ORIG_SRC}
    ${CMAKE_SOURCE_DIR}/vendor/widgets/include
    ${CMAKE_SOURCE_DIR}/vendor/nlohmann
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(spritegen PRIVATE
    core
    bugui_widgets
    ${SDL2_LIBRARIES}
    OpenGL::GL
    m
)
