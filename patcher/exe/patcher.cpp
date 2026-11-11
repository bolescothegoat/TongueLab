// TongueLab Slide Bone Patcher - a double-click tool, no installs needed.
//
// Makes slide-capable copies of Halo's tongues (linga1..10.nif) for TongueLab.
//   - Your own files are never changed: the copies go into a NEW mod folder.
//   - A file is only patched when its SHA-256 is exactly one this tool knows. Anything else is reported and skipped.
//   - The built-in table is a list of changed bytes (about 59 per tongue); no part of the original meshes is in this program.
//
// Command line (optional):   TongueLabSlideBonePatcher.exe [folder to search] [--out <new mod folder>] [--use <text>] [--dry-run] [--list]

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "patch_data.h"

namespace fs = std::filesystem;

// ---- output (console if there is one, UTF-8 bytes if redirected) -------------------------------------------------------
static HANDLE g_out = nullptr;
static bool   g_console = false;

static std::string ToUtf8(const std::wstring& w)
{
	if (w.empty()) return {};
	int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string s(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
	return s;
}
static std::wstring FromUtf8(const char* c)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, c, -1, nullptr, 0);
	std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
	if (n > 1) MultiByteToWideChar(CP_UTF8, 0, c, -1, w.data(), n);
	return w;
}
static void Out(const std::wstring& s)
{
	DWORD wr = 0;
	if (g_console) {
		WriteConsoleW(g_out, s.c_str(), static_cast<DWORD>(s.size()), &wr, nullptr);
	} else {
		auto u = ToUtf8(s);
		WriteFile(g_out, u.data(), static_cast<DWORD>(u.size()), &wr, nullptr);
	}
}
static void OutLine(const std::wstring& s = L"") { Out(s + L"\r\n"); }

// ---- SHA-256 (Windows CNG) ----------------------------------------------------------------------------------------------
static bool Sha256(const std::vector<std::uint8_t>& d, std::uint8_t out[32])
{
	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_HASH_HANDLE h = nullptr;
	bool ok = false;
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
		if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
			if (BCryptHashData(h, const_cast<PUCHAR>(d.data()), static_cast<ULONG>(d.size()), 0) == 0 && BCryptFinishHash(h, out, 32, 0) == 0) ok = true;
			BCryptDestroyHash(h);
		}
		BCryptCloseAlgorithmProvider(alg, 0);
	}
	return ok;
}
static bool ReadAll(const fs::path& p, std::vector<std::uint8_t>& d)
{
	std::ifstream f(p, std::ios::binary);
	if (!f) return false;
	f.seekg(0, std::ios::end);
	auto n = f.tellg();
	f.seekg(0);
	d.resize(static_cast<size_t>(n));
	f.read(reinterpret_cast<char*>(d.data()), n);
	return static_cast<bool>(f);
}
static bool Same32(const std::uint8_t* a, const std::uint8_t* b) { return std::equal(a, a + 32, b); }

static std::wstring Lower(std::wstring s)
{
	for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
	return s;
}

// ---- folder picker -------------------------------------------------------------------------------------------------------
static bool PickFolder(std::wstring& result)
{
	bool ok = false;
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	IFileDialog* dlg = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
		DWORD opts = 0;
		dlg->GetOptions(&opts);
		dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
		dlg->SetTitle(L"Select the folder to search for your tongue mods (your Mod Organizer 2 \"mods\" folder, or your Skyrim Data folder)");
		dlg->SetOkButtonLabel(L"Search this folder");
		if (SUCCEEDED(dlg->Show(nullptr))) {
			IShellItem* item = nullptr;
			if (SUCCEEDED(dlg->GetResult(&item))) {
				PWSTR path = nullptr;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
					result = path;
					CoTaskMemFree(path);
					ok = true;
				}
				item->Release();
			}
		}
		dlg->Release();
	}
	if (SUCCEEDED(hr)) CoUninitialize();
	return ok;
}

static bool LooksLikeModsFolder(const fs::path& root)
{
	int n = 0;
	std::error_code ec;
	for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
		if (it->is_directory(ec) && fs::exists(it->path() / L"meta.ini", ec) && ++n >= 3) return true;
	}
	return false;
}

static void WaitKey()
{
	OutLine();
	Out(L"Press Enter to close this window.");
	if (g_console) {
		HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
		wchar_t buf[8];
		DWORD rd = 0;
		ReadConsoleW(in, buf, 1, &rd, nullptr);
	}
}

