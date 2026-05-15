project(level_editor)

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

# Reuse the editor's non-UI source files (no ImGui deps)
set(EDITOR_SRC ${CMAKE_SOURCE_DIR}/editor/src)

add_executable(level_editor
    src/main.cpp
    ${EDITOR_SRC}/EditorBrushGeometryOps.cpp
    ${EDITOR_SRC}/EditorBrushOps.cpp
    ${EDITOR_SRC}/EditorConvexBrushOps.cpp
    ${EDITOR_SRC}/EditorSceneIO.cpp
    ${EDITOR_SRC}/EditorViewOps.cpp
)

target_include_directories(level_editor PRIVATE
    src
    ${EDITOR_SRC}
    ${CMAKE_SOURCE_DIR}/vendor/widgets/include
    ${CMAKE_SOURCE_DIR}/vendor/nlohmann
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(level_editor PRIVATE
    core
    bugui_widgets
    ${SDL2_LIBRARIES}
    OpenGL::GL
)

if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(level_editor PRIVATE -g -O0)
else()
    target_compile_options(level_editor PRIVATE -O3 -march=native -mtune=native)
endif()
