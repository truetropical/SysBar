#pragma warning(disable: 4530)
#pragma warning(disable: 4577)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <thread>
#include <vector>

#include "State.h"
#include "Config.h"
#include "Widget.h"
#include "Renderer.h"
#include "Sensor.h"
#include "SettingsDlg.h"
#include "HelpDlg.h"
#include "ThemeUtil.h"

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"dcomp.lib")
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d2d1.lib")
#pragma comment(lib,"dwrite.lib")
#pragma comment(lib,"winhttp.lib")
#pragma comment(lib,"comctl32.lib")

static State  g_state;
static HANDLE g_shutdownEvent = nullptr;
static HANDLE g_singleInstMutex = nullptr;

// --- Standardized Context Menu IDs ---
#define IDM_SETTINGS        1001
#define IDM_INSTRUCTIONS    1009
#define IDM_SUPPORT         1008
#define IDM_ABOUT           1004
#define IDM_LICENSE         1005
#define IDM_EXPLORE         1013
#define IDM_DONATE          1007
#define IDM_EXIT            1003

// Render a grayscale glyph mask and convert it to a premultiplied menu bitmap.
static HBITMAP CreateMenuGlyph(wchar_t glyph)
{
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 16;
    bmi.bmiHeader.biHeight = -16;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return nullptr;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) { DeleteDC(dc); return nullptr; }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    memset(bits, 0, 16 * 16 * 4);
    HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH, L"Segoe Fluent Icons");
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    wchar_t face[LF_FACESIZE]{};
    GetTextFaceW(dc, LF_FACESIZE, face);
    if (!font || _wcsicmp(face, L"Segoe Fluent Icons") != 0) {
        if (oldFont) SelectObject(dc, oldFont);
        if (font) DeleteObject(font);
        font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            DEFAULT_PITCH, L"Segoe MDL2 Assets");
        oldFont = font ? SelectObject(dc, font) : nullptr;
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT rect{ 0, 0, 16, 16 };
    DrawTextW(dc, &glyph, 1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    HIGHCONTRASTW contrast{ sizeof(contrast) };
    const bool highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON);
    COLORREF color = highContrast ? GetSysColor(COLOR_MENUTEXT) :
        (IsAppDarkTheme() ? RGB(255, 255, 255) : GetSysColor(COLOR_MENUTEXT));
    auto pixels = static_cast<unsigned int*>(bits);
    for (int i = 0; i < 256; ++i) {
        unsigned int alpha = pixels[i] & 0xff;
        pixels[i] = (alpha << 24) | ((GetRValue(color) * alpha / 255) << 16) |
            ((GetGValue(color) * alpha / 255) << 8) | (GetBValue(color) * alpha / 255);
    }
    if (oldFont) SelectObject(dc, oldFont);
    if (font) DeleteObject(font);
    SelectObject(dc, oldBitmap);
    DeleteDC(dc);
    return bitmap;
}

