// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Nazg - one keyboard configurator to rule them all.
//
// Application entry point. This file owns every SDL and renderer call in the project.
// Keep it that way: it is what makes switching the renderer backend (SDL_GPU -> OpenGL3)
// or adding an Emscripten path a single-file change. See CLAUDE.md.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

namespace
{
    constexpr int   c_DefaultWindowWidth  = 1280;
    constexpr int   c_DefaultWindowHeight = 800;
    constexpr char  c_WindowTitle[]       = "Nazg";
}

int main(int, char**)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init() failed: %s", SDL_GetError());
        return 1;
    }

    // Create the window, sized in logical units scaled by the display's content scale.
    const float mainScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

    const SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* pWindow = SDL_CreateWindow(c_WindowTitle,
                                           static_cast<int>(c_DefaultWindowWidth  * mainScale),
                                           static_cast<int>(c_DefaultWindowHeight * mainScale),
                                           windowFlags);
    if (pWindow == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow() failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowPosition(pWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(pWindow);

    // Create the GPU device. SDL picks the backend per platform: Metal on macOS,
    // D3D12 on Windows, Vulkan on Linux. We accept every shader format so SDL is free
    // to choose; ImGui ships its own precompiled shaders, so we supply none ourselves.
    SDL_GPUDevice* pGpuDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV |
                                                    SDL_GPU_SHADERFORMAT_DXIL |
                                                    SDL_GPU_SHADERFORMAT_MSL |
                                                    SDL_GPU_SHADERFORMAT_METALLIB,
                                                    true, nullptr);
    if (pGpuDevice == nullptr)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateGPUDevice() failed: %s", SDL_GetError());
        SDL_DestroyWindow(pWindow);
        SDL_Quit();
        return 1;
    }

    if (!SDL_ClaimWindowForGPUDevice(pGpuDevice, pWindow))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_ClaimWindowForGPUDevice() failed: %s", SDL_GetError());
        SDL_DestroyGPUDevice(pGpuDevice);
        SDL_DestroyWindow(pWindow);
        SDL_Quit();
        return 1;
    }
    SDL_SetGPUSwapchainParameters(pGpuDevice, pWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);

    const char* pGpuDriver = SDL_GetGPUDeviceDriver(pGpuDevice);
    SDL_Log("GPU backend: %s", pGpuDriver != nullptr ? pGpuDriver : "unknown");

    // Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(mainScale);
    style.FontScaleDpi = mainScale;

    ImGui_ImplSDL3_InitForSDLGPU(pWindow);

    ImGui_ImplSDLGPU3_InitInfo initInfo = {};
    initInfo.Device             = pGpuDevice;
    initInfo.ColorTargetFormat  = SDL_GetGPUSwapchainTextureFormat(pGpuDevice, pWindow);
    initInfo.MSAASamples        = SDL_GPU_SAMPLECOUNT_1;
    initInfo.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    initInfo.PresentMode        = SDL_GPU_PRESENTMODE_VSYNC;
    ImGui_ImplSDLGPU3_Init(&initInfo);

    const ImVec4 clearColor = ImVec4(0.09f, 0.09f, 0.11f, 1.0f);
    bool showDemoWindow = false;
    bool done = false;

    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(pWindow))
                done = true;
        }

        if ((SDL_GetWindowFlags(pWindow) & SDL_WINDOW_MINIMIZED) != 0)
        {
            SDL_Delay(10);
            continue;
        }

        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Placeholder UI. Everything here is scaffolding to be replaced by the real
        // device list and capability-driven views.
        {
            ImGui::Begin("Nazg");
            ImGui::TextUnformatted("One keyboard configurator to rule them all.");
            ImGui::Separator();
            ImGui::Text("SDL version    : %d.%d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
            ImGui::Text("Dear ImGui     : %s", IMGUI_VERSION);
            ImGui::Text("GPU backend    : %s", pGpuDriver != nullptr ? pGpuDriver : "unknown");
            ImGui::Text("Display scale  : %.2f", mainScale);
            ImGui::Text("Frame          : %.3f ms (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::Separator();
            ImGui::Checkbox("Dear ImGui demo window", &showDemoWindow);
            ImGui::End();
        }

        if (showDemoWindow)
            ImGui::ShowDemoWindow(&showDemoWindow);

        ImGui::Render();
        ImDrawData* pDrawData = ImGui::GetDrawData();
        const bool isMinimized = (pDrawData->DisplaySize.x <= 0.0f || pDrawData->DisplaySize.y <= 0.0f);

        SDL_GPUCommandBuffer* pCommandBuffer = SDL_AcquireGPUCommandBuffer(pGpuDevice);

        SDL_GPUTexture* pSwapchainTexture = nullptr;
        SDL_WaitAndAcquireGPUSwapchainTexture(pCommandBuffer, pWindow, &pSwapchainTexture, nullptr, nullptr);

        if (pSwapchainTexture != nullptr && !isMinimized)
        {
            // MANDATORY for this backend, and unlike every other ImGui renderer backend:
            // the vertex and index buffers are uploaded here, before the render pass.
            ImGui_ImplSDLGPU3_PrepareDrawData(pDrawData, pCommandBuffer);

            SDL_GPUColorTargetInfo targetInfo = {};
            targetInfo.texture     = pSwapchainTexture;
            targetInfo.clear_color = SDL_FColor{ clearColor.x, clearColor.y, clearColor.z, clearColor.w };
            targetInfo.load_op     = SDL_GPU_LOADOP_CLEAR;
            targetInfo.store_op    = SDL_GPU_STOREOP_STORE;

            SDL_GPURenderPass* pRenderPass = SDL_BeginGPURenderPass(pCommandBuffer, &targetInfo, 1, nullptr);
            ImGui_ImplSDLGPU3_RenderDrawData(pDrawData, pCommandBuffer, pRenderPass);
            SDL_EndGPURenderPass(pRenderPass);
        }

        SDL_SubmitGPUCommandBuffer(pCommandBuffer);
    }

    SDL_WaitForGPUIdle(pGpuDevice);
    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui::DestroyContext();

    SDL_ReleaseWindowFromGPUDevice(pGpuDevice, pWindow);
    SDL_DestroyGPUDevice(pGpuDevice);
    SDL_DestroyWindow(pWindow);
    SDL_Quit();

    return 0;
}
