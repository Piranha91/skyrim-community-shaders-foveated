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
		uint32_t EnableDebug = 1;  // Default to 1 so it's visible!
		std::array<float, 3> DebugHue = { 1.0f, 0.0f, 0.0f };
		float DebugAlpha = 0.3f;
		uint32_t ShowGazePoint = 1;
		float GazePointSize = 0.02f;
		uint32_t ShowMetrics = 1;
		uint32_t pad0[4] = { 0 };  // Keep your padding fix!
	};

	struct FoveatedRegionData
	{
		float GazePoint[2] = { 0.5f, 0.5f };  // 8 bytes
		float InnerRadius = 0.15f;            // 4 bytes
		float OuterRadius = 0.35f;            // 4 bytes
		// --- 16 bytes boundary ---

		float EdgeSoftness = 0.05f;  // 4 bytes
		uint32_t IsTracking = 0;     // 4 bytes
		float Confidence = 0.0f;     // 4 bytes
		// --- 28 bytes so far ---

		// 36 bytes previously (28 + 8). We need 48 bytes total.
		// So we need 20 bytes of padding (48 - 28 = 20).
		uint32_t pad[5] = { 0 };  // 5 * 4 = 20 bytes
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

	struct Hooks
	{
		struct IDXGISwapChain_Present
		{
			static HRESULT WINAPI thunk(IDXGISwapChain* _this, UINT SyncInterval, UINT Flags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install();
	};

	void Draw(IDXGISwapChain* swapChain);            
	void UpdateConstantBuffers();  

	void SaveSettings(json& o_json) override;
	void LoadSettings(json& o_json) override;
	void RestoreDefaultSettings() override;

	//=========================================================================
	// EYE TRACKING API
	//=========================================================================

	enum class EyeTrackingAPI
	{
		None,
		OpenXR,   // Standard OpenXR eye gaze
		Fallback  // Center-based fallback
	};

	bool InitializeEyeTracking();
	void UpdateEyeGazeData();
	void ShutdownEyeTracking();

	EyeTrackingAPI GetActiveAPI() const { return activeAPI; }
	const char* GetAPIName() const;
	bool IsEyeTrackingAvailable() const { return activeAPI == EyeTrackingAPI::OpenXR; }

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

	// Action set for eye tracking
	XrActionSet xrActionSet = XR_NULL_HANDLE;
	XrAction xrGazeAction = XR_NULL_HANDLE;

	bool openXRInitialized = false;
	bool eyeGazeSupported = false;

	//=========================================================================
	// SHARED TRACKING STATE
	//=========================================================================

	EyeTrackingAPI activeAPI = EyeTrackingAPI::None;

	// Projection helpers
	void ProjectGazeToScreen(const DirectX::XMFLOAT3& gazeDirection, float& outU, float& outV);

	//=========================================================================
	// RENDERING RESOURCES
	//=========================================================================

	winrt::com_ptr<ID3D11Buffer> foveatedBuffer;
	winrt::com_ptr<ID3D11Buffer> settingsBuffer;
	winrt::com_ptr<ID3D11PixelShader> debugPS;
	winrt::com_ptr<ID3D11VertexShader> debugVS;
	winrt::com_ptr<ID3D11BlendState> blendState;
	winrt::com_ptr<ID3D11RasterizerState> rasterizerState;

	// Debug metrics
	struct Metrics
	{
		uint32_t updateCount = 0;
		float averageConfidence = 0.0f;
	} metrics;

	bool initialized = false;
};