int wmain(int argc, wchar_t** argv)
{
	g_out = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD mode = 0;
	g_console = GetConsoleMode(g_out, &mode) != 0;
	const bool interactive = (argc == 1);

	std::wstring rootArg, outArg, useArg;
	bool dry = false, list = false;
	for (int i = 1; i < argc; ++i) {
		std::wstring a = argv[i];
		if (a == L"--dry-run") dry = true;
		else if (a == L"--list") list = true;
		else if (a == L"--out" && i + 1 < argc) outArg = argv[++i];
		else if (a == L"--use" && i + 1 < argc) useArg = argv[++i];
		else if (rootArg.empty() && a.rfind(L"--", 0) != 0) rootArg = a;
	}

	OutLine(L"TongueLab - Slide Bone Patcher");
	OutLine(L"==============================");
	OutLine();
	if (list) {
		OutLine(L"Tongue versions this tool can patch (first = preferred):");
		std::vector<std::string> seen;
		for (int t = 0; t < TONGUE_COUNT; ++t)
			for (int v = 0; v < TONGUES[t].nvariants; ++v) {
				std::string l = TONGUES[t].variants[v].label;
				if (std::find(seen.begin(), seen.end(), l) == seen.end()) seen.push_back(l);
			}
		for (auto& l : seen) OutLine(L"    " + FromUtf8(l.c_str()));
		return 0;
	}
	if (interactive) {
		OutLine(L"This makes slide-capable copies of Halo's tongues (linga1..10.nif) so TongueLab can slide them");
		OutLine(L"in and out of the mouth. Your own files are NEVER changed: the copies go into a new mod folder.");
		OutLine(L"A file is only patched if it is exactly a version this tool knows; anything else is skipped.");
		OutLine();
		OutLine(L"Next, choose the folder to search: your Mod Organizer 2 \"mods\" folder (best), or your Skyrim \"Data\" folder.");
		Out(L"Press Enter to choose the folder...");
		if (g_console) {
			HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
			wchar_t buf[8];
			DWORD rd = 0;
			ReadConsoleW(in, buf, 1, &rd, nullptr);
		}
		OutLine();
		if (!PickFolder(rootArg)) {
			OutLine(L"No folder chosen. Nothing was done.");
			WaitKey();
			return 1;
		}
	}
	if (rootArg.empty()) {
		OutLine(L"Usage: TongueLabSlideBonePatcher.exe <folder to search> [--out <new mod folder>] [--use <text>] [--dry-run] [--list]");
		OutLine(L"       or run it without arguments to pick the folder in a window.");
		return 2;
	}

	std::error_code ec;
	fs::path root = fs::absolute(rootArg, ec);
	if (!fs::is_directory(root, ec)) {
		OutLine(L"'" + root.wstring() + L"' is not a folder.");
		if (interactive) WaitKey();
		return 2;
	}

	// where the new mod folder goes
	wchar_t exePath[MAX_PATH * 2] = {};
	GetModuleFileNameW(nullptr, exePath, MAX_PATH * 2);
	fs::path outMod;
	if (!outArg.empty()) outMod = fs::absolute(outArg, ec);
	else if (LooksLikeModsFolder(root)) outMod = root / L"[TongueLab] Slide Bone Patch";
	else outMod = fs::path(exePath).parent_path() / L"TongueLab Slide Bone Patch";
	

	OutLine(L"Searching: " + root.wstring());
	OutLine(L"(this can take a moment on a big mods folder)");

	// 1. find every file called linga<N>.nif whose size is one we know
	std::map<std::wstring, int> index;   // lower-case file name -> tongue index
	std::vector<std::vector<std::uint32_t>> sizes(TONGUE_COUNT);
	for (int t = 0; t < TONGUE_COUNT; ++t) {
		index[Lower(FromUtf8(TONGUES[t].name))] = t;
		for (int v = 0; v < TONGUES[t].nvariants; ++v) sizes[t].push_back(TONGUES[t].variants[v].size);
	}
	std::vector<std::vector<fs::path>> cands(TONGUE_COUNT);
	std::vector<std::vector<fs::path>> otherSize(TONGUE_COUNT);
	size_t visited = 0;
	for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
		if (++visited % 20000 == 0) Out(L".");
		std::error_code e2;
		if (!it->is_regular_file(e2)) continue;
		auto found = index.find(Lower(it->path().filename().wstring()));
		if (found == index.end()) continue;
		auto sz = static_cast<std::uint32_t>(it->file_size(e2));
		if (std::find(sizes[found->second].begin(), sizes[found->second].end(), sz) != sizes[found->second].end()) cands[found->second].push_back(it->path());
		else otherSize[found->second].push_back(it->path());
	}
	OutLine();
	OutLine();

	// 2. match, patch, write
	struct Done { std::string name, label; fs::path from; };
	std::vector<Done> done;
	std::vector<std::string> skipped, missing, already, notes;
	for (int t = 0; t < TONGUE_COUNT; ++t) {
		const PTongue& tg = TONGUES[t];
		struct M { int v; fs::path p; std::vector<std::uint8_t> d; };
		std::vector<M> matches;
		bool isAlready = false;
		for (auto& p : cands[t]) {
			std::vector<std::uint8_t> d;
			std::uint8_t h[32];
			if (!ReadAll(p, d) || !Sha256(d, h)) continue;
			for (int v = 0; v < tg.nvariants; ++v) {
				if (Same32(h, tg.variants[v].src)) { matches.push_back({ v, p, d }); break; }
				if (Same32(h, tg.variants[v].dst)) { isAlready = true; break; }
			}
		}
		if (matches.empty()) {
			if (isAlready) already.push_back(tg.name);
			else if (cands[t].empty() && otherSize[t].empty()) missing.push_back(tg.name);
			else skipped.push_back(tg.name);
			continue;
		}
		// one version per output folder: Halo's tongues all share one folder (so one of their versions is used), SLO VE's four folders each get theirs
		std::map<std::string, std::vector<const M*>> buckets;
		for (auto& m : matches) buckets[tg.variants[m.v].out].push_back(&m);
		for (auto& kv : buckets) {
			auto& group = kv.second;
			const M* pick = nullptr;
			if (!useArg.empty() && group.size() > 1) {
				for (auto* m : group)
					if (Lower(FromUtf8(tg.variants[m->v].label)).find(Lower(useArg)) != std::wstring::npos) { pick = m; break; }
				if (!pick) { skipped.push_back(tg.name); continue; }
			} else {
				pick = *std::min_element(group.begin(), group.end(), [](const M* a, const M* b) { return a->v < b->v; });
			}
			for (auto* m : group)
				if (m->v != pick->v) {
					std::string l = tg.variants[m->v].label;
					if (std::find(notes.begin(), notes.end(), l) == notes.end()) notes.push_back(l);
				}
			const PVariant& var = tg.variants[pick->v];
			std::vector<std::uint8_t> nd = pick->d;
			bool good = true;
			for (int r = 0; r < var.nruns && good; ++r) {
				const PRun& run = var.runs[r];
				if (run.offset + run.len > nd.size() || !std::equal(run.orig, run.orig + run.len, nd.begin() + run.offset)) { good = false; break; }
				std::copy(run.patched, run.patched + run.len, nd.begin() + run.offset);
			}
			std::uint8_t nh[32];
			if (!good || !Sha256(nd, nh) || !Same32(nh, var.dst)) {
				OutLine(L"  " + FromUtf8(tg.name) + L": the patched result did not match its expected checksum. Nothing was written for it.");
				skipped.push_back(tg.name);
				continue;
			}
			if (!dry) {
				fs::path outDir = outMod / fs::path(FromUtf8(var.out));
				fs::create_directories(outDir, ec);
				std::ofstream f(outDir / FromUtf8(tg.name), std::ios::binary | std::ios::trunc);
				f.write(reinterpret_cast<const char*>(nd.data()), static_cast<std::streamsize>(nd.size()));
				if (!f) {
					OutLine(L"  could not write " + (outDir / FromUtf8(tg.name)).wstring());
					return 3;
				}
			}
			done.push_back({ tg.name, var.label, pick->p });
		}
	}

	for (auto& d : done) OutLine(L"  patched  " + FromUtf8(d.name.c_str()) + L"   [" + FromUtf8(d.label.c_str()) + L"]");
	for (auto& n : already) if (std::none_of(done.begin(), done.end(), [&](const Done& d) { return d.name == n; })) OutLine(L"  already slide-capable  " + FromUtf8(n.c_str()));
	for (auto& n : skipped) OutLine(L"  SKIPPED  " + FromUtf8(n.c_str()) + L"   (found, but not a version this patch is made for)");
	for (auto& n : missing) OutLine(L"  missing  " + FromUtf8(n.c_str()) + L"   (not found)");
	if (!notes.empty()) {
		OutLine();
		OutLine(L"  More than one known version was found. The first one in the list was used. Also found:");
		for (auto& l : notes) OutLine(L"     - " + FromUtf8(l.c_str()));
		OutLine(L"  (to use another one: run from a command prompt with  --use <part of its name>)");
	}
	OutLine();
	int rc;
	if (dry) {
		OutLine(L"Dry run: " + std::to_wstring(done.size()) + L" tongue files could be patched. Nothing was written.");
		rc = done.empty() ? 1 : 0;
	} else if (!done.empty()) {
		OutLine(L"Done: " + std::to_wstring(done.size()) + L" tongue files were written to:");
		OutLine(L"    " + outMod.wstring());
		OutLine();
		OutLine(L"What to do now:");
		OutLine(L"  Mod Organizer 2: press F5 (refresh). The new mod appears in the list. Enable it, and make sure it sits BELOW your tongue mods.");
		OutLine(L"  Vortex or manual: copy the \"meshes\" folder from there into your Skyrim Data folder (or zip the folder and install it).");
		OutLine(L"Your original files were not touched.");
		rc = 0;
	} else {
		OutLine(L"Nothing was patched. Do you have Halo's HDT Tongues installed? (Skyrim Special Edition nif files; the Skyrim LE format is not supported.)");
		rc = 1;
	}
	if (interactive) WaitKey();
	return rc;
}
