#pragma once

namespace ImGui::Renderer
{
	void  Install();
	void  Connect();
	float GetHUDCoverage();

	// Skyrim world units -> meters (1 game unit ≈ 1.428 cm).
	inline constexpr float kGameUnitToMeter = 0.01428f;

	// One world-anchored subtitle billboard, in Skyrim world space. Submitted as-is; the helper
	// converts worldPos to OpenVR tracking space itself at Submit time (see SubmitSubtitleQuads).
	struct SubtitleQuad
	{
		RE::NiPoint3 worldPos;        // Skyrim world-space anchor (billboard center)
		float        u0, v0, u1, v1;  // sub-rect of the shared panel texture, 0..1 (top-left origin)
		float        heightMeters;    // billboard height in meters (width follows the sub-rect aspect)
	};

	// True only in VR when connected to a helper that supports world-quad rendering (rev 004).
	bool WorldQuadActive();

	// Convert each quad's Skyrim anchor to OpenVR tracking space and submit the per-frame
	// billboard list to the helper. No-op (and clears the list) when WorldQuadActive() is false.
	void SubmitSubtitleQuads(const std::vector<SubtitleQuad>& a_quads);

	// members
	inline std::atomic initialized{ false };

	// Device/context captured at D3D init, used by RenderHud each frame
	inline ID3D11Device*        g_d3dDevice = nullptr;
	inline ID3D11DeviceContext* g_d3dContext = nullptr;
}
