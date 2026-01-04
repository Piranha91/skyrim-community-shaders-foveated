#pragma once
#include "Feature.h"
#include <array>
#include <openxr/openxr.h>

struct FoveatedDebug : Feature
{
	static FoveatedDebug* GetSingleton()
	{
		static FoveatedDebug singleton;
		return &singleton;
	}

	//=========================================================================
	// SETTINGS AND DATA STRUCTURES
	//=========================================================================

	struct Settings
	{
		uint32_t EnableDebug = 0;
		std::array<float, 3> DebugHue = { 1.0f, 0.0f, 0.0f };
		float DebugAlpha = 0.3f;
		uint32_t ShowGazePoint = 1;
		float GazePointSize = 0.02f;
		uint32_t ShowMetrics = 1;
		uint32_t pad0 = 0;
	};

	struct FoveatedRegionData
	{
		float GazePoint[2] = { 0.5f, 0.5f };  // Normalized screen coords (0-1)
		float InnerRadius = 0.15f;            // Inner full-res radius
		float OuterRadius = 0.35f;            // Outer transition radius
		float EdgeSoftness = 0.05f;           // Falloff between inner/outer
		uint32_t IsTracking = 0;              // Whether eye tracking is active
		float Confidence = 0.0f;              // Tracking confidence (0-1)
		uint32_t pad[2] = { 0, 0 };
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
	RE::BSGraphics::PixelShader* debugPS = nullptr;   // Changed type
	RE::BSGraphics::VertexShader* debugVS = nullptr;  // Changed type
	winrt::com_ptr<ID3D11BlendState> blendState;

	// Debug metrics
	struct Metrics
	{
		uint32_t updateCount = 0;
		float averageConfidence = 0.0f;
	} metrics;

	bool initialized = false;
};