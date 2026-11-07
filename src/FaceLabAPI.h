#pragma once
// FaceLab API v5 - drop this header into your SKSE plugin. No link library needed: it finds FaceLab.dll at run time.
//
//   if (const auto* fl = FaceLabAPI::Get()) {
//       fl->ApplyPreset(actor->GetFormID(), "MyMod", "MyMod.Kissing.Pleased", 0.5f, 10);
//       ...
//       fl->Clear(actor->GetFormID(), "MyMod", 0.5f);
//   }
//
// Rules
//  * Every call is safe from any thread: the work is queued to the game thread. Each call returns 1 when it was accepted.
//  * "owner" is your mod's name. Your face is a layer owned by you; clearing it never touches another mod's face.
//  * Several mods on one actor: the layer with the highest priority wins (ties: the newest). Use 10 for normal use, 50+ for something that must win.
//    When the winning layer is cleared, the next one down shows. Nobody ever has to know about the others.
//  * A face is not saved. It is dropped on a new game / load, so a stuck face cannot survive a crash.
//  * Weights are 1.0 = the full RaceMenu slider, up to 3.0. Preset files hold the same numbers x 100.
//  * If FaceLab is not installed Get() returns nullptr: keep your old behaviour as the fallback.
//  * IsSupported must be called on the game thread. It is false for heads FaceLab has no morph data for (only the High Poly head today).
#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace FaceLabAPI
{
	struct Weight
	{
		const char* channel;  // "BigAah", "MoodSad", ... (ChannelName lists them all)
		float       value;    // 0 - 3.0
	};

	struct Interface
	{
		std::uint32_t version;  // 5 (new functions are only ever added at the end; a v1 caller keeps working)

		// A named face from any mod's FaceLab/Presets/*.toml. A missing "X.Context.Pleased" falls back to X.Context.Neutral, X.Pleased, X.Neutral.
		int (*ApplyPreset)(std::uint32_t formID, const char* owner, const char* preset, float fadeSeconds, int priority);
		// A face you build yourself.
		int (*ApplyWeights)(std::uint32_t formID, const char* owner, const Weight* weights, std::uint32_t count, float fadeSeconds, int priority);
		// Remove your layer (fadeSeconds < 0: keep the previous fade).
		int (*Clear)(std::uint32_t formID, const char* owner, float fadeSeconds);
		// 1 if the preset (after fallback) exists.
		int (*HasPreset)(const char* preset);
		// 1 / 0 / -1 (no such actor or not loaded). Game thread only.
		int (*IsSupported)(std::uint32_t formID);
		std::uint32_t (*ChannelCount)();
		const char* (*ChannelName)(std::uint32_t index);

		// ---- version 2 (check version >= 2 before using) ----
		// Speech / lip sync is driving the mouth: fade the face's Mouth-group channels out by `amount` (0..1, 1 = fully) over fadeSeconds, so the lip sync
		// is not fighting a held open mouth. Call again with amount 0 when the speech ends. Several askers (owner) are fine: the biggest amount counts.
		int (*SetMouthYield)(std::uint32_t formID, const char* owner, float amount, float fadeSeconds);

		// ---- version 3 (check version >= 3 before using) ----
		// How open the mouth is right now as FaceLab draws it: the biggest of the Aah / BigAah / Eh / Oh morph weights, smoothed, and already scaled down by any
		// speech yield (1.0 = a full RaceMenu weight). -1 = FaceLab has no face on this actor at the moment. Safe from any thread, cheap enough to call every frame.
		float (*GetMouthOpen)(std::uint32_t formID);

		// ---- version 4 (check version >= 4 before using) ----
		// How much the face FaceLab shows on this actor is cross-eyed (its EyesCross channel): 0 = not at all, 1 = from a weight of 0.1 up. It follows the face's fade.
		// Mods that steer the eyes themselves (The Fourth Wall) let go of them in proportion. -1 = FaceLab has no face on her.
		float (*GetEyeControl)(std::uint32_t formID);

		// ---- version 5 (check version >= 5 before using) ----
		// The TongueOut weight of the face FaceLab shows on this actor: 0 = no tongue wanted, 1.0 = fully out, above 1 = further. Live and faded with the face.
		// TongueLab obeys it (and has OStim equip a tongue if she has none). -1 = FaceLab has no face on her.
		float (*GetTongue)(std::uint32_t formID);
	};

	inline const Interface* Get()
	{
		using Fn = const Interface* (*)();
		static const Interface* api = nullptr;
		if (!api) {  // a miss is not cached: plugins load in any order, so ask again after kDataLoaded
			if (const auto m = ::GetModuleHandleW(L"FaceLab.dll")) {
				if (const auto fn = reinterpret_cast<Fn>(::GetProcAddress(m, "FaceLab_GetInterfaceV1"))) {
					api = fn();
				}
			}
		}
		return api;
	}
}
