#ifdef OS_WINDOWS

#include "../window.h"
#include "../logger.h"

#include <cstdio>
#include <cstdlib>
#include <windows.h>

struct Native_Window {
    WNDCLASSEXW wndclass;
    HWND        hwnd;
    HMODULE     huxtheme;
};

LRESULT WINAPI wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void window_set_color_theme(Window *window);
void window_make_active(Window *window);

void window_create(Window *window)
{
    window->native = (Native_Window *)std::calloc(1, sizeof(Native_Window));

    if (window->native == nullptr)
    {
        log_error("Can't create a window");
    }

    window->native->wndclass.cbSize        = sizeof(WNDCLASSEXW);
    window->native->wndclass.style         = CS_CLASSDC;
    window->native->wndclass.lpfnWndProc   = wnd_proc;
    window->native->wndclass.cbClsExtra    = 0L;
    window->native->wndclass.cbWndExtra    = 0L;
    window->native->wndclass.hInstance     = GetModuleHandle(nullptr);
    //window->native->wndclass.hIcon         = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    window->native->wndclass.hCursor       = nullptr;
    window->native->wndclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    window->native->wndclass.lpszMenuName  = nullptr;
    window->native->wndclass.lpszClassName = window->title.c_str();
    window->native->wndclass.hIconSm       = nullptr;

    RegisterClassExW(&window->native->wndclass);

    int x_pos = (GetSystemMetrics(SM_CXSCREEN) - window->width) / 2;
    int y_pos = (GetSystemMetrics(SM_CYSCREEN) - window->height) / 2;
    window->native->hwnd = CreateWindowW(window->native->wndclass.lpszClassName,
                                         window->title.c_str(),
                                         WS_OVERLAPPEDWINDOW,
                                         x_pos,
                                         y_pos,
                                         window->width,
                                         window->height,
                                         nullptr,
                                         nullptr,
                                         window->native->wndclass.hInstance,
                                         nullptr);
    SetPropW(window->native->hwnd, L"Window", (HANDLE)window);

    window_set_color_theme(window);

    log_info("Window created (%dx%d)", window->width, window->height);
}

void window_set_color_theme(Window *window)
{
    enum class WINDOWCOMPOSITIONATTRIB
    {
        WCA_UNDEFINED = 0,
        WCA_NCRENDERING_ENABLED = 1,
        WCA_NCRENDERING_POLICY = 2,
        WCA_TRANSITIONS_FORCEDISABLED = 3,
        WCA_ALLOW_NCPAINT = 4,
        WCA_CAPTION_BUTTON_BOUNDS = 5,
        WCA_NONCLIENT_RTL_LAYOUT = 6,
        WCA_FORCE_ICONIC_REPRESENTATION = 7,
        WCA_EXTENDED_FRAME_BOUNDS = 8,
        WCA_HAS_ICONIC_BITMAP = 9,
        WCA_THEME_ATTRIBUTES = 10,
        WCA_NCRENDERING_EXILED = 11,
        WCA_NCADORNMENTINFO = 12,
        WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
        WCA_VIDEO_OVERLAY_ACTIVE = 14,
        WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
        WCA_DISALLOW_PEEK = 16,
        WCA_CLOAK = 17,
        WCA_CLOAKED = 18,
        WCA_ACCENT_POLICY = 19,
        WCA_FREEZE_REPRESENTATION = 20,
        WCA_EVER_UNCLOAKED = 21,
        WCA_VISUAL_OWNER = 22,
        WCA_HOLOGRAPHIC = 23,
        WCA_EXCLUDED_FROM_DDA = 24,
        WCA_PASSIVEUPDATEMODE = 25,
        WCA_USEDARKMODECOLORS = 26,
        WCA_LAST = 27
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

    window->native->huxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (window->native->huxtheme == 0)
    {
        log_warn("can't load uxtheme.dll");
        return;
    }

    fnShouldAppsUseDarkMode ShouldAppsUseDarkMode;
    ShouldAppsUseDarkMode = (fnShouldAppsUseDarkMode)GetProcAddress(window->native->huxtheme, MAKEINTRESOURCEA(132));
    fnAllowDarkModeForWindow AllowDarkModeForWindow;
    AllowDarkModeForWindow = (fnAllowDarkModeForWindow)GetProcAddress(window->native->huxtheme, MAKEINTRESOURCEA(133));
    fnSetPreferredAppMode SetPreferredAppMode;
    SetPreferredAppMode = (fnSetPreferredAppMode)GetProcAddress(window->native->huxtheme, MAKEINTRESOURCEA(135));
    fnSetWindowCompositionAttribute SetWindowCompositionAttribute;

    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32 == 0)
    {
        log_warn("can't get module handle for user32.dll");
        return;
    }

    SetWindowCompositionAttribute = (fnSetWindowCompositionAttribute)(GetProcAddress(hUser32, "SetWindowCompositionAttribute"));

    BOOL dark = ShouldAppsUseDarkMode();
    //log_info("color theme: {}", dark ? "Dark" : "Light");
    SetPropW(window->native->hwnd, L"UseImmersiveDarkModeColors", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(dark)));
    AllowDarkModeForWindow(window->native->hwnd, true);
    SetPreferredAppMode(PreferredAppMode::AllowDark);
    WINDOWCOMPOSITIONATTRIBDATA data = { WINDOWCOMPOSITIONATTRIB::WCA_USEDARKMODECOLORS, &dark, sizeof(dark) };
    SetWindowCompositionAttribute(window->native->hwnd, &data);
}

void window_destroy(Window *window)
{
    DestroyWindow(window->native->hwnd);
    UnregisterClassW(window->native->wndclass.lpszClassName,
                     window->native->wndclass.hInstance);
    FreeLibrary(window->native->huxtheme);
    std::free(window->native);
}

void window_show(Window *window)
{
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
    ShowWindow(window->native->hwnd, SW_SHOWDEFAULT);
    UpdateWindow(window->native->hwnd);

    window_make_active(window);
}

void window_make_active(Window *window)
{
    if (!IsWindow(window->native->hwnd)) return;

    // 1. Restore the window if it is minimized
    if (IsIconic(window->native->hwnd)) {
        ShowWindow(window->native->hwnd, SW_RESTORE);
    }
    else {
        ShowWindow(window->native->hwnd, SW_SHOW);
    }

    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_MENU; // ALT
    SendInput(1, &input, sizeof(INPUT));

    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));

    // 2. Attach thread input to bypass focus restrictions
    DWORD foregroundThreadId = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    DWORD targetThreadId = GetWindowThreadProcessId(window->native->hwnd, NULL);

    if (foregroundThreadId != targetThreadId) {
        AttachThreadInput(foregroundThreadId, targetThreadId, TRUE);
        SetForegroundWindow(window->native->hwnd);
        SetFocus(window->native->hwnd);
        AttachThreadInput(foregroundThreadId, targetThreadId, FALSE);
    }
    else {
        SetForegroundWindow(window->native->hwnd);
        SetActiveWindow(window->native->hwnd);
        SetFocus(window->native->hwnd);
    }
}

void window_read_input(Window *window)
{
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_QUIT)
            window->closed = true;
    }
}

LRESULT WINAPI wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
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
        log_info("Window resized (%dx%d)", window->width, window->height);
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
        log_info("Window destroyed");
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

#endif // OS_WINDOWS
