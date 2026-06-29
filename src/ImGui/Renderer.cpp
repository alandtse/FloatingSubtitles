#include "Renderer.h"

#include "ImGui/FontStyles.h"
#include "ImGuiVRHelperClientSDK.h"
#include "Manager.h"

namespace ImGui::Renderer
{
	namespace
	{
		ImGuiVRHelperPluginAPI::Client g_vrClient;
	}

	void Connect()
	{
		if (REL::Module::IsVR()) {
			// Register as a world-quad client: subtitles render as billboards anchored at the
			// speaker's world position (stable, no HUD-plane swim), not the head-locked HUD plane.
			if (!g_vrClient.Connect("FloatingSubtitles", Version::NAME.data(),
					ImGuiVRHelperPluginAPI::kClientFlag_WorldQuad)) {
				logger::warn("ImGuiVRHelper not found or registration failed — VR subtitle panel will not be available."sv);
				return;
			}

			logger::info("Connected to ImGuiVRHelper as world-quad client (world quads supported: {}).", g_vrClient.HasWorldQuads());

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

	bool WorldQuadActive()
	{
		return REL::Module::IsVR() && g_vrClient.IsConnected() && g_vrClient.HasWorldQuads();
	}

	void SubmitSubtitleQuads(const std::vector<SubtitleQuad>& a_quads)
	{
		if (!WorldQuadActive()) {
			return;
		}
		if (a_quads.empty()) {
			g_vrClient.SubmitWorldQuads(nullptr, 0);
			return;
		}

		// Head-independent Skyrim-world -> OpenVR-tracking map. The player's VR RoomNode is the
		// play-space origin: its world transform maps room space -> Skyrim world and excludes head
		// rotation (the HMD node is its child), so a stationary speaker maps to a constant tracking
		// position regardless of gaze (no swim). GetVRNodeData()/RoomNode are existing CommonLibVR
		// accessors.
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto nodeData = player ? player->GetVRNodeData() : nullptr;
		const auto room = nodeData ? nodeData->RoomNode.get() : nullptr;
		if (!room) {
			g_vrClient.SubmitWorldQuads(nullptr, 0);
			return;
		}
		const RE::NiTransform& roomXf = room->world;  // room-local -> Skyrim world
		const auto&            rRoom = roomXf.rotate.entry;
		const float            invScale = (roomXf.scale != 0.0f) ? (1.0f / roomXf.scale) : 1.0f;

		const bool logThisFrame = Manager::GetSingleton()->GetSettings().debugLog;

		std::vector<ImGuiVRHelperPluginAPI::WorldQuad> out;
		out.reserve(a_quads.size());
		for (const auto& q : a_quads) {
			// World -> room-local (Skyrim units, world-aligned axes): rotate^T * (P - origin) / scale.
			const RE::NiPoint3 rel = q.worldPos - roomXf.translate;
			const RE::NiPoint3 pRoom{
				(rRoom[0][0] * rel.x + rRoom[1][0] * rel.y + rRoom[2][0] * rel.z) * invScale,
				(rRoom[0][1] * rel.x + rRoom[1][1] * rel.y + rRoom[2][1] * rel.z) * invScale,
				(rRoom[0][2] * rel.x + rRoom[1][2] * rel.y + rRoom[2][2] * rel.z) * invScale
			};
			// Room-local is world-aligned (x-right, y-forward, z-up) -> OpenVR tracking
			// (x-right, y-up, z-toward-user) = (x, z, -y), game units -> meters. The room origin
			// is the OpenVR standing origin, so this is the tracking-space position directly.
			const RE::NiPoint3 p = pRoom * kGameUnitToMeter;

			ImGuiVRHelperPluginAPI::WorldQuad wq{};
			wq.u0 = q.u0;
			wq.v0 = q.v0;
			wq.u1 = q.u1;
			wq.v1 = q.v1;
			wq.pos[0] = p.x;
			wq.pos[1] = p.z;
			wq.pos[2] = -p.y;
			wq.height_m = q.heightMeters;
			out.push_back(wq);

			if (logThisFrame && &q == &a_quads.front()) {
				logger::debug("[WorldQuad] P_sky=({:.1f},{:.1f},{:.1f}) roomOrigin=({:.1f},{:.1f},{:.1f}) roomScale={:.4f}",
					q.worldPos.x, q.worldPos.y, q.worldPos.z, roomXf.translate.x, roomXf.translate.y, roomXf.translate.z, roomXf.scale);
				logger::debug("[WorldQuad]   roomBasis col0=({:.2f},{:.2f},{:.2f}) col1=({:.2f},{:.2f},{:.2f}) col2=({:.2f},{:.2f},{:.2f})",
					rRoom[0][0], rRoom[1][0], rRoom[2][0], rRoom[0][1], rRoom[1][1], rRoom[2][1], rRoom[0][2], rRoom[1][2], rRoom[2][2]);
				logger::debug("[WorldQuad]   pRoom=({:.1f},{:.1f},{:.1f})u -> P_trk=({:.3f},{:.3f},{:.3f})m h={:.2f}m",
					pRoom.x, pRoom.y, pRoom.z, wq.pos[0], wq.pos[1], wq.pos[2], q.heightMeters);
			}
		}

		g_vrClient.SubmitWorldQuads(out.data(), out.size());
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
					const auto ss = RE::BSGraphics::Renderer::GetScreenSize();
					logger::info("D3D initialized — VR subtitle rendering via ImGuiVRHelper. ScreenSize {}x{}", ss.width, ss.height);

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
				const ImVec2      displaySize{ static_cast<float>(screenSize.width),
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
