#include "pch-il2cpp.h"
#include "DirectX.h"
#include "Renderer.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "imgui/imstb_truetype.h"
#include "keybinds.h"
#include "menu.hpp"
#include "theme.hpp"
#include <mutex>
#include "logger.h"
#include "game.h"
#include "profiler.h"
#include "state.hpp"
#include <future>

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

HWND DirectX::window;
ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;
ID3D11RenderTargetView* pRenderTargetView = NULL;
D3D_PRESENT_FUNCTION oPresent = nullptr;
WNDPROC oWndProc;

HANDLE DirectX::hRenderSemaphore;
constexpr DWORD MAX_RENDER_THREAD_COUNT = 5; //Should be overkill for our purposes

typedef struct Cache
{
	ImGuiWindow* Window = nullptr;  //Window instance
	ImVec2       Winsize; //Size of the window
} cache_t;

static cache_t s_Cache;

ImVec2 DirectX::GetWindowSize()
{
    if (Screen_get_fullScreen(nullptr))
    {
        RECT rect;
        GetWindowRect(window, &rect);

        return { (float)(rect.right - rect.left),  (float)(rect.bottom - rect.top) };
    }

    return { (float)Screen_get_width(nullptr), (float)Screen_get_height(nullptr) };

}

LRESULT __stdcall dWndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (!State.ImGuiInitialized)
        return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);

    if (uMsg == WM_DPICHANGED && State.AdjustByDPI) {
        float dpi = HIWORD(wParam);
        State.dpiScale = dpi / 96.0f;
        State.dpiChanged = true;
        STREAM_DEBUG("DPI Scale: " << State.dpiScale);
    }

    if (uMsg == WM_SIZE) {
        // RenderTarget needs to be released because the resolution has changed 
        WaitForSingleObject(DirectX::hRenderSemaphore, INFINITE);
        if (pRenderTargetView) {
            pRenderTargetView->Release();
            pRenderTargetView = nullptr;
        }
        ReleaseSemaphore(DirectX::hRenderSemaphore, 1, NULL);
    }

    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    KeyBinds::WndProc(uMsg, wParam, lParam);

    if (KeyBinds::IsKeyPressed(State.KeyBinds.Toggle_Menu)) State.ShowMenu = !State.ShowMenu;

    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

bool ImGuiInitialization(IDXGISwapChain* pSwapChain) {
    if ((pDevice != NULL) || (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice)))) {
        pDevice->GetImmediateContext(&pContext);
        DXGI_SWAP_CHAIN_DESC sd;
        pSwapChain->GetDesc(&sd);
        DirectX::window = sd.OutputWindow;
        ID3D11Texture2D* pBackBuffer = NULL;
        pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
        if (!pBackBuffer)
            return false;
        pDevice->CreateRenderTargetView(pBackBuffer, NULL, &pRenderTargetView);
        pBackBuffer->Release();
        oWndProc = (WNDPROC)SetWindowLongPtr(DirectX::window, GWLP_WNDPROC, (LONG_PTR)dWndProc);
        if (State.AdjustByDPI) {
            State.dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(DirectX::window);
        }
        else {
            State.dpiScale = 1.0f;
        }
        State.dpiChanged = true;

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
        ImGui_ImplWin32_Init(DirectX::window);
        ImGui_ImplDX11_Init(pDevice, pContext);

        DirectX::hRenderSemaphore = CreateSemaphore(
            NULL,                                 // default security attributes
            MAX_RENDER_THREAD_COUNT,              // initial count
            MAX_RENDER_THREAD_COUNT,              // maximum count
            NULL);                                // unnamed semaphore);
        return true;
    }
    
    return false;
}

