/* WebView2 (Edge Chromium) embebido — requiere WebView2Loader.dll junto a jasboot_win32_bridge.dll
 * y el runtime WebView2 instalado en el sistema. */
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <cstdint>

#include "WebView2.h"

typedef HRESULT(STDMETHODCALLTYPE* CreateCoreWebView2EnvironmentWithOptionsProc)(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environmentCreatedHandler);

static CreateCoreWebView2EnvironmentWithOptionsProc g_pfnCreateEnv;

static int jbw_load_webview2_loader(void) {
    char dllPath[MAX_PATH];
    HMODULE self = GetModuleHandleA("jasboot_win32_bridge.dll");
    if (!self) self = GetModuleHandleA(NULL);
    if (!GetModuleFileNameA(self, dllPath, MAX_PATH)) return 0;
    char* slash = strrchr(dllPath, '\\');
    if (!slash) return 0;
    slash[1] = '\0';
    strcat(dllPath, "WebView2Loader.dll");
    HMODULE hm = LoadLibraryA(dllPath);
    if (!hm) hm = LoadLibraryA("WebView2Loader.dll");
    if (!hm) return 0;
    g_pfnCreateEnv = (CreateCoreWebView2EnvironmentWithOptionsProc)GetProcAddress(hm, "CreateCoreWebView2EnvironmentWithOptions");
    return g_pfnCreateEnv ? 1 : 0;
}

struct Wv2State {
    HWND host;
    ICoreWebView2Controller* controller;
    ICoreWebView2* core;
    char* url_utf8;
    Wv2State() : host(nullptr), controller(nullptr), core(nullptr), url_utf8(nullptr) {}
};

class EnvHandler final : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    ULONG m_ref;
    Wv2State* m_st;

public:
    explicit EnvHandler(Wv2State* st) : m_ref(1), m_st(st) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG c = --m_ref;
        if (c == 0) delete this;
        return c;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Environment* env) override;
};

class CtlHandler final : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    ULONG m_ref;
    Wv2State* m_st;

public:
    explicit CtlHandler(Wv2State* st) : m_ref(1), m_st(st) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG c = --m_ref;
        if (c == 0) delete this;
        return c;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Controller* controller) override;
};

HRESULT STDMETHODCALLTYPE EnvHandler::Invoke(HRESULT errorCode, ICoreWebView2Environment* env) {
    if (FAILED(errorCode) || !env) return errorCode;
    return env->CreateCoreWebView2Controller(m_st->host, new CtlHandler(m_st));
}

HRESULT STDMETHODCALLTYPE CtlHandler::Invoke(HRESULT errorCode, ICoreWebView2Controller* controller) {
    if (FAILED(errorCode) || !controller) return errorCode;
    m_st->controller = controller;
    controller->AddRef();
    HRESULT hr = controller->get_CoreWebView2(&m_st->core);
    if (FAILED(hr) || !m_st->core) return hr;
    if (m_st->url_utf8 && m_st->url_utf8[0]) {
        int nw = MultiByteToWideChar(CP_UTF8, 0, m_st->url_utf8, -1, nullptr, 0);
        if (nw > 1) {
            std::wstring w(static_cast<size_t>(nw), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, m_st->url_utf8, -1, &w[0], nw);
            m_st->core->Navigate(w.c_str());
        }
    }
    RECT rc;
    GetClientRect(m_st->host, &rc);
    controller->put_Bounds(rc);
    return S_OK;
}

extern "C" {

void* jbw_wv2_attach(uint64_t /* app_hwnd */, uint64_t host_hwnd_u, const char* url_utf8) {
    if (!jbw_load_webview2_loader()) {
        fprintf(stderr, "jasboot_win32_bridge: no WebView2Loader.dll junto a la DLL.\n");
        return nullptr;
    }
    static volatile long s_co = 0;
    if (InterlockedCompareExchange(&s_co, 1, 0) == 0) CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    auto* st = new Wv2State();
    st->host = (HWND)(uintptr_t)host_hwnd_u;
    st->url_utf8 = url_utf8 ? _strdup(url_utf8) : _strdup("about:blank");
    if (!st->url_utf8) {
        delete st;
        return nullptr;
    }

    EnvHandler* env_cb = new EnvHandler(st);
    HRESULT hr = g_pfnCreateEnv(nullptr, nullptr, nullptr, env_cb);
    if (FAILED(hr)) {
        env_cb->Release();
        free(st->url_utf8);
        delete st;
        fprintf(stderr, "jasboot_win32_bridge: CreateCoreWebView2Environment fallo (0x%lX)\n", (unsigned long)hr);
        return nullptr;
    }
    return st;
}

void jbw_wv2_sync_bounds(void* opaque) {
    auto* st = (Wv2State*)opaque;
    if (!st || !st->controller || !st->host) return;
    RECT rc;
    GetClientRect(st->host, &rc);
    st->controller->put_Bounds(rc);
}

void jbw_wv2_destroy(void* opaque) {
    auto* st = (Wv2State*)opaque;
    if (!st) return;
    if (st->core) {
        st->core->Release();
        st->core = nullptr;
    }
    if (st->controller) {
        st->controller->Close();
        st->controller->Release();
        st->controller = nullptr;
    }
    free(st->url_utf8);
    delete st;
}

} /* extern "C" */
