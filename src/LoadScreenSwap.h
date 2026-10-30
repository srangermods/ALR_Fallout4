#pragma once
#include "pch.h"

// ALR loading screen replacement.
//
// Every load screen shows one ALR carrier model (an LS<n>.nif quad). While the
// LoadingMenu is up, the AdvanceMovie hook in main.cpp calls Tick(), which finds
// the quad's geometry and gives it a cloned material whose diffuse is a random
// unpadded DDS from Textures\Interface\ALR_Backgrounds. Whitelisted load screens
// get their pinned image instead.
namespace LoadScreenSwap
{
	enum class FitMode
	{
		kCrop,    // keep aspect ratio, fill the 16:9 band, crop the overflow
		kStretch  // map the whole image onto the band, ignoring aspect ratio
	};

	struct Config
	{
		std::string folder;  // absolute path of Textures\\Interface\\ALR_Backgrounds\\ (PathBuilder::OutputPath())
		FitMode     fit{ FitMode::kCrop };
		bool        debugLog{ false };

		// Image files are identified by file name (case-insensitive), not by number, so they
		// can be named anything. Each pin lists candidate file names, best first; the first
		// one present in the folder wins, and that file is kept out of random picks.
		struct Pin
		{
			const RE::TESLoadScreen* screen{ nullptr };
			std::vector<std::string> files;  // lowercase candidates
		};
		std::vector<Pin> pins;
	};

	// kGameDataReady. Enables Tick() and registers the
	// LoadingMenu open/close sink.
	void Configure(Config a_config);

	// kGameDataReady. Reads the game's resolution and sets the load screens' shared BGSTransform
	// scale: 0.21 at or near 21:9, 0.314 at or near 32:9, otherwise the plugin's value (0.16, 16:9).
	// Safe to call repeatedly.
	void ApplyScreenScale(RE::BGSTransform* a_transform);

	// kGameDataReady. Sets the loading menu's scene-light game settings ([Interface]
	// fLoadingMenuSceneLight*). The engine reads them each time a loading screen's model
	// finishes loading, so values set here apply to every loading screen after this.
	// ALR's defaults (light 3 white, dimmer 2.13) are applied first, then a_overrides.
	void ApplyLoadScreenLighting(const std::vector<std::pair<std::string, float>>& a_overrides);

	// Called from the image-generation worker thread. While images are pending,
	// load screens are left untouched so a half-written DDS is never picked.
	void SetImagesPending(bool a_pending);

	// Called from the AdvanceMovie hook every frame, BEFORE the original function.
	// The original reads artScreen, requests that screen's model, then clears
	// artScreen to null, so this is the only place to see which screen was chosen.
	void BeforeAdvance(RE::LoadingMenu* a_menu);

	// Called from the AdvanceMovie hook every frame, after the original function.
	void Tick(RE::LoadingMenu* a_menu);
}
