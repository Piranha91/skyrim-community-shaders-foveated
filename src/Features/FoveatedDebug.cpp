#include "FoveatedDebug.h"
#include "State.h"
#include <array>  // Add this if not already included

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	FoveatedDebug::Settings,
	EnableDebug,
	DebugHue,
	DebugAlpha)

void FoveatedDebug::LoadSettings(json& o_json)
{
	settings = o_json.get<Settings>();
}

void FoveatedDebug::SaveSettings(json& o_json)
{
	o_json = settings;
}

void FoveatedDebug::RestoreDefaultSettings()
{
	settings = {};
}

void FoveatedDebug::SetupResources()
{
	if (!globals::game::isVR) {
		logger::info("FoveatedDebug: Not in VR mode, skipping setup");
		return;
	}

	logger::info("FoveatedDebug: Resources setup (placeholder)");
	// TODO: Initialize OpenXR and create D3D resources
}

void FoveatedDebug::PostPostLoad()
{
	if (!globals::game::isVR)
		return;

	logger::info("FoveatedDebug: PostPostLoad (placeholder)");
	// TODO: Initialize eye tracking
}

void FoveatedDebug::DataLoaded()
{
	logger::info("FoveatedDebug: DataLoaded (placeholder)");
	// TODO: Any game data initialization
}

void FoveatedDebug::ClearShaderCache()
{
	logger::info("FoveatedDebug: Clearing shader cache (placeholder)");
	// TODO: Reload shaders if needed
}

void FoveatedDebug::DrawSettings()
{
	if (ImGui::Checkbox("Enable Foveated Debug Overlay", (bool*)&settings.EnableDebug)) {
		ClearShaderCache();
	}

	if (settings.EnableDebug) {
		ImGui::Indent();
		ImGui::ColorEdit3("Debug Hue", settings.DebugHue.data());  // Use .data() for std::array
		ImGui::SliderFloat("Debug Alpha", &settings.DebugAlpha, 0.0f, 1.0f);
		ImGui::Unindent();
	}

	ImGui::Separator();
	ImGui::TextDisabled("OpenXR eye tracking integration - Coming soon!");
}