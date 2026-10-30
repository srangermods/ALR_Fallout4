#include "pch.h"
#include "LoadScreenSwap.h"

#include <atomic>
#include <charconv>
#include <cstring>

namespace
{
	using TT = RE::BSShaderProperty::TextureTypeEnum;
	using LoadScreenSwap::FitMode;

	constexpr float kVisibleHeight = 1080.0f / 2048.0f;
	constexpr float kDefaultAspect = 16.0f / 9.0f;

	struct WideScale
	{
		const char* name;
		float       minAspect;
		float       maxAspect;
		float       scale;
	};
	constexpr WideScale kWideScales[] = {
		{ "21:9", 2.2f, 2.5f, 0.21f },   // 1920x822 = 2.34, 2560x1080 = 2.37, 3440x1440 = 2.39
		{ "32:9", 3.3f, 3.8f, 0.314f },  // 3840x1080, 5120x1440, 7680x2160 = 3.56
	};

	constexpr std::uint32_t    kProbeFrames = 30;

	struct SwapImage
	{
		std::string   key;          // lowercase file name for whitelist
		std::string   texPath;
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
	};

	struct Layout
	{
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
		float         aspect{ kDefaultAspect };
		float         quadScale{ 1.0f };  // multiplier applied to the BGSTransform's authored scale
		float         u0{ 0.0f }, u1{ 1.0f }, v0{ 0.0f }, v1{ 1.0f };  // visible part, in quad UVs
	};

	struct UVXform
	{
		RE::NiPoint2 scale;
		RE::NiPoint2 offset;
	};

	// Config is written once at kGameDataReady, before any LoadingMenu exists.
	// Everything else is touched only from the AdvanceMovie hook (one thread);
	// the two flags are the only cross-thread state.
	LoadScreenSwap::Config g_config;
	std::atomic<bool>      g_enabled{ false };
	std::atomic<bool>      g_pending{ true };  // until the worker thread says images are done
	std::atomic<bool>      g_dirty{ true };    // pool needs a rescan

	// Last load screen whose model the engine requested (captured before AdvanceMovie
	// clears LoadingMenu::artScreen). Kept outside MenuState so a menu-open reset
	// arriving after the capture can't erase it.
	const RE::TESLoadScreen* g_requestedScreen{ nullptr };
	bool                     g_newRequest{ false };  // a screen was requested since the last swap

	std::vector<SwapImage>           g_pool;     // every readable DDS in the folder
	std::vector<std::size_t>     g_random;   // indices into g_pool eligible for random picks
	std::vector<std::pair<const RE::TESLoadScreen*, std::size_t>> g_resolvedPins;  // screen -> index into g_pool
	std::size_t                  g_lastPick{ static_cast<std::size_t>(-1) };
	std::mt19937                 g_rng{ std::random_device{}() };
	std::atomic<float>           g_appliedScale{ 1.0f };  // what ApplyScreenScale() multiplied the transform by

	struct MenuState
	{
		std::uint32_t         frame{ 0 };
		const RE::NiAVObject* target{ nullptr };  // identity only, never dereferenced
		bool                  done{ false };
		std::uint32_t         armedFrame{ 0 };  // frame the current model/screen was first seen

		// For the read-back check after a swap (debug log only)
		RE::BSShaderProperty*   swappedProp{ nullptr };
		const RE::BSShaderMaterial* swappedMat{ nullptr };
		std::uint32_t           swapFrame{ 0 };
	};
	MenuState g_menu;

