#include "NpcCore.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>

namespace NpcCore
{
	namespace
	{
		using S = Config::Settings;

		struct FloatKey
		{
			const char* section;
			const char* key;
			float S::*  mem;
			float       lo, hi;
			const char* help;
		};
		struct IntKey
		{
			const char* section;
			const char* key;
			int S::*    mem;
			int         lo, hi;
			const char* help;
		};
		struct BoolKey
		{
			const char* section;
			const char* key;
			bool S::*   mem;
			const char* help;
		};

		// names and ranges follow TongueLab.toml / Config.cpp
		const FloatKey kFloats[] = {
			{ "Mouth", "OpenThreshold", &S::openThreshold, 0.0f, 1.5f, "the mouth must be this open (0-1) before the tongue comes out" },
			{ "Mouth", "CloseThreshold", &S::closeThreshold, 0.0f, 1.5f, "the tongue goes back in when the mouth falls below this" },
			{ "Mouth", "FaceLabOpen", &S::faceOpen, 0.0f, 2.0f, "FaceLab mouth weight the tongue waits for before it comes out" },
			{ "Mouth", "FaceLabClose", &S::faceClose, 0.0f, 2.0f, "FaceLab mouth weight below which the tongue goes in" },
			{ "Mouth", "ShowDelay", &S::showDelay, 0.0f, 5.0f, "seconds the mouth must stay open first" },
			{ "Mouth", "HideDelay", &S::hideDelay, 0.0f, 5.0f, "seconds below the close threshold before the tongue goes in" },
			{ "Slide", "OutSeconds", &S::slideOut, 0.02f, 5.0f, "seconds to slide out" },
			{ "Slide", "InSeconds", &S::slideIn, 0.02f, 5.0f, "seconds to slide back in" },
			{ "Slide", "Distance", &S::slideDistance, -20.0f, 40.0f, "how far back into the mouth it goes" },
			{ "Slide", "ExtendedX", &S::extendX, -30.0f, 30.0f, "where the tongue sits when fully out: sideways" },
			{ "Slide", "ExtendedY", &S::extendY, -30.0f, 30.0f, "... forward (+) / back (-)" },
			{ "Slide", "ExtendedZ", &S::extendZ, -30.0f, 30.0f, "... up (+) / down (-)" },
			{ "Slide", "AskX", &S::askX, -30.0f, 30.0f, "for a tongue a FaceLab face asks for: added to the place above, sideways" },
			{ "Slide", "AskY", &S::askY, -30.0f, 30.0f, "... forward (+) / back (-)" },
			{ "Slide", "AskZ", &S::askZ, -30.0f, 30.0f, "... up (+) / down (-)" },
			{ "Slide", "AskOver", &S::askOver, 0.0f, 30.0f, "units further out per 1.0 of TongueOut above 1.0" },
			{ "Slide", "OffsetX", &S::slideX, -30.0f, 30.0f, "extra sideways offset while retracted" },
			{ "Slide", "OffsetZ", &S::slideZ, -30.0f, 30.0f, "extra up/down offset while retracted" },
			{ "Visual", "Size", &S::visSize, -60.0f, 100.0f, "all directions together, % change" },
			{ "Visual", "Length", &S::visLength, -60.0f, 150.0f, "along the tongue, % change" },
			{ "Visual", "Width", &S::visWidth, -60.0f, 100.0f, "side to side, % change" },
			{ "Visual", "Thickness", &S::visThickness, -60.0f, 100.0f, "up and down, % change" },
			{ "Visual", "Tilt", &S::visTilt, -45.0f, 45.0f, "tip up (+) / down (-), degrees" },
		};
		const IntKey kInts[] = {
			{ "Tongue", "Model", &S::tongueModel, 0, 99, "which tongue is put on when a face asks for one and there is no scene (index into the tongue list)" },
			{ "Slide", "Ease", &S::slideEase, 0, 3, "0 linear, 1 smooth, 2 starts fast ends slow, 3 starts slow ends fast" },
		};
		const BoolKey kBools[] = {
			{ "Mouth", "FollowFaceLab", &S::faceFollow, "when FaceLab draws the face, the tongue follows the mouth FaceLab draws" },
			{ "Slide", "Enabled", &S::slide, "slide the tongue out of / into the mouth (false = it pops)" },
		};
		constexpr int NF = static_cast<int>(sizeof(kFloats) / sizeof(kFloats[0]));
		constexpr int NI = static_cast<int>(sizeof(kInts) / sizeof(kInts[0]));
		constexpr int NB = static_cast<int>(sizeof(kBools) / sizeof(kBools[0]));

		std::string Num(float v)
		{
			return std::format("{}", v);
		}
	}

	int KeyCount() { return NF + NI + NB; }

