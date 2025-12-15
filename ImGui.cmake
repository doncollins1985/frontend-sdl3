
add_library(ImGui STATIC
        vendor/imgui/imgui.cpp
        vendor/imgui/imgui.h
        vendor/imgui/imgui_draw.cpp
        vendor/imgui/imgui_tables.cpp
        vendor/imgui/imgui_widgets.cpp
        vendor/imgui/backends/imgui_impl_sdl3.cpp
        vendor/imgui/backends/imgui_impl_sdl3.h
        vendor/imgui/backends/imgui_impl_opengl3.cpp
        vendor/imgui/backends/imgui_impl_opengl3.h
        vendor/imgui/backends/imgui_impl_opengl3_loader.h
        )

target_link_libraries(ImGui
        PUBLIC
        SDL3::SDL3$<$<STREQUAL:${SDL3_LINKAGE},static>:-static>
        )

if(ENABLE_FREETYPE AND Freetype_FOUND)
    target_sources(ImGui
            PRIVATE
            vendor/imgui/misc/freetype/imgui_freetype.cpp
            vendor/imgui/misc/freetype/imgui_freetype.h
            )

    target_compile_definitions(ImGui
            PUBLIC
            IMGUI_ENABLE_FREETYPE
            )

    target_link_libraries(ImGui
            PUBLIC
            Freetype::Freetype
            )
endif()

target_include_directories(ImGui
        PUBLIC
        ${CMAKE_SOURCE_DIR}/vendor/imgui
        ${CMAKE_SOURCE_DIR}/vendor/imgui/backends
        ${SDL3_INCLUDE_DIRS}
        )

# Build font embedding tool
add_executable(ImGuiBinaryToCompressedC EXCLUDE_FROM_ALL
        vendor/imgui/misc/fonts/binary_to_compressed_c.cpp
        )

# Add SDL3/OpenGL 3 Dear ImGui example application target for testing
# Note: ImGui might not have a specific 'example_sdl3_opengl3' folder yet in all versions, 
# but usually it's there or we can skip the demo if it's missing. 
# For now I will comment out the demo target to avoid build errors if the path is wrong, 
# or try to use it if I know the path.
# Let's check if the directory exists first or just omit it for now as it's EXCLUDE_FROM_ALL.
# I'll update it to what is likely correct or just remove it to be safe.
