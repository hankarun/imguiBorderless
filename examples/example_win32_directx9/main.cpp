// Dear ImGui: standalone example application for DirectX 9
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

#define WIN32_LEAN_AND_MEAN

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <d3d9.h>
#include <tchar.h>
#include <iostream>

#include <windowsx.h>

// Data
static LPDIRECT3D9              g_pD3D = nullptr;
static LPDIRECT3DDEVICE9        g_pd3dDevice = nullptr;
static D3DPRESENT_PARAMETERS    g_d3dpp = {};

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool fullscreen_ = false;
bool maximized = false;
int style = 0;
int ex_style = 0;
RECT window_rect;



struct Vector2
{
    int x = 0;
    int y = 0;
};


struct Size
{
    int width;
    int height;
};

Vector2 windowPos = { 100, 100 };
Size windowSize = { 1280, 800 };
int windowStyle = WS_TILEDWINDOW;
int imgui_cursor = 0;

// Get the horizontal and vertical screen sizes in pixel
Size GetDesktopResolution()
{
    RECT desktop;
    // Get a handle to the desktop window
    const HWND hDesktop = GetDesktopWindow();
    // Get the size of screen to the variable desktop
    GetWindowRect(hDesktop, &desktop);
    // The top left corner will have coordinates (0,0)
    // and the bottom right corner will have coordinates
    // (horizontal, vertical)
    Size size;
    size.width = desktop.right;
    size.height = desktop.bottom;
    return size;
}

void SetFullscreenImpl(HWND hwnd_, bool fullscreen, bool for_metro) {
    fullscreen_ = fullscreen;

    if (fullscreen_) {
        ShowWindow(hwnd_, SW_MAXIMIZE);
    }
    else {
        ShowWindow(hwnd_, SW_RESTORE);
    }
}

bool taskBarHovering = false;
bool done = false;
bool toggleFullscreen = false;

constexpr auto titlebar = IM_COL32(21, 21, 21, 255);
constexpr auto text = IM_COL32(192, 192, 192, 255);
constexpr auto textDarker = IM_COL32(128, 128, 128, 255);

inline ImU32 ColorWithMultipliedValue(const ImColor& color, float multiplier)
{
    const ImVec4& colRow = color.Value;
    float hue, sat, val;
    ImGui::ColorConvertRGBtoHSV(colRow.x, colRow.y, colRow.z, hue, sat, val);
    return ImColor::HSV(hue, sat, min(val * multiplier, 1.0f));
}

ImRect RectOffset(const ImRect& rect, float x, float y)
{
    ImRect result = rect;
    result.Min.x += x;
    result.Min.y += y;
    result.Max.x += x;
    result.Max.y += y;
    return result;
}

bool BeginMenubar(const ImRect& barRectangle)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;
    /*if (!(window->Flags & ImGuiWindowFlags_MenuBar))
        return false;*/

    IM_ASSERT(!window->DC.MenuBarAppending);
    ImGui::BeginGroup(); // Backup position on layer 0 // FIXME: Misleading to use a group for that backup/restore
    ImGui::PushID("##menubar");

    const ImVec2 padding = window->WindowPadding;

    // We don't clip with current window clipping rectangle as it is already set to the area below. However we clip with window full rect.
    // We remove 1 worth of rounding to Max.x to that text in long menus and small windows don't tend to display over the lower-right rounded area, which looks particularly glitchy.
    ImRect bar_rect = RectOffset(barRectangle, 0.0f, padding.y);// window->MenuBarRect();
    ImRect clip_rect(IM_ROUND(ImMax(window->Pos.x, bar_rect.Min.x + window->WindowBorderSize + window->Pos.x - 10.0f)), IM_ROUND(bar_rect.Min.y + window->WindowBorderSize + window->Pos.y),
        IM_ROUND(ImMax(bar_rect.Min.x + window->Pos.x, bar_rect.Max.x - ImMax(window->WindowRounding, window->WindowBorderSize))), IM_ROUND(bar_rect.Max.y + window->Pos.y));

    clip_rect.ClipWith(window->OuterRectClipped);
    ImGui::PushClipRect(clip_rect.Min, clip_rect.Max, false);

    // We overwrite CursorMaxPos because BeginGroup sets it to CursorPos (essentially the .EmitItem hack in EndMenuBar() would need something analogous here, maybe a BeginGroupEx() with flags).
    window->DC.CursorPos = window->DC.CursorMaxPos = ImVec2(bar_rect.Min.x + window->Pos.x, bar_rect.Min.y + window->Pos.y);
    window->DC.LayoutType = ImGuiLayoutType_Horizontal;
    window->DC.NavLayerCurrent = ImGuiNavLayer_Menu;
    window->DC.MenuBarAppending = true;
    ImGui::AlignTextToFramePadding();
    return true;
}

