#pragma once

#include "Config.h"

#include <chrono>
#include <filesystem>
#include <string>

// Per-NPC tongue settings (the game side). Files live in Data/SKSE/Plugins/TongueLab/ :
//     _default.toml                     everyone
//     _generic.toml                     NPCs that are not unique (guards, bandits, ...): all of them share it
//     <Plugin>_<LocalFormID>.toml       one NPC, e.g. Skyrim.esm_01A66E.toml  (the NPC's base form; the player is Skyrim.esm_000007)
// Layers, lowest first: TongueLab.toml < _default < _generic (if not unique) < the NPC's own file. A file only overrides the keys it holds.
// Files are picked up while the game runs (checked every few seconds).

namespace Npc
{
	struct Info
	{
		bool        def{ false };      // _default.toml applied
		bool        generic{ false };  // _generic.toml applied
		bool        own{ false };      // this NPC's own file applied
		int         values{ 0 };       // how many settings the layers changed (counted per layer)
		std::string file;              // name of this NPC's own file (whether it exists or not), for the menu
		[[nodiscard]] bool Any() const { return def || generic || own; }
	};

	void Init();                                                   // kDataLoaded: first scan of the folder
	void Poll(std::chrono::steady_clock::time_point now);          // call often; rescans the folder every few seconds
	// the settings that apply to this actor: `global` (TongueLab.toml) with the per-NPC layers over it
	Config::Settings Effective(RE::Actor* a, const Config::Settings& global, Info* info = nullptr);

	[[nodiscard]] std::filesystem::path Folder();
	[[nodiscard]] std::string           KeyOf(const RE::TESNPC* base);   // "Skyrim.esm_01A66E", empty for a form that has no plugin (created at run time)
}
