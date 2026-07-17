#ifdef OS_WINDOWS

#include "../window.h"
#include "../logger.h"

#include <cstdio>
#include <cstdlib>
#include <windows.h>

class Native_Window
{
public:
    static LRESULT WINAPI wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void use_system_app_mode();

    operator HWND() const { return this->hwnd; }

    WNDCLASSEXW wndclass;
    HWND        hwnd;
    HMODULE     huxtheme;
};

Window::Window(const wstr &title, u32 width, u32 height) : width(width), height(height), closed(false)
{
    this->native = new Native_Window();
    this->native->wndclass.cbSize        = sizeof(WNDCLASSEXW);
    this->native->wndclass.style         = CS_CLASSDC;
    this->native->wndclass.lpfnWndProc   = this->native->wnd_proc;
    this->native->wndclass.cbClsExtra    = 0L;
    this->native->wndclass.cbWndExtra    = 0L;
    this->native->wndclass.hInstance     = GetModuleHandle(nullptr);
    //this->native->wndclass.hIcon         = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    this->native->wndclass.hCursor       = nullptr;
    this->native->wndclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    this->native->wndclass.lpszMenuName  = nullptr;
    this->native->wndclass.lpszClassName = title.c_str();
    this->native->wndclass.hIconSm       = nullptr;

    RegisterClassExW(&this->native->wndclass);

    this->native->hwnd = CreateWindowW(this->native->wndclass.lpszClassName,
                                       title.c_str(),
                                       WS_OVERLAPPEDWINDOW,
                                       (GetSystemMetrics(SM_CXSCREEN) - this->width)  / 2,
                                       (GetSystemMetrics(SM_CYSCREEN) - this->height) / 2,
                                       this->width,
                                       this->height,
                                       nullptr,
                                       nullptr,
                                       this->native->wndclass.hInstance,
                                       nullptr);

    SetPropW(this->native->hwnd, L"Window", (HANDLE)this);

    this->native->use_system_app_mode();

    log_info("Window created (%dx%d)", this->width, this->height);
}

Window::~Window()
{
    DestroyWindow(this->native->hwnd);
    UnregisterClassW(this->native->wndclass.lpszClassName,
                     this->native->wndclass.hInstance);
    FreeLibrary(this->native->huxtheme);

    delete this->native;
}

void Window::show()
{
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
    ShowWindow(this->native->hwnd, SW_SHOWDEFAULT);
    UpdateWindow(this->native->hwnd);
}

void Window::read_input()
{
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_QUIT) this->closed = true;
    }
}

LRESULT WINAPI Native_Window::wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto window = (Window *)GetPropW(hWnd, L"Window");

    switch (msg)
    {
    case WM_SIZE:
    {
        if (wParam == SIZE_MINIMIZED)
            return 0;
        if (window == nullptr)
        {
            log_error("Window not found");
            return 0;
        }
        window->width  = (UINT)LOWORD(lParam);
        window->height = (UINT)HIWORD(lParam);
    } return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
    } return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void Native_Window::use_system_app_mode()
{
    enum class WINDOWCOMPOSITIONATTRIB
    {
        WCA_UNDEFINED                     = 0,
        WCA_NCRENDERING_ENABLED           = 1,
        WCA_NCRENDERING_POLICY            = 2,
        WCA_TRANSITIONS_FORCEDISABLED     = 3,
        WCA_ALLOW_NCPAINT                 = 4,
        WCA_CAPTION_BUTTON_BOUNDS         = 5,
        WCA_NONCLIENT_RTL_LAYOUT          = 6,
        WCA_FORCE_ICONIC_REPRESENTATION   = 7,
        WCA_EXTENDED_FRAME_BOUNDS         = 8,
        WCA_HAS_ICONIC_BITMAP             = 9,
        WCA_THEME_ATTRIBUTES              = 10,
        WCA_NCRENDERING_EXILED            = 11,
        WCA_NCADORNMENTINFO               = 12,
        WCA_EXCLUDED_FROM_LIVEPREVIEW     = 13,
        WCA_VIDEO_OVERLAY_ACTIVE          = 14,
        WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
        WCA_DISALLOW_PEEK                 = 16,
        WCA_CLOAK                         = 17,
        WCA_CLOAKED                       = 18,
        WCA_ACCENT_POLICY                 = 19,
        WCA_FREEZE_REPRESENTATION         = 20,
        WCA_EVER_UNCLOAKED                = 21,
        WCA_VISUAL_OWNER                  = 22,
        WCA_HOLOGRAPHIC                   = 23,
        WCA_EXCLUDED_FROM_DDA             = 24,
        WCA_PASSIVEUPDATEMODE             = 25,
        WCA_USEDARKMODECOLORS             = 26,
        WCA_LAST                          = 27
    };

    struct WINDOWCOMPOSITIONATTRIBDATA
    {
        WINDOWCOMPOSITIONATTRIB Attrib;
        PVOID pvData;
        SIZE_T cbData;
    };

    enum class PreferredAppMode
    {
        Default,
        AllowDark,
        ForceDark,
        ForceLight,
        Max
    };

    using fnShouldAppsUseDarkMode = bool (WINAPI*)();
    using fnAllowDarkModeForWindow = bool (WINAPI*)(HWND hWnd, bool allow);
    using fnSetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode appMode);
    using fnSetWindowCompositionAttribute = BOOL(WINAPI*)(HWND hWnd, WINDOWCOMPOSITIONATTRIBDATA*);

    this->huxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (this->huxtheme == 0)
    {
        log_warn("Could not load uxtheme.dll");
        return;
    }

    fnShouldAppsUseDarkMode ShouldAppsUseDarkMode;
    ShouldAppsUseDarkMode = (fnShouldAppsUseDarkMode)GetProcAddress(this->huxtheme, MAKEINTRESOURCEA(132));
    fnAllowDarkModeForWindow AllowDarkModeForWindow;
    AllowDarkModeForWindow = (fnAllowDarkModeForWindow)GetProcAddress(this->huxtheme, MAKEINTRESOURCEA(133));
    fnSetPreferredAppMode SetPreferredAppMode;
    SetPreferredAppMode = (fnSetPreferredAppMode)GetProcAddress(this->huxtheme, MAKEINTRESOURCEA(135));
    fnSetWindowCompositionAttribute SetWindowCompositionAttribute;

    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32 == 0)
    {
        log_warn("Could not get module handle for user32.dll");
        return;
    }

    SetWindowCompositionAttribute = (fnSetWindowCompositionAttribute)(GetProcAddress(hUser32, "SetWindowCompositionAttribute"));

    BOOL dark = ShouldAppsUseDarkMode();
    log_info("System app mode: %s", dark ? "Dark" : "Light");
    SetPropW(this->hwnd, L"UseImmersiveDarkModeColors", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(dark)));
    AllowDarkModeForWindow(this->hwnd, true);
    SetPreferredAppMode(PreferredAppMode::AllowDark);
    WINDOWCOMPOSITIONATTRIBDATA data = { WINDOWCOMPOSITIONATTRIB::WCA_USEDARKMODECOLORS, &dark, sizeof(dark) };
    SetWindowCompositionAttribute(this->hwnd, &data);
}

#endif // OS_WINDOWS