	std::string Lower(std::string_view a_str)
	{
		std::string out{ a_str };
		std::transform(out.begin(), out.end(), out.begin(), [](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
		return out;
	}

	bool ReadDDSSize(const std::filesystem::path& a_file, std::uint32_t& a_width, std::uint32_t& a_height)
	{
		std::ifstream in(a_file, std::ios::binary);
		char          header[20]{};
		if (!in.read(header, sizeof(header)) || std::memcmp(header, "DDS ", 4) != 0) {
			return false;
		}
		std::memcpy(&a_height, header + 12, 4);  // DDS_HEADER.dwHeight
		std::memcpy(&a_width, header + 16, 4);   // DDS_HEADER.dwWidth
		return a_width > 0 && a_height > 0;
	}

	void RescanPool()
	{
		g_pool.clear();
		g_random.clear();
		g_resolvedPins.clear();
		g_lastPick = static_cast<std::size_t>(-1);

		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(g_config.folder, ec)) {
			std::error_code fileEc;
			if (!entry.is_regular_file(fileEc) || Lower(entry.path().extension().string()) != ".dds") {
				continue;
			}

			SwapImage img;
			if (!ReadDDSSize(entry.path(), img.width, img.height)) {
				spdlog::warn("TextureSwap: skipping unreadable DDS {}", entry.path().string());
				continue;
			}
			img.key = Lower(entry.path().filename().string());
			img.texPath = "Textures\\Interface\\ALR_Backgrounds\\" + entry.path().filename().string();
			g_pool.push_back(std::move(img));
		}
		if (ec) {
			spdlog::warn("TextureSwap: couldn't read {}: {}", g_config.folder, ec.message());
		}

		// Resolve each whitelist pin to the first candidate name that exists in the folder
		std::vector<bool> pinnedFile(g_pool.size(), false);
		for (const auto& pin : g_config.pins) {
			std::size_t found = g_pool.size();
			for (const auto& candidate : pin.files) {
				for (std::size_t i = 0; i < g_pool.size(); ++i) {
					if (g_pool[i].key == candidate) {
						found = i;
						break;
					}
				}
				if (found != g_pool.size()) {
					break;
				}
			}

			if (found == g_pool.size()) {
				std::string tried;
				for (const auto& candidate : pin.files) {
					tried += (tried.empty() ? "" : ", ") + candidate;
				}
				spdlog::warn("TextureSwap: whitelist pin {:08X}: none of [{}] is in the folder", pin.screen->GetFormID(), tried);
				continue;
			}
			g_resolvedPins.emplace_back(pin.screen, found);
			pinnedFile[found] = true;
			if (g_config.debugLog) {
				spdlog::info("TextureSwap: whitelist pin {:08X} -> {}", pin.screen->GetFormID(), g_pool[found].key);
			}
		}

		for (std::size_t i = 0; i < g_pool.size(); ++i) {
			if (!pinnedFile[i]) {
				g_random.push_back(i);
			}
		}
		spdlog::info("TextureSwap: {} image(s) in {}, {} whitelist pin(s) resolved, {} eligible for random picks",
			g_pool.size(), g_config.folder, g_resolvedPins.size(), g_random.size());
	}

	const SwapImage* PickImage(const RE::TESLoadScreen* a_screen)
	{
		if (a_screen) {
			for (const auto& [screen, index] : g_resolvedPins) {
				if (screen == a_screen) {
					spdlog::info("TextureSwap: load screen {:08X} is whitelisted -> {}", a_screen->GetFormID(), g_pool[index].key);
					return &g_pool[index];
				}
			}
		}

		if (g_config.debugLog) {
			spdlog::info("TextureSwap: load screen {:08X} is not whitelisted; picking at random",
				a_screen ? a_screen->GetFormID() : 0u);
		}
		if (g_random.empty()) {
			return nullptr;
		}
		std::uniform_int_distribution<std::size_t> dist{ 0, g_random.size() - 1 };
		auto                                       pick = g_random[dist(g_rng)];
		if (g_random.size() > 1) {
			while (pick == g_lastPick) {
				pick = g_random[dist(g_rng)];
			}
		}
		g_lastPick = pick;
		return &g_pool[pick];
	}

	Layout ReadScreen()
	{
		Layout l;
		const char* source = "none";

		if (const auto* data = RE::BSGraphics::GetRendererData()) {
			const auto& window = data->renderWindow[0];
			if (window.windowWidth > 0 && window.windowHeight > 0) {
				l.width = static_cast<std::uint32_t>(window.windowWidth);
				l.height = static_cast<std::uint32_t>(window.windowHeight);
				source = "render window";
			}
		}
		if (source == std::string_view{ "none" }) {
			if (const auto* state = RE::BSGraphics::State::GetSingleton(); state && state->backBufferWidth && state->backBufferHeight) {
				l.width = state->backBufferWidth;
				l.height = state->backBufferHeight;
				source = "back buffer";
			}
		}

		const bool  sane = l.width >= 320 && l.height >= 200 && l.width <= 32768 && l.height <= 32768;
		const float aspect = sane ? static_cast<float>(l.width) / static_cast<float>(l.height) : 0.0f;
		if (g_config.debugLog) {
			spdlog::info("TextureSwap: resolution {}x{} from {}", l.width, l.height, source);
		}
		if (aspect >= 0.5f && aspect <= 8.0f) {
			l.aspect = aspect;
		} else {
			spdlog::warn("TextureSwap: screen size {}x{} doesn't look valid; assuming 16:9", l.width, l.height);
		}
		return l;
	}

	// Screen layout for the scale the model actually has
	Layout GetScreenLayout(float a_appliedScale)
	{
		Layout l = ReadScreen();
		l.quadScale = a_appliedScale;
		const float w = (std::min)(1.0f, kVisibleHeight * l.aspect / a_appliedScale);
		const float h = kVisibleHeight / a_appliedScale;
		l.u0 = 0.5f - w * 0.5f;
		l.u1 = 0.5f + w * 0.5f;
		l.v0 = 0.5f - h * 0.5f;
		l.v1 = 0.5f + h * 0.5f;
		return l;
	}

	UVXform ComputeUV(const SwapImage& a_img, FitMode a_fit, const Layout& a_layout)
	{
		float fracU = 1.0f;  // fraction of the image width/height that ends up on screen
		float fracV = 1.0f;
		if (a_fit == FitMode::kCrop) {
			const float imageAspect = static_cast<float>(a_img.width) / static_cast<float>(a_img.height);
			if (imageAspect > a_layout.aspect) {
				fracU = a_layout.aspect / imageAspect;  // wider than the screen: crop left/right
			} else {
				fracV = imageAspect / a_layout.aspect;  // taller than the screen: crop top/bottom
			}
		}

		UVXform x;
		x.scale.x = fracU / (a_layout.u1 - a_layout.u0);
		x.scale.y = fracV / (a_layout.v1 - a_layout.v0);
		x.offset.x = (1.0f - fracU) * 0.5f - a_layout.u0 * x.scale.x;
		x.offset.y = (1.0f - fracV) * 0.5f - a_layout.v0 * x.scale.y;
		return x;
	}

	RE::BSShaderProperty* GetShaderProperty(RE::BSGeometry* a_geom)
	{
		for (auto& prop : a_geom->properties) {
			if (auto* sp = netimmerse_cast<RE::BSShaderProperty*>(prop.get())) {
				return sp;
			}
		}
		return nullptr;
	}

	RE::BSLightingShaderMaterialBase* GetLightingMaterial(RE::BSShaderProperty* a_prop)
	{
		if (!a_prop || !a_prop->material || a_prop->material->GetType() != RE::BSShaderMaterial::Type::kLighting) {
			return nullptr;
		}
		return static_cast<RE::BSLightingShaderMaterialBase*>(a_prop->material);
	}

	const char* GetDiffuse(RE::BSLightingShaderMaterialBase* a_mat)
	{
		return a_mat && a_mat->textureSet ? a_mat->textureSet->GetTextureFilename(TT::kBase) : nullptr;
	}

	// Grab shape from NIF
	RE::BSGeometry* FindQuad(RE::NiAVObject* a_obj, int a_depth = 0)
	{
		if (!a_obj || a_depth > 8) {
			return nullptr;
		}
		if (auto* geom = a_obj->IsGeometry()) {
			return GetLightingMaterial(GetShaderProperty(geom)) ? geom : nullptr;
		}
		if (auto* node = a_obj->IsNode()) {
			for (auto& child : node->children) {  // slots can be null
				if (auto* found = FindQuad(child.get(), a_depth + 1)) {
					return found;
				}
			}
		}
		return nullptr;
	}

	bool Retexture(RE::BSGeometry* a_geom, const SwapImage& a_img, const UVXform& a_uv)
	{
		auto* prop = GetShaderProperty(a_geom);
		auto* oldMat = GetLightingMaterial(prop);
		if (!oldMat) {
			spdlog::warn("TextureSwap: '{}' skipped: shaderProperty={} material={} type={}",
				a_geom->name.c_str(), static_cast<const void*>(prop),
				prop ? static_cast<const void*>(prop->material) : nullptr,
				prop && prop->material ? static_cast<int>(prop->material->GetType()) : -1);
			return false;
		}

		RE::NiPointer<RE::BSShaderTextureSet> set{ RE::BSShaderTextureSet::CreateObject() };
		if (!set) {
			spdlog::warn("TextureSwap: BSShaderTextureSet::CreateObject() returned null");
			return false;
		}
		set->SetTextureFilename(TT::kBase, a_img.texPath.c_str());

		RE::BSTSmartPointer<RE::BSShaderMaterial> newMat{ oldMat->Create() };
		if (!newMat) {
			spdlog::warn("TextureSwap: material Create() returned null");
			return false;
		}
		newMat->CopyMembers(oldMat);
		auto* newLit = static_cast<RE::BSLightingShaderMaterialBase*>(newMat.get());
		newLit->ClearTextures();
		newLit->OnLoadTextureSet(set.get());

		for (int i = 0; i < 2; ++i) {
			newLit->texCoordScale[i] = a_uv.scale;
			newLit->texCoordOffset[i] = a_uv.offset;
		}
		
		newLit->textureClampMode = static_cast<RE::BSGraphics::TextureAddressMode>(0);

		const auto refBefore = newMat->QRefCount();
		prop->SetMaterial(newMat.get(), true);

		const bool setupOk = prop->SetupGeometry(a_geom);

		g_menu.swappedProp = prop;
		g_menu.swappedMat = prop->material;  // whatever SetMaterial actually installed

		if (g_config.debugLog) {
			spdlog::info("TextureSwap: '{}' -> '{}' {}x{} uvScale=({:.4f},{:.4f}) uvOffset=({:.4f},{:.4f}) ref {}->{} samePtr={} setup={}",
				a_geom->name.c_str(), a_img.texPath, a_img.width, a_img.height,
				a_uv.scale.x, a_uv.scale.y, a_uv.offset.x, a_uv.offset.y,
				refBefore, newMat->QRefCount(), prop->material == newMat.get(), setupOk);
		}
		return true;
	}

	RE::NiAVObject* GetModelRoot(RE::LoadingMenu* a_menu)
	{
		auto* entry = static_cast<const std::byte*>(a_menu->foregroundModel);
		if (!entry) {
			return nullptr;
		}
		std::uint32_t state = 0;
		std::memcpy(&state, entry + 0x0C, sizeof(state));
		state &= 0x70000000u;
		if (state != 0x30000000u && state != 0x40000000u) {
			return nullptr;  // still loading
		}
		RE::NiAVObject* root = nullptr;
		std::memcpy(&root, entry + 0x20, sizeof(root));
		return root;
	}

	class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event.menuName == RE::LoadingMenu::MENU_NAME) {
				g_menu = MenuState{};
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

void LoadScreenSwap::Configure(Config a_config)
{
	g_config = std::move(a_config);
	g_dirty = true;

	static MenuSink sink;
	static bool     registered = false;
	if (!registered) {
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->RegisterSink<RE::MenuOpenCloseEvent>(&sink);
			registered = true;
		}
	}

