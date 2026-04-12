project(level_editor)
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

add_executable(level_editor
    src/EditableMesh.cpp
    src/LevelEditorApp.cpp
    src/LevelEditorScene.cpp
    src/LevelEditorSceneIO.cpp
    src/LevelEditorTheme.cpp
    src/main.cpp
)

if (WIN32)
    set(LIBS_DIR "E:/windows/libs")
    target_include_directories(level_editor PUBLIC "${LIBS_DIR}/include" include src)
else()
    target_include_directories(level_editor PUBLIC
        include
        src
        ${CMAKE_SOURCE_DIR}/vendor/imgui
        ${CMAKE_SOURCE_DIR}/vendor/nlohmann
    )
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
    if (UNIX)
        target_compile_options(level_editor PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g -D_DEBUG)
        target_link_options(level_editor PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g -D_DEBUG)
    endif()
elseif(CMAKE_BUILD_TYPE MATCHES Release)
    target_compile_options(level_editor PRIVATE -O3 -DNDEBUG)
    target_link_options(level_editor PRIVATE -O3 -DNDEBUG)
endif()

if (WIN32)
    target_link_libraries(level_editor core "${LIBS_DIR}/lib/x64/SDL2main.lib" "${LIBS_DIR}/lib/x64/SDL2.lib" Winmm.lib opengl32.lib)
endif()

if (UNIX)
    target_link_libraries(level_editor core imgui m SDL2 GL)
endif()
