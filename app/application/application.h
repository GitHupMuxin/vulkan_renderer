#pragma once
#include <string>
#include <vector>
#include "engine/core/keycodes.hpp"
#include "engine/platform/window.h"
#include "engine/render/renderer.h"
#include "engine/render/fullscreen_pass.h"
#include "engine/scene/camera.h"
#include "engine/resource/resource_manager.h"
#include "app/ui/ui.hpp"

namespace app
{
    class Application
    {
        private:
            std::vector<const char* >                   argv_;
            engine::platform::Window                    window_;
            std::unique_ptr<engine::render::Renderer>   renderer_;
            engine::scene::Camera                       camera_;
            engine::scene::Scene                        scene_;
            std::unique_ptr<ui::UI>                     ui_;

            bool                                        paused_ = false;
            bool                                        resizing_ = false;
            uint32_t                                    dest_width_ = 0;
            uint32_t                                    dest_height_ = 0;

            void                                        HandleMouseMove(int32_t x, int32_t y);
            void                                        WindowResize();
            void                                        FileDropped(std::string filename);

        public:
            float                                       frameTimer = 1.0f;
            glm::vec2                                   mousePos;
            uint32_t                                    lastFPS = 0;

            ui::MouseButtons                            mouseButtons;
            std::map<std::string, std::string>          environments;
    	    std::string                                 selectedEnvironment = "papermill";
            bool                                        displayBackground = true;

            int32_t                                     debugViewInputs = 0;
            int32_t                                     debugViewEquation = 0;
            int32_t                                     debugBsdfType = 0;

            int32_t                                     animationIndex = 0;
            float                                       animationTimer = 0.0f;
            bool                                        animate = true;

            float                                       fpsTimer = 0.0f;
            uint32_t                                    frameCounter = 0;

            bool                                        prepared_ = false;


            Application();
            
            ~Application();

            void                                        SetArgs(int args, char* argv[]);
            void                                        InitVulkan();
            void                                        SetUpWindow(HINSTANCE hInstance, WNDPROC wndproc);
            void                                        InitRenderer();
            void                                        AddRenderPass(std::unique_ptr<engine::render::RenderPass> renderPass);
            void                                        PrepareFrame();
            void                                        InitResourceManager();
            void                                        LoadAssets();
            void                                        InitCamera();
            void                                        InitScene();
            void                                        SetUpUI();
            void                                        UpdateOverlay();
            void                                        RetCamera();
            void                                        RenderLoop();
            void                                        RenderFrame();
            void                                        UpdateCamera(float deltaTime);  
            void                                        HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

};


}


