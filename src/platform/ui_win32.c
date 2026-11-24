/*
 * ui_win32.c - the port's dialogs on Windows (see ui.h).
 *
 * One window class serves both: a message (icon, wrapped text, a row of
 * buttons in a footer) and the import's progress (two lines, a bar, the
 * percentage, a cancel button).  Sizes follow the monitor's DPI and the
 * system's message font; colours follow the system's app mode:
 *
 *              light                         dark
 *   body       COLOR_WINDOW                  2B2B2B
 *   footer     COLOR_BTNFACE                 202020
 *   text       COLOR_WINDOWTEXT              white
 *   buttons    the theme's push button       flat 3C3C3C, accent edge when focused
 *   bar        the system's accent colour (DWM AccentColor)
 *
 * - what Windows 11's own dialogs look like in each mode.  The title bar is
 * switched with DWMWA_USE_IMMERSIVE_DARK_MODE.  A change of the mode while a
 * dialog is open (WM_SETTINGCHANGE "ImmersiveColorSet") re-themes it.
 *
 * ui_init() opts the process into the dark mode for what Windows draws
 * itself (pop-up menus, the file and folder pickers) with uxtheme's
 * SetPreferredAppMode (ordinal 135, Windows 10 1809 and later) - not a
 * documented export, but the one every program with a dark mode uses, and
 * looked up at run time: without it those parts simply stay light.
 *
 * Test hooks: with LALE_UI_SHOT=<prefix> every dialog saves a picture of
 * itself as <prefix><n>.bmp; a message then answers itself with the button
 * LALE_UI_ANSWER (default: the Esc button).  LALE_UI_THEME=light|dark
 * overrides the system's mode, for the pictures of the other one.
 */
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/ui.h"
#include "platform/uitext.h"

#define CLASS_NAME L"Lale64Dialog"
#define ID_BUTTON0 100
#define MAX_BUTTONS 4
#define SHOT_TIMER 1

typedef struct Dlg {
    HWND     hwnd, owner;
    int      dark, dpi;
    HFONT    font;
    HBRUSH   body, footer;
    COLORREF text, dim, accent;
    /* message */
    wchar_t *message;
    HICON    icon;
    int      icon_size;
    RECT     text_rc;
    HWND     btn[MAX_BUTTONS];
    int      nbtn, def, esc, result, done;
    int      footer_top;
    /* progress */
    int      is_progress, cancel, percent, shot;
    wchar_t  what[300], doing[300];
    double   fraction;
    RECT     what_rc, doing_rc, bar_rc, pct_rc;
} Dlg;

static Dlg  progress_dlg;
static int  class_ready, shot_count;

static int S(const Dlg *d, int px) { return MulDiv(px, d->dpi, 96); }

static wchar_t *wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s ? s : "", -1, NULL, 0);
    wchar_t *w = (wchar_t *)calloc((size_t)(n > 0 ? n : 1), sizeof(wchar_t));
    if (w && n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s ? s : "", -1, w, n);
    return w;
}

static void wide_into(wchar_t *out, int cap, const char *s)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, s ? s : "", -1, out, cap))
        out[cap - 1] = 0;
}

/* ---- theme ---------------------------------------------------------------------------- */

int ui_win32_dark(void)
{
    DWORD v = 1u, n = sizeof v;
    const char *force = getenv("LALE_UI_THEME");            /* test hook: light | dark */
    if (force && *force)
        return strcmp(force, "dark") == 0;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &n) != ERROR_SUCCESS)
        return 0;
    return v == 0u;
}

/* The system's accent colour; the stock blues when it records none. */
static COLORREF theme_accent(int dark)
{
    DWORD v = 0, n = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", RRF_RT_REG_DWORD,
                     NULL, &v, &n) == ERROR_SUCCESS)
        return (COLORREF)(v & 0x00FFFFFFu);                 /* stored as ABGR = COLORREF */
    return dark ? RGB(96, 205, 255) : RGB(0, 103, 192);
}

