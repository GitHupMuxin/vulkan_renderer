#include <sys/stat.h>
#include "app/application/application.h"

namespace app
{
    Application::Application()
    {

    }

    Application::~Application()
    {

    }

    void Application::SetArgs(int args, char* argv[])
    {
        for (int i = 0; i < args; i++)
            this->argv_.emplace_back(argv[i]);
    }

    void Application::InitVulkan()
    {
        LOG_INFO("Initializing Vulkan...");
		engine::core::DeviceSetting setting;
    	setting.validation_ = true;
        engine::core::Device::Instance().Init(setting);
    }

    
    void Application::SetUpWindow(HINSTANCE hInstance, WNDPROC wndproc)
    {
        LOG_INFO("Setting up window...");
		this->window_.SetDescription({"Vulkan PBR", 1280, 720, false});
        this->window_.Init((void*)hInstance, (void*)wndproc);
    }

    void Application::InitRenderer()
    {
        LOG_INFO("Application: start to init renderer...");
        engine::render::RendererDescription rendererDescription;
		rendererDescription.enableValidation_ = true;
        this->renderer_ = std::make_unique<engine::render::Renderer>(rendererDescription);
        this->renderer_->Init(this->window_);

		this->renderer_->controller.animate = &this->animate;
		this->renderer_->controller.animationTimer = &this->animationTimer;
		this->renderer_->controller.frameTimer = &this->frameTimer;
		this->renderer_->controller.animationIndex = &this->animationIndex;
    }

    void Application::AddRenderPass(std::unique_ptr<engine::render::RenderPass> renderPass)
    {
        this->renderer_->AddRenderPass(std::move(renderPass));
    }

    void Application::PrepareFrame()
    {
        LOG_INFO("Application: Preparing frame...");
        this->renderer_->BindingScene(&this->scene_);
        this->renderer_->PrepareFrame();
        this->prepared_ = true;
    }


    void Application::InitCamera()
    {
        LOG_INFO("Application: start to init camera...");
        this->camera_ .type  = engine::scene::CameraType::lookat;

        this->camera_.SetPerspective(45.0f, (float)this->window_.GetWidth() / (float)this->window_.GetHeight(), 0.01f, 256.0f);
        this->camera_.rotationSpeed_ = 0.25f;
        this->camera_.movementSpeed_ = 0.1f;
        this->camera_.SetPosition({ 0.0f, 0.0f, 1.0f });
        this->camera_.SetRotation({ 0.0f, 0.0f, 0.0f });
    }

    void Application::InitScene()
    {
        LOG_INFO("Application: start to init scene...");
        this->scene_.SetCamera(&this->camera_);
        this->scene_.Init();
    }

    void Application::SetUpUI()
    {
        LOG_INFO("Application: start to set up UI...");
        this->ui_ = std::make_unique<ui::UI>(this->renderer_->GetRenderPass(), this->renderer_->GetPipelineCache(), engine::core::Device::Instance().GetSetting().sampleCount_);
		this->UpdateOverlay();
    }

