#include "Application.h"

int main(void)
{
	App::Application app;
	if (!app.Initialize()) {
		return -1;
	}
	app.Run();
	return app.Shutdown();
}