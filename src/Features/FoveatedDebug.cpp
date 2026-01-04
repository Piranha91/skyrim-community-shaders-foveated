#include "FoveatedDebug.h"
#include "ShaderCache.h"
#include "State.h"
#include <DirectXMath.h>
#include "Globals.h"
#include <openvr.h>
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr_platform.h>

#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>

#pragma comment(lib, "d3dcompiler.lib")

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
	logger::info("FoveatedDebug::SetupResources called, isVR={}", globals::game::isVR);

	if (!globals::game::isVR) {
		logger::info("FoveatedDebug: Skipping setup - not in VR mode");
		return;
	}

	auto device = globals::d3d::device;
	if (!device) {
		logger::error("FoveatedDebug: D3D device is null!");
		return;
	}

	// 1. Create Constant Buffers (Keep your existing code for this)
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	// Foveated Buffer
	bufferDesc.ByteWidth = sizeof(FoveatedRegionData);  // 48 bytes
	device->CreateBuffer(&bufferDesc, nullptr, foveatedBuffer.put());

	// Settings Buffer
	bufferDesc.ByteWidth = sizeof(Settings);  // 48 bytes
	device->CreateBuffer(&bufferDesc, nullptr, settingsBuffer.put());

	// 2. Create Rasterizer State (NO CULLING)
	// This ensures the triangle is drawn regardless of winding order
	D3D11_RASTERIZER_DESC rasterDesc = {};
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_NONE;  // <--- Critical fix
	rasterDesc.FrontCounterClockwise = FALSE;
	rasterDesc.DepthClipEnable = TRUE;

	device->CreateRasterizerState(&rasterDesc, rasterizerState.put());

	// 3. Create Blend State (ALPHA BLENDING)
	D3D11_BLEND_DESC blendDesc = {};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

	device->CreateBlendState(&blendDesc, blendState.put());

	// 4. Compile Shaders
	logger::info("FoveatedDebug: Buffers created, compiling shaders...");
	ClearShaderCache();

	// Check if shaders compiled
	if (!debugPS || !debugVS) {
		logger::error("FoveatedDebug: Shader compilation failed!");
		// Don't set initialized - leave it false
		return;
	}

	initialized = true;
	logger::info("FoveatedDebug: Resources setup complete.");
}

