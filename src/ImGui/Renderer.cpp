#include "Renderer.h"

#include <fstream>
#include "ImGui/FontStyles.h"
#include "Manager.h"
#include "ImGuiVRHelperClientSDK.h"

namespace ImGui::Renderer
{
	namespace
	{
		ImGuiVRHelperPluginAPI::Client g_vrClient;
	}

	void Connect()
	{
		if (REL::Module::IsVR()) {
			// Register as an always-on HUD layer (no interactive overlay, no dashboard)
			if (!g_vrClient.Connect("FloatingSubtitles", Version::NAME.data(),
					ImGuiVRHelperPluginAPI::kClientFlag_HUDMode)) {
				logger::warn("ImGuiVRHelper not found or registration failed — VR subtitle panel will not be available."sv);
				return;
			}

			logger::info("Connected to ImGuiVRHelper as HUD client."sv);

			// Provide the same font/style that the SSE path loads so the HUD
			// context renders with the right glyphs
			g_vrClient.SetHudStyleCallback([]() {
				ImGui::FontStyles::GetSingleton()->LoadFontStyles();
			});
		}
	}

	float GetHUDCoverage()
	{
		if (REL::Module::IsVR() && g_vrClient.IsConnected()) {
			return g_vrClient.GetHudCoverage();
		}
		return 1.0f;
	}

	struct CreateD3DAndSwapChain
	{
		static void thunk()
		{
			func();

			if (const auto renderer = RE::BSGraphics::Renderer::GetSingleton()) {
				const auto swapChain = reinterpret_cast<IDXGISwapChain*>(renderer->GetRuntimeData().renderWindows[0].swapChain);
				if (!swapChain) {
					logger::error("couldn't find swapChain");
					return;
				}

				DXGI_SWAP_CHAIN_DESC desc{};
				if (FAILED(swapChain->GetDesc(std::addressof(desc)))) {
					logger::error("IDXGISwapChain::GetDesc failed.");
					return;
				}

				const auto device = reinterpret_cast<ID3D11Device*>(renderer->GetRuntimeData().forwarder);
				const auto context = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);

				if (REL::Module::IsVR()) {
					logger::info("D3D initialized — VR subtitle rendering via ImGuiVRHelper."sv);

					// Store device/context so PostDisplay can call RenderHud
					g_d3dDevice = device;
					g_d3dContext = context;

					initialized.store(true);
				} else {
					logger::info("Initializing ImGui..."sv);

					ImGui::CreateContext();

					auto& io = ImGui::GetIO();
					io.IniFilename = nullptr;

					if (!ImGui_ImplWin32_Init(desc.OutputWindow)) {
						logger::error("ImGui initialization failed (Win32)");
						return;
					}
					if (!ImGui_ImplDX11_Init(device, context)) {
						logger::error("ImGui initialization failed (DX11)"sv);
						return;
					}

					ImGui::FontStyles::GetSingleton()->LoadFontStyles();

					logger::info("ImGui initialized."sv);

					initialized.store(true);
				}
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// IMenu::PostDisplay
	struct PostDisplay
	{
		static void thunk(RE::IMenu* a_menu)
		{
			func(a_menu);

			if (!initialized.load()) {
				return;
			}

			if (REL::Module::IsVR()) {
				if (!g_vrClient.IsConnected()) {
					return;
				}

				static const auto screenSize = RE::BSGraphics::Renderer::GetScreenSize();
				const ImVec2 displaySize{ static_cast<float>(screenSize.width),
					static_cast<float>(screenSize.height) };

				g_vrClient.RenderHud(g_d3dDevice, g_d3dContext, displaySize, []() {
					// disable windowing
					GImGui->NavWindowingTarget = nullptr;
					Manager::GetSingleton()->Draw();
				});
			} else {
				ImGui_ImplDX11_NewFrame();
				ImGui_ImplWin32_NewFrame();
				{
					//trick imgui into rendering at game's real resolution (ie. if upscaled with Display Tweaks)
					static const auto screenSize = RE::BSGraphics::Renderer::GetScreenSize();

					auto& io = ImGui::GetIO();
					io.DisplaySize.x = static_cast<float>(screenSize.width);
					io.DisplaySize.y = static_cast<float>(screenSize.height);
				}
				ImGui::NewFrame();
				{
					// disable windowing
					GImGui->NavWindowingTarget = nullptr;

					Manager::GetSingleton()->Draw();
				}
				ImGui::EndFrame();
				ImGui::Render();
				ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static inline std::size_t                      idx{ 0x6 };
	};

	void Install()
	{
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(75595, 77226), OFFSET(0x9, 0x275) };  // BSGraphics::InitD3D
		stl::write_thunk_call<CreateD3DAndSwapChain>(target.address());

		stl::write_vfunc<RE::HUDMenu, PostDisplay>();
	}
}
