#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum JBWControlType {
    JBW_CONTROL_PANEL = 1,
    JBW_CONTROL_LABEL = 2,
    JBW_CONTROL_BUTTON = 3,
    JBW_CONTROL_INPUT = 4,
    JBW_CONTROL_WEBVIEW = 5
} JBWControlType;

typedef struct JBWStyle {
    COLORREF bg;
    COLORREF text;
    COLORREF border;
    COLORREF shadow;
    int border_size;
    int radius;
    int shadow_size;
    int font_size;
    int font_weight;
    int padding_x;
    int padding_y;
    int transparent;
    int uses_custom_bg;
} JBWStyle;

typedef struct JBWLayout {
    int x;
    int y;
    int width;
    int height;
    int responsive;
    int x_pct;
    int y_pct;
    int w_pct;
    int h_pct;
    int min_width;
    int min_height;
    int max_width;
    int max_height;
} JBWLayout;

struct JBWApp;
typedef struct JBWControl {
    struct JBWApp* app;
    HWND hwnd;
    int id;
    int type;
    char* text;
    JBWStyle style;
    JBWLayout layout;
    HFONT font;
    HBRUSH brush_bg;
    HBRUSH brush_shadow;
    void* webview_opaque;
    struct JBWControl* next;
} JBWControl;

typedef struct JBWApp {
    HINSTANCE instance;
    HWND hwnd;
    int next_y;
    int next_id;
    COLORREF window_bg;
    HBRUSH window_brush;
    JBWControl* controls;
    JBWControl* pending_button;
} JBWApp;

extern void jbw_wv2_sync_bounds(void* opaque);
extern void jbw_wv2_destroy(void* opaque);
extern void* jbw_wv2_attach(uint64_t app_hwnd, uint64_t host_hwnd, const char* url_utf8);

static const char* JBW_CLASS_NAME = "JasbootWin32AppClass";
static const char* JBW_PANEL_CLASS_NAME = "JasbootWin32PanelClass";
static int jbw_classes_registered = 0;

static char* jbw_copy_text(const char* src, uint64_t len) {
    char* out = (char*)malloc((size_t)len + 1u);
    if (!out) return NULL;
    if (src && len) memcpy(out, src, (size_t)len);
    out[len] = '\0';
    return out;
}

static char* jbw_dup_cstr(const char* src) {
    size_t len = src ? strlen(src) : 0;
    return jbw_copy_text(src ? src : "", (uint64_t)len);
}

static COLORREF jbw_color_or_default(COLORREF candidate, COLORREF fallback) {
    return candidate == CLR_INVALID ? fallback : candidate;
}

static void jbw_style_defaults(JBWStyle* style, int type) {
    if (!style) return;
    memset(style, 0, sizeof(*style));
    style->bg = CLR_INVALID;
    style->text = RGB(31, 41, 55);
    style->border = RGB(203, 213, 225);
    style->shadow = RGB(148, 163, 184);
    style->border_size = 1;
    style->radius = 14;
    style->shadow_size = 0;
    style->font_size = (type == JBW_CONTROL_LABEL) ? 18 : 16;
    style->font_weight = (type == JBW_CONTROL_BUTTON) ? FW_BOLD : FW_NORMAL;
    style->padding_x = (type == JBW_CONTROL_BUTTON) ? 18 : 14;
    style->padding_y = (type == JBW_CONTROL_BUTTON) ? 10 : 8;
    style->transparent = (type == JBW_CONTROL_LABEL);
    style->uses_custom_bg = (type == JBW_CONTROL_PANEL || type == JBW_CONTROL_BUTTON);
    if (type == JBW_CONTROL_PANEL) {
        style->bg = RGB(255, 255, 255);
        style->border = RGB(226, 232, 240);
        style->shadow_size = 8;
    } else if (type == JBW_CONTROL_BUTTON) {
        style->bg = RGB(37, 99, 235);
        style->text = RGB(255, 255, 255);
        style->border = RGB(29, 78, 216);
        style->shadow = RGB(96, 165, 250);
        style->shadow_size = 6;
    } else if (type == JBW_CONTROL_INPUT) {
        style->bg = RGB(255, 255, 255);
        style->border = RGB(148, 163, 184);
    } else if (type == JBW_CONTROL_WEBVIEW) {
        style->transparent = 1;
        style->uses_custom_bg = 0;
    }
}

