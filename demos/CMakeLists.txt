project(demos)
cmake_policy(SET CMP0072 NEW)


set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ")

find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(glm REQUIRED)

if (WIN32)
    set(LIBS_DIR "E:/windows/libs")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}   -D_CRT_SECURE_NO_WARNINGS")
    if (MSVC)
        if(CMAKE_BUILD_TYPE MATCHES Debug)
            add_compile_options(/RTC1 /Od /Zi)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fsanitize=address")
        endif()     
    endif()

endif()

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



file(GLOB SOURCES "src/*.cpp")
add_executable(demos   ${SOURCES})

if (WIN32)
    target_include_directories(demos PUBLIC "${LIBS_DIR}/include" include src)
else() 
    target_include_directories(demos PUBLIC  include src)
endif()



if(CMAKE_BUILD_TYPE MATCHES Debug)

    if (UNIX)
        target_compile_options(demos PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g  -D_DEBUG )
        target_link_options(demos PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=leak -g  -D_DEBUG) 
    endif()


elseif(CMAKE_BUILD_TYPE MATCHES Release)
    target_compile_options(demos PRIVATE -O3   -DNDEBUG )
    target_link_options(demos PRIVATE -O3   -DNDEBUG )
endif()



if (WIN32)
    target_link_libraries(demos core "${LIBS_DIR}/lib/x64/SDL2main.lib" "${LIBS_DIR}/lib/x64/SDL2.lib"  Winmm.lib opengl32.lib)
endif()


if (UNIX)
    target_link_libraries(demos core  m SDL2 GL)
endif()

#message(STATUS "SDL2 Library Dir: ${LIB_DIR}/")