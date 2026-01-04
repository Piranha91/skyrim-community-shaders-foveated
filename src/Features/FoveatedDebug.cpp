#include "FoveatedDebug.h"
#include "ShaderCache.h"
#include "State.h"
#include <DirectXMath.h>

#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr_platform.h>

using namespace DirectX;

//=============================================================================
// FEATURE BASE CLASS IMPLEMENTATIONS
//=============================================================================

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	FoveatedDebug::Settings,
	EnableDebug,
	DebugHue,
	DebugAlpha,
	ShowGazePoint,
	GazePointSize,
	ShowMetrics)

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

	auto device = globals::d3d::device;

	// Create constant buffers
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	// Foveated region data buffer
	bufferDesc.ByteWidth = sizeof(FoveatedRegionData);
	if (FAILED(device->CreateBuffer(&bufferDesc, nullptr, foveatedBuffer.put()))) {
		logger::error("FoveatedDebug: Failed to create foveated buffer");
		return;
	}

	// Settings buffer
	bufferDesc.ByteWidth = sizeof(Settings);
	if (FAILED(device->CreateBuffer(&bufferDesc, nullptr, settingsBuffer.put()))) {
		logger::error("FoveatedDebug: Failed to create settings buffer");
		return;
	}

	// Create blend state for transparent overlay
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	if (FAILED(device->CreateBlendState(&blendDesc, blendState.put()))) {
		logger::error("FoveatedDebug: Failed to create blend state");
		return;
	}

	// Load shaders - defer until first use
	// Shaders will be loaded in ClearShaderCache()

	initialized = true;
	logger::info("FoveatedDebug: Resources created successfully");
}

void FoveatedDebug::PostPostLoad()
{
	if (!globals::game::isVR)
		return;

	InitializeEyeTracking();
}

void FoveatedDebug::DataLoaded()
{
	// Additional initialization if needed
}

void FoveatedDebug::ClearShaderCache()
{
	logger::info("FoveatedDebug: Recompiling shaders...");

	// Release old shaders if they exist
	if (debugPS) {
		debugPS = nullptr;
	}
	if (debugVS) {
		debugVS = nullptr;
	}

	// Compile pixel shader with PIXEL_SHADER define
	std::vector<std::pair<const char*, const char*>> psDefines = {
		{ "PIXEL_SHADER", "" },
		{ "FOVEATED_DEBUG", "" }
	};

	auto compiledPS = Util::CompileShader(
		L"Data\\Shaders\\FoveatedDebug\\FoveatedDebug.hlsl",  // Path with subdirectory
		psDefines,
		"ps_5_0",
		"main"  // Entry point name
	);

	if (compiledPS) {
		debugPS = reinterpret_cast<RE::BSGraphics::PixelShader*>(compiledPS);
		logger::info("FoveatedDebug: Pixel shader compiled successfully");
	} else {
		logger::error("FoveatedDebug: Failed to compile pixel shader");
	}

	// Compile vertex shader with VERTEX_SHADER define
	std::vector<std::pair<const char*, const char*>> vsDefines = {
		{ "VERTEX_SHADER", "" },
		{ "FOVEATED_DEBUG", "" }
	};

	auto compiledVS = Util::CompileShader(
		L"Data\\Shaders\\FoveatedDebug\\FoveatedDebug.hlsl",  // Path with subdirectory
		vsDefines,
		"vs_5_0",
		"main"  // Entry point name
	);

	if (compiledVS) {
		debugVS = reinterpret_cast<RE::BSGraphics::VertexShader*>(compiledVS);
		logger::info("FoveatedDebug: Vertex shader compiled successfully");
	} else {
		logger::error("FoveatedDebug: Failed to compile vertex shader");
	}
}

//=============================================================================
// EYE TRACKING INITIALIZATION
//=============================================================================

bool FoveatedDebug::InitializeEyeTracking()
{
	if (!globals::game::isVR) {
		logger::info("FoveatedDebug: Not in VR mode");
		return false;
	}

	logger::info("FoveatedDebug: Initializing eye tracking...");
	logger::info("FoveatedDebug: OpenXR integration disabled - using center-based fallback");

	// Use fallback for now
	activeAPI = EyeTrackingAPI::Fallback;
	regionData.GazePoint[0] = 0.5f;
	regionData.GazePoint[1] = 0.5f;
	regionData.IsTracking = 0;

	logger::info("FoveatedDebug: Initialization complete (center-based mode)");

	return true;
}

const char* FoveatedDebug::GetAPIName() const
{
	switch (activeAPI) {
	case EyeTrackingAPI::OpenXR:
		return "OpenXR";
	case EyeTrackingAPI::Fallback:
		return "Center Fallback";
	case EyeTrackingAPI::None:
		return "None";
	default:
		return "Unknown";
	}
}

//=============================================================================
// OPENXR IMPLEMENTATION
//=============================================================================

bool FoveatedDebug::InitializeOpenXR()
{
	logger::info("FoveatedDebug: OpenXR initialization disabled (conflicts with game runtime)");
	logger::info("FoveatedDebug: Full OpenXR integration requires hooking game's XrSession");

	// TODO: Proper OpenXR integration by hooking into game's existing session
	// For now, just return false to use center-based fallback

	return false;
}

void FoveatedDebug::UpdateOpenXRGaze()
{
	// TODO: Implement actual gaze tracking when we have session access
	// For now, this is a placeholder that uses center-based fallback
	regionData.GazePoint[0] = 0.5f;
	regionData.GazePoint[1] = 0.5f;
	regionData.IsTracking = 0;
	regionData.Confidence = 0.0f;
}