static void jbw_layout_defaults(JBWLayout* layout, int x, int y, int w, int h) {
    if (!layout) return;
    memset(layout, 0, sizeof(*layout));
    layout->x = x;
    layout->y = y;
    layout->width = w;
    layout->height = h;
    layout->min_width = 0;
    layout->min_height = 0;
    layout->max_width = 0;
    layout->max_height = 0;
}

static JBWControl* jbw_find_control_by_hwnd(JBWApp* app, HWND hwnd) {
    JBWControl* cur = app ? app->controls : NULL;
    while (cur) {
        if (cur->hwnd == hwnd) return cur;
        cur = cur->next;
    }
    return NULL;
}

static JBWControl* jbw_find_control_by_id(JBWApp* app, int id) {
    JBWControl* cur = app ? app->controls : NULL;
    while (cur) {
        if (cur->id == id) return cur;
        cur = cur->next;
    }
    return NULL;
}

static void jbw_refresh_font(JBWControl* control) {
    LOGFONTA lf;
    HDC hdc;
    if (!control || !control->hwnd) return;
    if (control->font) {
        DeleteObject(control->font);
        control->font = NULL;
    }
    memset(&lf, 0, sizeof(lf));
    hdc = GetDC(control->hwnd);
    lf.lfHeight = -MulDiv(control->style.font_size > 0 ? control->style.font_size : 16, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(control->hwnd, hdc);
    lf.lfWeight = control->style.font_weight > 0 ? control->style.font_weight : FW_NORMAL;
    lf.lfQuality = CLEARTYPE_QUALITY;
    strcpy(lf.lfFaceName, "Segoe UI");
    control->font = CreateFontIndirectA(&lf);
    if (control->font) SendMessageA(control->hwnd, WM_SETFONT, (WPARAM)control->font, TRUE);
}

static void jbw_refresh_brushes(JBWControl* control) {
    COLORREF bg;
    if (!control) return;
    if (control->brush_bg) {
        DeleteObject(control->brush_bg);
        control->brush_bg = NULL;
    }
    if (control->brush_shadow) {
        DeleteObject(control->brush_shadow);
        control->brush_shadow = NULL;
    }
    bg = jbw_color_or_default(control->style.bg, RGB(255, 255, 255));
    control->brush_bg = CreateSolidBrush(bg);
    control->brush_shadow = CreateSolidBrush(jbw_color_or_default(control->style.shadow, RGB(210, 214, 220)));
}

static int jbw_apply_limits(int value, int min_v, int max_v) {
    if (min_v > 0 && value < min_v) value = min_v;
    if (max_v > 0 && value > max_v) value = max_v;
    return value;
}

static void jbw_apply_control_layout(JBWControl* control) {
    RECT rc;
    int x;
    int y;
    int w;
    int h;
    if (!control || !control->app || !control->app->hwnd || !control->hwnd) return;
    GetClientRect(control->app->hwnd, &rc);
    if (control->layout.responsive) {
        x = (int)(((int64_t)(rc.right - rc.left) * control->layout.x_pct) / 1000);
        y = (int)(((int64_t)(rc.bottom - rc.top) * control->layout.y_pct) / 1000);
        w = (int)(((int64_t)(rc.right - rc.left) * control->layout.w_pct) / 1000);
        h = (int)(((int64_t)(rc.bottom - rc.top) * control->layout.h_pct) / 1000);
    } else {
        x = control->layout.x;
        y = control->layout.y;
        w = control->layout.width;
        h = control->layout.height;
    }
    w = jbw_apply_limits(w, control->layout.min_width, control->layout.max_width);
    h = jbw_apply_limits(h, control->layout.min_height, control->layout.max_height);
    MoveWindow(control->hwnd, x, y, w, h, TRUE);
    if (control->type == JBW_CONTROL_WEBVIEW && control->webview_opaque)
        jbw_wv2_sync_bounds(control->webview_opaque);
}

static void jbw_reflow_all(JBWApp* app) {
    JBWControl* cur = app ? app->controls : NULL;
    while (cur) {
        jbw_apply_control_layout(cur);
        cur = cur->next;
    }
}

static void jbw_draw_round_rect(HDC hdc, RECT rc, COLORREF fill, COLORREF border, int border_size, int radius) {
    HPEN pen = CreatePen(PS_SOLID, border_size > 0 ? border_size : 1, border);
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);
    HBRUSH old_brush = (HBRUSH)SelectObject(hdc, brush);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void jbw_draw_text_center(HDC hdc, RECT* rc, const char* text, COLORREF color) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextA(hdc, text ? text : "", -1, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static LRESULT CALLBACK jbw_panel_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    JBWControl* control = (JBWControl*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            RECT rc;
            BeginPaint(hwnd, &ps);
            GetClientRect(hwnd, &rc);
            if (control) {
                int shadow = control->style.shadow_size > 0 ? control->style.shadow_size : 0;
                if (shadow > 0) {
                    RECT shadow_rc = rc;
                    shadow_rc.left += shadow / 2;
                    shadow_rc.top += shadow / 2;
                    jbw_draw_round_rect(ps.hdc, shadow_rc, jbw_color_or_default(control->style.shadow, RGB(226, 232, 240)),
                        jbw_color_or_default(control->style.shadow, RGB(226, 232, 240)), 1, control->style.radius);
                }
                jbw_draw_round_rect(ps.hdc, rc,
                    jbw_color_or_default(control->style.bg, RGB(255, 255, 255)),
                    jbw_color_or_default(control->style.border, RGB(203, 213, 225)),
                    control->style.border_size,
                    control->style.radius);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK jbw_window_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    JBWApp* app = (JBWApp*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_SIZE:
            if (app) jbw_reflow_all(app);
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            JBWControl* control;
            HDC hdc = (HDC)wParam;
            HWND target = (HWND)lParam;
            if (!app) break;
            control = jbw_find_control_by_hwnd(app, target);
            if (!control) break;
            SetTextColor(hdc, jbw_color_or_default(control->style.text, RGB(31, 41, 55)));
            if (control->style.transparent && control->type == JBW_CONTROL_LABEL) {
                SetBkMode(hdc, TRANSPARENT);
                return (LRESULT)GetStockObject(NULL_BRUSH);
            }
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, jbw_color_or_default(control->style.bg, RGB(255, 255, 255)));
            return (LRESULT)(control->brush_bg ? control->brush_bg : GetStockObject(WHITE_BRUSH));
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lParam;
            JBWControl* control;
            RECT rc;
            RECT text_rc;
            if (!app || !di) break;
            control = jbw_find_control_by_id(app, (int)di->CtlID);
            if (!control || control->type != JBW_CONTROL_BUTTON) break;
            rc = di->rcItem;
            if (control->style.shadow_size > 0) {
                RECT shadow_rc = rc;
                shadow_rc.left += control->style.shadow_size / 2;
                shadow_rc.top += control->style.shadow_size / 2;
                jbw_draw_round_rect(di->hDC, shadow_rc,
                    jbw_color_or_default(control->style.shadow, RGB(191, 219, 254)),
                    jbw_color_or_default(control->style.shadow, RGB(191, 219, 254)),
                    1, control->style.radius);
            }
            if (di->itemState & ODS_SELECTED) {
                OffsetRect(&rc, 0, 1);
            }
            jbw_draw_round_rect(di->hDC, rc,
                jbw_color_or_default(control->style.bg, RGB(37, 99, 235)),
                jbw_color_or_default(control->style.border, RGB(29, 78, 216)),
                control->style.border_size,
                control->style.radius);
            text_rc = rc;
            InflateRect(&text_rc, -control->style.padding_x, -control->style.padding_y);
            if (control->font) SelectObject(di->hDC, control->font);
            jbw_draw_text_center(di->hDC, &text_rc, control->text, jbw_color_or_default(control->style.text, RGB(255, 255, 255)));
            return TRUE;
        }
        case WM_COMMAND: {
            if (HIWORD(wParam) == BN_CLICKED) {
                HWND control_hwnd = (HWND)lParam;
                JBWControl* control = app ? jbw_find_control_by_hwnd(app, control_hwnd) : NULL;
                if (control && control->type == JBW_CONTROL_BUTTON) {
                    app->pending_button = control;
                    return 0;
                }
            }
            break;
        }
        case WM_ERASEBKGND:
            if (app && app->window_brush) {
                RECT rc;
                HDC hdc = (HDC)wParam;
                GetClientRect(hwnd, &rc);
                FillRect(hdc, &rc, app->window_brush);
                return 1;
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static int jbw_register_classes(HINSTANCE instance) {
    WNDCLASSA wc;
    if (jbw_classes_registered) return 1;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = jbw_window_proc;
    wc.hInstance = instance;
    wc.lpszClassName = JBW_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = jbw_panel_proc;
    wc.hInstance = instance;
    wc.lpszClassName = JBW_PANEL_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 0;

    jbw_classes_registered = 1;
    return 1;
}

static JBWControl* jbw_control_new(JBWApp* app, int type, const char* text, int x, int y, int w, int h) {
    JBWControl* control;
    if (!app) return NULL;
    control = (JBWControl*)calloc(1, sizeof(JBWControl));
    if (!control) return NULL;
    control->app = app;
    control->id = app->next_id++;
    control->type = type;
    control->text = jbw_dup_cstr(text ? text : "");
    jbw_style_defaults(&control->style, type);
    jbw_layout_defaults(&control->layout, x, y, w, h);
    control->next = app->controls;
    app->controls = control;
    return control;
}

static void jbw_control_attach_hwnd(JBWControl* control, HWND hwnd) {
    if (!control) return;
    control->hwnd = hwnd;
    SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)control);
    jbw_refresh_brushes(control);
    jbw_refresh_font(control);
    jbw_apply_control_layout(control);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void jbw_control_set_text(JBWControl* control, const char* text) {
    if (!control) return;
    free(control->text);
    control->text = jbw_dup_cstr(text ? text : "");
    if (control->hwnd) SetWindowTextA(control->hwnd, control->text ? control->text : "");
}

static uint64_t jbw_create_text_control(JBWApp* app, const char* text, int type) {
    JBWControl* control;
    HWND hwnd;
    int x = 24;
    int y = app ? app->next_y : 0;
    int w = 760;
    int h = (type == JBW_CONTROL_BUTTON) ? 42 : 28;
    if (!app || !app->hwnd) return 0;
    control = jbw_control_new(app, type, text, x, y, w, h);
    if (!control) return 0;
    if (type == JBW_CONTROL_LABEL) {
        hwnd = CreateWindowExA(0, "STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, app->hwnd, NULL, app->instance, NULL);
        app->next_y += 38;
    } else if (type == JBW_CONTROL_BUTTON) {
        hwnd = CreateWindowExA(0, "BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            x, y, 240, h, app->hwnd, (HMENU)(INT_PTR)control->id, app->instance, NULL);
        control->layout.width = 240;
        app->next_y += 54;
    } else {
        hwnd = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            x, y, w, 34, app->hwnd, (HMENU)(INT_PTR)control->id, app->instance, NULL);
        control->layout.height = 34;
        app->next_y += 46;
    }
    if (!hwnd) return 0;
    jbw_control_attach_hwnd(control, hwnd);
    return (uint64_t)(uintptr_t)control;
}

__declspec(dllexport) uint64_t jbw_ventana_crear(uint64_t title_ptr, uint64_t title_len, uint64_t width, uint64_t height) {
    JBWApp* app;
    char* title;
    HINSTANCE instance = GetModuleHandleA(NULL);
    if (!jbw_register_classes(instance)) return 0;
    app = (JBWApp*)calloc(1, sizeof(JBWApp));
    if (!app) return 0;
    title = jbw_copy_text((const char*)(uintptr_t)title_ptr, title_len);
    if (!title) {
        free(app);
        return 0;
    }
    app->instance = instance;
    app->next_y = 20;
    app->next_id = 1000;
    app->window_bg = RGB(245, 247, 251);
    app->window_brush = CreateSolidBrush(app->window_bg);
    app->hwnd = CreateWindowExA(0, JBW_CLASS_NAME, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        (int)(width ? width : 960), (int)(height ? height : 680), NULL, NULL, instance, NULL);
    free(title);
    if (!app->hwnd) {
        if (app->window_brush) DeleteObject(app->window_brush);
        free(app);
        return 0;
    }
    SetWindowLongPtrA(app->hwnd, GWLP_USERDATA, (LONG_PTR)app);
    return (uint64_t)(uintptr_t)app;
}

__declspec(dllexport) uint64_t jbw_etiqueta_crear(uint64_t app_ptr, uint64_t text_ptr, uint64_t text_len, uint64_t unused) {
    char* text;
    uint64_t result;
    (void)unused;
    text = jbw_copy_text((const char*)(uintptr_t)text_ptr, text_len);
    if (!text) return 0;
    result = jbw_create_text_control((JBWApp*)(uintptr_t)app_ptr, text, JBW_CONTROL_LABEL);
    free(text);
    return result;
}

__declspec(dllexport) uint64_t jbw_boton_crear(uint64_t app_ptr, uint64_t text_ptr, uint64_t text_len, uint64_t unused) {
    char* text;
    uint64_t result;
    (void)unused;
    text = jbw_copy_text((const char*)(uintptr_t)text_ptr, text_len);
    if (!text) return 0;
    result = jbw_create_text_control((JBWApp*)(uintptr_t)app_ptr, text, JBW_CONTROL_BUTTON);
    free(text);
    return result;
}

__declspec(dllexport) uint64_t jbw_entrada_crear(uint64_t app_ptr, uint64_t text_ptr, uint64_t text_len, uint64_t unused) {
    char* text;
    uint64_t result;
    (void)unused;
    text = jbw_copy_text((const char*)(uintptr_t)text_ptr, text_len);
    if (!text) return 0;
    result = jbw_create_text_control((JBWApp*)(uintptr_t)app_ptr, text, JBW_CONTROL_INPUT);
    free(text);
    return result;
}

__declspec(dllexport) uint64_t jbw_panel_crear(uint64_t app_ptr, uint64_t unused0, uint64_t unused1, uint64_t unused2) {
    JBWApp* app = (JBWApp*)(uintptr_t)app_ptr;
    JBWControl* control;
    HWND hwnd;
    int x = 20;
    int y = app ? app->next_y : 0;
    if (!app || !app->hwnd) return 0;
    control = jbw_control_new(app, JBW_CONTROL_PANEL, "", x, y, 820, 160);
    if (!control) return 0;
    hwnd = CreateWindowExA(0, JBW_PANEL_CLASS_NAME, "", WS_CHILD | WS_VISIBLE, x, y, 820, 160, app->hwnd, NULL, app->instance, NULL);
    if (!hwnd) return 0;
    app->next_y += 178;
    jbw_control_attach_hwnd(control, hwnd);
    return (uint64_t)(uintptr_t)control;
}

__declspec(dllexport) uint64_t jbw_webview_crear(uint64_t app_ptr, uint64_t url_ptr, uint64_t url_len, uint64_t unused) {
    JBWApp* app = (JBWApp*)(uintptr_t)app_ptr;
    JBWControl* control;
    HWND host;
    char* url;
    void* wv;
    (void)unused;
    if (!app || !app->hwnd) return 0;
    url = jbw_copy_text((const char*)(uintptr_t)url_ptr, url_len);
    if (!url) return 0;
    control = jbw_control_new(app, JBW_CONTROL_WEBVIEW, "", 20, app->next_y, 800, 480);
    if (!control) {
        free(url);
        return 0;
    }
    host = CreateWindowExA(WS_EX_STATICEDGE, "STATIC", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 20,
        app->next_y, 800, 480, app->hwnd, NULL, app->instance, NULL);
    if (!host) {
        free(url);
        return 0;
    }
    app->next_y += 500;
    jbw_control_attach_hwnd(control, host);
    wv = jbw_wv2_attach((uint64_t)(uintptr_t)app->hwnd, (uint64_t)(uintptr_t)host, url);
    free(url);
    if (!wv) {
        DestroyWindow(host);
        control->hwnd = NULL;
        return 0;
    }
    control->webview_opaque = wv;
    return (uint64_t)(uintptr_t)control;
}

__declspec(dllexport) uint64_t jbw_app_color_fondo(uint64_t app_ptr, uint64_t color, uint64_t unused1, uint64_t unused2) {
    JBWApp* app = (JBWApp*)(uintptr_t)app_ptr;
    (void)unused1;
    (void)unused2;
    if (!app) return 0;
    app->window_bg = (COLORREF)color;
    if (app->window_brush) DeleteObject(app->window_brush);
    app->window_brush = CreateSolidBrush(app->window_bg);
    InvalidateRect(app->hwnd, NULL, TRUE);
    return 1;
}

__declspec(dllexport) uint64_t jbw_control_marco(uint64_t control_ptr, uint64_t x, uint64_t y, uint64_t packed_size) {
    JBWControl* control = (JBWControl*)(uintptr_t)control_ptr;
    if (!control) return 0;
    control->layout.responsive = 0;
    control->layout.x = (int)x;
    control->layout.y = (int)y;
    control->layout.width = (int)(packed_size & 0xFFFF);
    control->layout.height = (int)((packed_size >> 16) & 0xFFFF);
    jbw_apply_control_layout(control);
    return 1;
}

__declspec(dllexport) uint64_t jbw_control_responsivo(uint64_t control_ptr, uint64_t x_pct, uint64_t y_pct, uint64_t packed_size_pct) {
    JBWControl* control = (JBWControl*)(uintptr_t)control_ptr;
    if (!control) return 0;
    control->layout.responsive = 1;
    control->layout.x_pct = (int)x_pct;
    control->layout.y_pct = (int)y_pct;
    control->layout.w_pct = (int)(packed_size_pct & 0xFFFF);
    control->layout.h_pct = (int)((packed_size_pct >> 16) & 0xFFFF);
    jbw_apply_control_layout(control);
    return 1;
}

__declspec(dllexport) uint64_t jbw_control_limites(uint64_t control_ptr, uint64_t min_w, uint64_t min_h, uint64_t packed_max) {
    JBWControl* control = (JBWControl*)(uintptr_t)control_ptr;
    if (!control) return 0;
    control->layout.min_width = (int)min_w;
    control->layout.min_height = (int)min_h;
    control->layout.max_width = (int)(packed_max & 0xFFFF);
    control->layout.max_height = (int)((packed_max >> 16) & 0xFFFF);
    jbw_apply_control_layout(control);
    return 1;
}

__declspec(dllexport) uint64_t jbw_control_color_fondo(uint64_t control_ptr, uint64_t color, uint64_t unused1, uint64_t unused2) {
    JBWControl* control = (JBWControl*)(uintptr_t)control_ptr;
    (void)unused1;
    (void)unused2;
    if (!control) return 0;
    control->style.bg = (COLORREF)color;
    control->style.transparent = 0;
    jbw_refresh_brushes(control);
    InvalidateRect(control->hwnd, NULL, TRUE);
    return 1;
}

__declspec(dllexport) uint64_t jbw_control_color_texto(uint64_t control_ptr, uint64_t color, uint64_t unused1, uint64_t unused2) {
    JBWControl* control = (JBWControl*)(uintptr_t)control_ptr;
    (void)unused1;
    (void)unused2;
    if (!control) return 0;
    control->style.text = (COLORREF)color;
    InvalidateRect(control->hwnd, NULL, TRUE);
    return 1;
}

__declspec(dllexport) uint64_t jbw_control_color_borde(uint64_t control_ptr, uint64_t color, uint64_t unused1, uint64_t unused2) {
    JBWControl* control = (JBWControl*)(uintptr_t)control_ptr;
    (void)unused1;
    (void)unused2;
    if (!control) return 0;
    control->style.border = (COLORREF)color;
    InvalidateRect(control->hwnd, NULL, TRUE);
    return 1;
}

__declspec(dllexport) uint64_t jbw_control_borde(uint64_t control_ptr, uint64_t grosor, uint64_t radio, uint64_t unused) {
    JBWControl* control = (JBWControl*)(uintptr_t)control_ptr;
    (void)unused;
    if (!control) return 0;
    control->style.border_size = (int)grosor;
    control->style.radius = (int)radio;
    InvalidateRect(control->hwnd, NULL, TRUE);
    return 1;
}

__declspec(dllexport) uint64_t jbw_control_sombra(uint64_t control_ptr, uint64_t tamano, uint64_t color, uint64_t unused) {
    JBWControl* control = (JBWControl*)(uintptr_t)control_ptr;
    (void)unused;
    if (!control) return 0;
    control->style.shadow_size = (int)tamano;
    control->style.shadow = (COLORREF)color;
    jbw_refresh_brushes(control);
    InvalidateRect(control->hwnd, NULL, TRUE);
    return 1;
}

__declspec(dllexport) uint64_t jbw_control_fuente(uint64_t control_ptr, uint64_t tamano, uint64_t peso, uint64_t unused) {
    JBWControl* control = (JBWControl*)(uintptr_t)control_ptr;
    (void)unused;
    if (!control) return 0;
    control->style.font_size = (int)tamano;
    control->style.font_weight = (int)peso;
    jbw_refresh_font(control);
    InvalidateRect(control->hwnd, NULL, TRUE);
    return 1;
}

__declspec(dllexport) uint64_t jbw_control_padding(uint64_t control_ptr, uint64_t pad_x, uint64_t pad_y, uint64_t unused) {
    JBWControl* control = (JBWControl*)(uintptr_t)control_ptr;
    (void)unused;
    if (!control) return 0;
    control->style.padding_x = (int)pad_x;
    control->style.padding_y = (int)pad_y;
    InvalidateRect(control->hwnd, NULL, TRUE);
    return 1;
}

__declspec(dllexport) uint64_t jbw_control_texto(uint64_t control_ptr, uint64_t text_ptr, uint64_t text_len, uint64_t unused) {
    JBWControl* control = (JBWControl*)(uintptr_t)control_ptr;
    char* text;
    (void)unused;
    if (!control) return 0;
    text = jbw_copy_text((const char*)(uintptr_t)text_ptr, text_len);
    if (!text) return 0;
    jbw_control_set_text(control, text);
    free(text);
    InvalidateRect(control->hwnd, NULL, TRUE);
    return 1;
}

__declspec(dllexport) uint64_t jbw_control_visible(uint64_t control_ptr, uint64_t mostrar, uint64_t unused1, uint64_t unused2) {
    JBWControl* control = (JBWControl*)(uintptr_t)control_ptr;
    (void)unused1;
    (void)unused2;
    if (!control || !control->hwnd) return 0;
    ShowWindow(control->hwnd, mostrar ? SW_SHOW : SW_HIDE);
    return 1;
}

__declspec(dllexport) uint64_t jbw_mostrar(uint64_t app_ptr, uint64_t unused0, uint64_t unused1, uint64_t unused2) {
    JBWApp* app = (JBWApp*)(uintptr_t)app_ptr;
    (void)unused0;
    (void)unused1;
    (void)unused2;
    if (!app || !app->hwnd) return 0;
    ShowWindow(app->hwnd, SW_SHOW);
    UpdateWindow(app->hwnd);
    return 1;
}

__declspec(dllexport) uint64_t jbw_bucle(uint64_t app_ptr, uint64_t unused0, uint64_t unused1, uint64_t unused2) {
    MSG msg;
    JBWApp* app = (JBWApp*)(uintptr_t)app_ptr;
    (void)unused0;
    (void)unused1;
    (void)unused2;
    if (!app || !app->hwnd) return 0;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (uint64_t)(uint32_t)msg.wParam;
}

// Un mensaje por llamada: 1 = procesado, 0 = WM_QUIT (cerrar).
__declspec(dllexport) uint64_t jbw_ejecutar_paso(uint64_t app_ptr, uint64_t unused0, uint64_t unused1, uint64_t unused2) {
    MSG msg;
    JBWApp* app = (JBWApp*)(uintptr_t)app_ptr;
    (void)unused0;
    (void)unused1;
    (void)unused2;
    if (!app || !app->hwnd) return 0;
    if (GetMessageA(&msg, NULL, 0, 0) <= 0) return 0;
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
    return 1;
}

// Ultimo boton pulsado en el paso anterior (o 0); consume el pendiente.
__declspec(dllexport) uint64_t jbw_boton_pulsado(uint64_t app_ptr, uint64_t unused0, uint64_t unused1, uint64_t unused2) {
    JBWApp* app = (JBWApp*)(uintptr_t)app_ptr;
    JBWControl* c;
    (void)unused0;
    (void)unused1;
    (void)unused2;
    if (!app) return 0;
    c = app->pending_button;
    app->pending_button = NULL;
    return (uint64_t)(uintptr_t)c;
}

__declspec(dllexport) uint64_t jbw_destruir(uint64_t app_ptr, uint64_t unused0, uint64_t unused1, uint64_t unused2) {
    JBWApp* app = (JBWApp*)(uintptr_t)app_ptr;
    JBWControl* cur;
    JBWControl* next;
    (void)unused0;
    (void)unused1;
    (void)unused2;
    if (!app) return 0;
    cur = app->controls;
    while (cur) {
        next = cur->next;
        if (cur->webview_opaque) {
            jbw_wv2_destroy(cur->webview_opaque);
            cur->webview_opaque = NULL;
        }
        if (cur->font) DeleteObject(cur->font);
        if (cur->brush_bg) DeleteObject(cur->brush_bg);
        if (cur->brush_shadow) DeleteObject(cur->brush_shadow);
        free(cur->text);
        free(cur);
        cur = next;
    }
    app->controls = NULL;
    if (app->hwnd && IsWindow(app->hwnd)) DestroyWindow(app->hwnd);
    if (app->window_brush) DeleteObject(app->window_brush);
    free(app);
    return 1;
}