static void theme_apply(Dlg *d)
{
    BOOL on;
    d->dark = ui_win32_dark();
    if (d->body)
        DeleteObject(d->body);
    if (d->footer)
        DeleteObject(d->footer);
    d->body = CreateSolidBrush(d->dark ? RGB(43, 43, 43) : GetSysColor(COLOR_WINDOW));
    d->footer = CreateSolidBrush(d->dark ? RGB(32, 32, 32) : GetSysColor(COLOR_BTNFACE));
    d->text = d->dark ? RGB(255, 255, 255) : GetSysColor(COLOR_WINDOWTEXT);
    d->dim = d->dark ? RGB(180, 180, 180) : GetSysColor(COLOR_GRAYTEXT);
    d->accent = theme_accent(d->dark);
    on = d->dark ? TRUE : FALSE;
    /* DWMWA_USE_IMMERSIVE_DARK_MODE: 20 (19 on Windows 10 before build 18985) */
    if (FAILED(DwmSetWindowAttribute(d->hwnd, 20u, &on, sizeof on)))
        (void)DwmSetWindowAttribute(d->hwnd, 19u, &on, sizeof on);
    RedrawWindow(d->hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void ui_init(void)
{
    typedef LONG (WINAPI *VersionFn)(OSVERSIONINFOW *);
    typedef int (WINAPI *AppModeFn)(int);
    typedef void (WINAPI *FlushFn)(void);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    VersionFn version = nt ? (VersionFn)(void *)GetProcAddress(nt, "RtlGetVersion") : NULL;
    OSVERSIONINFOW v;
    memset(&v, 0, sizeof v);
    v.dwOSVersionInfoSize = sizeof v;
    if (!ux || !version || version(&v) != 0 || v.dwMajorVersion < 10 || v.dwBuildNumber < 17763)
        return;
    /* 135: AllowDarkModeForApp(TRUE) on 1809, SetPreferredAppMode(AllowDark = 1) since 1903 */
    AppModeFn mode = (AppModeFn)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    FlushFn flush = (FlushFn)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(136));    /* FlushMenuThemes */
    if (mode)
        (void)mode(1);
    if (flush)
        flush();
}

/* ---- drawing ---------------------------------------------------------------------------- */

static void draw_button(const Dlg *d, const DRAWITEMSTRUCT *di)
{
    RECT r = di->rcItem;
    wchar_t label[64] = L"";
    int pressed = (di->itemState & ODS_SELECTED) != 0u;
    int focus = (di->itemState & ODS_FOCUS) != 0u;
    HGDIOBJ old = SelectObject(di->hDC, d->font);
    GetWindowTextW(di->hwndItem, label, 64);
    FillRect(di->hDC, &r, d->footer);
    if (d->dark) {
        int round = S(d, 8);
        HBRUSH face = CreateSolidBrush(pressed ? RGB(50, 50, 50) : RGB(60, 60, 60));
        HPEN edge = CreatePen(PS_SOLID, 1, focus ? d->accent : RGB(86, 86, 86));
        HGDIOBJ ob = SelectObject(di->hDC, face), op = SelectObject(di->hDC, edge);
        RoundRect(di->hDC, r.left, r.top, r.right, r.bottom, round, round);
        SelectObject(di->hDC, ob);
        SelectObject(di->hDC, op);
        DeleteObject(face);
        DeleteObject(edge);
        SetTextColor(di->hDC, RGB(255, 255, 255));
    } else {
        HTHEME th = OpenThemeData(di->hwndItem, L"BUTTON");
        if (th) {
            DrawThemeBackground(th, di->hDC, BP_PUSHBUTTON, pressed ? PBS_PRESSED : focus ? PBS_DEFAULTED : PBS_NORMAL,
                                &r, NULL);
            CloseThemeData(th);
        } else {
            DrawFrameControl(di->hDC, &r, DFC_BUTTON, DFCS_BUTTONPUSH | (pressed ? DFCS_PUSHED : 0u));
        }
        SetTextColor(di->hDC, GetSysColor(COLOR_BTNTEXT));
    }
    SetBkMode(di->hDC, TRANSPARENT);
    DrawTextW(di->hDC, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(di->hDC, old);
}

static void paint(Dlg *d, HDC dc)
{
    RECT client, r;
    HGDIOBJ old = SelectObject(dc, d->font);
    GetClientRect(d->hwnd, &client);
    r = client;
    r.bottom = d->footer_top;
    FillRect(dc, &r, d->body);
    r = client;
    r.top = d->footer_top;
    FillRect(dc, &r, d->footer);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, d->text);
    if (!d->is_progress) {
        if (d->icon)
            DrawIconEx(dc, S(d, 20), S(d, 20), d->icon, d->icon_size, d->icon_size, 0, NULL, DI_NORMAL);
        r = d->text_rc;
        DrawTextW(dc, d->message, -1, &r, DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_EXPANDTABS);
    } else {
        wchar_t pct[16] = L"";
        HBRUSH track = CreateSolidBrush(d->dark ? RGB(70, 70, 70) : RGB(214, 214, 214));
        HBRUSH fill = CreateSolidBrush(d->accent);
        r = d->what_rc;
        DrawTextW(dc, d->what, -1, &r, DT_SINGLELINE | DT_NOPREFIX | DT_PATH_ELLIPSIS);
        SetTextColor(dc, d->dim);
        r = d->doing_rc;
        DrawTextW(dc, d->doing, -1, &r, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        r = d->bar_rc;
        FillRect(dc, &r, track);
        if (d->fraction >= 0.0) {
            double f = d->fraction > 1.0 ? 1.0 : d->fraction;
            r.right = r.left + (LONG)((double)(d->bar_rc.right - d->bar_rc.left) * f);
            _snwprintf(pct, 16, L"%%%d", d->percent);           /* Turkish puts the sign first */
        } else {                                                /* length unknown: a block going round */
            LONG w = d->bar_rc.right - d->bar_rc.left, x = (LONG)((GetTickCount() % 2000u) * (DWORD)w / 2000u);
            r.left = d->bar_rc.left + (x * 85) / 100;
            r.right = r.left + (w * 15) / 100;
        }
        FillRect(dc, &r, fill);
        r = d->pct_rc;
        DrawTextW(dc, pct, -1, &r, DT_SINGLELINE | DT_RIGHT | DT_NOPREFIX);
        DeleteObject(track);
        DeleteObject(fill);
    }
    SelectObject(dc, old);
}

/* The window as it is on the screen, title bar and all, to <prefix><n>.bmp. */
void ui_win32_shot(void *window)
{
    HWND hwnd = (HWND)window;
    const char *prefix = getenv("LALE_UI_SHOT");
    RECT wr;
    BITMAPINFO bi;
    void *bits = NULL;
    char path[600];
    if (!prefix || !GetWindowRect(hwnd, &wr))
        return;
    int w = wr.right - wr.left, h = wr.bottom - wr.top;
    HDC screen = GetDC(NULL), mem = CreateCompatibleDC(screen);
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(mem, bmp);
        PrintWindow(hwnd, mem, 2u /* PW_RENDERFULLCONTENT */);
        GdiFlush();
        SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_XRGB8888, bits, w * 4);
        if (s) {
            snprintf(path, sizeof path, "%s%d.bmp", prefix, ++shot_count);
            SDL_SaveBMP(s, path);
            SDL_DestroySurface(s);
        }
        SelectObject(mem, old);
    }
    if (bmp)
        DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

static LRESULT CALLBACK dlg_proc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    Dlg *d = (Dlg *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!d)
        return DefWindowProcW(h, msg, w, l);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(d, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        draw_button(d, (const DRAWITEMSTRUCT *)l);
        return TRUE;
    case WM_COMMAND:
        if (HIWORD(w) == BN_CLICKED && LOWORD(w) >= ID_BUTTON0 && LOWORD(w) < ID_BUTTON0 + MAX_BUTTONS) {
            d->result = LOWORD(w) - ID_BUTTON0;
            d->done = d->cancel = 1;
        }
        return 0;
    case WM_CLOSE:
        d->result = d->esc;
        d->done = d->cancel = 1;
        return 0;
    case WM_SETTINGCHANGE:
        if (l && CompareStringOrdinal((LPCWCH)l, -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
            theme_apply(d);
        break;
    case WM_TIMER:
        if (w == SHOT_TIMER) {
            const char *answer = getenv("LALE_UI_ANSWER");
            KillTimer(h, SHOT_TIMER);
            ui_win32_shot(d->hwnd);
            if (!d->is_progress) {
                d->result = answer ? atoi(answer) : d->esc;
                d->done = 1;
            }
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(h, msg, w, l);
}

/* ---- building a window --------------------------------------------------------------------- */

static HWND owner_of(SDL_Window *parent)
{
    return parent ? (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(parent), SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                                 NULL) : NULL;
}

static int dpi_of(HWND owner)
{
    typedef UINT (WINAPI *DpiWinFn)(HWND);
    typedef UINT (WINAPI *DpiSysFn)(void);
    HMODULE user = GetModuleHandleW(L"user32.dll");
    DpiWinFn for_window = (DpiWinFn)(void *)GetProcAddress(user, "GetDpiForWindow");      /* Windows 10 1607 */
    DpiSysFn for_system = (DpiSysFn)(void *)GetProcAddress(user, "GetDpiForSystem");
    UINT dpi = owner && for_window ? for_window(owner) : for_system ? for_system() : 96u;
    return dpi ? (int)dpi : 96;
}

static void start(Dlg *d, SDL_Window *parent)
{
    NONCLIENTMETRICSW m;
    if (!class_ready) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = dlg_proc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));                /* IDC_ARROW */
        wc.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
        wc.lpszClassName = CLASS_NAME;
        RegisterClassExW(&wc);
        class_ready = 1;
    }
    memset(d, 0, sizeof *d);
    d->owner = owner_of(parent);
    d->dpi = dpi_of(d->owner);
    memset(&m, 0, sizeof m);
    m.cbSize = sizeof m;
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0)) {
        m.lfMessageFont.lfHeight = -MulDiv(9, d->dpi, 72);                      /* the message font, at this DPI */
        d->font = CreateFontIndirectW(&m.lfMessageFont);
    }
    if (!d->font)
        d->font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

