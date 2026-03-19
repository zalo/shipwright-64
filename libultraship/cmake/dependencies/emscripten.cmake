#=================== ImGui ===================
# Emscripten provides SDL2 via ports
# We need to tell ImGui to use it
target_compile_options(ImGui PUBLIC -sUSE_SDL=2)
target_link_options(ImGui PUBLIC -sUSE_SDL=2)
add_compile_definitions(IMGUI_IMPL_OPENGL_ES3)
