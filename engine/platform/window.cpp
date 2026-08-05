#include <iostream>
#include <windows.h>
#include "engine/platform/window.h"
#include "engine/utils/log.h"

namespace engine::platform
{
    class Window::Impl
    {
        private:
            Window* owner_;
            HWND hwnd_;
            HINSTANCE hInstance_;

        public:
            Impl() = delete;
            Impl(Window* owner) : owner_(owner) { }
            ~Impl() 
            { 
                if (this->hwnd_) {
                    DestroyWindow(this->hwnd_);
                    this->hwnd_ = nullptr;
                }
            }

            void Init(void* hinstance, void* wndproc)
            {
                this->hInstance_ = (HINSTANCE)hinstance;
                WNDCLASSEX wndClass;

                wndClass.cbSize = sizeof(WNDCLASSEX);
                wndClass.style = CS_HREDRAW | CS_VREDRAW;
                wndClass.lpfnWndProc = (WNDPROC)wndproc;
                wndClass.cbClsExtra = 0;
                wndClass.cbWndExtra = 0;
                wndClass.hInstance = this->hInstance_;
                wndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
                wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
                wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
                wndClass.lpszMenuName = NULL;
                wndClass.lpszClassName = this->owner_->windowDescription_.title_.c_str();
                wndClass.hIconSm = LoadIcon(NULL, IDI_WINLOGO);

                if (!RegisterClassEx(&wndClass)) {
                    std::cerr << "RegisterClassEx failed: " << GetLastError() << std::endl;
                    std::string str = "RegisterClassEx failed: " + GetLastError() + '\n';
                    LOG_ERROR(str);
                    exit(1);
                }

                int screenWidth = GetSystemMetrics(SM_CXSCREEN);
                int screenHeight = GetSystemMetrics(SM_CYSCREEN);

                uint32_t width = this->owner_->windowDescription_.width_;
                uint32_t height = this->owner_->windowDescription_.height_;
                bool& fullscreen = this->owner_->windowDescription_.fullscreen_;
                if (fullscreen) {
                    DEVMODE dmScreenSettings;
                    memset(&dmScreenSettings, 0, sizeof(dmScreenSettings));
                    dmScreenSettings.dmSize = sizeof(dmScreenSettings);
                    dmScreenSettings.dmPelsWidth = screenWidth;
                    dmScreenSettings.dmPelsHeight = screenHeight;
                    dmScreenSettings.dmBitsPerPel = 32;
                    dmScreenSettings.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
                    if ((width != (uint32_t)screenWidth) && (height != (uint32_t)screenHeight)) {
                        if (ChangeDisplaySettings(&dmScreenSettings, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)	{
                            if (MessageBox(NULL, "Fullscreen Mode not supported!\n Switch to window mode?", "Error", MB_YESNO | MB_ICONEXCLAMATION) == IDYES) 
                                fullscreen = false;
                            else
                                return;
                        }
                    }
                }

                DWORD dwExStyle;
                DWORD dwStyle;

                if (fullscreen) {
                    dwExStyle = WS_EX_APPWINDOW;
                    dwStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
                } else {
                    dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
                    dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
                }

                RECT windowRect;
                windowRect.left = 0L;
                windowRect.top = 0L;
                windowRect.right = fullscreen ? (long)screenWidth : (long)width;
                windowRect.bottom = fullscreen ? (long)screenHeight : (long)height;

                AdjustWindowRectEx(&windowRect, dwStyle, FALSE, dwExStyle);

                this->hwnd_ = CreateWindowEx(dwExStyle,
                    this->owner_->windowDescription_.title_.c_str(),
                    this->owner_->windowDescription_.title_.c_str(),
                    dwStyle | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                    0,
                    0,
                    windowRect.right - windowRect.left,
                    windowRect.bottom - windowRect.top,
                    NULL,
                    NULL,
                    this->hInstance_,
                    NULL);

                if (!fullscreen) {
                    uint32_t x = (GetSystemMetrics(SM_CXSCREEN) - windowRect.right) / 2;
                    uint32_t y = (GetSystemMetrics(SM_CYSCREEN) - windowRect.bottom) / 2;
                    SetWindowPos(this->hwnd_, 0, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
                }

                if (!this->hwnd_) {
                    LOG_ERROR("Could not create window!\n");
                    fflush(stdout);
                    exit(1);
                }

                ShowWindow(this->hwnd_, SW_SHOW);
                SetForegroundWindow(this->hwnd_);
                SetFocus(this->hwnd_);                
            }
   
            void* GetNativeHandle()
            {
                return this->hwnd_;
            }

            void* GetNativeInstance()
            {
                return this->hInstance_;
            }
    };


    WindowDescription::WindowDescription() { }
    

    WindowDescription::WindowDescription(std::string title, uint32_t width, uint32_t height, bool fullscreen) : title_(title), width_(width), height_(height), fullscreen_(fullscreen)
    {

    }

    WindowDescription::WindowDescription(const WindowDescription& windowDescription) : title_(windowDescription.title_), width_(windowDescription.width_), height_(windowDescription.height_), fullscreen_(windowDescription.fullscreen_)
    {

    }

    WindowDescription::~WindowDescription() { }
           
    Window::Window() { }

    Window::Window(const WindowDescription& windowDescription) : windowDescription_(windowDescription)
    {

    }

    Window::~Window() { }

    void Window::Init(void* hinstance, void* wndproc)
    {
        LOG_INFO("Window: start to init window...");
        this->impl_ = std::make_unique<Window::Impl>(this);
        this->impl_->Init(hinstance, wndproc);
    }

    void Window::SetDescription(const WindowDescription& windowDescription)
    {
        this->windowDescription_ = windowDescription;
    }

    void* Window::NativeHandle()
    {
        return (void*)this->impl_->GetNativeHandle();
    }

    void* Window::NativeInstance()
    {
        return (void*)this->impl_->GetNativeInstance();
    }

    uint32_t Window::GetWidth()
    {
        return this->windowDescription_.width_;
    }

    uint32_t Window::GetHeight()
    {
        return this->windowDescription_.height_;
    }

    void Window::SetSize(uint32_t width, uint32_t height)
    {
        this->windowDescription_.width_ = width;
        this->windowDescription_.height_ = height;
    }


    


} 







