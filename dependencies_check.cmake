if(projectM4_VERSION VERSION_LESS 4.0.0)
    message(FATAL_ERROR "libprojectM version 4.0.0 or higher is required. Version found: ${projectM4_VERSION}.")
endif()

if(SDL3_VERSION VERSION_LESS 3.0.0)
    # This might be tricky if SDL3 versioning is still in preview, but let's assume valid version.
    # If SDL3_VERSION is empty, we might just warn or error out differently.
    message(STATUS "libSDL version found: ${SDL3_VERSION}")
endif()

if(Poco_VERSION VERSION_LESS 1.11.2 AND Poco_VERSION VERSION_GREATER_EQUAL 1.10.0)
    message(FATAL_ERROR "Your Poco library contains a serious bug which will cause crashes. Your version is ${Poco_VERSION}.
Affected versions are 1.10.0 to 1.11.1, including. Please upgrade Poco to at least 1.11.2 or downgrade to 1.9.x.
projectMSDL will NOT work with the affected versions.
See https://github.com/pocoproject/poco/issues/3507 for details on this particular issue.
")
endif()

if(Poco_VERSION VERSION_GREATER_EQUAL 1.10.0 AND Poco_VERSION VERSION_LESS_EQUAL 1.10.1)
    message(AUTHOR_WARNING "Poco versions 1.10.0 and 1.10.1 have a known issue with subsystem uninitialization order.\n"
            "It is HIGHLY recommended to use at least version 1.11.0, otherwise it can lead to crashes on application shutdown.")
endif()
