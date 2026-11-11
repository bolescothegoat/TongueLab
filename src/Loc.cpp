#include "PCH.h"

#include "Loc.h"

#include <toml++/toml.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>

namespace fs = std::filesystem;

namespace Loc
{
	namespace
	{
		using Map = std::unordered_map<std::string, std::string>;

		struct State
		{
			Map         tr;      // English -> translation
			Map         labels;  // English -> "translation###id" (built on demand)
			std::string name{ "english" };
			std::set<std::string> fonts;  // font codes (RU CN JA KO PL TH) the letters of this language need in SKSE Menu Framework; empty = the default Latin font does
		};

		std::mutex                          g_mx;
		std::shared_ptr<State>              g_state = std::make_shared<State>();
		std::vector<std::shared_ptr<State>> g_retired;  // never freed: a pointer handed out earlier in the same frame must stay valid
		std::string                         g_dir;
		bool                                g_logMissing = false;
		std::set<std::string>               g_missingLogged;

		std::string Lower(std::string s)
		{
			std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		// the placeholders of a text, in order: "%s", "%.2f", "%+.0f", "%%" is not one, and "{}"
		std::vector<std::string> Placeholders(const std::string& s)
		{
			std::vector<std::string> out;
			for (std::size_t i = 0; i < s.size(); ++i) {
				if (s[i] == '%') {
					if (i + 1 < s.size() && s[i + 1] == '%') {
						++i;
						continue;
					}
					std::size_t j = i + 1;
					while (j < s.size() && !std::isalpha(static_cast<unsigned char>(s[j]))) {
						++j;
					}
					out.push_back(s.substr(i, j - i + 1));
					i = j;
				} else if (s[i] == '{' && i + 1 < s.size() && s[i + 1] == '}') {
					out.push_back("{}");
					++i;
				}
			}
			return out;
		}

		// which font a language needs, from the letters its translations use (SKSE Menu Framework offers one font per script: EN, PL, RU, CN, JA)
		std::set<std::string> FontsNeeded(const Map& tr)
		{
			bool cyr = false, han = false, kana = false, hangul = false, thai = false, ext = false;
			for (const auto& kv : tr) {
				const std::string& t = kv.second;
				for (std::size_t i = 0; i < t.size();) {
					const unsigned char c = static_cast<unsigned char>(t[i]);
					std::uint32_t       cp = c;
					int                 len = 1;
					if (c >= 0xF0) {
						len = 4;
						cp = c & 0x07;
					} else if (c >= 0xE0) {
						len = 3;
						cp = c & 0x0F;
					} else if (c >= 0xC0) {
						len = 2;
						cp = c & 0x1F;
					}
					for (int k = 1; k < len && i + k < t.size(); ++k) {
						cp = (cp << 6) | (static_cast<unsigned char>(t[i + k]) & 0x3F);
					}
					i += len;
					if (cp >= 0x400 && cp <= 0x52F) {
						cyr = true;
					} else if ((cp >= 0x3040 && cp <= 0x30FF)) {
						kana = true;
					} else if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF)) {
						han = true;
					} else if ((cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x1100 && cp <= 0x11FF)) {
						hangul = true;
					} else if (cp >= 0x0E00 && cp <= 0x0E7F) {
						thai = true;
					} else if (cp >= 0x100 && cp <= 0x17F) {
						ext = true;
					}
				}
			}
			std::set<std::string> out;
			if (cyr) out.insert("RU");
			if (kana) out.insert("JA");
			else if (han) out.insert("CN");
			if (hangul) out.insert("KO");
			if (thai) out.insert("TH");
			if (ext) out.insert("PL");
			return out;
		}

		// true when the text has no letters outside its placeholders: there is nothing a translator could change
		bool NoLetters(const char* t)
		{
			for (; *t; ++t) {
				if (*t == '%') {
					++t;
					while (*t && !std::isalpha(static_cast<unsigned char>(*t)) && *t != '%') {
						++t;
					}
					if (!*t) {
						break;
					}
				} else if (*t == '{' && t[1] == '}') {
					++t;
				} else if (std::isalpha(static_cast<unsigned char>(*t))) {
					return false;
				}
			}
			return true;
		}

		std::string GameLanguage()
		{
			if (auto* st = RE::GetINISetting("sLanguage:General"); st && st->GetType() == RE::Setting::Type::kString && st->GetString()) {
				return Lower(st->GetString());
			}
			return "english";
		}

		fs::path FileFor(const std::string& name)
		{
			std::error_code ec;
			for (const auto& e : fs::directory_iterator(g_dir, ec)) {
				if (e.is_regular_file() && Lower(e.path().extension().string()) == ".toml" && Lower(e.path().stem().string()) == Lower(name)) {
					return e.path();
				}
			}
			return {};
		}