static void RebuildFont() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Arial.ttf", 14 * State.dpiScale, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    do {
        const ImWchar* glyph_ranges;
        wchar_t locale[LOCALE_NAME_MAX_LENGTH] = { 0 };
        ::GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
        if (!_wcsnicmp(locale, L"zh-", 3)) {
            // China
            glyph_ranges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
        }
        else if (!_wcsnicmp(locale, L"ja-", 3)) {
            // Japan
            glyph_ranges = io.Fonts->GetGlyphRangesJapanese();
        }
        else if (!_wcsnicmp(locale, L"ko-", 3)) {
            // Korea
            glyph_ranges = io.Fonts->GetGlyphRangesKorean();
        }
        else {
            break;
        }
        NONCLIENTMETRICSW nm = { sizeof(NONCLIENTMETRICSW) };
        if (!::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, nm.cbSize, &nm, 0))
            break;
        auto hMessageFont = ::CreateFontIndirectW(&nm.lfMessageFont);
        if (!hMessageFont)
            break;
        void* fontData = nullptr;
        HDC hdc = ::GetDC(DirectX::window);
        auto hDefaultFont = ::SelectObject(hdc, hMessageFont);
        do {
            // Try to get TTC first
            DWORD dwTableTag = 0x66637474;/*TTCTag*/
            DWORD dwSize = ::GetFontData(hdc, dwTableTag, 0, 0, 0);
            if (dwSize == GDI_ERROR) {
                // Maybe TTF
                dwTableTag = 0;
                dwSize = ::GetFontData(hdc, 0, 0, 0, 0);
                if (dwSize == GDI_ERROR)
                    break;
            }
            fontData = IM_ALLOC(dwSize);
            if (!fontData)
                break;
            if (::GetFontData(hdc, dwTableTag, 0, fontData, dwSize) != dwSize)
                break;
            ImFontConfig config;
            if (dwTableTag != 0) {
                // Get index of font within TTC
                DWORD dwTTFSize = ::GetFontData(hdc, 0, 0, 0, 0);
                if (dwTTFSize < dwSize) {
                    auto offsetTTF = dwSize - dwTTFSize;
                    int n = stbtt_GetNumberOfFonts((unsigned char*)fontData);
                    for (int index = 0; index<n; index++) {
                        if (offsetTTF == ttULONG((unsigned char*)fontData + 12 + index * 4)) {
                            config.FontNo = index;
                            break;
                        }
                    }
                }
            }
            config.MergeMode = true;
            io.Fonts->AddFontFromMemoryTTF(fontData, dwSize, 14 * State.dpiScale, &config, glyph_ranges);
            fontData = nullptr;
        } while (0);
        if (fontData)
            IM_FREE(fontData);
        ::SelectObject(hdc, hDefaultFont);
        ::DeleteObject(hMessageFont);
        ::ReleaseDC(DirectX::window, hdc);
    } while (0);
    io.Fonts->Build();
}

std::once_flag init_d3d;
HRESULT __stdcall dPresent(IDXGISwapChain* __this, UINT SyncInterval, UINT Flags) {
    std::call_once(init_d3d, [&] {
        if (SUCCEEDED(__this->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice)))
        {
            pDevice->GetImmediateContext(&pContext);
        }
    });
	if (!State.ImGuiInitialized) {
        if (ImGuiInitialization(__this)) {
            ImVec2 size = DirectX::GetWindowSize();
            State.ImGuiInitialized = true;
            STREAM_DEBUG("ImGui Initialized successfully!");
            STREAM_DEBUG("Fullscreen: " << Screen_get_fullScreen(nullptr));
            STREAM_DEBUG("Unity Window Resolution: " << +Screen_get_width(nullptr) << "x" << +Screen_get_height(nullptr));
            STREAM_DEBUG("DirectX Window Size: " << +size.x << "x" << +size.y);
        } else {
            ReleaseSemaphore(DirectX::hRenderSemaphore, 1, NULL);
            return oPresent(__this, SyncInterval, Flags);
        }
    }

    if (!Profiler::HasInitialized)
    {
        Profiler::InitProfiling();
    }

    WaitForSingleObject(DirectX::hRenderSemaphore, INFINITE);

    // resolution changed
    if (!pRenderTargetView) {
        ID3D11Texture2D* pBackBuffer = nullptr;
        __this->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
        assert(pBackBuffer);
        pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &pRenderTargetView);
        pBackBuffer->Release();

        ImVec2 size = DirectX::GetWindowSize();
        STREAM_DEBUG("Unity Window Resolution: " << +Screen_get_width(nullptr) << "x" << +Screen_get_height(nullptr));
        STREAM_DEBUG("DirectX Window Size: " << +size.x << "x" << +size.y);
    }

    if (State.dpiChanged) {
        State.dpiChanged = false;
        ImGui_ImplDX11_InvalidateDeviceObjects();
        RebuildFont();
    }

    il2cpp_gc_disable();

    ApplyTheme();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (State.ShowMenu)
    {
        ImGuiRenderer::Submit([]() { Menu::Render(); });
    }

    // Render in a separate thread
	std::async(std::launch::async, ImGuiRenderer::ExecuteQueue).wait();

    ImGui::EndFrame();
    ImGui::Render();

    pContext->OMSetRenderTargets(1, &pRenderTargetView, NULL);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    il2cpp_gc_enable();

    HRESULT result = oPresent(__this, SyncInterval, Flags);

    ReleaseSemaphore(DirectX::hRenderSemaphore, 1, NULL);

    return result;
}

void DirectX::Shutdown() {
    assert(hRenderSemaphore != NULL); //Initialization is now in a hook, so we might as well guard against this
    for (uint8_t i = 0; i < MAX_RENDER_THREAD_COUNT; i++) //This ugly little hack means we use up all the render queues so we can end everything
    {
        assert(WaitForSingleObject(hRenderSemaphore, INFINITE) == WAIT_OBJECT_0); //Since this is only used on debug builds, we'll leave this for now
    }
    oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)oWndProc);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CloseHandle(hRenderSemaphore);
}