/* Creates the window with a client area of w x h, centred on its owner. */
static void create(Dlg *d, int w, int h)
{
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    RECT r = { 0, 0, w, h }, o;
    wchar_t *title = wide(T_TITLE);
    HMONITOR mon;
    MONITORINFO mi;
    AdjustWindowRectEx(&r, style, FALSE, ex);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    if (!d->owner || IsIconic(d->owner) || !GetWindowRect(d->owner, &o)) {
        POINT p = { 0, 0 };
        mon = MonitorFromPoint(p, MONITOR_DEFAULTTOPRIMARY);
        memset(&o, 0, sizeof o);
    } else
        mon = MonitorFromWindow(d->owner, MONITOR_DEFAULTTONEAREST);
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(mon, &mi);
    if (o.right == o.left)
        o = mi.rcWork;
    int x = o.left + (o.right - o.left - ww) / 2, y = o.top + (o.bottom - o.top - wh) / 2;
    if (x + ww > mi.rcWork.right) x = mi.rcWork.right - ww;
    if (y + wh > mi.rcWork.bottom) y = mi.rcWork.bottom - wh;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;
    d->hwnd = CreateWindowExW(ex, CLASS_NAME, title ? title : L"", style, x, y, ww, wh, d->owner, NULL,
                              GetModuleHandleW(NULL), NULL);
    free(title);
    if (d->hwnd)
        SetWindowLongPtrW(d->hwnd, GWLP_USERDATA, (LONG_PTR)d);
}