void EndMenubar()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return;
    ImGuiContext& g = *GImGui;

    // Nav: When a move request within one of our child menu failed, capture the request to navigate among our siblings.
    if (ImGui::NavMoveRequestButNoResultYet() && (g.NavMoveDir == ImGuiDir_Left || g.NavMoveDir == ImGuiDir_Right) && (g.NavWindow->Flags & ImGuiWindowFlags_ChildMenu))
    {
        // Try to find out if the request is for one of our child menu
        ImGuiWindow* nav_earliest_child = g.NavWindow;
        while (nav_earliest_child->ParentWindow && (nav_earliest_child->ParentWindow->Flags & ImGuiWindowFlags_ChildMenu))
            nav_earliest_child = nav_earliest_child->ParentWindow;
        if (nav_earliest_child->ParentWindow == window && nav_earliest_child->DC.ParentLayoutType == ImGuiLayoutType_Horizontal && (g.NavMoveFlags & ImGuiNavMoveFlags_Forwarded) == 0)
        {
            // To do so we claim focus back, restore NavId and then process the movement request for yet another frame.
            // This involve a one-frame delay which isn't very problematic in this situation. We could remove it by scoring in advance for multiple window (probably not worth bothering)
            const ImGuiNavLayer layer = ImGuiNavLayer_Menu;
            IM_ASSERT(window->DC.NavLayersActiveMaskNext & (1 << layer)); // Sanity check
            ImGui::FocusWindow(window);
            ImGui::SetNavID(window->NavLastIds[layer], layer, 0, window->NavRectRel[layer]);
            g.NavDisableHighlight = true; // Hide highlight for the current frame so we don't see the intermediary selection.
            g.NavDisableMouseHover = g.NavMousePosDirty = true;
            ImGui::NavMoveRequestForward(g.NavMoveDir, g.NavMoveClipDir, g.NavMoveFlags, g.NavMoveScrollFlags); // Repeat
        }
    }

    IM_MSVC_WARNING_SUPPRESS(6011); // Static Analysis false positive "warning C6011: Dereferencing NULL pointer 'window'"
    // IM_ASSERT(window->Flags & ImGuiWindowFlags_MenuBar); // NOTE(Yan): Needs to be commented out because Jay
    IM_ASSERT(window->DC.MenuBarAppending);
    ImGui::PopClipRect();
    ImGui::PopID();
    window->DC.MenuBarOffset.x = window->DC.CursorPos.x - window->Pos.x; // Save horizontal position so next append can reuse it. This is kinda equivalent to a per-layer CursorPos.
    g.GroupStack.back().EmitItem = false;
    ImGui::EndGroup(); // Restore position on layer 0
    window->DC.LayoutType = ImGuiLayoutType_Vertical;
    window->DC.NavLayerCurrent = ImGuiNavLayer_Main;
    window->DC.MenuBarAppending = false;
}

