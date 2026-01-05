#pragma once
#include "Feature.h"
#include <array>
#include <openxr/openxr.h>

struct FoveatedDebug : Feature
{
	//=========================================================================
	// SETTINGS AND DATA STRUCTURES
	//=========================================================================

	struct Settings
	{
		uint32_t EnableDebug = 1;
		std::array<float, 3> DebugHue = { 1.0f, 0.0f, 0.0f };
		float DebugAlpha = 0.3f;
		uint32_t ShowGazePoint = 1;
		float GazePointSize = 0.02f;
		uint32_t ShowMetrics = 1;
		uint32_t pad0[4] = { 0 };
	};

	struct FoveatedRegionData
	{
		float GazePoint[2] = { 0.5f, 0.5f };
		float InnerRadius = 0.15f;
		float OuterRadius = 0.35f;

		float EdgeSoftness = 0.05f;
		uint32_t IsTracking = 0;
		float Confidence = 0.0f;

		uint32_t pad[5] = { 0 };
	};

	Settings settings;
	FoveatedRegionData regionData;

	//=========================================================================
	// FEATURE BASE CLASS OVERRIDES
	//=========================================================================

	virtual inline std::string GetName() override { return "Foveated Debug"; }
	virtual inline std::string GetShortName() override { return "FoveatedDebug"; }

	inline std::string_view GetShaderDefineName() override { return "FOVEATED_DEBUG"; }
	bool HasShaderDefine(RE::BSShader::Type) override { return true; }
	bool SupportsVR() override { return true; }

	void SetupResources() override;
	void DataLoaded() override;
	void PostPostLoad() override;
	void DrawSettings() override;
	void ClearShaderCache() override;
	void Prepass() override;

	//=========================================================================
	// HOOKS
	//=========================================================================

	struct Hooks
	{
		struct IDXGISwapChain_Present
		{
			static HRESULT WINAPI thunk(IDXGISwapChain* _this, UINT SyncInterval, UINT Flags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ID3D11DeviceContext_CopySubresourceRegion
		{
			static void STDMETHODCALLTYPE thunk(
				ID3D11DeviceContext* _this,
				ID3D11Resource* pDstResource,
				UINT DstSubresource,
				UINT DstX,
				UINT DstY,
				UINT DstZ,
				ID3D11Resource* pSrcResource,
				UINT SrcSubresource,
				const D3D11_BOX* pSrcBox);
			static inline decltype(&thunk) func;
		};

		struct IVRCompositor_Submit
		{
			static vr::EVRCompositorError thunk(vr::IVRCompositor* _this, vr::EVREye eEye, const vr::Texture_t* pTexture, const vr::VRTextureBounds_t* pBounds, vr::EVRSubmitFlags nSubmitFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install();
	};

	//=========================================================================
	// PUBLIC METHODS
	//=========================================================================

	void Draw(IDXGISwapChain* swapChain);
	void DrawOverlayToTexture(ID3D11Texture2D* texture);
	void UpdateConstantBuffers();
	void UpdateEyeGazeData();

	void SaveSettings(json& o_json) override;
	void LoadSettings(json& o_json) override;
	void RestoreDefaultSettings() override;

	//=========================================================================
	// EYE TRACKING API
	//=========================================================================

	enum class EyeTrackingAPI
	{
		None,
		OpenXR,
		Fallback
	};

	bool InitializeEyeTracking();
	void ShutdownEyeTracking();

	EyeTrackingAPI GetActiveAPI() const { return activeAPI; }
	const char* GetAPIName() const;
	bool IsEyeTrackingAvailable() const { return activeAPI == EyeTrackingAPI::OpenXR; }

	//=========================================================================
	// RENDERING RESOURCES (public for hook access)
	//=========================================================================

	winrt::com_ptr<ID3D11Buffer> foveatedBuffer;
	winrt::com_ptr<ID3D11Buffer> settingsBuffer;
	winrt::com_ptr<ID3D11PixelShader> debugPS;
	winrt::com_ptr<ID3D11VertexShader> debugVS;
	winrt::com_ptr<ID3D11BlendState> blendState;
	winrt::com_ptr<ID3D11RasterizerState> rasterizerState;

	bool initialized = false;

	struct Metrics
	{
		uint32_t updateCount = 0;
		float averageConfidence = 0.0f;
	} metrics;

private:
	//=========================================================================
	// OPENXR IMPLEMENTATION
	//=========================================================================

	bool InitializeOpenXR();
	void UpdateOpenXRGaze();
	void ShutdownOpenXR();
	bool CreateOpenXRActions();

	XrInstance xrInstance = XR_NULL_HANDLE;
	XrSystemId xrSystemId = XR_NULL_SYSTEM_ID;
	XrSession xrSession = XR_NULL_HANDLE;
	XrSpace xrViewSpace = XR_NULL_HANDLE;
	XrSpace xrGazeSpace = XR_NULL_HANDLE;

	XrActionSet xrActionSet = XR_NULL_HANDLE;
	XrAction xrGazeAction = XR_NULL_HANDLE;

	bool openXRInitialized = false;
	bool eyeGazeSupported = false;

	EyeTrackingAPI activeAPI = EyeTrackingAPI::None;

	void ProjectGazeToScreen(const DirectX::XMFLOAT3& gazeDirection, float& outU, float& outV);
};