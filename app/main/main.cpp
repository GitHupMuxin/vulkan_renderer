#include <memory>
#include "app/application/application.h"

std::unique_ptr<app::Application> application = std::make_unique<app::Application>();

// OS specific macros for the example main entry points
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	application->HandleMessage(hWnd, uMsg, wParam, lParam);
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	static std::ofstream errLog("error.log");
	std::cerr.rdbuf(errLog.rdbuf());

	static std::ofstream outLog("output.log");
	std::cout.rdbuf(outLog.rdbuf());

	application->SetArgs(__argc, __argv);
	application->InitVulkan();
	application->SetUpWindow(hInstance, WndProc);
	application->InitResourceManager();
	application->InitCamera();
	application->InitScene();
	application->InitRenderer();
	// application->AddRenderPass(std::move(renderPass));
	std::unique_ptr<engine::render::PBRRenderPass> pbrRenderPass_ = std::make_unique<engine::render::PBRRenderPass>();
	std::unique_ptr<engine::render::SkyBoxRenderPass> skyboxRenderPass_ = std::make_unique<engine::render::SkyBoxRenderPass>();
	LOG_INFO("Application: Adding skybox and PBR render passes...");
	application->AddRenderPass(std::move(skyboxRenderPass_));
	application->AddRenderPass(std::move(pbrRenderPass_));

	application->PrepareFrame();

	application->SetUpUI();

	application->RenderLoop();
	application.reset();

	return 0;
}