void FoveatedDebug::ClearShaderCache()
{
	logger::info("FoveatedDebug: Recompiling shaders...");

	// Release old shaders
	debugPS = nullptr;
	debugVS = nullptr;

	// Compile Pixel Shader using existing utility
	std::vector<std::pair<const char*, const char*>> psDefines = {
		{ "PIXEL_SHADER", "1" },
		{ "FOVEATED_DEBUG", "1" }
	};

	auto compiledPS = Util::CompileShader(
		L"Data\\Shaders\\FoveatedDebug\\FoveatedDebug.hlsl",
		psDefines,
		"ps_5_0",
		"main");

	if (compiledPS) {
		// attach() takes ownership of the raw pointer without AddRef
		debugPS.attach(static_cast<ID3D11PixelShader*>(compiledPS));
		logger::info("FoveatedDebug: Pixel shader compiled successfully");
	} else {
		logger::error("FoveatedDebug: Failed to compile pixel shader");
	}

	// Compile Vertex Shader
	std::vector<std::pair<const char*, const char*>> vsDefines = {
		{ "VERTEX_SHADER", "1" },
		{ "FOVEATED_DEBUG", "1" }
	};

	auto compiledVS = Util::CompileShader(
		L"Data\\Shaders\\FoveatedDebug\\FoveatedDebug.hlsl",
		vsDefines,
		"vs_5_0",
		"main");

	if (compiledVS) {
		debugVS.attach(static_cast<ID3D11VertexShader*>(compiledVS));
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
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("Toggles the foveated region visualization in both the desktop mirror and VR headset.");
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

void FoveatedDebug::UpdateConstantBuffers()
{
	if (!foveatedBuffer || !settingsBuffer)
		return;

	auto context = globals::d3d::context;

	// Update foveated region data
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(context->Map(foveatedBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		memcpy(mapped.pData, &regionData, sizeof(FoveatedRegionData));
		context->Unmap(foveatedBuffer.get(), 0);
	}

	// Update settings
	if (SUCCEEDED(context->Map(settingsBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		memcpy(mapped.pData, &settings, sizeof(Settings));
		context->Unmap(settingsBuffer.get(), 0);
	}
}

// 1. Update the Hook to pass the SwapChain
HRESULT WINAPI FoveatedDebug::Hooks::IDXGISwapChain_Present::thunk(IDXGISwapChain* _this, UINT SyncInterval, UINT Flags)
{
	// Use the global instance, not a separate singleton
	globals::features::foveatedDebug.Draw(_this);
	return func(_this, SyncInterval, Flags);
}

// 2. Update the Draw function
void FoveatedDebug::Draw(IDXGISwapChain* swapChain)
{
	// Add at the very start of Draw():
	logger::info("FoveatedDebug::Draw called - EnableDebug={}, initialized={}, PS={}, VS={}",
		settings.EnableDebug, initialized, (bool)debugPS, (bool)debugVS);

	if (!settings.EnableDebug || !initialized || !debugPS || !debugVS || !swapChain) {
		return;
	}

	auto context = globals::d3d::context;

	// --- NEW: Get BackBuffer RTV ---
	winrt::com_ptr<ID3D11Texture2D> backBuffer;
	if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.put_void()))) {
		return;
	}

	winrt::com_ptr<ID3D11RenderTargetView> backBufferRTV;
	if (FAILED(globals::d3d::device->CreateRenderTargetView(backBuffer.get(), nullptr, backBufferRTV.put()))) {
		return;
	}
	// -------------------------------

	// Try to install compositor hook if not done yet (lazy initialization)
	TryInstallCompositorHook();

	// Update data (Eye Gaze / Constants)
	UpdateEyeGazeData();
	UpdateConstantBuffers();

	// --- Save Old State ---
	ID3D11RenderTargetView* oldRTVs[8] = { nullptr };
	ID3D11DepthStencilView* oldDSV = nullptr;
	context->OMGetRenderTargets(8, oldRTVs, &oldDSV);

	ID3D11RasterizerState* oldRS = nullptr;
	context->RSGetState(&oldRS);

	ID3D11BlendState* oldBlend = nullptr;
	float oldBlendFactor[4];
	UINT oldMask;
	context->OMGetBlendState(&oldBlend, oldBlendFactor, &oldMask);

	// Set viewport to match the BackBuffer (Screen Resolution)
	D3D11_TEXTURE2D_DESC desc;
	backBuffer->GetDesc(&desc);
	D3D11_VIEWPORT viewport = {};
	viewport.Width = static_cast<float>(desc.Width);
	viewport.Height = static_cast<float>(desc.Height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	context->RSSetViewports(1, &viewport);

	// 2. Set Render Target (BackBuffer, NO Depth)
	ID3D11RenderTargetView* rtvs[1] = { backBufferRTV.get() };
	context->OMSetRenderTargets(1, rtvs, nullptr);  // Ensure nullptr DSV

	// 3. Set States (CRITICAL)
	context->RSSetState(rasterizerState.get());  // Force No Culling

	float blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	context->OMSetBlendState(blendState.get(), blendFactor, 0xFFFFFFFF);  // Force Alpha Blend

	// 4. Bind Shaders & Buffers
	auto foveatedCB = foveatedBuffer.get();
	auto settingsCB = settingsBuffer.get();
	context->PSSetConstantBuffers(10, 1, &foveatedCB);
	context->PSSetConstantBuffers(11, 1, &settingsCB);

	context->PSSetShader(debugPS.get(), nullptr, 0);
	context->VSSetShader(debugVS.get(), nullptr, 0);

	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->IASetInputLayout(nullptr);

	// 5. Draw
	context->Draw(3, 0);

	logger::info("FoveatedDebug: Drew overlay to {}x{} backbuffer", desc.Width, desc.Height);

	// --- Restore Old State ---
	context->OMSetRenderTargets(8, oldRTVs, oldDSV);
	context->RSSetState(oldRS);  // Restore Rasterizer
	context->OMSetBlendState(oldBlend, oldBlendFactor, oldMask);

	// Cleanup References
	if (oldRS)
		oldRS->Release();
	if (oldBlend)
		oldBlend->Release();
	for (auto* rtv : oldRTVs)
		if (rtv)
			rtv->Release();
	if (oldDSV)
		oldDSV->Release();

	// Unbind Shaders
	context->PSSetShader(nullptr, nullptr, 0);
	context->VSSetShader(nullptr, nullptr, 0);
}

// 1. Clean up PostPostLoad (Remove Hooks::Install from here)
void FoveatedDebug::PostPostLoad()
{
	if (!globals::game::isVR)
		return;

	// Only initialize logic that doesn't need the GPU/Window here
	InitializeEyeTracking();
}

// 2. Use DataLoaded to install the hook
// This runs after the game engine and D3D are fully initialized.
void FoveatedDebug::DataLoaded()
{
	// It is now safe to access globals::d3d::swapChain
	Hooks::Install();
}

vr::EVRCompositorError FoveatedDebug::Hooks::IVRCompositor_Submit::thunk(
	vr::IVRCompositor* _this,
	vr::EVREye eEye,
	const vr::Texture_t* pTexture,
	const vr::VRTextureBounds_t* pBounds,
	vr::EVRSubmitFlags nSubmitFlags)
{
	static bool loggedOnce = false;
	if (!loggedOnce) {
		logger::info("FoveatedDebug: Submit hook called! Eye={}, Texture={}, Type={}",
			(int)eEye,
			pTexture ? pTexture->handle : nullptr,
			pTexture ? (int)pTexture->eType : -1);
		loggedOnce = true;
	}

	// Draw our overlay onto the eye texture before it's submitted
	if (pTexture && pTexture->eType == vr::TextureType_DirectX) {
		auto* d3dTexture = static_cast<ID3D11Texture2D*>(pTexture->handle);
		globals::features::foveatedDebug.DrawToEyeTexture(d3dTexture);
	}

	return func(_this, eEye, pTexture, pBounds, nSubmitFlags);
}

void FoveatedDebug::DrawToEyeTexture(ID3D11Texture2D* eyeTexture)
{
	if (!settings.EnableDebug || !initialized || !debugPS || !debugVS || !eyeTexture)
		return;

	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	// Create a temporary RTV for the eye texture
	winrt::com_ptr<ID3D11RenderTargetView> eyeRTV;
	if (FAILED(device->CreateRenderTargetView(eyeTexture, nullptr, eyeRTV.put()))) {
		return;
	}

	// Get texture dimensions for viewport
	D3D11_TEXTURE2D_DESC desc;
	eyeTexture->GetDesc(&desc);

	// Save current state
	ID3D11RenderTargetView* oldRTVs[8] = { nullptr };
	ID3D11DepthStencilView* oldDSV = nullptr;
	context->OMGetRenderTargets(8, oldRTVs, &oldDSV);

	ID3D11RasterizerState* oldRS = nullptr;
	context->RSGetState(&oldRS);

	ID3D11BlendState* oldBlend = nullptr;
	float oldBlendFactor[4];
	UINT oldMask;
	context->OMGetBlendState(&oldBlend, oldBlendFactor, &oldMask);

	D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
	UINT numViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	context->RSGetViewports(&numViewports, oldViewports);

	// Set up for drawing
	D3D11_VIEWPORT viewport = {};
	viewport.Width = static_cast<float>(desc.Width);
	viewport.Height = static_cast<float>(desc.Height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	context->RSSetViewports(1, &viewport);

	ID3D11RenderTargetView* rtvs[1] = { eyeRTV.get() };
	context->OMSetRenderTargets(1, rtvs, nullptr);

	context->RSSetState(rasterizerState.get());
	float blendFactor[4] = { 0, 0, 0, 0 };
	context->OMSetBlendState(blendState.get(), blendFactor, 0xFFFFFFFF);

	// Bind shaders and buffers
	auto foveatedCB = foveatedBuffer.get();
	auto settingsCB = settingsBuffer.get();
	context->PSSetConstantBuffers(10, 1, &foveatedCB);
	context->PSSetConstantBuffers(11, 1, &settingsCB);

	context->PSSetShader(debugPS.get(), nullptr, 0);
	context->VSSetShader(debugVS.get(), nullptr, 0);

	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->IASetInputLayout(nullptr);

	// Draw the overlay
	context->Draw(3, 0);

	// Restore state
	context->OMSetRenderTargets(8, oldRTVs, oldDSV);
	context->RSSetState(oldRS);
	context->OMSetBlendState(oldBlend, oldBlendFactor, oldMask);
	context->RSSetViewports(numViewports, oldViewports);

	// Cleanup
	if (oldRS)
		oldRS->Release();
	if (oldBlend)
		oldBlend->Release();
	for (auto* rtv : oldRTVs)
		if (rtv)
			rtv->Release();
	if (oldDSV)
		oldDSV->Release();
}

void FoveatedDebug::TryInstallCompositorHook()
{
	if (compositorHookInstalled)
		return;

	logger::info("FoveatedDebug: Attempting compositor hook installation...");

	auto* bsOpenVR = RE::BSOpenVR::GetSingleton();
	if (!bsOpenVR) {
		logger::warn("FoveatedDebug: BSOpenVR singleton is null");
		return;
	}

	logger::info("FoveatedDebug: BSOpenVR found at {}", (void*)bsOpenVR);

	auto* compositor = bsOpenVR->vrContext.vrCompositor;
	logger::info("FoveatedDebug: vrContext.vrCompositor = {}", (void*)compositor);

	if (!compositor) {
		// Try the global OpenVR function as fallback
		compositor = vr::VRCompositor();
		logger::info("FoveatedDebug: vr::VRCompositor() = {}", (void*)compositor);
	}

	if (!compositor) {
		logger::warn("FoveatedDebug: No compositor available");
		return;
	}

	// Hook the compositor
	auto vtable = *reinterpret_cast<void***>(compositor);
	logger::info("FoveatedDebug: Compositor vtable at {}", (void*)vtable);

	Hooks::IVRCompositor_Submit::func = reinterpret_cast<decltype(Hooks::IVRCompositor_Submit::func)>(vtable[5]);
	logger::info("FoveatedDebug: Original Submit function at {}", (void*)Hooks::IVRCompositor_Submit::func);

	DWORD oldProtect;
	if (!VirtualProtect(&vtable[5], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		logger::error("FoveatedDebug: VirtualProtect failed");
		return;
	}

	vtable[5] = reinterpret_cast<void*>(&Hooks::IVRCompositor_Submit::thunk);
	VirtualProtect(&vtable[5], sizeof(void*), oldProtect, &oldProtect);

	compositorHookInstalled = true;
	logger::info("FoveatedDebug: Compositor hook installed successfully!");
}

void FoveatedDebug::Hooks::Install()
{
	// Desktop mirror hook (existing)
	if (globals::d3d::swapChain) {
		stl::detour_vfunc<8, IDXGISwapChain_Present>(globals::d3d::swapChain);
		logger::info("FoveatedDebug: Installed Present hook");
	}

	// Try VR compositor hook now, but it may not be available yet
	globals::features::foveatedDebug.TryInstallCompositorHook();
}