void drawTitleBar(float& titlebarHeight_)
{
    const float titlebarHeight = 58.0f;
    const bool isMaximized = fullscreen_;
    float titlebarVerticalOffset = isMaximized ? -6.0f : 0.0f;
    const ImVec2 windowPadding = ImGui::GetCurrentWindow()->WindowPadding;

    ImGui::SetCursorPos(ImVec2(windowPadding.x, windowPadding.y + titlebarVerticalOffset));
    const ImVec2 titlebarMin = ImGui::GetCursorScreenPos();
    const ImVec2 titlebarMax = { ImGui::GetCursorScreenPos().x + ImGui::GetWindowWidth() - windowPadding.y * 2.0f,
                                 ImGui::GetCursorScreenPos().y + titlebarHeight };
    auto* bgDrawList = ImGui::GetBackgroundDrawList();
    auto* fgDrawList = ImGui::GetForegroundDrawList();
    bgDrawList->AddRectFilled(titlebarMin, titlebarMax, titlebar);
    // DEBUG TITLEBAR BOUNDS
    // fgDrawList->AddRect(titlebarMin, titlebarMax, UI::Colors::Theme::invalidPrefab);

    // Logo
    {
        const int logoWidth = 48;// m_LogoTex->GetWidth();
        const int logoHeight = 48;// m_LogoTex->GetHeight();
        const ImVec2 logoOffset(16.0f + windowPadding.x, 5.0f + windowPadding.y + titlebarVerticalOffset);
        const ImVec2 logoRectStart = { ImGui::GetItemRectMin().x + logoOffset.x, ImGui::GetItemRectMin().y + logoOffset.y };
        const ImVec2 logoRectMax = { logoRectStart.x + logoWidth, logoRectStart.y + logoHeight };
        //fgDrawList->AddImage(m_AppHeaderIcon->GetDescriptorSet(), logoRectStart, logoRectMax);
    }

    ImGui::BeginHorizontal("Titlebar", { ImGui::GetWindowWidth() - windowPadding.y * 2.0f, ImGui::GetFrameHeightWithSpacing() });

    static float moveOffsetX;
    static float moveOffsetY;
    const float w = ImGui::GetContentRegionAvail().x;
    const float buttonsAreaWidth = 94;

    // Title bar drag area
    // On Windows we hook into the GLFW win32 window internals
    ImGui::SetCursorPos(ImVec2(windowPadding.x, windowPadding.y + titlebarVerticalOffset)); // Reset cursor pos
    // DEBUG DRAG BOUNDS
    // fgDrawList->AddRect(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + w - buttonsAreaWidth, ImGui::GetCursorScreenPos().y + titlebarHeight), UI::Colors::Theme::invalidPrefab);
    ImGui::InvisibleButton("##titleBarDragZone", ImVec2(w - buttonsAreaWidth, titlebarHeight));

    taskBarHovering = ImGui::IsItemHovered();

    if (isMaximized)
    {
        float windowMousePosY = ImGui::GetMousePos().y - ImGui::GetCursorScreenPos().y;
        if (windowMousePosY >= 0.0f && windowMousePosY <= 5.0f)
            taskBarHovering = true; // Account for the top-most pixels which don't register
    }

    // Draw Menubar
    //if (m_MenubarCallback)
    {
        ImGui::SuspendLayout();
        {
            ImGui::SetItemAllowOverlap();
            const float logoHorizontalOffset = 4 + windowPadding.x;
            ImGui::SetCursorPos(ImVec2(logoHorizontalOffset, 6.0f + titlebarVerticalOffset));

            const ImRect menuBarRect = { ImGui::GetCursorPos(), { ImGui::GetContentRegionAvail().x + ImGui::GetCursorScreenPos().x, ImGui::GetFrameHeightWithSpacing() } };

            ImGui::BeginGroup();
            if (BeginMenubar(menuBarRect))
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("Quit"))
                    {
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help"))
                {
                    if (ImGui::MenuItem("About"))
                    {
                    }
                    ImGui::EndMenu();
                }
            }
            EndMenubar();
            ImGui::EndGroup();

            if (ImGui::IsItemHovered())
                taskBarHovering = false;
        }

        ImGui::ResumeLayout();
    }

    {
        // Centered Window title
        std::string projectName("Project Name");
        ImVec2 currentCursorPos = ImGui::GetCursorPos();
        ImVec2 textSize = ImGui::CalcTextSize(projectName.c_str());
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() * 0.5f - textSize.x * 0.5f, 2.0f + windowPadding.y + 6.0f));
        ImGui::Text("%s", projectName.c_str()); // Draw title
        ImGui::SetCursorPos(currentCursorPos);
    }

    // Window buttons
    const ImU32 buttonColN = ColorWithMultipliedValue(text, 0.9f);
    const ImU32 buttonColH = ColorWithMultipliedValue(text, 1.2f);
    const ImU32 buttonColP = textDarker;
    const float buttonWidth = 14.0f;
    const float buttonHeight = 14.0f;

    // Minimize Button
    int iconSize = 10;

    ImGui::Spring();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
    {
        const int iconWidth = iconSize;
        const int iconHeight = iconSize;
        const float padY = (buttonHeight - (float)iconHeight) / 2.0f;
        if (ImGui::InvisibleButton("Minimize", ImVec2(buttonWidth, buttonHeight)))
        {
            //Minimize
        }
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - buttonWidth);
        ImGui::Text(".");
        //UI::DrawButtonImage(m_IconMinimize, buttonColN, buttonColH, buttonColP, UI::RectExpanded(UI::GetItemRect(), 0.0f, -padY));
    }


    // Maximize Button
    ImGui::Spring(-1.0f, 17.0f);
    // ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
    {
        const int iconWidth = iconSize;
        const int iconHeight = iconSize;

        const bool isMaximized = fullscreen_;

        if (ImGui::InvisibleButton("Maximize", ImVec2(buttonWidth, buttonHeight)))
        {
            // Toggle full screen
            fullscreen_ = !fullscreen_;
        }
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - buttonWidth);
        ImGui::Text("o");
        //UI::DrawButtonImage(isMaximized ? m_IconRestore : m_IconMaximize, buttonColN, buttonColH, buttonColP);
    }

    // Close Button
    ImGui::Spring(-1.0f, 15.0f);
    // ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
    {
        const int iconWidth = iconSize;
        const int iconHeight = iconSize;
        if (ImGui::InvisibleButton("Close", ImVec2(buttonWidth, buttonHeight)))
            done = true;

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - buttonWidth);
        ImGui::Text("x");
        //UI::DrawButtonImage(m_IconClose, UI::Colors::Theme::text, UI::Colors::ColorWithMultipliedValue(UI::Colors::Theme::text, 1.4f), buttonColP);
    }

    ImGui::Spring(-1.0f, 18.0f);
    ImGui::EndHorizontal();

    titlebarHeight_ = titlebarHeight;
}