static void ApplyMenuGlyph(HMENU menu, UINT idOrPos, BOOL byPos, wchar_t glyph, std::vector<HBITMAP>& tracker)
{
    HBITMAP bitmap = CreateMenuGlyph(glyph);
    if (!bitmap) return;
    MENUITEMINFOW item{};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_BITMAP;
    item.hbmpItem = bitmap;
    if (SetMenuItemInfoW(menu, idOrPos, byPos, &item)) tracker.push_back(bitmap);
    else DeleteObject(bitmap);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    State* s = &g_state;

    if (m == s->wmTaskbarCreated && s->wmTaskbarCreated != 0)
    {
        HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (tray) SetWindowLongPtrW(h, GWLP_HWNDPARENT, (LONG_PTR)tray);

        AppBarUnregister(s);
        AppBarRegister(s);
        if (!s->isMenuOpen) RepositionWidget(s);
        return 0;
    }

    switch (m)
    {
    case WM_CREATE:
        return 0;

    case WM_SIZE:
    {
        UINT nw = LOWORD(l);
        UINT nh = HIWORD(l);
        if (nw > 0 && nh > 0)
            ResizeSwapChain(s, nw, nh);
        return 0;
    }

    case WM_DPICHANGED:
        return 0;

    case WM_DPICHANGED_AFTERPARENT:
    {
        if (!s->isMenuOpen) RepositionWidget(s);
        RECT rc{};
        GetClientRect(h, &rc);
        UINT nw = (UINT)(rc.right - rc.left);
        UINT nh = (UINT)(rc.bottom - rc.top);
        if (nw > 0 && nh > 0) ResizeSwapChain(s, nw, nh);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }

    case WM_TIMER:
        if (w == 1) {
            KillTimer(h, 1);
            s->isVisible = true;
            RepositionWidget(s);
            ShowWindow(h, SW_SHOWNA);
            return 0;
        }
        break;

    case WM_USER + 1:
        if (!s->isMenuOpen) RepositionWidget(s);
        return 0;

    case WM_USER + 2:
        if (w == ABN_POSCHANGED && !s->isMenuOpen) RepositionWidget(s);
        return 0;

    case WM_USER + 3:
        SaveConfig(s);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        Draw(s);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;

    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONUP:
        return 0;

    case WM_CONTEXTMENU:
    {
        if (s->isMenuOpen) return 0;
        s->isMenuOpen = true;

        // 1. Build the Help Flyout (Submenu)
        HMENU hHelp = CreatePopupMenu();
        AppendMenuW(hHelp, MF_STRING, IDM_INSTRUCTIONS, L"User Guide");
        AppendMenuW(hHelp, MF_STRING, IDM_SUPPORT, L"Tech Support");
        AppendMenuW(hHelp, MF_STRING, IDM_ABOUT, L"About");
        AppendMenuW(hHelp, MF_STRING, IDM_LICENSE, L"License");
        AppendMenuW(hHelp, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hHelp, MF_STRING, IDM_EXPLORE, L"Explore More Apps");
        AppendMenuW(hHelp, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hHelp, MF_STRING, IDM_DONATE, L"Support the Project");

        // 2. Build the Main Context Menu
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Settings");

        UINT helpPosition = 1;
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)hHelp, L"Help");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

        // 3. Apply Native Bitmaps for perfectly aligned columns
        std::vector<HBITMAP> menuBitmaps; // Tracks created bitmaps to delete them later

        ApplyMenuGlyph(menu, IDM_SETTINGS, FALSE, 0xE713, menuBitmaps);
        ApplyMenuGlyph(menu, helpPosition, TRUE, 0xE897, menuBitmaps);
        ApplyMenuGlyph(menu, IDM_EXIT, FALSE, 0xE711, menuBitmaps);
        ApplyMenuGlyph(hHelp, IDM_INSTRUCTIONS, FALSE, 0xE8A5, menuBitmaps);
        ApplyMenuGlyph(hHelp, IDM_SUPPORT, FALSE, 0xE95B, menuBitmaps);
        ApplyMenuGlyph(hHelp, IDM_ABOUT, FALSE, 0xE946, menuBitmaps);
        ApplyMenuGlyph(hHelp, IDM_LICENSE, FALSE, 0xE8D7, menuBitmaps);
        ApplyMenuGlyph(hHelp, IDM_EXPLORE, FALSE, 0xE71D, menuBitmaps);
        ApplyMenuGlyph(hHelp, IDM_DONATE, FALSE, 0xEB51, menuBitmaps);

        // --- Execute Menu ---
        APPBARDATA abd{ sizeof(abd) };
        abd.hWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
        SHAppBarMessage(ABM_GETTASKBARPOS, &abd);

        int  sx = GET_X_LPARAM(l);
        int  sy = GET_Y_LPARAM(l);
        UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON;

        RECT wr{};
        GetWindowRect(h, &wr);

        switch (abd.uEdge)
        {
        case ABE_BOTTOM: default: flags |= TPM_BOTTOMALIGN; sy = wr.top;    break;
        case ABE_TOP:             flags |= TPM_TOPALIGN;    sy = wr.bottom; break;
        case ABE_LEFT:            flags |= TPM_LEFTALIGN;   sx = wr.right;  break;
        case ABE_RIGHT:           flags |= TPM_RIGHTALIGN;  sx = wr.left;   break;
        }

        SetForegroundWindow(h);
        int cmd = TrackPopupMenu(menu, flags, sx, sy, 0, h, nullptr);
        PostMessageW(h, WM_NULL, 0, 0);

        // 4. Cleanup Memory
        s->isMenuOpen = false;
        DestroyMenu(menu);
        for (HBITMAP bmp : menuBitmaps) {
            DeleteObject(bmp);
        }

        // 5. Handle Clicks
        switch (cmd)
        {
        case IDM_SETTINGS:     ShowSettingsDialog(s);  break;
        case IDM_INSTRUCTIONS: OpenUserGuide();        break;
        case IDM_SUPPORT:      OpenTechSupport();      break;
        case IDM_ABOUT:        ShowAboutDialog(s);     break;
        case IDM_LICENSE:      OpenLicense();          break;
        case IDM_DONATE:       OpenDonationPage();     break;
        case IDM_EXPLORE:
            ShellExecuteW(nullptr, L"open", L"https://truetropical.github.io/", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case IDM_EXIT:         DestroyWindow(h);       break;
        }
        return 0;
    }

    case WM_DESTROY:
        AppBarUnregister(s);
        s->running = false;
        if (g_shutdownEvent) SetEvent(g_shutdownEvent);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    InitAppTheme();

    State* s = &g_state;
    s->hInst = hInst;

    if (wcsstr(GetCommandLineW(), L"--autostart") != nullptr) {
        s->isVisible = false;
    }
    else {
        s->isVisible = true;
    }

    {
        INITCOMMONCONTROLSEX icex{ sizeof(icex), ICC_LINK_CLASS };
        InitCommonControlsEx(&icex);
    }

    g_singleInstMutex = CreateMutexW(nullptr, FALSE, L"TaskbarWidget_SingleInstance");
    if (!g_singleInstMutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND existing = FindWindowW(WC_WIDGET, nullptr);
        if (existing) SetForegroundWindow(existing);
        CloseHandle(g_singleInstMutex);
        return 0;
    }

    g_shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_shutdownEvent) { CloseHandle(g_singleInstMutex); return 1; }

    s->wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    s->iniPath = GetIniPath();
    LoadConfig(s);

    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray) return 1;

    HWND notify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr);

    RECT tr{}, nr{};
    GetWindowRect(tray, &tr);
    if (notify) GetWindowRect(notify, &nr);

    s->widgetH = tr.bottom - tr.top;
    if (s->widgetH <= 0) s->widgetH = 40;
    s->widgetW = ComputeWidgetWidth(s);

    int startX = notify ? (nr.left - s->widgetW) : (tr.right - s->widgetW);
    if (startX < tr.left) startX = tr.left;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = WC_WIDGET;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    s->hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST,
        WC_WIDGET, L"",
        WS_POPUP | (s->isVisible ? WS_VISIBLE : 0),
        startX, tr.top, s->widgetW, s->widgetH, nullptr, nullptr, hInst, nullptr);

    if (!s->hwnd) return 1;
    if (tray) SetWindowLongPtrW(s->hwnd, GWLP_HWNDPARENT, (LONG_PTR)tray);

    if (!s->isVisible) {
        SetTimer(s->hwnd, 1, 5000, nullptr);
    }

    AppBarRegister(s);
    if (!InitDComp(s))
    {
        AppBarUnregister(s);
        CloseHandle(g_shutdownEvent);
        CloseHandle(g_singleInstMutex);
        return 1;
    }
    RepositionWidget(s);
    Draw(s);

    std::thread sensorThread(SensorThreadProc, s, g_shutdownEvent);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    SetEvent(g_shutdownEvent);
    if (sensorThread.joinable()) sensorThread.join();

    CloseHandle(g_shutdownEvent);
    g_shutdownEvent = nullptr;

    CloseHandle(g_singleInstMutex);
    g_singleInstMutex = nullptr;

    return 0;
}