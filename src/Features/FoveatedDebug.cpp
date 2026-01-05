#include "FoveatedDebug.h"
#include "Globals.h"
#include "ShaderCache.h"
#include "State.h"
#include <DirectXMath.h>
#include <openvr.h>
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr_platform.h>

#include "RE/B/BSOpenVR.h"

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

	// 1. Create Constant Buffers
	D3D11_BUFFER_DESC bufferDesc = {};
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	// Foveated Buffer
	bufferDesc.ByteWidth = sizeof(FoveatedRegionData);
	device->CreateBuffer(&bufferDesc, nullptr, foveatedBuffer.put());

	// Settings Buffer
	bufferDesc.ByteWidth = sizeof(Settings);
	device->CreateBuffer(&bufferDesc, nullptr, settingsBuffer.put());

	// 2. Create Rasterizer State (NO CULLING)
	D3D11_RASTERIZER_DESC rasterDesc = {};
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = D3D11_CULL_NONE;
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

	if (!debugPS || !debugVS) {
		logger::error("FoveatedDebug: Shader compilation failed!");
		return;
	}

	initialized = true;
	logger::info("FoveatedDebug: Resources setup complete.");
}

void FoveatedDebug::ClearShaderCache()
{
	logger::info("FoveatedDebug: Recompiling shaders...");

	debugPS = nullptr;
	debugVS = nullptr;

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
		debugPS.attach(static_cast<ID3D11PixelShader*>(compiledPS));
		logger::info("FoveatedDebug: Pixel shader compiled successfully");
	} else {
		logger::error("FoveatedDebug: Failed to compile pixel shader");
	}

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
// EYE TRACKING
//=============================================================================

