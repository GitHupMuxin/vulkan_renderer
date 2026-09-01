#include <cstdlib>
#include <string>
#include <memory>
#include "app/application/application.h"
#include "engine/utils/log.h"

namespace
{
	app::Application* gApplication = nullptr;
}

// OS specific macros for the example main entry points
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (gApplication != nullptr)
	{
		gApplication->HandleMessage(hWnd, uMsg, wParam, lParam);
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	const std::string logPath = std::string(VK_LOG_DIR) + "engine.log";
	if (!engine::utils::Logger::Instance().Initialize(
		engine::utils::MakeDefaultLoggerConfig(logPath)))
	{
		return EXIT_FAILURE;
	}
	LOG_INFO("Application: logging initialized: " << logPath);

	auto application = std::make_unique<app::Application>();
	gApplication = application.get();

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
	const engine::render::FrameGraphNodeId skyboxNodeId = application->AddRenderPass(std::move(skyboxRenderPass_));
	const engine::render::FrameGraphNodeId pbrNodeId = application->AddRenderPass(std::move(pbrRenderPass_));

	application->AddRenderPassDependency(
		skyboxNodeId,
		pbrNodeId,
		engine::render::RenderResourceId::MainColor,
		engine::render::ResourceHazard::ReadAfterWrite
	);

	application->PrepareFrame();

	application->SetUpUI();

	application->RenderLoop();
	gApplication = nullptr;
	application.reset();

	return EXIT_SUCCESS;
}
