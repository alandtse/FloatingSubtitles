#pragma once

namespace ImGui::Renderer
{
	void  Install();
	void  Connect();
	float GetHUDCoverage();

	// members
	inline std::atomic initialized{ false };

	// Device/context captured at D3D init, used by RenderHud each frame
	inline ID3D11Device*        g_d3dDevice = nullptr;
	inline ID3D11DeviceContext* g_d3dContext = nullptr;
}
