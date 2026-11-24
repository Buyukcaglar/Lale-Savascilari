/*
 * about_win32.c - the About box on Windows (see about.h), after the Civ64
 * port's: "About…" in the window's system menu above Close, a native modal
 * dialog built in memory (DLGTEMPLATE + DialogBoxIndirectParamW) with the
 * program icon, two credit lines, the repository link, a scrolling read-only
 * licence box and an OK button; light or dark following the system's app
 * theme (Settings > Personalization > Colors), re-themed when it changes.
 *
 * SDL owns the window procedure; the menu command arrives as WM_SYSCOMMAND
 * (sent, not posted), so the window is subclassed to see it.  While the box
 * is open the game does not run (this thread is in the dialog's loop) and
 * pc_resync_clock() afterwards keeps the emulated clock from catching up on
 * the time it was open - the game is paused, as in Civ64.
 */
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <stdlib.h>
#include <string.h>

#include "platform/about.h"
#include "platform/pc.h"
#include "platform/ui.h"
#include "platform/uitext.h"

#define SC_LALE_ABOUT    0x1080u      /* system-menu command (low 4 bits 0, below 0xF000) */
#define IDC_ABOUT_ICON    201
#define IDC_ABOUT_LINK    202
#define IDC_ABOUT_LICENSE 203

#define DARK_BG    RGB(32, 32, 32)
#define DARK_FIELD RGB(43, 43, 43)
#define DARK_TEXT  RGB(255, 255, 255)
#define DARK_LINK  RGB(96, 205, 255)

static HWND    g_hwnd;
static WNDPROC g_sdl_proc;
static int     g_open;
static int     g_dark;
static HBRUSH  g_bg, g_field;

/* ---- UTF-8 -> UTF-16 ---------------------------------------------------------- */

static wchar_t *wide(const char *s, int crlf)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t) * (crlf ? 2 : 1));
    wchar_t *t;
    int i, k = 0;
    if (!w)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    if (!crlf)
        return w;
    /* the edit control wants CRLF line ends */
    t = (wchar_t *)malloc((size_t)n * 2 * sizeof(wchar_t));
    if (!t)
        return w;
    for (i = 0; i < n; i++) {
        if (w[i] == L'\n')
            t[k++] = L'\r';
        t[k++] = w[i];
    }
    free(w);
    return t;
}

/* ---- theme ---------------------------------------------------------------------- */

static int theme_dark(void) { return ui_win32_dark(); }

/* The title bar of a window light or dark with the system's app mode. */
static void title_bar(HWND h)
{
    BOOL on = theme_dark() ? TRUE : FALSE;
    /* DWMWA_USE_IMMERSIVE_DARK_MODE: 20 (19 on Windows 10 before build 18985) */
    if (FAILED(DwmSetWindowAttribute(h, 20u, &on, sizeof on)))
        (void)DwmSetWindowAttribute(h, 19u, &on, sizeof on);
}