/* The buttons, right-aligned in the footer; returns the width they need. */
static int buttons_width(Dlg *d, HDC dc, const char *const *labels, int n, int *widths)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        wchar_t *w = wide(labels[i]);
        SIZE sz = { 0, 0 };
        if (w)
            GetTextExtentPoint32W(dc, w, (int)wcslen(w), &sz);
        free(w);
        widths[i] = sz.cx + S(d, 32) > S(d, 96) ? sz.cx + S(d, 32) : S(d, 96);
        total += widths[i] + (i ? S(d, 8) : 0);
    }
    return total;
}

static void add_buttons(Dlg *d, const char *const *labels, int n, const int *widths, int client_w)
{
    int x = client_w - S(d, 20), bh = S(d, 30), y = d->footer_top + S(d, 14);
    for (int i = n - 1; i >= 0; i--) {
        wchar_t *w = wide(labels[i]);
        x -= widths[i];
        d->btn[i] = CreateWindowExW(0, L"BUTTON", w ? w : L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                    x, y, widths[i], bh, d->hwnd, (HMENU)(INT_PTR)(ID_BUTTON0 + i),
                                    GetModuleHandleW(NULL), NULL);
        SendMessageW(d->btn[i], WM_SETFONT, (WPARAM)d->font, FALSE);
        free(w);
        x -= S(d, 8);
    }
    d->nbtn = n;
}