    void Application::UpdateOverlay()
    {
        auto& device = engine::core::Device::Instance();

        ImGuiIO& io = ImGui::GetIO();

        auto width = this->window_.GetWidth();
        auto height = this->window_.GetHeight();

		ImVec2 lastDisplaySize = io.DisplaySize;
		io.DisplaySize = ImVec2((float)width, (float)height);
		io.DeltaTime = this->frameTimer;

		io.MousePos = ImVec2(this->mousePos.x, this->mousePos.y);
		io.MouseDown[0] = this->mouseButtons.left;
		io.MouseDown[1] = this->mouseButtons.right;

		ui_->pushConstBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
		ui_->pushConstBlock.translate = glm::vec2(-1.0f);

		float scale = 1.0f;

		ImGui::NewFrame();

		ImGui::SetNextWindowPos(ImVec2(10, 10));
		ImGui::SetNextWindowSize(ImVec2(200 * scale, (this->scene_.sceneObjects_[0].model->GetAnimations().size() > 0 ? 500 : 420) * scale), ImGuiSetCond_Always);
		ImGui::Begin("Vulkan glTF 2.0 PBR", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
		ImGui::PushItemWidth(100.0f * scale);

		ui_->Text("www.saschawillems.de");
		ui_->Text("%.1d fps (%.2f ms)", lastFPS, (1000.0f / lastFPS));

		if (ui_->Header("Scene")) {
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
			if (ui_->Combo("File", selectedScene, scenes)) {
				vkDeviceWaitIdle(device);
				loadScene(scenes[selectedScene]);
				setupDescriptors();
			}
#else
			if (ui_->Button("Open gltf file")) {
				std::string filename = "";
#if defined(_WIN32)
				char buffer[MAX_PATH];
				OPENFILENAMEA ofn;
				ZeroMemory(&buffer, sizeof(buffer));
				ZeroMemory(&ofn, sizeof(ofn));
				ofn.lStructSize = sizeof(ofn);
				ofn.lpstrFilter = "glTF files\0*.gltf;*.glb\0";
				ofn.lpstrFile = buffer;
				ofn.nMaxFile = MAX_PATH;
				ofn.lpstrTitle = "Select a glTF file to load";
				ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
				if (GetOpenFileNameA(&ofn)) {
					filename = buffer;
				}
#elif defined(__linux__) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
				char buffer[1024];
				FILE *file = popen("zenity --title=\"Select a glTF file to load\" --file-filter=\"glTF files | *.gltf *.glb\" --file-selection", "r");
				if (file) {
					while (fgets(buffer, sizeof(buffer), file)) {
						filename += buffer;
					};
					filename.erase(std::remove(filename.begin(), filename.end(), '\n'), filename.end());
					std::cout << filename << std::endl;
				}
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
				opengltfFileButtonClicked = true;
				// We must load file in render()
#endif
				if (!filename.empty()) {
					vkDeviceWaitIdle(device.GetLogicalDeviceHandle());
					// this->LoadScene(filename);
					// this->SetupDescriptors();
				}
			}
#endif
			if (ui_->Combo("Environment##env", selectedEnvironment, environments)) {
				vkDeviceWaitIdle(device.GetLogicalDeviceHandle());
				// loadEnvironment(environments[selectedEnvironment]);
				// setupDescriptors();
			}
		}

		if (ui_->Header("Environment")) {
			ui_->Checkbox("Background", &displayBackground);
			ui_->Slider("Exposure", &this->scene_.params_.exposure, 0.1f, 10.0f);
			ui_->Slider("Gamma", &this->scene_.params_.gamma, 0.1f, 4.0f);
			ui_->Slider("IBL", &this->scene_.params_.scaleIBLAmbient, 0.0f, 1.0f);
		}

		if (ui_->Header("Camera")) {
			const std::vector<std::string> cameraTypes = { "Look at", "First Person" };            
			int32_t cameraTypeSelection = (int32_t)this->camera_.type;
			if (ui_->Combo("Type", &cameraTypeSelection, cameraTypes)) {
				this->camera_.type = (engine::scene::CameraType)cameraTypeSelection;
				this->RetCamera();
			}
		}

		if (ui_->Header("Debug view")) {
			const std::vector<std::string> bsdfType = {
				"Cook-torrance", "Kulla-Conty"
			};
			if (ui_->Combo("BSDF", &debugBsdfType, bsdfType))
				this->scene_.params_.debugBsdfType = static_cast<float>(debugBsdfType);
			const std::vector<std::string> debugNamesInputs = {
				"none", "Base color", "Normal", "Occlusion", "Emissive", "Metallic", "Roughness"
			};
			if (ui_->Combo("Inputs", &debugViewInputs, debugNamesInputs)) {
				this->scene_.params_.debugViewInputs = static_cast<float>(debugViewInputs);
			}
			const std::vector<std::string> debugNamesEquation = {
				"none", "Diff (l,n)", "F (l,h)", "G (l,v,h)", "D (h)", "Specular"
			};
			if (ui_->Combo("PBR equation", &debugViewEquation, debugNamesEquation)) {
				this->scene_.params_.debugViewEquation = static_cast<float>(debugViewEquation);
			}
		}

		if (this->scene_.sceneObjects_[0].model->GetAnimations().size() > 0) {
			if (ui_->Header("Animations")) {
				ui_->Checkbox("Animate", &animate);
				std::vector<std::string> animationNames;
				for (auto animation : this->scene_.sceneObjects_[0].model->GetAnimations()) {
					animationNames.push_back(animation.name);
				}
				ui_->Combo("Animation", &animationIndex, animationNames);
			}
		}

		ImGui::PopItemWidth();
		ImGui::End();
		ImGui::Render();

		ImDrawData* imDrawData = ImGui::GetDrawData();

		// Check if ui buffers need to be recreated
		if (imDrawData) {
			VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
			VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

			bool updateBuffers = (ui_->vertexBuffer.buffer == VK_NULL_HANDLE) || (ui_->vertexBuffer.count != imDrawData->TotalVtxCount) || (ui_->indexBuffer.buffer == VK_NULL_HANDLE) || (ui_->indexBuffer.count != imDrawData->TotalIdxCount);

			if (updateBuffers) {
				vkDeviceWaitIdle(device.GetLogicalDeviceHandle());
				if (ui_->vertexBuffer.buffer) {
					ui_->vertexBuffer.Destroy();
				}
				ui_->vertexBuffer.Create(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, vertexBufferSize);
				ui_->vertexBuffer.count = imDrawData->TotalVtxCount;
				if (ui_->indexBuffer.buffer) {
					ui_->indexBuffer.Destroy();
				}
				ui_->indexBuffer.Create(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, indexBufferSize);
				ui_->indexBuffer.count = imDrawData->TotalIdxCount;
			}

			// Upload data
			ImDrawVert* vtxDst = (ImDrawVert*)ui_->vertexBuffer.mapped;
			ImDrawIdx* idxDst = (ImDrawIdx*)ui_->indexBuffer.mapped;
			for (int n = 0; n < imDrawData->CmdListsCount; n++) {
				const ImDrawList* cmd_list = imDrawData->CmdLists[n];
				memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
				memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
				vtxDst += cmd_list->VtxBuffer.Size;
				idxDst += cmd_list->IdxBuffer.Size;
			}

			ui_->vertexBuffer.Flush();
			ui_->indexBuffer.Flush();

		}

    }


    void Application::RetCamera()
    {
        this->camera_.SetPosition({ 0.0f, 0.0f, 1.0f });
		this->camera_.SetRotation({ 0.0f, 0.0f, 0.0f });
		this->camera_.UpdateViewMatrix();
    }


    void Application::RenderLoop()
    {
        LOG_INFO("Application: start to run render loop...");
        auto&device = engine::core::Device::Instance();
        // destWidth = width;


		// destHeight = height;
		MSG msg;
		bool quitMessageReceived = false;
		while (!quitMessageReceived) {
			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
				if (msg.message == WM_QUIT) {
					LOG_INFO("Application: WM_QUIT received, exiting render loop...");
					quitMessageReceived = true;
					break;
				}
			}
			if (!IsIconic((HWND)this->window_.NativeHandle())) {
				this->RenderFrame();
			}
		}

        vkDeviceWaitIdle(device.GetLogicalDeviceHandle());
		LOG_INFO("Application: render loop exited, cleaning up...");
    }

