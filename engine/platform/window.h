#pragma once
#include <string>
#include <vulkan/vulkan.h>
#include <memory>

namespace engine::platform
{

    struct WindowDescription
    {
        public:
            std::string     title_;
            uint32_t        width_ = 1280;
            uint32_t        height_ = 720;
            bool            fullscreen_ = false;

        WindowDescription();
        WindowDescription(std::string title, uint32_t width, uint32_t height, bool fullscreen);
        WindowDescription(const WindowDescription& windowDescription);
        ~WindowDescription();
    };

    class Window
    {
        public:
            WindowDescription       windowDescription_;

            Window();
            Window(const WindowDescription& windowDescription);
            ~Window();

            void                    Init(void* hinstance, void* wndproc);
            void                    SetDescription(const WindowDescription& windowDescription);
            void*                   NativeHandle();
            void*                   NativeInstance();
            uint32_t                GetWidth();
            uint32_t                GetHeight();
            void                    SetSize(uint32_t width, uint32_t height);

        private:
            class Impl;
            std::unique_ptr<Impl>   impl_;
    };


}



