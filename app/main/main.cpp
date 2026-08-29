#include <string>
#include <memory>
#include "app/application/application.h"
#include "engine/utils/log.h"

std::unique_ptr<app::Application> application = std::make_unique<app::Application>();

// OS specific macros for the example main entry points
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	application->HandleMessage(hWnd, uMsg, wParam, lParam);
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	auto& logger = engine::utils::Logger::Instance();
#if !defined(NDEBUG)
	logger.EnableConsoleOutput(true);
	logger.SetConsoleLogLevel(engine::utils::LogLevel::Info);
	logger.SetFileLogLevel(engine::utils::LogLevel::Debug);
#else
	logger.SetFileLogLevel(engine::utils::LogLevel::Info);
#endif
	const std::string logPath = std::string(VK_LOG_DIR) + "engine.log";
	if (!logger.SetLogFile(logPath))
	{
		LOG_ERROR("Application: failed to open log file: " << logPath);
	}
	else
	{
		LOG_INFO("Application: logging initialized: " << logPath);
	}

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