		bool Load(const std::string& want, std::shared_ptr<State>& out)
		{
			out = std::make_shared<State>();
			const std::string name = Lower(want);
			if (name.empty() || name == "english") {
				return true;
			}
			const auto file = FileFor(name);
			if (file.empty()) {
				logger::warn("Language '{}': no file {}/{}.toml - using English", want, g_dir, want);
				return false;
			}
			toml::table tbl;
			try {
				tbl = toml::parse_file(file.string());
			} catch (const toml::parse_error& e) {
				logger::warn("Language file {} not read ({}) - using English", file.string(), e.description());
				return false;
			}
			int used = 0, empty = 0, bad = 0;
			if (const auto* strings = tbl["strings"].as_table()) {
				for (const auto& [k, v] : *strings) {
					const auto* sv = v.as_string();
					if (!sv) {
						continue;
					}
					const std::string key(k.str());
					const std::string val = sv->get();
					if (val.empty()) {
						++empty;
						continue;
					}
					if (Placeholders(key) != Placeholders(val)) {
						logger::warn("Language {}: ignored a translation whose placeholders (%s %d {{}} ...) differ from the English: '{}'", name, key);
						++bad;
						continue;
					}
					out->tr[key] = val;
					++used;
				}
			} else {
				logger::warn("Language file {} has no [strings] table - using English", file.string());
				return false;
			}
			out->name = name;
			out->fonts = FontsNeeded(out->tr);
			if (!out->fonts.empty()) {
				std::string codes;
				for (const auto& c : out->fonts) {
					codes += (codes.empty() ? "" : " ") + c;
				}
				logger::info("Language {}: its letters need the SKSE Menu Framework font(s): {} (Settings > Font, or PrimaryFont in SKSEMenuFramework.ini)", name, codes);
			}
			logger::info("Language: {} ({} string(s) translated{}{}) from {}", name, used, empty ? std::format(", {} left empty", empty) : "", bad ? std::format(", {} ignored (placeholders)", bad) : "", file.string());
			return true;
		}

		std::shared_ptr<State> Cur()
		{
			std::lock_guard lk(g_mx);
			return g_state;
		}
	}

	void Init(const std::string& folder, const std::string& setting)
	{
		g_dir = folder;
		Set(setting);
	}

	bool Set(const std::string& setting)
	{
		std::string want = setting.empty() ? "auto" : setting;
		if (Lower(want) == "auto") {
			want = GameLanguage();
		}
		std::shared_ptr<State> next;
		const bool             ok = Load(want, next);
		std::lock_guard        lk(g_mx);
		g_retired.push_back(g_state);
		g_state = std::move(next);
		g_missingLogged.clear();
		return ok;
	}

	const char* T(const char* english)
	{
		if (!english || !*english || NoLetters(english)) {
			return english;  // nothing to translate: empty, or only placeholders / punctuation ("%s", "%-22s")
		}
		const auto st = Cur();
		if (st->tr.empty()) {
			return english;
		}
		if (const auto it = st->tr.find(english); it != st->tr.end()) {
			return it->second.c_str();
		}
		if (g_logMissing) {
			std::lock_guard lk(g_mx);
			if (g_missingLogged.insert(english).second) {
				logger::info("Language {}: no translation for '{}'", st->name, english);
			}
		}
		return english;
	}

	const char* L(const char* english)
	{
		if (!english || !*english) {
			return english;
		}
		const auto st = Cur();
		if (st->tr.empty()) {
			return english;
		}
		std::lock_guard lk(g_mx);
		if (const auto it = st->labels.find(english); it != st->labels.end()) {
			return it->second.c_str();
		}
		const std::string eng(english);
		const auto        id = eng.find("##");
		const std::string left = id == std::string::npos ? eng : eng.substr(0, id);
		const auto        tr = st->tr.find(left);
		if (tr == st->tr.end()) {
			if (g_logMissing && g_missingLogged.insert(left).second) {
				logger::info("Language {}: no translation for '{}'", st->name, left);
			}
			return st->labels.emplace(eng, eng).first->second.c_str();
		}
		// translated text, then the id: what was after the English's own "##" (an explicit id), or the English text itself ("###" = the id is the rest)
		const std::string label = tr->second + (id == std::string::npos ? "###" + eng : eng.substr(id));
		return st->labels.emplace(eng, label).first->second.c_str();
	}

	std::vector<std::string> Available()
	{
		std::set<std::string> names{ "english" };
		std::error_code       ec;
		for (const auto& e : fs::directory_iterator(g_dir, ec)) {
			if (e.is_regular_file() && Lower(e.path().extension().string()) == ".toml") {
				const auto stem = Lower(e.path().stem().string());
				if (!stem.empty() && stem[0] != '_') {
					names.insert(stem);
				}
			}
		}
		return { names.begin(), names.end() };
	}

	std::string FontAdvice()
	{
		const auto st = Cur();
		if (st->fonts.empty()) {
			return {};
		}
		// SKSE Menu Framework keeps its choice in its own ini
		static std::string                           cached, fontNow;
		static std::chrono::steady_clock::time_point last{};
		static bool                                  haveIni = false;
		const auto                                   now = std::chrono::steady_clock::now();
		std::lock_guard                              lk(g_mx);
		if (now - last > std::chrono::seconds(3)) {
			last = now;
			haveIni = false;
			std::ifstream f("Data/SKSE/Plugins/SKSEMenuFramework.ini");
			for (std::string line; std::getline(f, line);) {
				const auto eq = line.find('=');
				if (eq != std::string::npos && Lower(line.substr(0, 11)) == "primaryfont") {
					fontNow = line.substr(eq + 1);
					const char* ws = " \t\r\n";
					fontNow.erase(0, fontNow.find_first_not_of(ws));
					fontNow.erase(fontNow.find_last_not_of(ws) + 1);
					haveIni = true;
				}
			}
		}
		if (!haveIni) {
			return {};  // cannot tell: say nothing
		}
		for (const auto& code : st->fonts) {
			if (fontNow.find("(" + code + ")") == std::string::npos) {
				return "Futura Condensed (" + code + ")";
			}
		}
		return {};
	}

	std::string Current()
	{
		return Cur()->name;
	}

	int Count()
	{
		return static_cast<int>(Cur()->tr.size());
	}

	void SetLogMissing(bool on)
	{
		g_logMissing = on;
	}
}