void FoveatedDebug::ShutdownOpenXR()
{
	if (xrGazeSpace != XR_NULL_HANDLE) {
		xrDestroySpace(xrGazeSpace);
		xrGazeSpace = XR_NULL_HANDLE;
	}
	if (xrViewSpace != XR_NULL_HANDLE) {
		xrDestroySpace(xrViewSpace);
		xrViewSpace = XR_NULL_HANDLE;
	}
	if (xrGazeAction != XR_NULL_HANDLE) {
		xrDestroyAction(xrGazeAction);
		xrGazeAction = XR_NULL_HANDLE;
	}
	if (xrActionSet != XR_NULL_HANDLE) {
		xrDestroyActionSet(xrActionSet);
		xrActionSet = XR_NULL_HANDLE;
	}
	if (xrSession != XR_NULL_HANDLE) {
		xrDestroySession(xrSession);
		xrSession = XR_NULL_HANDLE;
	}
	if (xrInstance != XR_NULL_HANDLE) {
		xrDestroyInstance(xrInstance);
		xrInstance = XR_NULL_HANDLE;
	}
	openXRInitialized = false;
	eyeGazeSupported = false;
}

bool FoveatedDebug::CreateOpenXRActions()
{
	// TODO: Implement when we have session access
	return false;
}

//=============================================================================
// GAZE PROJECTION
//=============================================================================

void FoveatedDebug::ProjectGazeToScreen(const DirectX::XMFLOAT3& gazeDirection, float& outU, float& outV)
{
	// Normalize the gaze direction
	XMVECTOR gaze = XMLoadFloat3(&gazeDirection);
	gaze = XMVector3Normalize(gaze);

	XMFLOAT3 gazeNorm;
	XMStoreFloat3(&gazeNorm, gaze);

	// Simple tangent-based projection
	if (std::abs(gazeNorm.z) > 0.001f) {
		float tanX = gazeNorm.x / -gazeNorm.z;
		float tanY = gazeNorm.y / -gazeNorm.z;

		const float fovScale = 0.5f;
		outU = 0.5f + tanX * fovScale;
		outV = 0.5f - tanY * fovScale;

		outU = std::clamp(outU, 0.0f, 1.0f);
		outV = std::clamp(outV, 0.0f, 1.0f);
	} else {
		outU = 0.5f;
		outV = 0.5f;
	}
}

//=============================================================================
// UPDATE AND RENDER
//=============================================================================

void FoveatedDebug::UpdateEyeGazeData()
{
	if (!settings.EnableDebug)
		return;

	switch (activeAPI) {
	case EyeTrackingAPI::OpenXR:
		UpdateOpenXRGaze();
		break;

	case EyeTrackingAPI::Fallback:
	case EyeTrackingAPI::None:
	default:
		// Fallback: use screen center
		regionData.GazePoint[0] = 0.5f;
		regionData.GazePoint[1] = 0.5f;
		regionData.IsTracking = 0;
		regionData.Confidence = 0.0f;
		break;
	}

	metrics.updateCount++;
	if (regionData.IsTracking) {
		metrics.averageConfidence = (metrics.averageConfidence * 0.95f) + (regionData.Confidence * 0.05f);
	}
}

void FoveatedDebug::DrawSettings()
{
	ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Eye Tracking Status");
	ImGui::Separator();

	ImGui::Text("API: %s", GetAPIName());
	ImGui::Text("OpenXR Instance: %s", openXRInitialized ? "Initialized" : "Not initialized");
	ImGui::Text("Eye Gaze Support: %s", eyeGazeSupported ? "Available" : "Not available");
	ImGui::Text("Tracking: %s", regionData.IsTracking ? "Active" : "Inactive");

	if (regionData.IsTracking) {
		ImGui::Text("Gaze: (%.3f, %.3f)", regionData.GazePoint[0], regionData.GazePoint[1]);
		ImGui::Text("Confidence: %.2f%%", regionData.Confidence * 100.0f);
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	if (ImGui::Checkbox("Enable Debug Overlay", (bool*)&settings.EnableDebug)) {
		ClearShaderCache();
	}

	if (settings.EnableDebug) {
		ImGui::Indent();

		ImGui::ColorEdit3("Overlay Color", settings.DebugHue.data());
		ImGui::SliderFloat("Overlay Alpha", &settings.DebugAlpha, 0.0f, 1.0f);

		ImGui::Spacing();
		ImGui::Checkbox("Show Gaze Point", (bool*)&settings.ShowGazePoint);
		if (settings.ShowGazePoint) {
			ImGui::SliderFloat("Crosshair Size", &settings.GazePointSize, 0.005f, 0.1f);
		}

		ImGui::Checkbox("Show Debug Metrics", (bool*)&settings.ShowMetrics);

		ImGui::Spacing();
		ImGui::Text("Foveated Region Settings:");
		ImGui::SliderFloat("Inner Radius", &regionData.InnerRadius, 0.05f, 0.5f);
		ImGui::SliderFloat("Outer Radius", &regionData.OuterRadius, 0.1f, 0.8f);
		ImGui::SliderFloat("Edge Softness", &regionData.EdgeSoftness, 0.01f, 0.2f);

		ImGui::Unindent();
	}

	if (settings.ShowMetrics) {
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Debug Metrics:");
		ImGui::Text("Updates: %u", metrics.updateCount);
		if (activeAPI == EyeTrackingAPI::OpenXR) {
			ImGui::Text("Avg Confidence: %.2f%%", metrics.averageConfidence * 100.0f);
		}
	}
}