static void finish(Dlg *d)
{
    if (d->hwnd)
        DestroyWindow(d->hwnd);
    if (d->font && d->font != GetStockObject(DEFAULT_GUI_FONT))
        DeleteObject(d->font);
    if (d->body)
        DeleteObject(d->body);
    if (d->footer)
        DeleteObject(d->footer);
    free(d->message);
    memset(d, 0, sizeof *d);
}

/* One message of the thread's queue through the dialog: 1 when it was a key
   the dialog answers itself (Return, Esc). */
static void route(Dlg *d, MSG *msg)
{
    if (msg->message == WM_KEYDOWN && (msg->hwnd == d->hwnd || IsChild(d->hwnd, msg->hwnd))) {
        if (msg->wParam == VK_ESCAPE) {
            d->result = d->esc;
            d->done = d->cancel = 1;
            return;
        }
        if (msg->wParam == VK_RETURN && !d->is_progress) {
            HWND f = GetFocus();
            d->result = d->def;
            for (int i = 0; i < d->nbtn; i++)
                if (d->btn[i] == f)
                    d->result = i;
            d->done = 1;
            return;
        }
    }
    if (!IsDialogMessageW(d->hwnd, msg)) {
        TranslateMessage(msg);
        DispatchMessageW(msg);
    }
}

/* ---- the message ------------------------------------------------------------------------------- */

int ui_message(SDL_Window *parent, int error, const char *text, const char *const *buttons, int nbuttons,
               int def, int esc)
{
    static const char *const ok[] = { T_OK };
    Dlg dlg, *d = &dlg;
    int widths[MAX_BUTTONS], result;
    MSG msg;
    if (!buttons || nbuttons < 1) {
        buttons = ok;
        nbuttons = 1;
        def = esc = 0;
    }
    if (nbuttons > MAX_BUTTONS)
        nbuttons = MAX_BUTTONS;
    start(d, parent);
    d->message = wide(text);
    d->def = def;
    d->esc = esc;
    d->result = esc;
    d->icon_size = S(d, 32);
    /* the program's own icon; the system's error sign for an error */
    d->icon = error ? (HICON)LoadImageW(NULL, MAKEINTRESOURCEW(32513) /* IDI_ERROR */, IMAGE_ICON, d->icon_size,
                                        d->icon_size, LR_SHARED)
                    : (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1), IMAGE_ICON, d->icon_size,
                                        d->icon_size, LR_SHARED);

    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, d->font);
    RECT tr = { 0, 0, S(d, 460), 0 };
    DrawTextW(dc, d->message ? d->message : L"", -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_EXPANDTABS);
    int bw = buttons_width(d, dc, buttons, nbuttons, widths);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);

    int pad = S(d, 20), text_x = pad + d->icon_size + S(d, 16);
    int client_w = text_x + tr.right + pad;
    if (client_w < bw + 2 * pad)
        client_w = bw + 2 * pad;
    if (client_w < S(d, 380))
        client_w = S(d, 380);
    int body_h = (tr.bottom > d->icon_size ? tr.bottom : d->icon_size) + 2 * pad;
    d->footer_top = body_h;
    SetRect(&d->text_rc, text_x, pad, client_w - pad, pad + tr.bottom);
    create(d, client_w, body_h + S(d, 58));
    if (!d->hwnd) {                                         /* no window: at least say it */
        SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION, T_TITLE, text, parent);
        finish(d);
        return esc;
    }
    add_buttons(d, buttons, nbuttons, widths, client_w);
    theme_apply(d);
    if (d->owner)
        EnableWindow(d->owner, FALSE);
    ShowWindow(d->hwnd, SW_SHOW);
    SetForegroundWindow(d->hwnd);
    SetFocus(d->btn[def >= 0 && def < nbuttons ? def : 0]);
    if (error)
        MessageBeep(MB_ICONERROR);
    if (getenv("LALE_UI_SHOT"))
        SetTimer(d->hwnd, SHOT_TIMER, 500, NULL);
    while (!d->done) {
        BOOL got = GetMessageW(&msg, NULL, 0, 0);
        if (got <= 0) {                                     /* WM_QUIT: pass it on to the program */
            if (got == 0)
                PostQuitMessage((int)msg.wParam);
            break;
        }
        route(d, &msg);
    }
    result = d->result;
    if (d->owner) {
        EnableWindow(d->owner, TRUE);
        SetForegroundWindow(d->owner);
    }
    finish(d);
    return result;
}