// Main code
int main(int, char**)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dear ImGui DirectX9 Example", windowStyle, windowPos.x, windowPos.y, windowSize.width, windowSize.height, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    while (!done)
    {
        toggleFullscreen = false;
        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Start the Dear ImGui frame
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        {
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, fullscreen_ ? ImVec2(6.0f, 6.0f) : ImVec2(1.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::Begin("Main", 0, window_flags);
            float titleBarHeight = 58;
            ImGui::PopStyleVar(3);
            drawTitleBar(titleBarHeight);
            ImGui::DockSpace(ImGui::GetID("Dock"));
            ImGui::ShowDemoWindow();
            ImGui::End();
        }


        // Rendering
        ImGui::EndFrame();
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        D3DCOLOR clear_col_dx = D3DCOLOR_RGBA((int)(clear_color.x * clear_color.w * 255.0f), (int)(clear_color.y * clear_color.w * 255.0f), (int)(clear_color.z * clear_color.w * 255.0f), (int)(clear_color.w * 255.0f));
        g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
        if (g_pd3dDevice->BeginScene() >= 0)
        {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }

        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        HRESULT result = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);

        if (toggleFullscreen)
            SetFullscreenImpl(hwnd, !fullscreen_, false);


        // Handle loss of D3D9 device
        if (result == D3DERR_DEVICELOST && g_pd3dDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
            ResetDevice();
    }

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd)
{
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr)
        return false;

    // Create the D3DDevice
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN; // Need to use an explicit format with alpha if needing per-pixel alpha composition.
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;           // Present with vsync
    //g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;   // Present without vsync, maximum unthrottled framerate
    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
        return false;

    return true;
}

void CleanupDeviceD3D()
{
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = nullptr; }
}

