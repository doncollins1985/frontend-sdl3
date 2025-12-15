if(NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR "SDL3 target not found. Ensure SDL3 is installed and find_package(SDL3) was successful.")
endif()
