// Finpulse entry point: window, custom title bar, input routing (ADR-0002).
#include <windows.h>
#include <dwmapi.h>
#include <objbase.h>
#include <windowsx.h>
#include <wtsapi32.h>

#include <cstdarg>
#include <cstdio>
#include <memory>

#include "app/app.h"

using namespace fp;

namespace {

App* g_app = nullptr;
}  // namespace
FILE* g_dbg = nullptr;  // input trace, enabled by "debug-input.flag" next to the exe
void dbg(const char* fmt, ...) {
    if (!g_dbg) return;
    va_list a;
    va_start(a, fmt);
    fprintf(g_dbg, "%10lu ", GetTickCount());
    vfprintf(g_dbg, fmt, a);
    fputc('\n', g_dbg);
    fflush(g_dbg);
    va_end(a);
}
namespace {
constexpr UINT_PTR kTimerCaret = 1, kTimerLock = 2, kTimerToast = 3;
constexpr wchar_t kClass[] = L"FinpulseWindow";

float dipScale(HWND h) {
    UINT d = GetDpiForWindow(h);
    return (d ? d : 96) / 96.0f;
}

void render(HWND h) {
    if (!g_app) return;
    g_app->frame();
    // cursor follows the last frame
    POINT pt;
    GetCursorPos(&pt);
    if (WindowFromPoint(pt) == h) {
        LPCWSTR c = IDC_ARROW;
        if (g_app->ctx.cursor == ui::Cursor::Hand) c = IDC_HAND;
        else if (g_app->ctx.cursor == ui::Cursor::IBeam) c = IDC_IBEAM;
        ScreenToClient(h, &pt);
        if (pt.y >= 0) SetCursor(LoadCursorW(nullptr, c));
    }
    if (g_app->ctx.wantsCaret) SetTimer(h, kTimerCaret, 530, nullptr);
    else KillTimer(h, kTimerCaret);
    if (g_app->ctx.repaint) InvalidateRect(h, nullptr, FALSE);
}

void setMods() {
    auto& in = g_app->ctx.in;
    in.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    in.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    in.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
}

void mouseAt(HWND h, LPARAM lp) {
    float s = dipScale(h);
    g_app->ctx.in.mx = GET_X_LPARAM(lp) / s;
    g_app->ctx.in.my = GET_Y_LPARAM(lp) / s;
}

LRESULT hitTest(HWND h, LPARAM lp) {
    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    ScreenToClient(h, &pt);
    RECT rc;
    GetClientRect(h, &rc);
    float s = dipScale(h);
    bool maxed = IsZoomed(h);
    int border = (int)(6 * s);
    if (!maxed) {
        bool l = pt.x < border, r = pt.x >= rc.right - border, t = pt.y < border, b = pt.y >= rc.bottom - border;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (b && l) return HTBOTTOMLEFT;
        if (b && r) return HTBOTTOMRIGHT;
        if (l) return HTLEFT;
        if (r) return HTRIGHT;
        if (t) return HTTOP;
        if (b) return HTBOTTOM;
    }
    float x = pt.x / s, y = pt.y / s;
    if (y < ui::theme::titleBarH && g_app) {
        for (auto& ex : g_app->ctx.captionExclusions)
            if (ex.contains(x, y)) return HTCLIENT;
        if (!g_app->dialog) return HTCAPTION;
    }
    return HTCLIENT;
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_dbg && (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK || msg == WM_MOUSEMOVE ||
                  msg == WM_NCLBUTTONDOWN || msg == WM_MOUSEACTIVATE || msg == WM_ACTIVATE || msg == WM_MOUSELEAVE ||
                  msg == WM_CAPTURECHANGED || msg == WM_NCLBUTTONUP))
        dbg("msg=0x%04x wp=%llx x=%d y=%d", msg, (unsigned long long)wp, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    switch (msg) {
        case WM_NCCALCSIZE:
            if (wp) {
                if (IsZoomed(h)) {
                    // maximized windows overhang the monitor by the frame thickness
                    auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                    UINT dpi = GetDpiForWindow(h);
                    int fx = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    int fy = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    p->rgrc[0].left += fx;
                    p->rgrc[0].right -= fx;
                    p->rgrc[0].top += fy;
                    p->rgrc[0].bottom -= fy;
                }
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            LRESULT r = hitTest(h, lp);
            if (r != HTCLIENT) dbg("nchittest=%d", (int)r);
            return r;
        }
        case WM_NCACTIVATE:
            return DefWindowProcW(h, msg, wp, -1);  // do not let Windows repaint a non-client frame
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            float s = dipScale(h);
            mm->ptMinTrackSize.x = (LONG)(520 * s);
            mm->ptMinTrackSize.y = (LONG)(600 * s);
            return 0;
        }
        case WM_DPICHANGED: {
            if (g_app) g_app->gfx.setDpi((float)HIWORD(wp));
            auto* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SIZE:
            if (g_app) {
                if (wp == SIZE_MINIMIZED) { g_app->onMinimize(); return 0; }
                g_app->maximized = wp == SIZE_MAXIMIZED;
                g_app->gfx.resize();
                render(h);
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            EndPaint(h, &ps);
            render(h);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            if (!g_app) break;
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
            TrackMouseEvent(&tme);
            mouseAt(h, lp);
            g_app->onActivity();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (g_app) { g_app->ctx.in.mx = g_app->ctx.in.my = -1e5f; InvalidateRect(h, nullptr, FALSE); }
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            if (!g_app) break;
            SetCapture(h);
            mouseAt(h, lp);
            setMods();
            g_app->ctx.in.down = true;
            g_app->ctx.in.pressed = true;
            g_app->ctx.in.downX = g_app->ctx.in.mx;
            g_app->ctx.in.downY = g_app->ctx.in.my;
            g_app->ctx.in.dbl = msg == WM_LBUTTONDBLCLK;
            g_app->onActivity();
            render(h);
            return 0;
        case WM_LBUTTONUP:
            if (!g_app) break;
            ReleaseCapture();
            mouseAt(h, lp);
            setMods();
            g_app->ctx.in.down = false;
            g_app->ctx.in.released = true;
            render(h);
            return 0;
        case WM_RBUTTONDOWN:
            if (g_app) { mouseAt(h, lp); g_app->ctx.in.rpressed = true; render(h); }
            return 0;
        case WM_MOUSEWHEEL:
            if (g_app) { g_app->ctx.in.wheel += (float)GET_WHEEL_DELTA_WPARAM(wp); g_app->onActivity(); render(h); }
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (!g_app) break;
            if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;
            setMods();
            g_app->ctx.in.keys.push_back((unsigned)wp);
            g_app->onActivity();
            render(h);
            if (msg == WM_SYSKEYDOWN && wp != VK_LEFT && wp != VK_RIGHT && wp != VK_MENU) break;
            return 0;
        case WM_CHAR:
            if (g_app && wp >= 32) { g_app->ctx.in.chars += (wchar_t)wp; render(h); }
            return 0;
        case WM_SYSCHAR:
            return 0;  // no menu beep on Alt+key
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && g_app) {
                LPCWSTR c = IDC_ARROW;
                if (g_app->ctx.cursor == ui::Cursor::Hand) c = IDC_HAND;
                else if (g_app->ctx.cursor == ui::Cursor::IBeam) c = IDC_IBEAM;
                SetCursor(LoadCursorW(nullptr, c));
                return TRUE;
            }
            break;
        case WM_TIMER:
            if (!g_app) break;
            if (wp == kTimerLock) { g_app->onTimer(); InvalidateRect(h, nullptr, FALSE); }
            else InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_WTSSESSION_CHANGE:
            if (g_app && (wp == WTS_SESSION_LOCK || wp == WTS_CONSOLE_DISCONNECT || wp == WTS_REMOTE_DISCONNECT)) {
                g_app->onSessionLock();
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        case WM_POWERBROADCAST:
            if (g_app && wp == PBT_APMSUSPEND) g_app->onSessionLock();
            return TRUE;
        case WM_ACTIVATE:
            if (g_app) InvalidateRect(h, nullptr, FALSE);
            break;
        case WM_CLOSE:
            if (g_app) {
                WINDOWPLACEMENT wpl{sizeof(wpl)};
                GetWindowPlacement(h, &wpl);
                RECT n = wpl.rcNormalPosition;
                wchar_t buf[96];
                swprintf(buf, 96, L"%ld,%ld,%ld,%ld,%d", n.left, n.top, n.right, n.bottom, IsZoomed(h) ? 1 : 0);
                g_app->lib.catalog().setSetting("window", buf);
                g_app->shutdown();
                g_app = nullptr;
            }
            WTSUnRegisterSessionNotification(h);
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ADR-0008: never let Windows Error Reporting upload crash dumps of this process.
void excludeFromWer() {
    HMODULE wer = LoadLibraryExW(L"wer.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!wer) return;
    using Fn = HRESULT(WINAPI*)(PCWSTR, BOOL);
    auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(wer, "WerAddExcludedApplication")));
    if (fn) fn(exePath().c_str(), FALSE);
    FreeLibrary(wer);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Finpulse.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(kClass, nullptr);
        if (other) {
            if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }
    excludeFromWer();
    // ADR-0008: no "check online for a solution" dialog; a local notice instead. Data is safe in SQLite WAL.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS*) -> LONG {
        MessageBoxW(nullptr, L"Finpulse stopped unexpectedly. Your data is saved, just start it again.", L"Finpulse",
                    MB_ICONERROR | MB_TOPMOST);
        return EXCEPTION_EXECUTE_HANDLER;
    });
    if (GetFileAttributesW((exeDirectory() + L"\\debug-input.flag").c_str()) != INVALID_FILE_ATTRIBUTES)
        g_dbg = _wfopen((exeDirectory() + L"\\debug-input.log").c_str(), L"a");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    // initial placement: centered 1280x820 DIP on the primary work area
    UINT sysDpi = GetDpiForSystem();
    float s = sysDpi / 96.0f;
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = std::min((int)(1280 * s), (int)(work.right - work.left) - 40);
    int hgt = std::min((int)(820 * s), (int)(work.bottom - work.top) - 40);
    int x = work.left + ((work.right - work.left) - w) / 2, y = work.top + ((work.bottom - work.top) - hgt) / 2;

    HWND h = CreateWindowExW(WS_EX_APPWINDOW, kClass, L"Finpulse", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, hgt,
                             nullptr, nullptr, inst, nullptr);
    if (!h) return 1;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    int corner = 2;  // DWMWCP_ROUND (Windows 11)
    DwmSetWindowAttribute(h, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof(corner));
    COLORREF border = RGB(0x23, 0x29, 0x37);
    DwmSetWindowAttribute(h, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));
    MARGINS m{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(h, &m);

    auto app = std::make_unique<App>();
    if (!app->init(h)) {
        MessageBoxW(h, L"Finpulse could not start: graphics or data folder unavailable.", L"Finpulse", MB_ICONERROR);
        return 1;
    }
    g_app = app.get();

    // restore window placement
    std::wstring wpS = app->lib.catalog().setting("window");
    int maxed = 0;
    if (!wpS.empty()) {
        RECT r{};
        if (swscanf(wpS.c_str(), L"%ld,%ld,%ld,%ld,%d", &r.left, &r.top, &r.right, &r.bottom, &maxed) == 5 &&
            r.right - r.left > 300 && r.bottom - r.top > 300) {
            WINDOWPLACEMENT wpl{sizeof(wpl)};
            wpl.rcNormalPosition = r;
            wpl.showCmd = SW_HIDE;
            SetWindowPlacement(h, &wpl);
        }
    }
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    WTSRegisterSessionNotification(h, NOTIFY_FOR_THIS_SESSION);
    SetTimer(h, kTimerLock, 5000, nullptr);
    ShowWindow(h, maxed ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(h);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_app = nullptr;
    app.reset();
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}
