#pragma once
#include "Feature.h"
#include <array>

struct FoveatedDebug : Feature
{
	static FoveatedDebug* GetSingleton()
	{
		static FoveatedDebug singleton;
		return &singleton;
	}

	struct Settings
	{
		uint32_t EnableDebug = 0;
		std::array<float, 3> DebugHue = { 1.0f, 0.0f, 0.0f };
		float DebugAlpha = 0.3f;
		uint32_t pad0 = 0;
	};

	Settings settings;

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

private:
	bool initialized = false;
};