#include "Util.h"
#include "RE.h"
#include "Renderer.h"

namespace ImGui
{
	ImU32 GetColorU32(const ImVec4& col, float alpha_mul)
	{
		ImGuiStyle& style = GImGui->Style;
		ImVec4      c = col;
		c.w *= style.Alpha * alpha_mul;
		return ColorConvertFloat4ToU32(c);
	}

	float WorldToScreenLoc(const RE::NiPoint3& worldLocIn, ImVec2& screenLocOut, bool a_log)
	{
		float zVal = -1.0f;
		auto  camera = RE::Main::WorldRootCamera();
		bool  projected = false;

		if (camera) {
			if (REL::Module::IsVR()) {
				auto&             mtx = camera->GetVRRuntimeData().worldToCam;
				RE::NiRect<float> vrPort(0.0f, 1.0f, 1.0f, 0.0f);
				projected = RE::NiCamera::WorldPtToScreenPt3(
					mtx,
					vrPort,
					worldLocIn,
					screenLocOut.x,
					screenLocOut.y,
					zVal,
					1e-5f);
			} else {
				projected = camera->WorldPtToScreenPt3(worldLocIn, screenLocOut.x, screenLocOut.y, zVal, 1e-5f);
			}
		}

		if (!projected) {
			return -1.0f;
		}

		if (a_log) {
			logger::debug("[WorldToScreenLoc] World input: ({:.2f}, {:.2f}, {:.2f})", worldLocIn.x, worldLocIn.y, worldLocIn.z);
			logger::debug("[WorldToScreenLoc] Projected raw: ({:.4f}, {:.4f}), zVal: {:.4f}", screenLocOut.x, screenLocOut.y, zVal);
		}

		if (REL::Module::IsVR()) {
			const float coverage = ImGui::Renderer::GetHUDCoverage();
			if (a_log) {
				logger::debug("[WorldToScreenLoc] VR coverage: {:.4f}", coverage);
			}
			if (coverage > 0.0f) {
				screenLocOut.x = 0.5f + (screenLocOut.x - 0.5f) / coverage;
				screenLocOut.y = 0.5f + (screenLocOut.y - 0.5f) / coverage;
				if (a_log) {
					logger::debug("[WorldToScreenLoc] Projected after coverage: ({:.4f}, {:.4f})", screenLocOut.x, screenLocOut.y);
				}
			}
		}

		const ImVec2 rect = ImGui::GetIO().DisplaySize;
		screenLocOut.x = rect.x * screenLocOut.x;
		screenLocOut.y = rect.y * (1.0f - screenLocOut.y);

		if (a_log) {
			logger::debug("[WorldToScreenLoc] Final screen out: ({:.2f}, {:.2f}), displaySize: ({:.2f}, {:.2f})", screenLocOut.x, screenLocOut.y, rect.x, rect.y);
		}

		return zVal;
	}

	void DrawCircle(const RE::NiPoint3& a_pos, float radius, ImU32 color)
	{
		ImVec2 screenPos;
		auto   zDepth = WorldToScreenLoc(a_pos, screenPos);
		if (zDepth > 0.0f) {
			auto drawList = ImGui::GetBackgroundDrawList();
			drawList->AddCircle(screenPos, radius, color, 0, 3.0f);
		}
	}

	void DrawLine(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, ImU32 color)
	{
		ImVec2 screenFrom;
		ImVec2 screenTo;
		auto   zFrom = WorldToScreenLoc(a_from, screenFrom);
		auto   zTo = WorldToScreenLoc(a_to, screenTo);
		if (zFrom > 0.0f && zTo > 0.0f) {
			auto drawList = ImGui::GetBackgroundDrawList();
			drawList->AddLine(screenFrom, screenTo, color, 3.0f);
		}
	}

	void DrawTextAtPoint(const RE::NiPoint3& a_pos, const char* a_text, ImU32 color)
	{
		ImVec2 screenPos;
		auto   zDepth = WorldToScreenLoc(a_pos, screenPos);
		if (zDepth > 0.0f) {
			ImGui::PushFont(nullptr, 30);
			{
				auto drawList = ImGui::GetBackgroundDrawList();
				drawList->AddCircleFilled(screenPos, 6.0f, color);
				drawList->AddText(ImVec2(screenPos.x + 6.0f + ImGui::GetStyle().ItemSpacing.x, screenPos.y - 3.f), color, a_text);
			}
			ImGui::PopFont();
		}
	}

	void DrawBSBound(const RE::BSBound& bound, const RE::NiPoint3& a_position, ImU32 color)
	{
		std::array<RE::NiPoint3, 8> corners;

		int index = 0;
		for (int dx : { -1, 1 }) {
			for (int dy : { -1, 1 }) {
				for (int dz : { -1, 1 }) {
					corners[index++] = RE::NiPoint3{
						bound.center.x + dx * bound.extents.x,
						bound.center.y + dy * bound.extents.y,
						bound.center.z + dz * bound.extents.z
					} + a_position;
				}
			}
		}

		static const int edgeIdx[12][2] = {
			// pairs of corner indices
			{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },  // bottom rectangle
			{ 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },  // top rectangle
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }   // vertical legs
		};

		for (auto [i0, i1] : edgeIdx) {
			ImGui::DrawLine(corners[i0], corners[i1], color);
		}
	}

	ImVec2 GetNativeViewportPos()
	{
		return GetMainViewport()->Pos;
	}

	ImVec2 GetNativeViewportSize()
	{
		return GetMainViewport()->Size;
	}

	ImVec2 GetNativeViewportCenter()
	{
		const auto Size = GetNativeViewportSize();
		return { Size.x * 0.5f, Size.y * 0.5f };
	}
}
