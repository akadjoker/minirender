project(core)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -frtti -fno-strict-aliasing")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -mtune=native")

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

set(SOURCES
    src/Animation.cpp
    src/AnimationStateMachine.cpp
    src/AnimatedMeshManager.cpp
    src/Animator.cpp
    src/Batch.cpp
    src/BinaryStream.cpp
    src/Camera.cpp
    src/Collision.cpp
    src/Color.cpp
    src/DebugDraw.cpp
    src/Device.cpp
    src/Font.cpp
    src/Input.cpp
    src/Material.cpp
    src/MaterialManager.cpp
    src/Math.cpp
    src/Mesh.cpp
    src/MeshLoader.cpp
    src/MeshManager.cpp
    src/Node.cpp
    src/Node3D.cpp
    src/Pixmap.cpp
    src/RenderState.cpp
    src/RenderTarget.cpp
    src/Renderer.cpp
    src/Scene.cpp
    src/Shader.cpp
    src/ShaderManager.cpp
    src/TerrainNode.cpp
    src/TextureManager.cpp
    src/Tree.cpp
    src/Utils.cpp
    src/Vertex.cpp
    src/VertexAnimation.cpp
    src/WaterNode.cpp
    src/glad.cpp
    src/pch.cpp
)

add_library(core SHARED  ${SOURCES})

if (WIN32)
    target_include_directories(core PUBLIC "${LIBS_DIR}/include" include src)
else() 
    target_include_directories(core PUBLIC  include src
    ${CMAKE_SOURCE_DIR}/vendor/imgui
)
endif()

target_include_directories(core PRIVATE 
    src
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_IMAGE_INCLUDE_DIRS}
)

target_link_libraries(core 
    imgui
    OpenGL::GL
    ${SDL2_LIBRARIES}
    SDL2_image
    glm::glm
)
target_include_directories(core PUBLIC include src)

target_precompile_headers(core PUBLIC src/pch.h)

if(CMAKE_BUILD_TYPE MATCHES Debug)
  #   target_compile_options(core PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g -Winvalid-pch)
  #   target_link_options(core PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g -Winvalid-pch) 
endif()
  
