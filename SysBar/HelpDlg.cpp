#include "HelpDlg.h"
#include "Config.h"
#include "DialogUtils.h"
#include "ThemeUtil.h"
#include <commctrl.h>
#include <winver.h>
#include <vector>

#pragma comment(lib, "version.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

static const wchar_t* SITE_BASE = L"https://truetropical.github.io";

#define IDC_SYSLINK 201

static HWND CreateSysLink(HWND parent, HINSTANCE hInst, const wchar_t* text, int x, int y, int w, int h)
{
    HWND hLink = CreateWindowExW(0, WC_LINK, text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, (HMENU)IDC_SYSLINK, hInst, nullptr);
    SendMessageW(hLink, WM_SETFONT, (WPARAM)GetUIFont(), TRUE);
    if (IsAppDarkTheme()) SetWindowTheme(hLink, L"DarkMode_Explorer", nullptr);
    return hLink;
}

static void CreateOKButton(HWND parent, HINSTANCE hInst, int winW, int y)
{
    HWND hOK = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, (winW - 88) / 2, y, 88, 26, parent, (HMENU)IDOK, hInst, nullptr);
    SendMessageW(hOK, WM_SETFONT, (WPARAM)GetUIFont(), TRUE);
    if (IsAppDarkTheme()) SetWindowTheme(hOK, L"DarkMode_Explorer", nullptr);
}

static LRESULT HandleCtlColor(WPARAM wParam)
{
    if (IsAppDarkTheme()) {
        SetTextColor((HDC)wParam, RGB(255, 255, 255));
        SetBkColor((HDC)wParam, RGB(32, 32, 32));
        return (LRESULT)GetDarkBrush();
    }
    SetBkColor((HDC)wParam, RGB(255, 255, 255));
    return (LRESULT)GetStockObject(WHITE_BRUSH);
}

static LRESULT HandleSysLinkClick(LPARAM lParam)
{
    LPNMHDR pnmh = (LPNMHDR)lParam;
    if (pnmh->idFrom == IDC_SYSLINK && (pnmh->code == NM_CLICK || pnmh->code == NM_RETURN)) {
        PNMLINK pNMLink = (PNMLINK)pnmh;
        ShellExecuteW(nullptr, L"open", pNMLink->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
        return 1;
    }
    return 0;
}

static void RegisterDialogClass(const wchar_t* className, WNDPROC proc, HINSTANCE hInst)
{
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = proc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = className;
    RegisterClassExW(&wc);
}

static const wchar_t* ABOUT_TEXT =
L"SysBar \u2014 Real-Time Hardware Monitor\r\n"
L"Author: Mutant Wizard (<a href=\"https://truetropical.github.io\">truetropical.github.io</a>)\r\n\r\n"
L"A lightweight, transparent taskbar widget that displays real-time "
L"hardware sensor data alongside your system clock. No ads. No telemetry. "
L"No internet access. All data comes from your local LibreHardwareMonitor instance.\r\n\r\n"
L"\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\r\n\r\n"
L"ATTRIBUTION\r\n\r\n"
L"LibreHardwareMonitor\r\n  Sensor data provider. License: MPL-2.0\r\n  <a href=\"https://github.com/LibreHardwareMonitor/LibreHardwareMonitor\">github.com/LibreHardwareMonitor</a>\r\n\r\n"
L"nlohmann/json\r\n  JSON parsing library. License: MIT\r\n  <a href=\"https://github.com/nlohmann/json\">github.com/nlohmann/json</a>\r\n\r\n"
L"All other code copyright \u00a9 2026 Mutant Wizard.\r\n\r\n"
L"Full details: <a href=\"https://truetropical.github.io/sysbar/sysbar-about.html\">truetropical.github.io/sysbar/sysbar-about.html</a>";

static std::wstring GetAboutVersion()
{
    std::vector<wchar_t> path(32768);
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return L"Version unavailable";
    DWORD ignored = 0;
    DWORD size = GetFileVersionInfoSizeW(path.data(), &ignored);
    if (!size) return L"Version unavailable";
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.data(), 0, size, data.data())) return L"Version unavailable";
    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoSize = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) ||
        infoSize < sizeof(VS_FIXEDFILEINFO) || info->dwSignature != 0xFEEF04BD) return L"Version unavailable";
    return L"Version " + std::to_wstring(HIWORD(info->dwProductVersionMS)) + L"." +
        std::to_wstring(LOWORD(info->dwProductVersionMS)) + L"." +
        std::to_wstring(HIWORD(info->dwProductVersionLS));
}

static State* s_aboutState = nullptr;

static LRESULT CALLBACK AboutWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_CREATE:
    {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(l);
        int winW = 420; int winH = 440; int margin = 15;
        ApplyWindowTheme(h);
        SetClientSizeAndPosition(h, winW, winH, s_aboutState);
        int btnH = 26; int btnY = winH - btnH - margin;
        CreateOKButton(h, cs->hInstance, winW, btnY);
        int textH = btnY - margin - 10;
        std::wstring about = ABOUT_TEXT;
        size_t lineEnd = about.find(L"\r\n");
        if (lineEnd != std::wstring::npos)
            about.insert(lineEnd + 2, GetAboutVersion() + L" \u2014 Windows 10 1809+ | x64\r\n");
        CreateSysLink(h, cs->hInstance, about.c_str(), margin, margin, winW - (margin * 2), textH);
        return 0;
    }
    
    case WM_ERASEBKGND:
    {
        // FIX: Replaces the ugly white dialog background class brush with the Dark brush
        if (IsAppDarkTheme()) {
            RECT rc;
            GetClientRect(h, &rc);
            FillRect((HDC)w, &rc, GetDarkBrush());
            return 1;
        }
        return DefWindowProcW(h, m, w, l);
    }

    case WM_NOTIFY:         return HandleSysLinkClick(l);
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC: return HandleCtlColor(w);
    case WM_COMMAND:
        if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) DestroyWindow(h);
        return 0;
    case WM_CLOSE:   DestroyWindow(h); return 0;
    case WM_DESTROY: if (s_aboutState) s_aboutState->hwndAbout = nullptr; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void ShowAboutDialog(State* s)
{
    if (s->hwndAbout && IsWindow(s->hwndAbout)) { SetForegroundWindow(s->hwndAbout); return; }
    RegisterDialogClass(WC_ABOUT, AboutWndProc, s->hInst);
    s_aboutState = s;
    s->hwndAbout = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW, WC_ABOUT, L"SysBar \u2014 About & Attribution", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr, nullptr, s->hInst, nullptr);
}

void OpenUserGuide() { wchar_t url[256]; wcscpy_s(url, SITE_BASE); wcscat_s(url, L"/sysbar/sysbar-guide.html"); ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL); }
void OpenLicense() { wchar_t url[256]; wcscpy_s(url, SITE_BASE); wcscat_s(url, L"/license.html"); ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL); }
void OpenTechSupport() { wchar_t url[256]; wcscpy_s(url, SITE_BASE); wcscat_s(url, L"/tech-support.html"); ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL); }
void OpenDonationPage() { wchar_t url[256]; wcscpy_s(url, SITE_BASE); wcscat_s(url, L"/donate.html"); ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL); }