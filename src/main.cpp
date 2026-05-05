#include "ProjectMSDLApplication.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

int main(int argc, char* argv[])
{
    Poco::AutoPtr<ProjectMSDLApplication> pApp = new ProjectMSDLApplication;
    try
    {
        pApp->init(argc, argv);
    }
    catch (Poco::Exception& exc)
    {
        pApp->logger().log(exc);
        return Poco::Util::Application::EXIT_CONFIG;
    }
    return pApp->run();
}