	bool Parse(const std::string& text, Override& out, std::vector<std::string>& problems)
	{
		out = Override{};
		toml::table tbl;
		try {
			tbl = toml::parse(text);
		} catch (const toml::parse_error& e) {
			problems.push_back(std::string("not valid TOML: ") + std::string(e.description()));
			return false;
		}
		if (auto n = tbl["name"].value<std::string>()) {
			out.name = *n;
		}
		std::set<std::string> known;
		auto                  mark = [&](const char* sec, const char* key) { known.insert(std::string(sec) + "." + key); };

		for (int k = 0; k < NF; ++k) {
			const auto& d = kFloats[k];
			mark(d.section, d.key);
			const auto* node = tbl[d.section][d.key].node();
			if (!node) {
				continue;
			}
			if (auto v = node->value<double>()) {
				const float x = std::clamp(static_cast<float>(*v), d.lo, d.hi);
				if (std::isfinite(x)) {
					out.f.push_back({ k, x });
				}
			} else {
				problems.push_back(std::string("[") + d.section + "] " + d.key + ": expected a number - ignored");
			}
		}
		for (int k = 0; k < NI; ++k) {
			const auto& d = kInts[k];
			mark(d.section, d.key);
			const auto* node = tbl[d.section][d.key].node();
			if (!node) {
				continue;
			}
			if (auto v = node->value<std::int64_t>()) {
				out.i.push_back({ k, static_cast<int>(std::clamp<std::int64_t>(*v, d.lo, d.hi)) });
			} else {
				problems.push_back(std::string("[") + d.section + "] " + d.key + ": expected a whole number - ignored");
			}
		}
		for (int k = 0; k < NB; ++k) {
			const auto& d = kBools[k];
			mark(d.section, d.key);
			const auto* node = tbl[d.section][d.key].node();
			if (!node) {
				continue;
			}
			if (const auto* bn = node->as_boolean()) {   // strict: toml++ would turn the number 3 into true
				out.b.push_back({ k, bn->get() });
			} else {
				problems.push_back(std::string("[") + d.section + "] " + d.key + ": expected true or false - ignored");
			}
		}
		// anything else in the file
		for (auto&& [secName, secNode] : tbl) {
			const std::string sec(secName.str());
			if (sec == "name" || sec == "note") {
				continue;
			}
			if (const auto* t = secNode.as_table()) {
				for (auto&& [keyName, keyNode] : *t) {
					(void)keyNode;
					if (!known.contains(sec + "." + std::string(keyName.str()))) {
						problems.push_back("[" + sec + "] " + std::string(keyName.str()) + ": not a setting a per-NPC file can hold - ignored");
					}
				}
			} else {
				problems.push_back(sec + ": expected a [section] - ignored");
			}
		}
		return true;
	}

		Override All(const Config::Settings& s)
	{
		Override o;
		for (int k = 0; k < NF; ++k) {
			o.f.push_back({ k, s.*(kFloats[k].mem) });
		}
		for (int k = 0; k < NI; ++k) {
			o.i.push_back({ k, s.*(kInts[k].mem) });
		}
		for (int k = 0; k < NB; ++k) {
			o.b.push_back({ k, s.*(kBools[k].mem) });
		}
		return o;
	}

	void Apply(const Override& o, Config::Settings& s)
	{
		for (auto& [k, v] : o.f) {
			s.*(kFloats[k].mem) = v;
		}
		for (auto& [k, v] : o.i) {
			s.*(kInts[k].mem) = v;
		}
		for (auto& [k, v] : o.b) {
			s.*(kBools[k].mem) = v;
		}
	}

	Override Diff(const Config::Settings& base, const Config::Settings& edited)
	{
		Override o;
		for (int k = 0; k < NF; ++k) {
			if (std::fabs(base.*(kFloats[k].mem) - edited.*(kFloats[k].mem)) > 1e-6f) {
				o.f.push_back({ k, edited.*(kFloats[k].mem) });
			}
		}
		for (int k = 0; k < NI; ++k) {
			if (base.*(kInts[k].mem) != edited.*(kInts[k].mem)) {
				o.i.push_back({ k, edited.*(kInts[k].mem) });
			}
		}
		for (int k = 0; k < NB; ++k) {
			if (base.*(kBools[k].mem) != edited.*(kBools[k].mem)) {
				o.b.push_back({ k, edited.*(kBools[k].mem) });
			}
		}
		return o;
	}

	std::string Serialize(const Override& o, const std::string& name)
	{
		std::string out;
		out += "# TongueLab - tongue settings for one NPC (or _default / _generic). Only the lines kept here override the global settings;\r\n";
		out += "# delete a line to go back to the default. Same names as TongueLab.toml. Saved by the in-game menu, safe to edit by hand.\r\n";
		if (!name.empty()) {
			std::string n;
			for (char c : name) {
				n += (c == '"' || c == '\\') ? '_' : c;
			}
			out += "name = \"" + n + "\"\r\n";
		}
		const char* sections[] = { "Tongue", "Mouth", "Slide", "Visual" };
		for (const char* sec : sections) {
			std::string body;
			for (auto& [k, v] : o.i) {
				if (std::string(kInts[k].section) == sec) {
					body += std::format("{} = {}   # {}\r\n", kInts[k].key, v, kInts[k].help);
				}
			}
			for (auto& [k, v] : o.b) {
				if (std::string(kBools[k].section) == sec) {
					body += std::format("{} = {}   # {}\r\n", kBools[k].key, v ? "true" : "false", kBools[k].help);
				}
			}
			for (auto& [k, v] : o.f) {
				if (std::string(kFloats[k].section) == sec) {
					body += std::format("{} = {}   # {}\r\n", kFloats[k].key, Num(v), kFloats[k].help);
				}
			}
			if (!body.empty()) {
				out += std::string("\r\n[") + sec + "]\r\n" + body;
			}
		}
		return out;
	}

	std::string MakeKey(const std::string& plugin, std::uint32_t localId)
	{
		std::string p;
		for (char c : plugin) {
			p += (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') ? '_' : c;
		}
		return std::format("{}_{:06X}", p, localId & 0xFFFFFFu);
	}
}
