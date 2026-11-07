#include "PCH.h"

#include "Npc.h"

#include "NpcCore.h"

#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace Npc
{
	namespace
	{
		struct Entry
		{
			std::filesystem::file_time_type stamp{};
			bool                            seen{ false };  // read at least once (a broken file is not read again until it changes)
			bool                            ok{ false };
			NpcCore::Override               ov;
		};

		std::mutex                                g_mx;
		std::unordered_map<std::string, Entry>    g_files;  // lower-case file name without .toml -> content
		std::chrono::steady_clock::time_point     g_next{};

		std::string Lower(std::string s)
		{
			for (auto& c : s) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return s;
		}

		bool ReadText(const std::filesystem::path& p, std::string& out)
		{
			std::ifstream f(p, std::ios::binary);
			if (!f) {
				return false;
			}
			std::ostringstream ss;
			ss << f.rdbuf();
			out = ss.str();
			return true;
		}

		void Scan(bool a_first)
		{
			std::error_code                                                                      ec;
			std::unordered_map<std::string, std::pair<std::filesystem::path, std::filesystem::file_time_type>> now;
			for (std::filesystem::directory_iterator it(Folder(), ec), end; !ec && it != end; it.increment(ec)) {
				std::error_code e2;
				if (!it->is_regular_file(e2) || Lower(it->path().extension().string()) != ".toml") {
					continue;
				}
				now[Lower(it->path().stem().string())] = { it->path(), it->last_write_time(e2) };
			}
			std::lock_guard lk(g_mx);
			for (auto it = g_files.begin(); it != g_files.end();) {
				if (!now.contains(it->first)) {
					logger::info("NPC settings: {}.toml was removed", it->first);
					it = g_files.erase(it);
				} else {
					++it;
				}
			}
			for (auto& [key, pt] : now) {
				auto& e = g_files[key];
				if (e.seen && e.stamp == pt.second) {
					continue;
				}
				e.seen = true;
				e.stamp = pt.second;
				std::string text;
				if (!ReadText(pt.first, text)) {
					e.ok = false;
					logger::warn("NPC settings: could not read {}", pt.first.string());
					continue;
				}
				std::vector<std::string> problems;
				e.ok = NpcCore::Parse(text, e.ov, problems);
				if (!e.ok) {
					logger::warn("NPC settings: {} is not read: {}", pt.first.filename().string(), problems.empty() ? "unknown error" : problems.front());
					continue;
				}
				logger::info("NPC settings: {} {}: {} setting(s){}{}", a_first ? "found" : "(re)loaded", pt.first.filename().string(), e.ov.Count(), e.ov.name.empty() ? "" : " for ", e.ov.name);
				for (std::size_t i = 0; i < problems.size() && i < 6; ++i) {
					logger::warn("NPC settings: {}: {}", pt.first.filename().string(), problems[i]);
				}
			}
		}
	}

	std::filesystem::path Folder()
	{
		return std::filesystem::path("Data/SKSE/Plugins/TongueLab");
	}

	std::string KeyOf(const RE::TESNPC* base)
	{
		if (!base) {
			return {};
		}
		const auto* file = base->GetFile(0);
		if (!file) {
			return {};
		}
		return NpcCore::MakeKey(std::string(file->GetFilename()), base->GetLocalFormID());
	}

	void Init()
	{
		Scan(true);
		std::lock_guard lk(g_mx);
		int npcs = 0;
		for (auto& [k, e] : g_files) {
			if (k != "_default" && k != "_generic") {
				++npcs;
			}
		}
		logger::info("NPC settings folder {}: {} file(s) ({}_default, {}_generic, {} NPC file(s))", Folder().string(), g_files.size(), g_files.contains("_default") ? "" : "no ",
			g_files.contains("_generic") ? "" : "no ", npcs);
	}

	void Poll(std::chrono::steady_clock::time_point now)
	{
		if (now < g_next) {
			return;
		}
		g_next = now + std::chrono::milliseconds(2500);
		Scan(false);
	}

	Config::Settings Effective(RE::Actor* a, const Config::Settings& global, Info* info)
	{
		Config::Settings s = global;
		Info             local;
		Info&            in = info ? *info : local;
		in = Info{};
		const RE::TESNPC* base = a ? a->GetActorBase() : nullptr;
		const std::string key = KeyOf(base);
		if (!key.empty()) {
			in.file = key + ".toml";
		}
		std::lock_guard lk(g_mx);
		if (g_files.empty()) {
			return s;
		}
		auto layer = [&](const std::string& name, bool& flag) {
			auto it = g_files.find(name);
			if (it != g_files.end() && it->second.ok) {
				NpcCore::Apply(it->second.ov, s);
				in.values += it->second.ov.Count();
				flag = true;
			}
		};
		layer("_default", in.def);
		if (base && !base->IsUnique()) {
			layer("_generic", in.generic);
		}
		if (!key.empty()) {
			layer(Lower(key), in.own);
		}
		return s;
	}
}