/* ---- the progress --------------------------------------------------------------------------------- */

int ui_progress(SDL_Window *parent, const char *what, const char *doing, double fraction)
{
    static const char *const cancel[] = { T_CANCEL };
    Dlg *d = &progress_dlg;
    wchar_t w1[300], w2[300];
    MSG msg;
    if (!d->hwnd) {
        int widths[1];
        start(d, parent);
        d->is_progress = 1;
        HDC dc = GetDC(NULL);
        HGDIOBJ old = SelectObject(dc, d->font);
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        (void)buttons_width(d, dc, cancel, 1, widths);
        SelectObject(dc, old);
        ReleaseDC(NULL, dc);
        int pad = S(d, 20), cw = S(d, 480), line = tm.tmHeight, pct_w = S(d, 52), y = pad;
        SetRect(&d->what_rc, pad, y, cw - pad, y + line);
        y += line + S(d, 6);
        SetRect(&d->doing_rc, pad, y, cw - pad, y + line);
        y += line + S(d, 14);
        SetRect(&d->bar_rc, pad, y + (line - S(d, 6)) / 2, cw - pad - pct_w, y + (line - S(d, 6)) / 2 + S(d, 6));
        SetRect(&d->pct_rc, cw - pad - pct_w, y, cw - pad, y + line);
        y += line + pad;
        d->footer_top = y;
        d->fraction = -1.0;
        d->percent = -1;
        create(d, cw, y + S(d, 58));
        if (!d->hwnd) {
            finish(d);
            return 0;
        }
        add_buttons(d, cancel, 1, widths, cw);
        theme_apply(d);
        if (d->owner)
            EnableWindow(d->owner, FALSE);
        ShowWindow(d->hwnd, SW_SHOW);
        SetForegroundWindow(d->hwnd);
        SetFocus(d->btn[0]);
    }
    wide_into(w1, 300, what);
    wide_into(w2, 300, doing);
    int pct = fraction < 0.0 ? -1 : (int)(fraction * 100.0);
    if (pct > 100)
        pct = 100;
    d->fraction = fraction;
    if (pct != d->percent || pct < 0 || wcscmp(w1, d->what) != 0 || wcscmp(w2, d->doing) != 0) {
        static DWORD last;
        DWORD now = GetTickCount();
        if (pct >= 0 || now - last >= 40u || wcscmp(w2, d->doing) != 0) {   /* the going-round block: 25 a second */
            last = now;
            d->percent = pct;
            wcscpy(d->what, w1);
            wcscpy(d->doing, w2);
            InvalidateRect(d->hwnd, NULL, FALSE);
        }
    }
    if (!d->shot && getenv("LALE_UI_SHOT") && fraction > 0.3) {
        d->shot = 1;
        UpdateWindow(d->hwnd);
        ui_win32_shot(d->hwnd);
    }
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            d->cancel = 1;
            break;
        }
        route(d, &msg);
    }
    return d->cancel;
}

void ui_progress_close(void)
{
    Dlg *d = &progress_dlg;
    if (!d->hwnd)
        return;
    if (d->owner) {
        EnableWindow(d->owner, TRUE);
        SetForegroundWindow(d->owner);
    }
    finish(d);
}

#endif /* _WIN32 */
