#pragma once

// Per-NPC tongue settings: the part that knows nothing about the game (so it can be tested on its own).
//
// A settings file is TOML with the same section and key names as TongueLab.toml. Only the keys that are present override anything.
// Layers, lowest first:  TongueLab.toml  <  _default.toml  <  _generic.toml (NPCs that are not unique)  <  <Plugin>_<LocalFormID>.toml (that NPC)

#include "Config.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace NpcCore
{
	struct Override
	{
		std::string                        name;  // optional readable name written in the file (documentation only)
		std::vector<std::pair<int, float>> f;     // index into the float key table -> value
		std::vector<std::pair<int, int>>   i;
		std::vector<std::pair<int, bool>>  b;

		[[nodiscard]] int Count() const { return static_cast<int>(f.size() + i.size() + b.size()); }
	};

	// Reads the text of one file. Returns false only when the file is not valid TOML (nothing is returned then).
	// `problems` gets one line for everything that was ignored: unknown keys, values of the wrong type.
	bool Parse(const std::string& text, Override& out, std::vector<std::string>& problems);

	// Puts the values of o over s.
	void Apply(const Override& o, Config::Settings& s);

	// What differs between `base` and `edited` among the settings a file can hold (for "save this NPC").
	Override Diff(const Config::Settings& base, const Config::Settings& edited);

	// Text of a settings file (with comments) for o. `name` is the NPC's readable name.
	std::string Serialize(const Override& o, const std::string& name);

	// "Skyrim.esm_01A66E": the plugin that defines the NPC and its id inside that plugin (stays valid when the load order changes).
	std::string MakeKey(const std::string& plugin, std::uint32_t localId);

		// Every key a file can hold, with the value s has now (used to write the example files)
	Override All(const Config::Settings& s);

	// How many keys a file can hold (for the README / tests)
	int KeyCount();
}