void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL)
        IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0 // From Windows SDK 8.1+ headers
#endif

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_CREATE:
    {
        RECT size_rect;
        GetWindowRect(hWnd, &size_rect);

        // Inform the application of the frame change to force redrawing with the new
        // client area that is extended into the title bar
        SetWindowPos(
            hWnd, NULL,
            size_rect.left, size_rect.top,
            size_rect.right - size_rect.left, size_rect.bottom - size_rect.top,
            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE
        );

        break;
    }
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            g_d3dpp.BackBufferWidth = LOWORD(lParam);
            g_d3dpp.BackBufferHeight = HIWORD(lParam);
            ResetDevice();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    case WM_DPICHANGED:
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DpiEnableScaleViewports)
        {
            //const int dpi = HIWORD(wParam);
            //printf("WM_DPICHANGED to %d (%.0f%%)\n", dpi, (float)dpi / 96.0f * 100.0f);
            const RECT* suggested_rect = (RECT*)lParam;
            ::SetWindowPos(hWnd, nullptr, suggested_rect->left, suggested_rect->top, suggested_rect->right - suggested_rect->left, suggested_rect->bottom - suggested_rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        break;
    case WM_NCHITTEST:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);

        // Check borders first
        //if (!window->win32.maximized)
        {
            RECT rc;
            GetClientRect(hWnd, &rc);

            const int verticalBorderSize = GetSystemMetrics(SM_CYFRAME);

            struct Border
            {
                int left = 4;
                int right = 4;
                int top = 4;
                int bottom = 4;
            }border_thickness;

            enum { left = 1, top = 2, right = 4, bottom = 8 };
            int hit = 0;
            if (pt.x <= border_thickness.left)
                hit |= left;
            if (pt.x >= rc.right - border_thickness.right)
                hit |= right;
            if (pt.y <= border_thickness.top || pt.y < verticalBorderSize)
                hit |= top;
            if (pt.y >= rc.bottom - border_thickness.bottom)
                hit |= bottom;

            if (hit & top && hit & left)        return HTTOPLEFT;
            if (hit & top && hit & right)       return HTTOPRIGHT;
            if (hit & bottom && hit & left)     return HTBOTTOMLEFT;
            if (hit & bottom && hit & right)    return HTBOTTOMRIGHT;
            if (hit & left)                     return HTLEFT;
            if (hit & top)                      return HTTOP;
            if (hit & right)                    return HTRIGHT;
            if (hit & bottom)                   return HTBOTTOM;
        }

        if (taskBarHovering)
            return HTCAPTION;

        // In client area
        return HTCLIENT;
    }
    case WM_NCCALCSIZE:
        if (wParam)
        {
            /* Detect whether window is maximized or not. We don't need to change the resize border when win is
            *  maximized because all resize borders are gone automatically */
            WINDOWPLACEMENT wPos;
            // GetWindowPlacement fail if this member is not set correctly.
            wPos.length = sizeof(wPos);
            GetWindowPlacement(hWnd, &wPos);
            const int resizeBorderX = GetSystemMetrics(SM_CXFRAME);
            const int resizeBorderY = GetSystemMetrics(SM_CYFRAME);

            NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)lParam;
            RECT* requestedClientRect = params->rgrc;

            requestedClientRect->right -= resizeBorderX;
            requestedClientRect->left += resizeBorderX;
            requestedClientRect->bottom -= resizeBorderY;
            requestedClientRect->top += 0;

            return WVR_ALIGNTOP | WVR_ALIGNLEFT;

            //RECT borderThickness;
            //SetRectEmpty(&borderThickness);
            //AdjustWindowRectEx(&borderThickness,
            //    GetWindowLongPtr(hwnd, GWL_STYLE) & ~WS_CAPTION, FALSE, NULL);
            //borderThickness.left *= -1;
            //borderThickness.top *= -1;
            //NCCALCSIZE_PARAMS* sz = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            //// Add 1 pixel to the top border to make the window resizable from the top border
            //sz->rgrc[0].top -= borderThickness.top;
            //return 0;
        }break;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