static void theme_apply(HWND dlg)
{
    BOOL on;
    HWND c = GetDlgItem(dlg, IDC_ABOUT_LICENSE);
    g_dark = theme_dark();
    on = g_dark ? TRUE : FALSE;
    (void)on;
    title_bar(dlg);
    if (c) {                                   /* the licence's scroll bar */
        (void)SetWindowTheme(c, g_dark ? L"DarkMode_Explorer" : NULL, NULL);
        (void)SendMessageW(c, WM_THEMECHANGED, 0u, 0);
    }
    (void)RedrawWindow(dlg, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

/* The OK button, owner-drawn: no Windows theme draws a dark push button.
   Light: the themed push button; dark: a flat face with a lighter border,
   the link blue when focused. */
static void draw_button(const DRAWITEMSTRUCT *di)
{
    RECT r = di->rcItem;
    wchar_t text[16] = L"";
    int pressed = (di->itemState & ODS_SELECTED) != 0u;
    int focus = (di->itemState & ODS_FOCUS) != 0u;
    HFONT font = (HFONT)SendMessageW(di->hwndItem, WM_GETFONT, 0u, 0);
    HGDIOBJ old = font ? SelectObject(di->hDC, font) : NULL;
    (void)GetWindowTextW(di->hwndItem, text, 16);
    if (g_dark) {
        HBRUSH face = CreateSolidBrush(pressed ? RGB(45, 45, 45) : RGB(60, 60, 60));
        HBRUSH edge = CreateSolidBrush(focus ? DARK_LINK : RGB(100, 100, 100));
        (void)FillRect(di->hDC, &r, g_bg);
        (void)FillRect(di->hDC, &r, face);
        (void)FrameRect(di->hDC, &r, edge);
        (void)DeleteObject(face);
        (void)DeleteObject(edge);
        (void)SetTextColor(di->hDC, DARK_TEXT);
    } else {
        HTHEME th = OpenThemeData(di->hwndItem, L"BUTTON");
        if (th) {
            (void)FillRect(di->hDC, &r, GetSysColorBrush(COLOR_BTNFACE));
            (void)DrawThemeBackground(th, di->hDC, BP_PUSHBUTTON, pressed ? PBS_PRESSED : PBS_DEFAULTED, &r, NULL);
            (void)CloseThemeData(th);
        } else {
            (void)DrawFrameControl(di->hDC, &r, DFC_BUTTON, DFCS_BUTTONPUSH | (pressed ? DFCS_PUSHED : 0u));
        }
        (void)SetTextColor(di->hDC, GetSysColor(COLOR_BTNTEXT));
        if (focus && (di->itemState & ODS_NOFOCUSRECT) == 0u) {
            RECT f = r;
            (void)InflateRect(&f, -3, -3);
            (void)DrawFocusRect(di->hDC, &f);
        }
    }
    (void)SetBkMode(di->hDC, TRANSPARENT);
    if (pressed)
        (void)OffsetRect(&r, 1, 1);
    (void)DrawTextW(di->hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (old)
        (void)SelectObject(di->hDC, old);
}

/* Word breaks of the licence box: at blanks only.  The edit control's own
   rule also breaks after letters it does not know, among them Turkish ş and
   ğ ("birleş-tirme"). */
static int CALLBACK break_at_blanks(LPWSTR text, int at, int len, int code)
{
    switch (code) {
    case WB_ISDELIMITER:
        return text[at] == L' ' || text[at] == L'\r' || text[at] == L'\n' || text[at] == L'\t';
    case WB_LEFT:
        while (at > 0 && text[at - 1] != L' ' && text[at - 1] != L'\n')
            at--;
        return at;
    case WB_RIGHT:
        while (at < len && text[at] != L' ' && text[at] != L'\r' && text[at] != L'\n')
            at++;
        return at;
    default:
        return 0;
    }
}

/* ---- the dialog ------------------------------------------------------------------- */

static INT_PTR CALLBACK about_proc(HWND dlg, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HICON icon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                       GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
        wchar_t *lic = wide(about_licenses, 1);
        if (icon)
            (void)SendDlgItemMessageW(dlg, IDC_ABOUT_ICON, STM_SETICON, (WPARAM)icon, 0);
        (void)SendDlgItemMessageW(dlg, IDC_ABOUT_LICENSE, EM_SETWORDBREAKPROC, 0, (LPARAM)break_at_blanks);
        if (lic) {
            (void)SetDlgItemTextW(dlg, IDC_ABOUT_LICENSE, lic);
            free(lic);
        }
        g_bg = CreateSolidBrush(DARK_BG);
        g_field = CreateSolidBrush(DARK_FIELD);
        theme_apply(dlg);
        (void)SetFocus(GetDlgItem(dlg, IDOK));
        if (getenv("LALE_UI_SHOT"))             /* test hook (ui_win32.c): a picture, then closed */
            (void)SetTimer(dlg, 1u, 500u, NULL);
        return FALSE;                          /* focus set */
    }
    case WM_TIMER:
        (void)KillTimer(dlg, 1u);
        ui_win32_shot(dlg);
        EndDialog(dlg, 1);
        return TRUE;
    case WM_DESTROY:
        if (g_bg) { (void)DeleteObject(g_bg); g_bg = NULL; }
        if (g_field) { (void)DeleteObject(g_field); g_field = NULL; }
        break;
    case WM_SETTINGCHANGE:
        if (l && CompareStringOrdinal((LPCWCH)l, -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
            theme_apply(dlg);
        break;
    case WM_DRAWITEM:
        if (w == IDOK) {
            draw_button((const DRAWITEMSTRUCT *)l);
            return TRUE;
        }
        break;
    case WM_CTLCOLORDLG:
        if (g_dark)
            return (INT_PTR)g_bg;
        break;
    case WM_CTLCOLORSTATIC: {
        if ((HWND)l == GetDlgItem(dlg, IDC_ABOUT_LINK)) {
            (void)SetTextColor((HDC)w, g_dark ? DARK_LINK : RGB(0, 0, 204));
            (void)SetBkMode((HDC)w, TRANSPARENT);
            return (INT_PTR)(g_dark ? g_bg : GetSysColorBrush(COLOR_BTNFACE));
        }
        if (g_dark) {                          /* labels, icon, the read-only licence */
            int field = (HWND)l == GetDlgItem(dlg, IDC_ABOUT_LICENSE);
            (void)SetTextColor((HDC)w, DARK_TEXT);
            (void)SetBkColor((HDC)w, field ? DARK_FIELD : DARK_BG);
            return (INT_PTR)(field ? g_field : g_bg);
        }
        break;
    }
    case WM_SETCURSOR:
        if ((HWND)w == GetDlgItem(dlg, IDC_ABOUT_LINK)) {
            (void)SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32649)));   /* IDC_HAND */
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(w) == IDC_ABOUT_LINK && HIWORD(w) == STN_CLICKED) {
            wchar_t *u = wide(about_url, 0);
            if (u) {
                (void)ShellExecuteW(dlg, L"open", u, NULL, NULL, SW_SHOWNORMAL);
                free(u);
            }
            return TRUE;
        }
        if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) {
            EndDialog(dlg, 1);
            return TRUE;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

/* DLGTEMPLATE builder: WORD-aligned header, DWORD-aligned items */
typedef struct Tmpl { WORD buf[1024]; size_t n; } Tmpl;

static void t_word(Tmpl *t, WORD v) { if (t->n < 1024u) t->buf[t->n++] = v; }
static void t_str(Tmpl *t, const wchar_t *s) { do t_word(t, (WORD)*s); while (*s++); }
static void t_item(Tmpl *t, DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls,
                   const wchar_t *text)
{
    if (t->n & 1u)
        t_word(t, 0);
    t_word(t, LOWORD(style)); t_word(t, HIWORD(style));
    t_word(t, 0); t_word(t, 0);                /* extended style */
    t_word(t, (WORD)x); t_word(t, (WORD)y); t_word(t, (WORD)cx); t_word(t, (WORD)cy);
    t_word(t, id);
    t_word(t, 0xFFFF); t_word(t, cls);
    t_str(t, text);
    t_word(t, 0);                              /* no creation data */
}

void about_show(void)
{
    static Tmpl t;
    DWORD style = DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    wchar_t *title = wide(about_title, 0), *l1 = wide(about_line1, 0), *l2 = wide(about_line2, 0),
            *url = wide(about_url, 0), *lic = wide(T_ABOUT_LICENSES, 0), *ok = wide(T_OK, 0);
    if (g_open || !title || !l1 || !l2 || !url || !lic || !ok)
        goto out;
    memset(&t, 0, sizeof t);
    t_word(&t, LOWORD(style)); t_word(&t, HIWORD(style));
    t_word(&t, 0); t_word(&t, 0);
    t_word(&t, 7);                             /* items */
    t_word(&t, 0); t_word(&t, 0); t_word(&t, 260); t_word(&t, 196);
    t_word(&t, 0);                             /* no menu */
    t_word(&t, 0);                             /* default class */
    t_str(&t, title);
    t_word(&t, 9);
    t_str(&t, L"Segoe UI");
    t_item(&t, WS_CHILD | WS_VISIBLE | SS_ICON, 10, 10, 21, 20, IDC_ABOUT_ICON, 0x0082, L"");
    t_item(&t, WS_CHILD | WS_VISIBLE | SS_LEFT, 42, 10, 208, 9, 0xFFFF, 0x0082, l1);
    t_item(&t, WS_CHILD | WS_VISIBLE | SS_LEFT, 42, 21, 208, 9, 0xFFFF, 0x0082, l2);
    t_item(&t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | SS_LEFT | SS_NOTIFY, 42, 32, 208, 9,
           IDC_ABOUT_LINK, 0x0082, url);
    t_item(&t, WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 48, 240, 9, 0xFFFF, 0x0082, lic);
    t_item(&t, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
           10, 59, 240, 108, IDC_ABOUT_LICENSE, 0x0081, L"");
    t_item(&t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 200, 174, 50, 15, IDOK, 0x0080, ok);
    g_open = 1;
    (void)DialogBoxIndirectParamW(GetModuleHandleW(NULL), (LPCDLGTEMPLATEW)t.buf, g_hwnd, about_proc, 0);
    g_open = 0;
    pc_resync_clock();                         /* the game was paused: no catching up */
out:
    free(title); free(l1); free(l2); free(url); free(lic); free(ok);
}

/* ---- the system menu ------------------------------------------------------------------ */

static LRESULT CALLBACK subclass_proc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    if (msg == WM_SYSCOMMAND && (w & 0xFFF0u) == SC_LALE_ABOUT) {
        about_show();
        return 0;
    }
    if (msg == WM_SETTINGCHANGE && l && CompareStringOrdinal((LPCWCH)l, -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
        title_bar(h);                          /* the game's window follows the system's mode too */
    return CallWindowProcW(g_sdl_proc, h, msg, w, l);
}

void about_install(SDL_Window *window)
{
    HMENU m;
    HWND h = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                          SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (!h || g_hwnd)
        return;
    g_hwnd = h;
    title_bar(h);                              /* the game's window: light or dark title bar */
    m = GetSystemMenu(h, FALSE);
    if (m) {
        /* "About…" in its own group just above Close (the menu's last item) */
        int n = GetMenuItemCount(m);
        int at = n > 0 ? n - 1 : 0;
        wchar_t *label = wide("&" T_ABOUT_MENU, 0);
        (void)InsertMenuW(m, (UINT)at, MF_BYPOSITION | MF_STRING, SC_LALE_ABOUT, label ? label : L"?");
        free(label);
        (void)InsertMenuW(m, (UINT)at + 1, MF_BYPOSITION | MF_SEPARATOR, 0u, NULL);
    }
    g_sdl_proc = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)subclass_proc);
}

#endif /* _WIN32 */
