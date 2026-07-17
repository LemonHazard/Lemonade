#ifdef OS_WINDOWS

#include <cstdio>
#include <windows.h>

int main(int, char**);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine, int nCmdShow)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    AllocConsole();

    FILE* pDummy;
    freopen_s(&pDummy, "CONIN$",  "r", stdin);
    freopen_s(&pDummy, "CONOUT$", "w", stdout);
    freopen_s(&pDummy, "CONOUT$", "w", stderr);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hOut, &mode);
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);

    int res = main(0, NULL);

    FreeConsole();

    return res;
}

#endif // OS_WINDOWS