	g_enabled = true;
	spdlog::info("TextureSwap: enabled (folder={}, fit={}, {} whitelisted screen(s))",
		g_config.folder, g_config.fit == FitMode::kCrop ? "crop" : "stretch", g_config.pins.size());
}

void LoadScreenSwap::ApplyScreenScale(RE::BGSTransform* a_transform)
{
	// Remember the record's authored scale (the 16:9 value, 0.16) the first time, so
	// kGameDataReady firing again starts from it rather than from a value set here.
	static std::unordered_map<RE::BGSTransform*, float> authoredScale;

	const Layout screen = ReadScreen();
	if (!a_transform) {
		g_appliedScale = 1.0f;
		spdlog::warn("TextureSwap: shared BGSTransform not found; scale left as is");
		return;
	}

	const auto [entry, inserted] = authoredScale.try_emplace(a_transform, a_transform->data.scale);
	const float authored = entry->second;
	const WideScale* wide = nullptr;
	for (const auto& candidate : kWideScales) {
		if (screen.aspect >= candidate.minAspect && screen.aspect <= candidate.maxAspect) {
			wide = &candidate;
			break;
		}
	}
	const float scale = wide ? wide->scale : authored;

	a_transform->data.scale = scale;
	g_appliedScale = authored > 0.0f ? scale / authored : 1.0f;  // used to fit the image to what's visible
	spdlog::info("TextureSwap: resolution {}x{}, aspect {:.3f} ({}): transform {:08X} scale {:.3f} (plugin value {:.3f})",
		screen.width, screen.height, screen.aspect, wide ? wide->name : "16:9 / other",
		a_transform->GetFormID(), scale, authored);
}

void LoadScreenSwap::ApplyLoadScreenLighting(const std::vector<std::pair<std::string, float>>& a_overrides)
{
	std::vector<std::pair<std::string, float>> values = {
		{ "fLoadingMenuSceneLight3DiffuseColorR", 1.0f },
		{ "fLoadingMenuSceneLight3DiffuseColorG", 1.0f },
		{ "fLoadingMenuSceneLight3DiffuseColorB", 1.0f },
		{ "fLoadingMenuSceneLight3DimmerValue", 2.13f },
	};
	for (const auto& [key, value] : a_overrides) {
		auto it = std::find_if(values.begin(), values.end(), [&k = key](const auto& a_entry) { return a_entry.first == k; });
		if (it != values.end()) {
			it->second = value;
		} else {
			values.emplace_back(key, value);
		}
	}

	for (const auto& [key, value] : values) {
		const std::string fullName = key + ":Interface";
		auto*             setting = RE::GetINISetting(fullName);
		if (!setting) {
			spdlog::warn("TextureSwap: game setting {} not found; skipped", fullName);
			continue;
		}
		if (setting->GetType() != RE::Setting::SETTING_TYPE::kFloat) {
			spdlog::warn("TextureSwap: game setting {} is not a float; skipped", fullName);
			continue;
		}
		const float previous = setting->GetFloat();
		setting->SetFloat(value);
		spdlog::info("TextureSwap: {} = {:.3f} (was {:.3f})", fullName, value, previous);
	}
}

void LoadScreenSwap::SetImagesPending(bool a_pending)
{
	g_pending = a_pending;
	if (!a_pending) {
		g_dirty = true;  // rescan on the next loading screen
	}
}

void LoadScreenSwap::BeforeAdvance(RE::LoadingMenu* a_menu)
{
	if (g_enabled && a_menu->artScreen) {
		g_requestedScreen = a_menu->artScreen;
		g_newRequest = true;
		if (g_config.debugLog) {
			spdlog::info("TextureSwap: engine requested load screen {:08X}", a_menu->artScreen->GetFormID());
		}
	}
}

void LoadScreenSwap::Tick(RE::LoadingMenu* a_menu)
{
	if (!g_enabled) {
		return;
	}

	++g_menu.frame;
	if (!g_pending && g_dirty.exchange(false)) {
		RescanPool();
	}

	auto* target = GetModelRoot(a_menu);

	//Debug stuff
	if (g_config.debugLog){
		if (g_menu.swappedProp && g_menu.frame == g_menu.swapFrame + 30 && target == g_menu.target) {
			auto*       mat = GetLightingMaterial(g_menu.swappedProp);
			const char* diffuse = GetDiffuse(mat);
			spdlog::info("TextureSwap: read-back 30 frames later: material {} (ours={}), diffuse='{}'",
				static_cast<const void*>(g_menu.swappedProp->material), g_menu.swappedProp->material == g_menu.swappedMat,
				diffuse ? diffuse : "<no texture set>");
		}
		if (g_menu.frame <= kProbeFrames) {
		spdlog::info("[TextureSwap probe] f={} modelRequested={} foregroundModel={} root={} zoomTarget={} artScreen={}",
			g_menu.frame, a_menu->modelRequested, a_menu->foregroundModel, static_cast<const void*>(target),
			static_cast<const void*>(a_menu->zoomTarget), static_cast<const void*>(a_menu->artScreen));
		}
	}

	

	if (!target) {
		return;
	}
	// Every load screen uses the same LS0.nif, so the model root can be the same
	// cached object from one screen to the next. Re-arm on a new model instance OR
	// on a new screen request, so the image always follows the screen being shown.
	if (target != g_menu.target || g_newRequest) {
		if (target != g_menu.target) {
			// A different model: the property saved for the read-back check belongs to the
			// old one, which the engine may have freed. Forget it so it's never read.
			g_menu.swappedProp = nullptr;
			g_menu.swappedMat = nullptr;
		}
		g_menu.target = target;
		g_menu.done = false;
		g_menu.armedFrame = g_menu.frame;
		g_newRequest = false;
	}
	if (g_menu.done) {
		return;
	}

	if (g_pending) {
		g_menu.done = true;
		spdlog::info("TextureSwap: images are still being generated; load screen left as is");
		return;
	}

	auto* geom = FindQuad(target);
	if (!geom) {
		// Normally found on the first try; allow a couple of seconds in case the
		// shape is attached under the root a few frames after the root appears.
		constexpr std::uint32_t kMaxWaitFrames = 120;
		if (g_menu.frame - g_menu.armedFrame < kMaxWaitFrames) {
			return;
		}
		g_menu.done = true;
		spdlog::warn("TextureSwap: no shape with a lit material under the load-screen model root '{}'; check LS0.nif",
			target->name.c_str());
		return;
	}
	g_menu.done = true;
	if (g_config.debugLog) {
		const char* diffuse = GetDiffuse(GetLightingMaterial(GetShaderProperty(geom)));
		spdlog::info("TextureSwap: quad '{}' diffuse='{}'", geom->name.c_str(), diffuse ? diffuse : "<no texture set>");
	}

	const SwapImage* img = PickImage(g_requestedScreen);
	if (!img) {
		spdlog::warn("TextureSwap: no images available in {}", g_config.folder);
		return;
	}

	const auto layout = GetScreenLayout(g_appliedScale);
	const auto uv = ComputeUV(*img, g_config.fit, layout);
	spdlog::info("TextureSwap: screen {}x{} (aspect {:.3f}), model scale x{:.3f}, visible u {:.4f}-{:.4f} v {:.4f}-{:.4f}",
		layout.width, layout.height, layout.aspect, layout.quadScale, layout.u0, layout.u1, layout.v0, layout.v1);
	if (Retexture(geom, *img, uv)) {
		g_menu.swapFrame = g_menu.frame;
		spdlog::info("TextureSwap: {} on '{}' (frame {}, pool {} image(s))", img->texPath, geom->name.c_str(), g_menu.frame, g_pool.size());
	}
}
