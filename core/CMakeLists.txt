project(core)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -frtti -fno-strict-aliasing")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -mtune=native")

find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
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
    src/CSG.cpp
    src/DebugDraw.cpp
    src/Device.cpp
    src/Effects.cpp
    src/Font.cpp
    src/FrameAnimator.cpp
    src/GridMapData.cpp
    src/GridMapMeshBuilder.cpp
    src/GridMapNode.cpp
    src/Input.cpp
    src/Material.cpp
    src/Math.cpp
    src/M8Texture.cpp
    src/Mesh.cpp
    src/MeshB3DLoader.cpp
    src/MeshGLTFLoader.cpp
    src/MeshMD2Loader.cpp
    src/MeshMD3Loader.cpp
    src/MeshBSPLoader.cpp
    src/MeshIQMLoader.cpp
    src/MeshLoader.cpp
    src/MeshManager.cpp
    src/Node.cpp
    src/Node3D.cpp
    src/Pixmap.cpp
    src/ParticleNode.cpp
    src/RenderState.cpp
    src/RenderTarget.cpp
    src/Scene.cpp
    src/Shader.cpp
    src/ShaderManager.cpp
    src/TerrainNode.cpp
    src/TextMap.cpp
    src/TextMapLoader.cpp
    src/TextureManager.cpp
    src/Tree.cpp
    src/Utils.cpp
    src/Vertex.cpp
    src/VertexAnimation.cpp
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
    ${CMAKE_SOURCE_DIR}/vendor/stb
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(core 
    imgui
    OpenGL::GL
    ${SDL2_LIBRARIES}
    glm::glm
)
target_include_directories(core PUBLIC include src)

target_sources(core PRIVATE
    ${CMAKE_SOURCE_DIR}/vendor/stb/stb_impl.cpp
)

target_precompile_headers(core PUBLIC src/pch.h)

if(CMAKE_BUILD_TYPE MATCHES Debug)
  #   target_compile_options(core PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g -Winvalid-pch)
  #   target_link_options(core PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g -Winvalid-pch) 
endif()
  