    void Application::RenderFrame()
    {
        auto tStart = std::chrono::high_resolution_clock::now();

		ui_->updateTimer -= frameTimer;
		if (ui_->updateTimer <= 0.0f) {
			this->UpdateOverlay();
			ui_->updateTimer = 1.0f / 60.0f;
		}


		if (!this->renderer_->BeginFrame(this->window_.GetWidth(), this->window_.GetHeight()))
		{
			return;
		}

		this->renderer_->Render();

		this->ui_->Draw(this->renderer_->GetCurrentCommandBuffer());

		this->renderer_->EndFrame();


		this->frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = (float)tDiff / 1000.0f;
		this->UpdateCamera(frameTimer);
		this->fpsTimer += (float)tDiff;
		if (this->fpsTimer > 1000.0f) {
			this->lastFPS = static_cast<uint32_t>((float)this->frameCounter * (1000.0f / this->fpsTimer));
			this->fpsTimer = 0.0f;
			this->frameCounter = 0;
		}
    }

    void Application::UpdateCamera(float deltaTime)
    {
        this->camera_.Update(deltaTime);
        this->camera_.UpdateViewMatrix();
    }


    void Application::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_CLOSE:
            LOG_INFO("Application: WM_CLOSE received, shutting down...");
            this->prepared_ = false;
            DestroyWindow(hWnd);
            PostQuitMessage(0);
            break;
        case WM_PAINT:
            ValidateRect(hWnd, NULL);
            break;
        case WM_KEYDOWN:
            switch (wParam) {
            case KEY_P:
                this->paused_ = !this->paused_;
                break;
            case KEY_ESCAPE:
                PostQuitMessage(0);
                break;
            }
            if (this->camera_.type == engine::scene::CameraType::firstperson) {
                switch (wParam) {
                case KEY_W:
                    this->camera_.keys_.up = true;
                    break;
                case KEY_S:
                    this->camera_.keys_.down = true;
                    break;
                case KEY_A:
                    this->camera_.keys_.left = true;
                    break;
                case KEY_D:
                    this->camera_.keys_.right = true;
                    break;
                }
            }
            break;
        case WM_KEYUP:
            if (this->camera_.type == engine::scene::CameraType::firstperson) {
                switch (wParam) {
                case KEY_W:
                    this->camera_.keys_.up = false;
                    break;
                case KEY_S:
                    this->camera_.keys_.down = false;
                    break;
                case KEY_A:
                    this->camera_.keys_.left = false;
                    break;
                case KEY_D:
                    this->camera_.keys_.right = false;
                    break;
                }
            }
            break;
        case WM_LBUTTONDOWN:
            this->mousePos = glm::vec2(static_cast<float>(LOWORD(lParam)), static_cast<float>(HIWORD(lParam)));
            this->mouseButtons.left = true;
            break;
        case WM_RBUTTONDOWN:
            this->mousePos = glm::vec2(static_cast<float>(LOWORD(lParam)), static_cast<float>(HIWORD(lParam)));
            this->mouseButtons.right = true;
            break;
        case WM_MBUTTONDOWN:
            this->mousePos = glm::vec2(static_cast<float>(LOWORD(lParam)), static_cast<float>(HIWORD(lParam)));
            this->mouseButtons.middle = true;
            break;
        case WM_LBUTTONUP:
            this->mouseButtons.left = false;
            break;
        case WM_RBUTTONUP:
            this->mouseButtons.right = false;
            break;
        case WM_MBUTTONUP:
            this->mouseButtons.middle = false;
            break;
        case WM_MOUSEWHEEL: {
            short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            this->camera_.position_.z -= static_cast<float>(wheelDelta) * 0.005f * this->camera_.movementSpeed_;
            this->camera_.updated_ = true;
            break;
        }
        case WM_MOUSEMOVE:
            this->HandleMouseMove(LOWORD(lParam), HIWORD(lParam));
            break;
        case WM_SIZE:
            if (this->prepared_ && (wParam != SIZE_MINIMIZED)) {
                if (this->resizing_ || (wParam == SIZE_MAXIMIZED) || (wParam == SIZE_RESTORED)) {
                    this->dest_width_ = LOWORD(lParam);
                    this->dest_height_ = HIWORD(lParam);
                    this->window_.SetSize(LOWORD(lParam), HIWORD(lParam));
                    this->WindowResize();
                }
            }
            break;
        case WM_ENTERSIZEMOVE:
            this->resizing_ = true;
            break;
        case WM_EXITSIZEMOVE:
            this->resizing_ = false;
            break;
        case WM_DROPFILES: {
            HDROP hDrop = reinterpret_cast<HDROP>(wParam);
            char filename[MAX_PATH];
            uint32_t count = DragQueryFileA(hDrop, -1, nullptr, 0);
            for (uint32_t i = 0; i < count; ++i) {
                if (DragQueryFileA(hDrop, i, filename, MAX_PATH)) {
                    this->FileDropped(filename);
                }
                break;
            }
            DragFinish(hDrop);
            break;
        }
        }
    }

    void Application::HandleMouseMove(int32_t x, int32_t y) {
        int32_t dx = static_cast<int32_t>(this->mousePos.x) - x;
        int32_t dy = static_cast<int32_t>(this->mousePos.y) - y;

        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            this->mousePos = glm::vec2(static_cast<float>(x), static_cast<float>(y));
            return;
        }

        if (this->mouseButtons.left) {
            this->camera_.rotation_.x += dy * this->camera_.rotationSpeed_ * 0.5f;
            this->camera_.rotation_.y -= dx * this->camera_.rotationSpeed_ * 0.5f;
            this->camera_.updated_ = true;
        }
        if (this->mouseButtons.right) {
            this->camera_.position_.z += dy * 0.005f * this->camera_.movementSpeed_;
            this->camera_.updated_ = true;
        }
        if (this->mouseButtons.middle) {
            this->camera_.position_.x -= dx * 0.01f;
            this->camera_.position_.y -= dy * 0.01f;
            this->camera_.updated_ = true;
        }
        this->mousePos = glm::vec2(static_cast<float>(x), static_cast<float>(y));
    }

    void Application::WindowResize() {
        uint32_t width = this->window_.GetWidth();
        uint32_t height = this->window_.GetHeight();
        if (width == 0 || height == 0) {
            return;
        }

        this->renderer_->RequestResize(width, height);

        // Update camera aspect ratio for the new size
        this->camera_.SetPerspective(45.0f, (float)width / (float)height, 0.01f, 256.0f);
        this->camera_.UpdateViewMatrix();
    }

    void Application::FileDropped(std::string filename) {
        // TODO: reload scene from dropped file
    }

    void Application::InitResourceManager()
    {
        LOG_INFO("Initializing Resource Manager...");
        engine::resource::ResourceManager::Instance().Init();
    }


    void Application::LoadAssets()
    {
        std::string assetpath = engine::resource::ResourceManager::assetPath_;
        struct stat info;
		if (stat(assetpath.c_str(), &info) != 0) {
			std::string msg = "Could not locate asset path in \"" + assetpath + "\".\nMake sure binary is run from correct relative directory!";
			std::cerr << msg << std::endl;
			exit(-1);
		}

		std::string sceneFile = assetpath + "models/DamagedHelmet/glTF-Embedded/DamagedHelmet.gltf";
		sceneFile = assetpath + "models/MetalRoughSpheres/glTF-Embedded/MetalRoughSpheres.gltf";
		std::string envMapFile = assetpath + "environments/papermill.ktx";

		// loadScene(sceneFile.c_str());
        // this->modelHandel_ = engine::resource::ResourceManager::Instance().LoadModel(sceneFile.c_str());

		// loadEnvironment(envMapFile.c_str());
        // this->skyboxHandel_ = engine::resource::ResourceManager::Instance().LoadSkyBox(envMapFile.c_str());
    }

}