bool FoveatedDebug::InitializeEyeTracking()
{
	if (!globals::game::isVR) {
		logger::info("FoveatedDebug: Not in VR mode");
		return false;
	}

	logger::info("FoveatedDebug: Initializing eye tracking...");
	logger::info("FoveatedDebug: OpenXR integration disabled - using center-based fallback");

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

bool FoveatedDebug::InitializeOpenXR()
{
	return false;
}

void FoveatedDebug::UpdateOpenXRGaze()
{
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

void FoveatedDebug::ShutdownEyeTracking()
{
	ShutdownOpenXR();
	activeAPI = EyeTrackingAPI::None;
}

bool FoveatedDebug::CreateOpenXRActions()
{
	return false;
}

void FoveatedDebug::ProjectGazeToScreen(const DirectX::XMFLOAT3& gazeDirection, float& outU, float& outV)
{
	XMVECTOR gaze = XMLoadFloat3(&gazeDirection);
	gaze = XMVector3Normalize(gaze);

	XMFLOAT3 gazeNorm;
	XMStoreFloat3(&gazeNorm, gaze);

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

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(context->Map(foveatedBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		memcpy(mapped.pData, &regionData, sizeof(FoveatedRegionData));
		context->Unmap(foveatedBuffer.get(), 0);
	}

	if (SUCCEEDED(context->Map(settingsBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		memcpy(mapped.pData, &settings, sizeof(Settings));
		context->Unmap(settingsBuffer.get(), 0);
	}
}

//=============================================================================
// HOOKS
//=============================================================================

HRESULT WINAPI FoveatedDebug::Hooks::IDXGISwapChain_Present::thunk(IDXGISwapChain* _this, UINT SyncInterval, UINT Flags)
{
	globals::features::foveatedDebug.Draw(_this);
	return func(_this, SyncInterval, Flags);
}

void STDMETHODCALLTYPE FoveatedDebug::Hooks::ID3D11DeviceContext_CopySubresourceRegion::thunk(
	ID3D11DeviceContext* _this,
	ID3D11Resource* pDstResource,
	UINT DstSubresource,
	UINT DstX,
	UINT DstY,
	UINT DstZ,
	ID3D11Resource* pSrcResource,
	UINT SrcSubresource,
	const D3D11_BOX* pSrcBox)
{
	static thread_local bool inHook = false;

	auto& feature = globals::features::foveatedDebug;

	// Check if this is a VR frame copy (source is VR-sized texture)
	if (!inHook && feature.settings.EnableDebug && feature.initialized &&
		feature.debugPS && feature.debugVS && pSrcResource) {
		ID3D11Texture2D* srcTexture = nullptr;
		if (SUCCEEDED(pSrcResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&srcTexture))) {
			D3D11_TEXTURE2D_DESC desc;
			srcTexture->GetDesc(&desc);

			// Check if this is a VR-sized texture being copied
			bool isVRCopy = (desc.Width >= 2000 && desc.Height >= 2000);

			if (isVRCopy) {
				inHook = true;
				feature.DrawOverlayToTexture(srcTexture);
				inHook = false;
			}

			srcTexture->Release();
		}
	}

	// Call original
	func(_this, pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox);
}

// ----------------------------------------------------------------------------
// VR COMPOSITOR SUBMIT HOOK
// ----------------------------------------------------------------------------

vr::EVRCompositorError FoveatedDebug::Hooks::IVRCompositor_Submit::thunk(
	vr::IVRCompositor* _this,
	vr::EVREye eEye,
	const vr::Texture_t* pTexture,
	const vr::VRTextureBounds_t* pBounds,
	vr::EVRSubmitFlags nSubmitFlags)
{
	// Log once to confirm hook is firing and what type of texture we have
	static bool loggedSubmit = false;

	// 1. Intercept the texture before submission
	if (pTexture && pTexture->handle) {
		// OpenVR passes D3D11 textures as void* handles
		ID3D11Texture2D* texture = static_cast<ID3D11Texture2D*>(pTexture->handle);

		if (!loggedSubmit) {
			D3D11_TEXTURE2D_DESC desc;
			texture->GetDesc(&desc);
			logger::info("FoveatedDebug: Submit Hook firing. Eye: {}, Format: {}, Size: {}x{}",
				(int)eEye, (int)desc.Format, desc.Width, desc.Height);
			loggedSubmit = true;
		}

		// 2. Draw your overlay
		globals::features::foveatedDebug.DrawOverlayToTexture(texture);
	}

	// 3. Pass to original function (sends to headset)
	return func(_this, eEye, pTexture, pBounds, nSubmitFlags);
}

void FoveatedDebug::Hooks::Install()
{
	// Desktop mirror hook
	if (globals::d3d::swapChain) {
		stl::detour_vfunc<8, IDXGISwapChain_Present>(globals::d3d::swapChain);
		logger::info("FoveatedDebug: Installed Present hook");
	}

	// Hook CopySubresourceRegion - vtable index 46
	if (globals::d3d::context) {
		auto vtable = *reinterpret_cast<void***>(globals::d3d::context);

		ID3D11DeviceContext_CopySubresourceRegion::func =
			reinterpret_cast<decltype(ID3D11DeviceContext_CopySubresourceRegion::func)>(vtable[46]);

		DWORD oldProtect;
		VirtualProtect(&vtable[46], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
		vtable[46] = reinterpret_cast<void*>(&ID3D11DeviceContext_CopySubresourceRegion::thunk);
		VirtualProtect(&vtable[46], sizeof(void*), oldProtect, &oldProtect);

		logger::info("FoveatedDebug: Installed CopySubresourceRegion hook");
	}

	// Hook VR Submit
	auto openvr = RE::BSOpenVR::GetSingleton();
	if (openvr) {
		// Try to get compositor from context first (more robust for CommonLibVR)
		vr::IVRCompositor* compositor = openvr->vrContext.vrCompositor;

		// Fallback/Safety check using static getter if available
		if (!compositor) {
			compositor = RE::BSOpenVR::GetIVRCompositor();
		}

		if (compositor) {
			// IVRCompositor::Submit is index 5
			stl::detour_vfunc<5, IVRCompositor_Submit>(compositor);
			logger::info("FoveatedDebug: Installed IVRCompositor::Submit hook");
		} else {
			logger::warn("FoveatedDebug: Failed to hook VR Submit - Compositor not found");
		}
	}
}

//=============================================================================
// DRAW FUNCTIONS
//=============================================================================

void FoveatedDebug::Draw(IDXGISwapChain* swapChain)
{
	if (!settings.EnableDebug || !initialized || !debugPS || !debugVS)
		return;

	auto context = globals::d3d::context;
	auto device = globals::d3d::device;

	UpdateEyeGazeData();
	UpdateConstantBuffers();

	// Desktop mirror rendering
	if (swapChain) {
		winrt::com_ptr<ID3D11Texture2D> backBuffer;
		if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.put_void()))) {
			return;
		}

		winrt::com_ptr<ID3D11RenderTargetView> backBufferRTV;
		if (FAILED(device->CreateRenderTargetView(backBuffer.get(), nullptr, backBufferRTV.put()))) {
			return;
		}

		// Save state
		ID3D11RenderTargetView* oldRTVs[8] = { nullptr };
		ID3D11DepthStencilView* oldDSV = nullptr;
		context->OMGetRenderTargets(8, oldRTVs, &oldDSV);

		ID3D11RasterizerState* oldRS = nullptr;
		context->RSGetState(&oldRS);

		ID3D11BlendState* oldBlend = nullptr;
		float oldBlendFactor[4];
		UINT oldMask;
		context->OMGetBlendState(&oldBlend, oldBlendFactor, &oldMask);

		// Set viewport
		D3D11_TEXTURE2D_DESC desc;
		backBuffer->GetDesc(&desc);
		D3D11_VIEWPORT viewport = {};
		viewport.Width = static_cast<float>(desc.Width);
		viewport.Height = static_cast<float>(desc.Height);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, &viewport);

		// Set render target
		ID3D11RenderTargetView* rtvs[1] = { backBufferRTV.get() };
		context->OMSetRenderTargets(1, rtvs, nullptr);

		// Set states
		context->RSSetState(rasterizerState.get());
		float blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
		context->OMSetBlendState(blendState.get(), blendFactor, 0xFFFFFFFF);

		// Bind shaders
		auto foveatedCB = foveatedBuffer.get();
		auto settingsCB = settingsBuffer.get();
		context->PSSetConstantBuffers(10, 1, &foveatedCB);
		context->PSSetConstantBuffers(11, 1, &settingsCB);

		context->PSSetShader(debugPS.get(), nullptr, 0);
		context->VSSetShader(debugVS.get(), nullptr, 0);

		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->IASetInputLayout(nullptr);

		// Draw
		context->Draw(3, 0);

		// Restore state
		context->OMSetRenderTargets(8, oldRTVs, oldDSV);
		context->RSSetState(oldRS);
		context->OMSetBlendState(oldBlend, oldBlendFactor, oldMask);

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

		context->PSSetShader(nullptr, nullptr, 0);
		context->VSSetShader(nullptr, nullptr, 0);
	}
}

void FoveatedDebug::DrawOverlayToTexture(ID3D11Texture2D* texture)
{
	if (!settings.EnableDebug || !initialized || !debugPS || !debugVS || !texture)
		return;

	auto context = globals::d3d::context;
	auto device = globals::d3d::device;

	// --- 1. RTV Creation (Standard checks) ---
	D3D11_TEXTURE2D_DESC desc;
	texture->GetDesc(&desc);

	// Ignore Depth/Typeless formats
	if (desc.Format == DXGI_FORMAT_R16_TYPELESS || desc.Format == DXGI_FORMAT_D16_UNORM ||
		desc.Format == DXGI_FORMAT_R24G8_TYPELESS || desc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
		desc.Format == DXGI_FORMAT_R32_TYPELESS || desc.Format == DXGI_FORMAT_D32_FLOAT) {
		return;
	}

	winrt::com_ptr<ID3D11RenderTargetView> tempRTV;
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Format = desc.Format;

	// Handle Typeless Color
	if (desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
		rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	else if (desc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS)
		rtvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	else if (desc.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS)
		rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

	if (FAILED(device->CreateRenderTargetView(texture, &rtvDesc, tempRTV.put()))) {
		if (FAILED(device->CreateRenderTargetView(texture, nullptr, tempRTV.put())))
			return;
	}

	UpdateEyeGazeData();
	float originalGazeX = regionData.GazePoint[0];
	float originalGazeY = regionData.GazePoint[1];

	// --- 2. Save State ---
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
	ID3D11VertexShader* oldVS = nullptr;
	ID3D11PixelShader* oldPS = nullptr;
	context->VSGetShader(&oldVS, nullptr, nullptr);
	context->PSGetShader(&oldPS, nullptr, nullptr);
	D3D11_PRIMITIVE_TOPOLOGY oldTopology;
	context->IAGetPrimitiveTopology(&oldTopology);
	ID3D11InputLayout* oldInputLayout = nullptr;
	context->IAGetInputLayout(&oldInputLayout);

	// --- 3. Setup Drawing ---
	ID3D11RenderTargetView* rtvPtr = tempRTV.get();
	context->OMSetRenderTargets(1, &rtvPtr, nullptr);
	context->RSSetState(rasterizerState.get());
	float blendFactor[4] = { 0, 0, 0, 0 };
	context->OMSetBlendState(blendState.get(), blendFactor, 0xFFFFFFFF);
	context->OMSetDepthStencilState(nullptr, 0);

	auto foveatedCB = foveatedBuffer.get();
	auto settingsCB = settingsBuffer.get();
	context->PSSetConstantBuffers(10, 1, &foveatedCB);
	context->PSSetConstantBuffers(11, 1, &settingsCB);
	context->PSSetShader(debugPS.get(), nullptr, 0);
	context->VSSetShader(debugVS.get(), nullptr, 0);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->IASetInputLayout(nullptr);

	// --- 4. Stereo Draw Loop ---
	float width = static_cast<float>(desc.Width) / 2.0f;
	float height = static_cast<float>(desc.Height);

	// Get VR System to query projection
	auto vrSystem = RE::BSOpenVR::GetIVRSystem();

	for (int eye = 0; eye < 2; ++eye) {
		float opticalCenterX = 0.5f;
		float opticalCenterY = 0.5f;

		// Calculate Optical Center from Projection Raw
		if (vrSystem) {
			float l, r, t, b;
			vrSystem->GetProjectionRaw(static_cast<vr::EVREye>(eye), &l, &r, &t, &b);

			// The optical center (straight ahead) is at tan(0) = 0.
			// Total width span = r - l.
			// Distance from left edge to 0 = 0 - l = -l.
			// Normalized X = -l / (r - l)
			opticalCenterX = -l / (r - l);

			// Total height span = t - b. (Note: OpenVR 't' is usually negative for "top" in texture space?
			// Actually OpenVR Tangents are usually Up=+Y. But let's stick to the standard UV formula)
			// If we assume standard OpenVR tangents (Up+, Right+):
			// Center Y (V) is derived from mapping [b, t] to [1, 0] (V flips).
			// V_center = t / (t - b)  (assuming t is positive up)
			opticalCenterY = t / (t - b);
		}

		// Apply fallback gaze (center of vision) using the calculated optical center
		if (activeAPI == EyeTrackingAPI::Fallback || activeAPI == EyeTrackingAPI::None) {
			regionData.GazePoint[0] = opticalCenterX;
			regionData.GazePoint[1] = opticalCenterY;
		}
		// Note: If activeAPI == OpenXR, you should theoretically transform the gaze
		// using these same tangents, but we'll stick to fixing the fallback overlay for now.

		UpdateConstantBuffers();

		D3D11_VIEWPORT viewport = {};
		viewport.Width = width;
		viewport.Height = height;
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		viewport.TopLeftX = (eye == 0) ? 0.0f : width;
		viewport.TopLeftY = 0.0f;

		context->RSSetViewports(1, &viewport);
		context->Draw(3, 0);
	}

	// Restore original gaze
	regionData.GazePoint[0] = originalGazeX;
	regionData.GazePoint[1] = originalGazeY;

	// --- 5. Restore State ---
	context->OMSetRenderTargets(8, oldRTVs, oldDSV);
	context->RSSetState(oldRS);
	context->OMSetBlendState(oldBlend, oldBlendFactor, oldMask);
	context->RSSetViewports(numViewports, oldViewports);
	context->VSSetShader(oldVS, nullptr, 0);
	context->PSSetShader(oldPS, nullptr, 0);
	context->IASetPrimitiveTopology(oldTopology);
	context->IASetInputLayout(oldInputLayout);

	// Cleanup
	if (oldRS)
		oldRS->Release();
	if (oldBlend)
		oldBlend->Release();
	if (oldVS)
		oldVS->Release();
	if (oldPS)
		oldPS->Release();
	if (oldInputLayout)
		oldInputLayout->Release();
	for (auto* rtv : oldRTVs)
		if (rtv)
			rtv->Release();
	if (oldDSV)
		oldDSV->Release();
}

//=============================================================================
// LIFECYCLE
//=============================================================================

void FoveatedDebug::PostPostLoad()
{
	if (!globals::game::isVR)
		return;

	InitializeEyeTracking();
}

void FoveatedDebug::DataLoaded()
{
	Hooks::Install();
}

void FoveatedDebug::Prepass()
{
	if (!settings.EnableDebug || !initialized || !debugPS || !debugVS)
		return;

	if (!globals::game::isVR)
		return;

	static bool loggedOnce = false;
	if (!loggedOnce) {
		auto renderer = RE::BSGraphics::Renderer::GetSingleton();
		if (renderer) {
			logger::info("FoveatedDebug: Checking renderer for VR targets...");

			auto& runtimeData = renderer->GetRuntimeData();

			for (int i = 0; i < RE::RENDER_TARGETS::kVRTOTAL; i++) {
				auto& rt = runtimeData.renderTargets[i];
				if (rt.texture) {
					D3D11_TEXTURE2D_DESC desc;
					rt.texture->GetDesc(&desc);
					logger::info("  RT[{}]: {}x{}", i, desc.Width, desc.Height);
				}
			}
		}
		loggedOnce = true;
	}
}