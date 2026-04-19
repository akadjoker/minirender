cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0072 NEW)

project(minirender)

# Use GLES3 platform headers in imgui backend — no glad dependency inside imgui.
# Works on desktop (glad loads the symbols first), Android, Raspberry Pi, Emscripten.
add_compile_definitions(IMGUI_IMPL_OPENGL_ES3)

add_subdirectory(vendor/imgui)
add_subdirectory(vendor/poly2tri)
add_subdirectory(core)
#add_subdirectory(demos)
#add_subdirectory(editor)
#add_subdirectory(mapload_demo)
#add_subdirectory(gridmap_demo)
#add_subdirectory(spritegen)
add_subdirectory(level_editor)

 
 
