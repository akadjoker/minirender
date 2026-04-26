project(collision)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_executable(collision
    main.cpp
    CollisionSystem.cpp
    CollisionWorld.cpp
)

target_include_directories(collision PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/vendor/imgui
)

target_link_libraries(collision PRIVATE core)

add_executable(collision_tests
    tests.cpp
    CollisionSystem.cpp
    CollisionWorld.cpp
)

target_include_directories(collision_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(collision_tests PRIVATE core)
