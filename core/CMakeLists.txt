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

file(GLOB SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_library(core SHARED
    ${SOURCES}
    ${CMAKE_SOURCE_DIR}/vendor/stb/stb_impl.cpp
)

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

target_precompile_headers(core PUBLIC src/pch.h)

if(CMAKE_BUILD_TYPE MATCHES Debug)
  #   target_compile_options(core PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g -Winvalid-pch)
  #   target_link_options(core PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g -Winvalid-pch) 
endif()
  
