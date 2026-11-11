#include "PCH.h"

#include "Config.h"
#include "Tongue.h"
#include "FaceLabAPI.h"
#include "Npc.h"
#include "UI.h"

#include <Windows.h>
#include <psapi.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <thread>

namespace Tongue
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		// ---- the tongues OStim (and HDT Tongues - OStim) can equip -------------------------------------------------------------
		std::unordered_set<RE::FormID> g_forms;  // every tongue armor + its variants
		std::vector<std::size_t>       g_slots;  // biped slot indices those armors sit in
		// tongues of another mod that equips and drives them itself (SLO VE): their mouth is not OStim's tongue pose, so any open mouth counts for them
		std::unordered_set<RE::FormID> g_externalForms;

		// A voice mod can tell us when one of its clips (a moan / a spoken line) is playing on an actor: every loaded DLL that exports
		//   extern "C" int TongueLab_SpeechKind(std::uint32_t formID);   // 0 silent, 1 a moan is playing, 2 a spoken line is playing (main thread)
		// is asked, and the biggest answer counts. They are found once, at start.
		using SpeechKindFn = int (*)(std::uint32_t);
		std::vector<SpeechKindFn> g_speechProviders;

		int SpeechKindOf(std::uint32_t a_id)
		{
			int kind = 0;
			for (const auto fn : g_speechProviders) {
				kind = std::max(kind, fn(a_id));
			}
			return kind;
		}

		void FindSpeechProviders()
		{
			std::vector<HMODULE> mods(1024);
			DWORD                need = 0;
			if (!::EnumProcessModules(::GetCurrentProcess(), mods.data(), static_cast<DWORD>(mods.size() * sizeof(HMODULE)), &need)) {
				return;
			}
			if (need > mods.size() * sizeof(HMODULE)) {
				mods.resize(need / sizeof(HMODULE));
				if (!::EnumProcessModules(::GetCurrentProcess(), mods.data(), static_cast<DWORD>(mods.size() * sizeof(HMODULE)), &need)) {
					return;
				}
			}
			std::string names;
			for (std::size_t i = 0; i < need / sizeof(HMODULE) && i < mods.size(); ++i) {
				if (const auto p = ::GetProcAddress(mods[i], "TongueLab_SpeechKind")) {
					g_speechProviders.push_back(reinterpret_cast<SpeechKindFn>(p));
					wchar_t file[MAX_PATH]{};
					::GetModuleFileNameW(mods[i], file, MAX_PATH);
					names += (names.empty() ? "" : ", ") + std::filesystem::path(file).filename().string();
				}
			}
			logger::info("voice hook (TongueLab_SpeechKind): {}", g_speechProviders.empty() ? "no voice mod found (tongues are not pulled in for speech)" : "connected to " + names);
		}

		struct State
		{
			bool              shown{ false };
			float             openFor{ 0.0f };
			float             closedFor{ 0.0f };
			Clock::time_point changedAt{};  // when 'shown' last changed (the tongue is held for a moment so it cannot flutter)
			Clock::time_point lastSpeech{};
			bool              wasPresent{ false };
			// slide: the tongue meshes from 'HDT Tongues - Slide Bone' are skinned to the kinematic anchor 'tong1 1' instead of the head,
			// so moving that one node (back along the head's -Y) slides the whole tongue into the mouth
			RE::NiAVObject*   checkedFor{ nullptr };  // the tongue node the lines below were worked out for
			RE::NiAVObject*   anchor{ nullptr };      // null = this tongue cannot slide
			RE::NiPoint3      restPos{};
			float             axis[3][3]{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };  // the slider axes in the anchor's parent frame: Y is along the tongue's own length
			float             slide{ 0.0f };          // 0 = fully inside, 1 = fully out
			Clock::time_point equippedAt{};
			Clock::time_point lastTry{};
			bool              reported{ false };
			Clock::time_point lastBeat{};
			Clock::time_point askedAt{};            // the last moment a FaceLab face asked for the tongue
			Clock::time_point lastEquipTry{};
			RE::FormID        directItem{ 0 };      // a tongue we put on this actor ourselves (no OStim scene): taken off again a moment after the face ends
			Clock::time_point lastDirectTry{};
			bool              directDiagDone{ false };  // the one-time 'our tongue is not showing' report was written
			int               directModel{ -1 };    // the model index of that tongue (to notice when another is asked for)
			// the scene swap ("Tongue to use"): a scene equipped tongue swapFrom, we put swapItem on instead; swapItem is ours and is taken off when the scene's tongue goes
			RE::FormID        swapItem{ 0 };
			RE::FormID        swapFrom{ 0 };
			int               swapModel{ -1 };
			Clock::time_point lastSwapTry{};
			Clock::time_point lastSwapCheck{};
			Clock::time_point swapBlockUntil{};
			Clock::time_point swapFightAt{};
			int               swapFights{ 0 };
			Clock::time_point peekUntil{};  // a hidden tongue is shown for a moment so the engine binds its skin to the skeleton (the slide bone only exists after that)
			Clock::time_point nextPeek{};
			bool              weEquipped{ false };  // we asked OStim for this tongue (so we take it off again when the face is over)
			float             reach{ 1.0f };        // how far out a FaceLab-asked tongue goes: 0..1 of the normal fully-out place
			float             askBlend{ 0.0f };     // 0..1: how much of the "Asked" offsets applies now
			int               equipTries{ 0 };      // equip requests for this ask (it stops after a few: the scene may be over)
			float             over{ 0.0f };         // ...and how much further than that (TongueOut above 1.0)
			bool              faceSeen{ false };    // FaceLab has drawn this actor's face while this tongue was on
			bool              speechHold{ false };  // last pulled in by a spoken line, until it is fully out again (fast in, slow out)
			bool              resetSent{ false };   // the reset that goes before the freeze has been sent (the freeze follows a moment later)
			std::chrono::steady_clock::time_point resetAt{};
			bool              frozen{ false };      // the simulated tongue bones are frozen while the Tuning page is open (see FreezeForEdit)
			std::vector<std::string> frozenBones;
		};
		std::unordered_map<RE::FormID, State> g_state;  // main thread only
		// what TongueLab makes of FaceLab's tongue request, per character, for other plugins to show (TongueLab_AskStatus):
		//   0 no request, 1 asked but this character is out of scope (TongueLab's Who settings), 2 asked, waiting for a tongue to be put on, 3 asked and a tongue is on
		//   -1 TongueLab is switched off, -2 no tongue armor was found in OStim's equip objects
		std::mutex                                 g_askMx;
		std::unordered_map<RE::FormID, int>       g_askCode;
		std::atomic<int>                          g_askGlobal{ 0 };
		std::mutex                                 g_nearMx;
		std::vector<Tongue::Nearby>                g_near;  // who is near the player (refreshed with the actor list)

		std::mutex        g_diagMx;
		std::vector<Diag> g_diag;

		std::vector<RE::ActorHandle> g_actors;
		Clock::time_point            g_actorsAt{};
		Clock::time_point            g_last{};
		std::atomic<bool>            g_queued{ false };
		std::atomic<bool>            g_wasEnabled{ true };
		std::atomic<bool>            g_previewHold{ false };
		std::atomic<bool>            g_previewPull{ false };  // held visible at the pulled-back place
		std::atomic<long long>       g_editingMs{ -1 };  // when the Tuning page was last drawn (steady clock, ms); -1 = never
		std::atomic<std::uint32_t>   g_editChar{ 0 };    // the one character the Visual page is editing (0 = everyone / not one character)
		std::jthread                 g_ticker;

		std::mutex                                        g_climaxMx;
		std::unordered_map<RE::FormID, Clock::time_point> g_climax;  // actor -> end of the climax window (event thread writes, main thread reads)

		// ---- finding the tongues: OStim's "equip objects" files, and the tongue armors themselves ---------------------------------
		bool AddArmor(RE::TESObjectARMO* armor)
		{
			if (!armor || !g_forms.insert(armor->GetFormID()).second) {
				return false;
			}
			const auto mask = static_cast<std::uint32_t>(armor->GetSlotMask().underlying());
			for (std::size_t bit = 0; bit < 32; ++bit) {
				if ((mask & (1u << bit)) && std::find(g_slots.begin(), g_slots.end(), bit) == g_slots.end()) {
					g_slots.push_back(bit);
				}
			}
			return true;
		}

		void AddForm(const std::string& a_mod, const std::string& a_id)
		{
			RE::FormID local = 0;
			try {
				local = static_cast<RE::FormID>(std::stoul(a_id, nullptr, 16));
			} catch (...) {
				return;
			}
			auto* dh = RE::TESDataHandler::GetSingleton();
			auto* form = dh ? dh->LookupForm(local, a_mod) : nullptr;
			auto* armor = form ? form->As<RE::TESObjectARMO>() : nullptr;
			if (!armor) {
				logger::warn("tongue form {}:{} is not an armor / not loaded", a_mod, a_id);
				return;
			}
			AddArmor(armor);
		}

		// The tongues are defined by the tongue mod itself (Halo's HDT Tongues: ten armors whose meshes are meshes\morten\lingas\lingaN.nif).
		// OStim's equip-object files only list them for OStim, so a game without OStim (SexLab only) has no such files: the armors are found by their mesh path.
		// the tongue mesh an armor shows (lower case, backslashes), or empty when it is not one of the tongue meshes
		std::string TongueMeshKey(RE::TESObjectARMO* armor)
		{
			if (!armor) {
				return {};
			}
			for (auto* addon : armor->armorAddons) {
				if (!addon) {
					continue;
				}
				for (auto& model : addon->bipedModels) {
					const char* path = model.GetModel();
					if (!path || !*path) {
						continue;
					}
					std::string p(path);
					std::ranges::transform(p, p.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					std::ranges::replace(p, '/', '\\');
					// Halo's: meshes\morten\lingas\lingaN.nif.  SLO VE's own copies: meshes\SLOVE\tongues\lingaN.nif (and ...\khajiit\, ...\argonian\)
					static const std::regex slove(R"re(\\tongues\\([a-z]+\\)?linga\d+\.nif$)re");
					if (p.find("\\lingas\\linga") != std::string::npos || std::regex_search(p, slove)) {
						return p;
					}
				}
			}
			return {};
		}

		int ScanTongueArmors()
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) {
				return 0;
			}
			// other mods can carry their own armors for the very same tongue mesh (Fill Her Up does): one armor per mesh, the tongue mod's own preferred,
			// and none for a mesh that OStim's files already gave us
			std::set<std::string> have;
			for (auto id : g_forms) {
				if (auto key = TongueMeshKey(RE::TESForm::LookupByID<RE::TESObjectARMO>(id)); !key.empty()) {
					have.insert(key);
				}
			}
			const auto isOwn = [](RE::TESObjectARMO* a) {
				const auto* file = a ? a->GetFile(0) : nullptr;
				return file && _stricmp(file->GetFilename().data(), "Tongues.esp") == 0;
			};
			std::map<std::string, RE::TESObjectARMO*> best;
			for (auto* armor : dh->GetFormArray<RE::TESObjectARMO>()) {
				if (!armor || g_forms.contains(armor->GetFormID())) {
					continue;
				}
				const auto key = TongueMeshKey(armor);
				if (key.empty() || have.contains(key)) {
					continue;
				}
				if (auto it = best.find(key); it == best.end() || (isOwn(armor) && !isOwn(it->second))) {
					best[key] = armor;
				}
			}
			int found = 0;
			for (auto& [key, armor] : best) {
				if (AddArmor(armor)) {
					++found;
					if (key.find("\\lingas\\") == std::string::npos) {
						g_externalForms.insert(armor->GetFormID());  // not Halo's mesh folder: another mod's tongue (SLO VE)
					}
				}
			}
			return found;
		}

		void ScanEquipObjects()
		{
			const std::regex pair(R"re("mod"\s*:\s*"([^"]+)"\s*,\s*"formid"\s*:\s*"([^"]+)")re", std::regex::icase);
			const std::regex typeTongue(R"re("type"\s*:\s*"tongue")re", std::regex::icase);
			std::error_code  ec;
			int              files = 0;
			for (auto& e : std::filesystem::directory_iterator("Data/SKSE/Plugins/OStim/equip objects", ec)) {
				if (e.path().extension() != ".json") {
					continue;
				}
				std::ifstream     f(e.path(), std::ios::binary);
				std::stringstream ss;
				ss << f.rdbuf();
				const std::string text = ss.str();
				if (!std::regex_search(text, typeTongue)) {
					continue;
				}
				++files;
				for (std::sregex_iterator it(text.begin(), text.end(), pair), end; it != end; ++it) {
					AddForm((*it)[1].str(), (*it)[2].str());
				}
			}
			const int byMesh = ScanTongueArmors();
			std::string slots;
			for (auto b : g_slots) {
				slots += " " + std::to_string(b);
			}
			logger::info("tongue objects: {} json file(s), {} armor form(s) ({} found by their mesh path), biped slot(s):{}", files, g_forms.size(), byMesh, slots);
			if (g_forms.empty()) {
				logger::warn("no tongue found: TongueLab needs a tongue mod whose armors use meshes\\morten\\lingas\\ (Halo's HDT Tongues) or OStim equip-object files for one. It will do nothing until one is installed.");
			}
		}

		// ---- the decision, per actor, every ~16 ms ------------------------------------------------------------------------
		float Best(const RE::BSFaceGenKeyframeMultiple& k)
		{
			float m = 0.0f;
			if (k.values && k.count >= 16) {
				for (int i : { 0, 1, 6, 11 }) {  // Aah, BigAah, Eh, Oh
					m = std::max(m, k.values[i]);
				}
			}
			return m;
		}

		struct MouthInfo
		{
			float open{ 0.0f };   // how open the mouth is (Aah, BigAah, Eh, Oh)
			float pose{ 0.0f };   // how much it is OStim's tongue pose: BigAah and Eee together (the tongue expression is BigAah 100 + Eee 80)
			float other{ 0.0f };  // the same openness from the other source, for the log
			float bigAah{ 0.0f }, eee{ 0.0f }, aah{ 0.0f };
		};

		MouthInfo Mouth(RE::Actor* a_actor, int a_source, float a_poseLevel)
		{
			MouthInfo m;
			auto*     fg = a_actor->GetFaceGenAnimationData();
			if (!fg) {
				return m;
			}
			RE::BSSpinLockGuard lock(fg->lock);
			const auto&         k = a_source == 0 ? fg->phoneme3 : fg->phenomeKeyFrame;
			m.open = Best(k);
			m.other = Best(a_source == 0 ? fg->phenomeKeyFrame : fg->phoneme3);
			if (k.values && k.count >= 16) {
				m.aah = k.values[0];
				m.bigAah = k.values[1];
				m.eee = k.values[5];
				m.pose = std::min(1.0f, std::min(m.bigAah, m.eee / 0.8f) / std::max(0.05f, a_poseLevel));
			}
			return m;
		}

		// Debug only: finds the moments the mouth snaps shut while OStim's open-mouth pose is on, and records both phoneme arrays
		// (phoneme3 = what is being shown, phenomeKeyFrame = what mods write) so the log says which one was changed.
		std::string PhonemeDump(const RE::BSFaceGenAnimationData* a_fg)
		{
			std::string out;
			for (const auto* k : { &a_fg->phoneme3, &a_fg->phenomeKeyFrame }) {
				out += out.empty() ? "shown[" : " keyframe[";
				if (k->values) {
					for (std::uint32_t i = 0; i < k->count && i < 16; ++i) {
						char b[16];
						std::snprintf(b, sizeof b, "%s%.2f", i ? " " : "", k->values[i]);
						out += b;
					}
				}
				out += "]";
			}
			return out;
		}

		void WatchMouth(RE::Actor* a_actor, int a_speechKind, bool a_tongueShown, float a_sinceEquip)
		{
			struct W
			{
				float                 peak{ 0.0f };
				bool                  dipped{ false };
				Clock::time_point     peakAt{};
				Clock::time_point     dipAt{};
			};
			static std::map<RE::FormID, W> w;
			auto* fg = a_actor->GetFaceGenAnimationData();
			if (!fg) {
				return;
			}
			const auto now = Clock::now();
			auto&      x = w[a_actor->GetFormID()];
			float      big = 0.0f;
			std::string dump;
			{
				RE::BSSpinLockGuard lock(fg->lock);
				if (fg->phoneme3.values && fg->phoneme3.count >= 16) {
					big = std::max({ fg->phoneme3.values[0], fg->phoneme3.values[1], fg->phoneme3.values[6], fg->phoneme3.values[11] });
				}
				if (big > 0.45f && !x.dipped) {
					x.peak = big;
					x.peakAt = now;
					return;
				}
				if (big > 0.45f) {
					dump = PhonemeDump(fg);
				} else if (!x.dipped && x.peak > 0.45f && big < x.peak * 0.5f && now - x.peakAt < std::chrono::seconds(1)) {
					dump = PhonemeDump(fg);
				}
			}
			if (big > 0.45f && x.dipped) {
				x.dipped = false;
				x.peak = big;
				x.peakAt = now;
				logger::info("MOUTHWATCH [{}] open again after {:.0f} ms: {}", a_actor->GetName(), std::chrono::duration<float, std::milli>(now - x.dipAt).count(), dump);
			} else if (!dump.empty()) {
				x.dipped = true;
				x.dipAt = now;
				logger::info("MOUTHWATCH [{}] CLOSED (BigAah {:.2f} -> {:.2f}) speech kind={} tongue shown={} {:.1f}s after equip: {}", a_actor->GetName(), x.peak, big, a_speechKind, a_tongueShown, a_sinceEquip, dump);
			} else if (!x.dipped && now - x.peakAt > std::chrono::seconds(5)) {
				x.peak = 0.0f;
			}
		}

		// OStim's own equip objects: it tracks them and removes them at scene end
		void OStimObject(const char* a_fn, RE::Actor* a_actor)
		{
			static const bool ostim = ::GetModuleHandleA("OStim.dll") != nullptr;  // without OStim there is no OActor script to call: the tongue is put on directly
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!ostim || !vm || !a_actor) {
				return;
			}
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
			vm->DispatchStaticCall("OActor", a_fn,RE::MakeFunctionArguments(static_cast<RE::Actor*>(a_actor), RE::BSFixedString("tongue")), none);
		}

		std::string TongueName(RE::Actor* a_actor)
		{
			auto& biped = a_actor->GetBiped();
			if (!biped) {
				return {};
			}
			for (auto idx : g_slots) {
				if (idx >= static_cast<std::size_t>(RE::BIPED_OBJECTS::kTotal)) {
					continue;
				}
				auto& o = biped->objects[idx];
				if (o.item && o.partClone && g_forms.contains(o.item->GetFormID())) {
					std::string out = std::string(o.item->GetName()) + " (" + std::to_string(o.item->GetFormID()) + " / ";
					char        hex[16];
					std::snprintf(hex, sizeof(hex), "%08X", o.item->GetFormID());
					out = std::string(o.item->GetName()) + " [" + hex + "]";
					if (o.addon) {
						for (auto& m : o.addon->bipedModels) {
							if (m.GetModel()) {
								out += std::string(" model '") + m.GetModel() + "'";
								break;
							}
						}
					}
					return out;
				}
			}
			return {};
		}

		// the form id of the tongue armor this actor wears (0 = none)
		RE::FormID WornTongueId(RE::Actor* a_actor)
		{
			auto& biped = a_actor->GetBiped();
			if (!biped) {
				return 0;
			}
			for (auto idx : g_slots) {
				if (idx >= static_cast<std::size_t>(RE::BIPED_OBJECTS::kTotal)) {
					continue;
				}
				auto& o = biped->objects[idx];
				if (o.item && o.partClone && g_forms.contains(o.item->GetFormID())) {
					return o.item->GetFormID();
				}
			}
			return 0;
		}
		
		RE::NiAVObject* FindTongue(RE::Actor* a_actor)
		{
			auto& biped = a_actor->GetBiped();
			if (!biped) {
				return nullptr;
			}
			for (auto idx : g_slots) {
				if (idx >= static_cast<std::size_t>(RE::BIPED_OBJECTS::kTotal)) {
					continue;
				}
				auto& o = biped->objects[idx];
				if (o.item && o.partClone && g_forms.contains(o.item->GetFormID())) {
					return o.partClone.get();
				}
			}
			return nullptr;
		}

		// The skinned bone 'tong1 1' of this tongue, or null when the tongue is the stock one (its rigid part is skinned to the head, so
		// moving 'tong1 1' alone would only drag the loose end).
		RE::NiAVObject* FindAnchor(RE::NiAVObject* a_root)
		{
			if (!a_root) {
				return nullptr;
			}
			if (auto* geom = a_root->AsGeometry()) {
				auto& skin = geom->GetGeometryRuntimeData().skinInstance;
				if (skin && skin->bones) {
					for (std::uint32_t i = 0; i < skin->numMatrices; ++i) {
						auto* bone = skin->bones[i];
						if (bone) {
							// HDT-SMP renames the bones of every worn armor ("hdtSSEPhysics_AutoRename_Armor_0000000A tong1 1"), so match the end of the name
							const std::string_view n = bone->name.c_str();
							if (n == "tong1 1"sv || (n.size() > 7 && n.substr(n.size() - 7) == "tong1 1"sv)) {
								return bone;
							}
						}
						if (false) {
							return bone;
						}
					}
				}
				return nullptr;
			}
			if (auto* node = a_root->AsNode()) {
				// the bone node lives inside the tongue's own mesh tree from the start, long before the skin's bone pointers are filled in
				const std::string_view n = node->name.c_str();
				if (n == "tong1 1"sv || (n.size() > 7 && n.substr(n.size() - 7) == "tong1 1"sv)) {
					return node;
				}
				for (auto& child : node->GetChildren()) {
					if (auto* found = FindAnchor(child.get())) {
						return found;
					}
				}
			}
			return nullptr;
		}

		float Ease(float t, int a_kind)
		{
			t = std::clamp(t, 0.0f, 1.0f);
			switch (a_kind) {
			case 0:
				return t;
			case 2:
				return 1.0f - (1.0f - t) * (1.0f - t);
			case 3:
				return t * t;
			default:
				return t * t * (3.0f - 2.0f * t);
			}
		}

		std::string Describe(RE::NiAVObject* a_root, int a_depth = 0)
		{
			std::string out;
			if (!a_root) {
				return out;
			}
			if (auto* geom = a_root->AsGeometry()) {
				auto& skin = geom->GetGeometryRuntimeData().skinInstance;
				out += "shape '" + std::string(geom->name.c_str()) + "' skin=" + (skin ? "yes" : "no");
				if (skin && skin->bones) {
					out += " bones=[";
					for (std::uint32_t i = 0; i < skin->numMatrices && i < 8; ++i) {
						out += std::string(i ? ", " : "") + (skin->bones[i] ? skin->bones[i]->name.c_str() : "null");
					}
					out += "]";
				} else if (skin) {
					out += " (bones not linked)";
				}
				return out + "; ";
			}
			if (auto* node = a_root->AsNode(); node && a_depth < 6) {
				for (auto& child : node->GetChildren()) {
					out += Describe(child.get(), a_depth + 1);
				}
			}
			return out;
		}

		void Refresh(const Clock::time_point a_now)
		{
			if (a_now - g_actorsAt < std::chrono::milliseconds(250)) {
				return;
			}
			g_actorsAt = a_now;
			g_actors.clear();
			std::vector<std::pair<float, Tongue::Nearby>> nearby;
			auto* pc = RE::PlayerCharacter::GetSingleton();
			if (!pc) {
				return;
			}
			const auto pos = pc->GetPosition();
			if (pc->Get3D()) {
				g_actors.push_back(pc->GetHandle());
			}
			if (auto* pl = RE::ProcessLists::GetSingleton()) {
				pl->ForEachHighActor([&](RE::Actor* a) {
					if (a && a != pc && a->Get3D() && a->GetPosition().GetDistance(pos) < 3000.0f) {
						g_actors.push_back(a->GetHandle());
					if (!a->IsChild() && a->GetName() && *a->GetName()) {
						nearby.push_back({ a->GetPosition().GetDistance(pos), { a->GetName(), a->GetFormID() } });
					}
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			std::ranges::sort(nearby, [](const auto& l, const auto& r) { return l.first < r.first; });
			std::vector<Tongue::Nearby> list;
			for (const auto& [d, n] : nearby) {
				if (list.size() >= 5) {  // the five nearest are plenty to pick from
					break;
				}
				list.push_back(n);
			}
			std::lock_guard lk(g_nearMx);
			g_near = std::move(list);
		}

		bool InScope(RE::Actor* a, const Config::Settings& s)
		{
			if (a->IsPlayerRef()) {
				if (!s.player) {
					return false;
				}
			} else if (!s.others) {
				return false;
			}
			auto*      base = a->GetActorBase();
			const bool female = base && base->GetSex() == RE::SEX::kFemale;
			return female ? s.females : s.males;
		}


		// ---- the tongue's own shape (Visual page) -------------------------------------------------------------------------------
		// A skinned mesh is drawn through its bones: vertex = sum of weight * boneWorld * skinToBone * vertex. Every HALOS slider is a transform of the WHOLE tongue
		// (size, length, width, thickness, tilt about its centre), and a transform M applied to the vertices is the same as composing it into each bone's skinToBone:
		// B'(v) = B(M v)  ->  rotate' = R * A,  translate' = t + s * R * (c - A c)   (M v = A (v - c) + c). The skin data is plain memory, so this is live.
		// The layout of the engine's skin-data object is not in the headers: it is found by looking for an array of valid bone transforms (see Detect) and nothing is
		// written unless every bone entry passes. The originals are kept, so the sliders are absolute and 0 puts the shipped mesh back.
		struct XfData
		{
			float r[9];  // rotation, row-major, acts on a column vector
			float t[3];
			float s;     // uniform scale
		};
		static_assert(sizeof(XfData) == 0x34);

		struct VisualState
		{
			RE::NiPointer<RE::NiSkinData> hold;  // keeps the skin data (and so its address) alive and unique while we remember it
			std::uint8_t*                 first{ nullptr };
			std::size_t                   stride{ 0 };
			std::size_t                   xf{ 0 };
			std::vector<XfData>           original;
			std::vector<XfData>           written;  // what is in memory now (to notice anybody else changing it)
			float                         c[3]{};   // the tongue's centre in shape space
			bool                          ok{ false };
			bool                          failed{ false };
			float                         applied[5]{};  // size, length, width, thickness, tilt as last written (0 = the original)
			// who last shaped it: the skin data of one tongue model may be SHARED by every character wearing that model, so two characters
			// who want different shapes cannot both have theirs. The first one wins; the log says so once.
			RE::FormID                    owner{ 0 };
			Clock::time_point             ownerSeen{};
			bool                          sharedWarned{ false };
		};
				std::unordered_set<RE::FormID> g_npcKeyLogged;  // debug: characters whose settings file name was written to the log
		std::unordered_set<RE::FormID> g_npcLogged;  // characters whose per-NPC settings were announced in the log
		std::unordered_map<const void*, VisualState> g_visual;
		std::atomic<int> g_visOk{ 0 }, g_visFail{ 0 };

		bool MemOk(const void* a_p, std::size_t a_n, bool a_write)
		{
			MEMORY_BASIC_INFORMATION mbi{};
			if (!a_p || !::VirtualQuery(a_p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
				return false;
			}
			if (reinterpret_cast<std::uintptr_t>(a_p) + a_n > reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
				return false;
			}
			if (a_write) {
				return (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
			}
			return true;
		}

		bool ValidXf(const XfData& x)
		{
			for (float f : x.r) {
				if (!std::isfinite(f) || std::fabs(f) > 1.5f) {
					return false;
				}
			}
			for (float f : x.t) {
				if (!std::isfinite(f) || std::fabs(f) > 3000.0f) {
					return false;
				}
			}
			if (!std::isfinite(x.s) || x.s < 0.5f || x.s > 2.0f) {
				return false;
			}
			// The scale field and the rotation rows together make the transform: a bone we re-bound (the slide anchor) keeps a scale of 0.947 with rows of length 1.056.
			// So judge the EFFECTIVE row length (scale * row) and the angle between rows, not the bare matrix.
			for (int i = 0; i < 3; ++i) {
				const float* a = &x.r[i * 3];
				const float* b = &x.r[((i + 1) % 3) * 3];
				const float  na = a[0] * a[0] + a[1] * a[1] + a[2] * a[2];
				const float  nb = b[0] * b[0] + b[1] * b[1] + b[2] * b[2];
				const float  eff = na * x.s * x.s;
				if (eff < 0.9f || eff > 1.1f || na < 0.01f || nb < 0.01f) {
					return false;
				}
				if (std::fabs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) / std::sqrt(na * nb) > 0.05f) {
					return false;
				}
			}
			return true;
		}

		// The object is a standard NiSkinData (seen in the first test's memory dump): the global skin transform at +0x18 (identity), then the bone array pointer at
		// +0x50 and the bone count at +0x58; each bone entry is 0x58 bytes with the skinToBone transform FIRST. Tried first; the scan below is only a fallback.
		bool DetectNiSkinData(const std::uint8_t* a_obj, std::uint32_t a_n, VisualState& a_vs, std::string& a_how)
		{
			const auto* nd = reinterpret_cast<const RE::NiSkinData*>(a_obj);
			const auto  stride = sizeof(RE::NiSkinData::BoneData);
			const auto  hex = [](const void* p, std::size_t n) {
				std::string s;
				if (MemOk(p, n, false)) {
					for (std::size_t i = 0; i < n; ++i) {
						s += std::format("{:02X}{}", static_cast<const std::uint8_t*>(p)[i], (i % 16 == 15) ? '|' : ' ');
					}
				}
				return s;
			};
			if (!MemOk(nd, sizeof(RE::NiSkinData), false) || nd->bones != a_n || !nd->boneData || !MemOk(nd->boneData, stride * a_n, true)) {
				logger::warn("Visual: NiSkinData layout rejected before looking at the bones: bones={} (wanted {}), boneData={:p}, readable={}, writable={}", nd->bones, a_n,
					static_cast<const void*>(nd->boneData), nd->boneData && MemOk(nd->boneData, stride * a_n, false), nd->boneData && MemOk(nd->boneData, stride * a_n, true));
				return false;
			}
			for (std::uint32_t i = 0; i < a_n; ++i) {
				XfData x;
				std::memcpy(&x, reinterpret_cast<const std::uint8_t*>(nd->boneData) + i * stride, sizeof x);
				if (!ValidXf(x)) {
					logger::warn("Visual: NiSkinData layout: bone {} of {} does not start with a valid transform. Raw bone array ({} bytes per bone assumed, 2 bones shown): {}", i, a_n, stride,
						hex(nd->boneData, stride * std::min<std::uint32_t>(a_n, 2)));
					return false;
				}
			}
			a_vs.first = reinterpret_cast<std::uint8_t*>(nd->boneData);
			a_vs.stride = stride;
			a_vs.xf = 0;
			a_how = std::format("NiSkinData layout: bone array via +0x50, {} bytes per bone", stride);
			return true;
		}

		// Looks inside the skin-data object for an array of n bone entries (a 16 byte bound and a 52 byte transform, in either order) that all hold valid transforms.
		bool Detect(const std::uint8_t* a_obj, std::uint32_t a_n, VisualState& a_vs, std::string& a_how)
		{
			struct Cand
			{
				const std::uint8_t* base;
				std::string         how;
			};
			std::vector<Cand> cands;
			for (std::size_t off = 0x10; off <= 0x50; off += 4) {
				cands.push_back({ a_obj + off, std::format("inline at +0x{:X}", off) });
			}
			for (std::size_t off = 0x10; off <= 0x50; off += 8) {
				if (!MemOk(a_obj + off, 8, false)) {
					continue;
				}
				const auto* q = *reinterpret_cast<const std::uint8_t* const*>(a_obj + off);
				if (reinterpret_cast<std::uintptr_t>(q) > 0x10000) {
					cands.push_back({ q, std::format("pointer at +0x{:X}", off) });
				}
			}
			constexpr std::size_t kStride = 0x44;
			int                   hits = 0;
			for (const auto& c : cands) {
				if (!MemOk(c.base, kStride * a_n, false)) {
					continue;
				}
				for (const std::size_t xf : { std::size_t{ 0x00 }, std::size_t{ 0x10 } }) {
					bool good = true;
					for (std::uint32_t i = 0; i < a_n && good; ++i) {
						XfData x;
						std::memcpy(&x, c.base + i * kStride + xf, sizeof x);
						good = ValidXf(x);
					}
					if (good) {
						++hits;
						if (hits == 1) {
							a_vs.first = const_cast<std::uint8_t*>(c.base);
							a_vs.stride = kStride;
							a_vs.xf = xf;
							a_how = c.how + (xf == 0 ? ", transform first" : ", bound first");
						}
					}
				}
			}
			if (hits != 1) {
				a_how = hits == 0 ? "no array of valid bone transforms found" : std::format("{} candidate layouts fit: not guessing", hits);
				return false;
			}
			return true;
		}

		void CollectGeometry(RE::NiAVObject* a_root, std::vector<RE::BSGeometry*>& a_out, int a_depth = 0)
		{
			if (!a_root || a_depth > 6) {
				return;
			}
			if (auto* g = a_root->AsGeometry()) {
				a_out.push_back(g);
				return;
			}
			if (auto* n = a_root->AsNode()) {
				for (auto& c : n->GetChildren()) {
					CollectGeometry(c.get(), a_out, a_depth + 1);
				}
			}
		}

		void ApplyVisual(RE::NiAVObject* a_root, const Config::Settings& s, RE::FormID a_owner)
		{
			if (!a_root || (!s.visual && g_visual.empty())) {
				return;
			}
			const float want[5] = { s.visual ? s.visSize : 0.0f, s.visual ? s.visLength : 0.0f, s.visual ? s.visWidth : 0.0f, s.visual ? s.visThickness : 0.0f,
				s.visual ? s.visTilt : 0.0f };
			std::vector<RE::BSGeometry*> geoms;
			CollectGeometry(a_root, geoms);
			for (auto* g : geoms) {
				auto* si = g->GetGeometryRuntimeData().skinInstance.get();
				if (!si || !si->skinData || si->numMatrices == 0) {
					continue;
				}
				const void* key = si->skinData.get();
				auto        it = g_visual.find(key);
				if (it == g_visual.end()) {
					if (!s.visual) {
						continue;  // nothing changed so far and nothing wanted: do not even look
					}
					it = g_visual.emplace(key, VisualState{}).first;
				}
				auto& vs = it->second;
					if (vs.failed) {
						continue;
					}
					{
						const auto nowT = Clock::now();
						// The skin data is one object shared by every character wearing this tongue model. While the Visual page is editing one character, that
						// character takes it over (so the sliders show on the one being edited); the others then show the same shape.
						const auto editMs = g_editingMs.load();
						const bool editorSteals = editMs >= 0 && g_editChar.load() == a_owner && a_owner != 0 &&
						                          std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count() - editMs <= 300;
						if (!editorSteals && vs.owner != 0 && vs.owner != a_owner && nowT - vs.ownerSeen < std::chrono::seconds(2)) {
							if (!vs.sharedWarned && vs.ok && std::memcmp(vs.applied, want, sizeof want) != 0) {
								vs.sharedWarned = true;
								logger::warn("Visual: the skin data of '{}' is SHARED by two characters ({:08X} and {:08X}) who want different tongue shapes: the first keeps its shape, the second gets the same one", g->name.c_str(), vs.owner, a_owner);
							}
							continue;
						}
						vs.owner = a_owner;
						vs.ownerSeen = nowT;
					}
				if (!vs.ok) {
					vs.hold = si->skinData;
					std::string how;
					if (!DetectNiSkinData(reinterpret_cast<const std::uint8_t*>(key), si->numMatrices, vs, how) && !Detect(reinterpret_cast<const std::uint8_t*>(key), si->numMatrices, vs, how)) {
						vs.failed = true;
						++g_visFail;
						std::string dump;
						if (MemOk(key, 0x80, false)) {
							for (std::size_t i = 0; i < 0x80; ++i) {
								dump += std::format("{:02X}{}", reinterpret_cast<const std::uint8_t*>(key)[i], (i % 16 == 15) ? '|' : ' ');
							}
						}
						logger::warn("Visual: could not recognise the skin data of '{}' ({} bones): {}. Nothing was changed. First bytes: {}", g->name.c_str(), si->numMatrices, how, dump);
						continue;
					}
					vs.original.resize(si->numMatrices);
					for (std::uint32_t i = 0; i < si->numMatrices; ++i) {
						std::memcpy(&vs.original[i], vs.first + i * vs.stride + vs.xf, sizeof(XfData));
					}
					vs.written = vs.original;
					const auto& bc = g->GetModelData().modelBound.center;
					vs.c[0] = bc.x;
					vs.c[1] = bc.y;
					vs.c[2] = bc.z;
					vs.ok = true;
					++g_visOk;
					logger::info("Visual: skin data of '{}' recognised ({} bones, {}); centre of the tongue ({:.2f}, {:.2f}, {:.2f})", g->name.c_str(), si->numMatrices, how, vs.c[0], vs.c[1], vs.c[2]);
				}
				if (std::memcmp(vs.applied, want, sizeof want) == 0) {
					continue;
				}
				// somebody else changed the bone transforms since we wrote them: leave it alone
				bool intact = MemOk(vs.first, vs.stride * vs.original.size(), true);
				for (std::size_t i = 0; i < vs.original.size() && intact; ++i) {
					XfData now;
					std::memcpy(&now, vs.first + i * vs.stride + vs.xf, sizeof now);
					for (int k = 0; k < 9; ++k) {
						intact = intact && std::fabs(now.r[k] - vs.written[i].r[k]) < 1e-3f;
					}
					for (int k = 0; k < 3; ++k) {
						intact = intact && std::fabs(now.t[k] - vs.written[i].t[k]) < 1e-2f;
					}
				}
				if (!intact) {
					vs.failed = true;
					logger::warn("Visual: the bone transforms of '{}' were changed by something else: leaving them alone", g->name.c_str());
					continue;
				}
				// A = Rx(tilt) * diag(sx, sy, sz): x is side to side, y along the tongue, z up and down
				const float sz0 = 1.0f + want[0] / 100.0f;
				const float sx = sz0 * (1.0f + want[2] / 100.0f), sy = sz0 * (1.0f + want[1] / 100.0f), sz = sz0 * (1.0f + want[3] / 100.0f);
				const float th = want[4] * 3.14159265f / 180.0f, cs = std::cos(th), sn = std::sin(th);
				const float A[9] = { sx, 0.0f, 0.0f, 0.0f, cs * sy, -sn * sz, 0.0f, sn * sy, cs * sz };
				float       d[3];  // c - A c
				for (int i = 0; i < 3; ++i) {
					d[i] = vs.c[i] - (A[i * 3] * vs.c[0] + A[i * 3 + 1] * vs.c[1] + A[i * 3 + 2] * vs.c[2]);
				}
				for (std::size_t b = 0; b < vs.original.size(); ++b) {
					const XfData& o = vs.original[b];
					XfData        n = o;
					for (int i = 0; i < 3; ++i) {
						for (int j = 0; j < 3; ++j) {
							n.r[i * 3 + j] = o.r[i * 3] * A[j] + o.r[i * 3 + 1] * A[3 + j] + o.r[i * 3 + 2] * A[6 + j];
						}
						n.t[i] = o.t[i] + o.s * (o.r[i * 3] * d[0] + o.r[i * 3 + 1] * d[1] + o.r[i * 3 + 2] * d[2]);
					}
					std::memcpy(vs.first + b * vs.stride + vs.xf, &n, sizeof n);
					vs.written[b] = n;
				}
				std::memcpy(vs.applied, want, sizeof want);
				logger::info("Visual: '{}' set to size {:+.0f}% length {:+.0f}% width {:+.0f}% thickness {:+.0f}% tilt {:+.0f} deg", g->name.c_str(), want[0], want[1], want[2], want[3], want[4]);
			}
		}

		// The Tuning page forces an engine update of the whole tongue every tick (PushIfEditing). If that update puts the shipped skin data back, the Visual shape is lost
		// until the page is closed. Called right after that update: if the bone transforms in memory are no longer what we wrote, write them again, and say so in the log.
		void VerifyVisual(RE::NiAVObject* a_root)
		{
			if (!a_root || g_visual.empty()) {
				return;
			}
			static std::unordered_set<const void*> s_intactLogged;
			static auto                            s_lastChangedLog = Clock::now() - std::chrono::seconds(10);
			std::vector<RE::BSGeometry*> geoms;
			CollectGeometry(a_root, geoms);
			for (auto* g : geoms) {
				auto* si = g->GetGeometryRuntimeData().skinInstance.get();
				if (!si || !si->skinData) {
					continue;
				}
				const void* key = si->skinData.get();
				const auto  it = g_visual.find(key);
				if (it == g_visual.end() || !it->second.ok || it->second.failed) {
					continue;
				}
				auto& vs = it->second;
				if (!MemOk(vs.first, vs.stride * vs.written.size(), true)) {
					continue;
				}
				float worst = 0.0f;
				for (std::size_t i = 0; i < vs.written.size(); ++i) {
					XfData now;
					std::memcpy(&now, vs.first + i * vs.stride + vs.xf, sizeof now);
					for (int k = 0; k < 9; ++k) {
						worst = std::max(worst, std::fabs(now.r[k] - vs.written[i].r[k]));
					}
					for (int k = 0; k < 3; ++k) {
						worst = std::max(worst, std::fabs(now.t[k] - vs.written[i].t[k]));
					}
				}
				if (worst > 1e-4f) {
					for (std::size_t i = 0; i < vs.written.size(); ++i) {
						std::memcpy(vs.first + i * vs.stride + vs.xf, &vs.written[i], sizeof(XfData));
					}
					if (Clock::now() - s_lastChangedLog > std::chrono::seconds(3)) {
						s_lastChangedLog = Clock::now();
						logger::info("Visual: the Tuning page's geometry update changed the bone transforms of '{}' (largest difference {:.4f}); our Visual shape was written back", g->name.c_str(), worst);
					}
				} else if (s_intactLogged.insert(key).second) {
					logger::info("Visual: the Tuning page's geometry update left the bone transforms of '{}' as we wrote them (so a wrong size on screen is not the skin data)", g->name.c_str());
				}
			}
		}

		// While the Tuning page is on screen the game is paused and the engine does not refresh the actors' scene graphs, so a changed local position of the
		// slide bone is not drawn until the game runs again. Push it through ourselves, and only then (in normal play the engine and the cloth physics do it).
		void CollectNodes(RE::NiAVObject* a_node, std::vector<RE::NiAVObject*>& a_out)
		{
			if (!a_node || a_out.size() >= 256) {
				return;
			}
			a_out.push_back(a_node);
			if (auto* node = a_node->AsNode()) {
				for (auto& c : node->GetChildren()) {
					CollectNodes(c.get(), a_out);
				}
			}
		}

		void PushIfEditing(RE::NiAVObject* a_node, RE::NiAVObject* a_skin = nullptr)
		{
			const auto last = g_editingMs.load();
			const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
			if (!a_node || last < 0 || nowMs - last > 300) {
				return;
			}
			// The whole chain has to follow, not only the anchor: its children (the simulated tongue bones) are the front of the tongue. They are updated as
			// dirty, and any of them that still did not move with the anchor (the cloth physics owns them and it is not stepping while the game is paused) is moved
			// by the same amount, so the whole tongue shifts together.
			std::vector<RE::NiAVObject*> nodes;
			CollectNodes(a_node, nodes);
			std::vector<RE::NiPoint3> before;
			before.reserve(nodes.size());
			for (auto* n : nodes) {
				before.push_back(n->world.translate);
			}
			RE::NiUpdateData data{};
			data.time = 0.0f;
			data.flags.set(RE::NiUpdateData::Flag::kDirty);
			a_node->UpdateDownwardPass(data, 0);
			const RE::NiPoint3 delta = a_node->world.translate - before[0];
			std::size_t        moved = 0;
			if (delta.Length() > 1e-4f) {
				for (std::size_t i = 1; i < nodes.size(); ++i) {
					if ((nodes[i]->world.translate - before[i]).Length() < 1e-4f) {
						nodes[i]->world.translate += delta;
						++moved;
					}
				}
			}
			// The tongue's own geometry is skinned to these bones but is not under them, so the push above never reached it: its bone matrices are only
			// refreshed when the geometry itself is updated. Without this the rigid part stays where it was and only the loose end follows.
			if (a_skin) {
				RE::NiUpdateData skinData{};
				skinData.time = 0.0f;
				skinData.flags.set(RE::NiUpdateData::Flag::kDirty);
				a_skin->UpdateDownwardPass(skinData, 0);
				VerifyVisual(a_skin);
			}
			if (Config::Get().debug) {
				static auto        s_lastLog = Clock::now() - std::chrono::seconds(5);
				static RE::NiPoint3 s_lastLocal{ 1e9f, 0.0f, 0.0f };
				const bool         changed = (a_node->local.translate - s_lastLocal).Length() > 0.01f;
				if (changed && Clock::now() - s_lastLog > std::chrono::milliseconds(150)) {
					s_lastLog = Clock::now();
					s_lastLocal = a_node->local.translate;
					std::string all;
					for (auto* n : nodes) {
						all += std::format(" [{} local=({:.2f},{:.2f},{:.2f}) world=({:.2f},{:.2f},{:.2f})]", n->name.c_str(), n->local.translate.x, n->local.translate.y, n->local.translate.z,
							n->world.translate.x, n->world.translate.y, n->world.translate.z);
					}
					logger::info("EDITPUSH anchor world moved by ({:.2f},{:.2f},{:.2f}) in this push, followed-by-hand={}, geometry-updated={}:{}", delta.x, delta.y, delta.z, moved, a_skin ? "yes" : "no", all);
				}
			}
		}

		// The sliders (out / sideways / up) are measured in a frame whose Y runs along the tongue's own length (from the anchor towards the tip), not along the
		// parent bone's Y, which is a good 20 degrees off: otherwise "forward" slides the tongue diagonally. Worked out once per tongue from the first child bone.
		void WorkOutAxis(State& st, const char* a_who)
		{
			float id[3][3]{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
			std::memcpy(st.axis, id, sizeof id);
			auto* node = st.anchor ? st.anchor->AsNode() : nullptr;
			if (!node) {
				return;
			}
			for (auto& c : node->GetChildren()) {
				if (!c) {
					continue;
				}
				const RE::NiPoint3 t = c->local.translate;
				const float        len = t.Length();
				if (len < 0.05f) {
					continue;
				}
				const RE::NiPoint3 d = st.anchor->local.rotate * (t * (1.0f / len));  // towards the tip, in the anchor's parent frame
				const float        dl = d.Length();
				if (dl < 0.5f) {
					return;
				}
				const RE::NiPoint3 f = d * (1.0f / dl);
				const float        cs = f.y;  // dot with (0,1,0)
				if (cs < 0.5f) {  // more than 60 degrees off: this is not the layout we know, keep the old axes
					logger::info("[{}] slider axes: the tongue runs {:.0f} degrees off the old Y axis, keeping the old axes", a_who, std::acos(std::clamp(cs, -1.0f, 1.0f)) * 57.2958f);
					return;
				}
				// shortest rotation taking (0,1,0) to f (Rodrigues)
				const float vx = 1.0f * f.z - 0.0f * f.y;  // a x f with a = (0,1,0): (f.z, 0, -f.x)
				const float vy = 0.0f;
				const float vz = -f.x;
				const float k = 1.0f / (1.0f + cs);
				const float R[3][3] = {
					{ 1.0f + k * (-vz * vz - vy * vy), -vz + k * (vx * vy), vy + k * (vx * vz) },
					{ vz + k * (vx * vy), 1.0f + k * (-vx * vx - vz * vz), -vx + k * (vy * vz) },
					{ -vy + k * (vx * vz), vx + k * (vy * vz), 1.0f + k * (-vx * vx - vy * vy) }
				};
				std::memcpy(st.axis, R, sizeof R);
				logger::info("[{}] slider axes: forward follows the tongue ({:.1f} degrees from the parent bone's Y)", a_who, std::acos(std::clamp(cs, -1.0f, 1.0f)) * 57.2958f);
				return;
			}
		}

		RE::NiPoint3 AxisMap(const State& st, float x, float y, float z)
		{
			const auto& r = st.axis;
			return RE::NiPoint3(r[0][0] * x + r[0][1] * y + r[0][2] * z, r[1][0] * x + r[1][1] * y + r[1][2] * z, r[2][0] * x + r[2][1] * y + r[2][2] * z);
		}

		// While the Tuning page is open the game is paused and the cloth physics does not step, so the simulated tip of the tongue keeps its old place while the
		// base follows the sliders. FSMP can freeze bones (they then follow their nodes): freeze the simulated bones while the page is being used and give them back
		// when it is not. Only the tongue's own bones are touched.
		bool Editing()
		{
			const auto last = g_editingMs.load();
			const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
			return last >= 0 && nowMs - last <= 300;
		}

		void CallFsmp(const char* a_fn, RE::BSScript::IFunctionArguments* a_args)
		{
			if (auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton()) {
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
				vm->DispatchStaticCall("DynamicHDT", a_fn, a_args, none);
			}
		}

		// Papyrus Actor.QueueNiNodeUpdate: makes the engine build the model of an item that was just equipped. While a menu has the game paused (FaceLab's menu does) another
		// character's new tongue is in her inventory and equipped but its model is not built until the game runs again; this builds it at once (tested live on Ysolda, 2026-11-08).
		void QueueNiNodeUpdate(RE::Actor* a_actor)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!a_actor || !policy) {
				return;
			}
			const auto handle = policy->GetHandleForObject(static_cast<RE::VMTypeID>(a_actor->GetFormType()), a_actor);
			if (handle == policy->EmptyHandle()) {
				return;
			}
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
			vm->DispatchMethodCall(handle, "Actor", "QueueNiNodeUpdate", RE::MakeFunctionArguments(), none);
		}

		void FreezeForEdit(RE::Actor* a_actor, State& st)
		{
			if (!a_actor) {
				return;
			}
			const bool want = Editing() && st.anchor;
			if (!want && st.resetSent && !st.frozen) {
				st.resetSent = false;  // the page was left before the freeze: nothing to give back
			}
			if (want && !st.frozen) {
				// The tongue is reset first, so the tongue drawn while editing is the resting pose that stays when the menu is closed (the real resting pose of the physics
				// sags a little, about 1.5 units at the tip). FSMP carries the reset out a moment later, and a reset that lands after the freeze gives the bones their
				// physics back (measured live: TogglePhysics returns each bone's previous state), so the freeze is sent only after a short wait.
				if (!st.resetSent) {
					CallFsmp("ResetPhysics", RE::MakeFunctionArguments(static_cast<RE::Actor*>(a_actor), false));
					st.resetSent = true;
					st.resetAt = std::chrono::steady_clock::now();
					return;
				}
				if (std::chrono::duration<float>(std::chrono::steady_clock::now() - st.resetAt).count() < 0.4f) {
					return;
				}
				std::vector<RE::NiAVObject*> nodes;
				CollectNodes(st.anchor, nodes);
				std::vector<RE::BSFixedString> names;
				st.frozenBones.clear();
				for (std::size_t i = 1; i < nodes.size(); ++i) {  // not the anchor: it is already driven by us
					names.emplace_back(nodes[i]->name.c_str());
					st.frozenBones.emplace_back(nodes[i]->name.c_str());
				}
				if (!names.empty()) {
					CallFsmp("TogglePhysics", RE::MakeFunctionArguments(static_cast<RE::Actor*>(a_actor), std::move(names), false));
					st.frozen = true;
					if (Config::Get().debug) {
						logger::info("[{}] tongue physics frozen for editing ({} bones)", a_actor->GetName(), st.frozenBones.size());
					}
				}
			} else if (!want && st.frozen) {
				std::vector<RE::BSFixedString> names;
				for (auto& n : st.frozenBones) {
					names.emplace_back(n.c_str());
				}
				CallFsmp("TogglePhysics", RE::MakeFunctionArguments(static_cast<RE::Actor*>(a_actor), std::move(names), true));
				CallFsmp("ResetPhysics", RE::MakeFunctionArguments(static_cast<RE::Actor*>(a_actor), false));
				st.frozen = false;
				st.resetSent = false;
				st.frozenBones.clear();
				if (Config::Get().debug) {
					logger::info("[{}] tongue physics given back after editing", a_actor->GetName());
				}
			}
		}

		// ---- the preview tongue (Tuning page): a tongue on the player without a scene ----------------------------------------------------
		std::atomic<bool>        g_prevOn{ false };
		std::atomic<RE::FormID>  g_prevItem{ 0 };
		std::atomic<RE::FormID>  g_prevActor{ 0x14 };  // who the preview tongue is on
		std::atomic<int>         g_modelOverride{ -1 };  // see Tongue::SetModelOverride

		std::vector<RE::FormID> BuildSortedForms()
		{
			std::vector<RE::FormID> v(g_forms.begin(), g_forms.end());
			// Halo's tongues (meshes\lingas\) first: they are the ones that can slide, so the default pick (index 0) is one of them and not OStim's stock tongue
			const auto isHalo = [](RE::FormID id) { return TongueMeshKey(RE::TESForm::LookupByID<RE::TESObjectARMO>(id)).find("\\lingas\\") != std::string::npos; };
			std::sort(v.begin(), v.end(), [&](RE::FormID a, RE::FormID b) {
				const bool ha = isHalo(a), hb = isHalo(b);
				return ha != hb ? ha : a < b;
			});
			return v;
		}

		// The sorted list is needed every frame for every character and by the menu thread: sort once, rebuild only if the set of tongue armors changes.
		std::vector<RE::FormID> SortedForms()
		{
			static std::mutex                   mx;
			static std::vector<RE::FormID>      cache;
			static std::size_t                  cachedFor = static_cast<std::size_t>(-1);
			std::lock_guard                     lk(mx);
			if (cachedFor != g_forms.size()) {
				cache = BuildSortedForms();
				cachedFor = g_forms.size();
			}
			return cache;
		}

		bool PlayerHas(RE::Actor* a_pc, RE::FormID a_id)
		{
			const auto inv = a_pc->GetInventory([a_id](RE::TESBoundObject& a_o) { return a_o.GetFormID() == a_id; });
			return !inv.empty();
		}

		void RemovePreview(const char* a_why)
		{
			const auto id = g_prevItem.exchange(0);
			g_prevOn = false;
			if (!id) {
				return;
			}
			auto* pc = g_prevActor.load() == 0x14 ? RE::PlayerCharacter::GetSingleton() : RE::TESForm::LookupByID<RE::Actor>(g_prevActor.load());
			auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(id);
			auto* mgr = RE::ActorEquipManager::GetSingleton();
			if (pc && armor && mgr && PlayerHas(pc, id)) {  // only if it is still there (a loaded save does not have it)
				mgr->UnequipObject(pc, armor, nullptr, 1, nullptr, false, true, false, true);
				pc->RemoveItem(armor, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
			logger::info("preview tongue taken off {} ({})", pc ? pc->GetName() : "?", a_why);
		}

		void DropOurTongue(RE::Actor* a_actor, RE::FormID a_id)
		{
			auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(a_id);
			auto* mgr = RE::ActorEquipManager::GetSingleton();
			if (armor && mgr && PlayerHas(a_actor, a_id)) {
				mgr->UnequipObject(a_actor, armor, nullptr, 1, nullptr, false, true, false, true);
				a_actor->RemoveItem(armor, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
		}

		void Tick()
		{
			g_queued = false;
			UI::Flush();  // menu edits that have been still for a moment are written now, even if the menu was closed
			if (g_prevOn.load()) {  // never leave it on the player: gone a minute after the menu is closed
				const auto last = g_editingMs.load();
				const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
				if (last < 0 || nowMs - last > (g_prevActor.load() == 0x14 ? 60000 : 120000)) {
					RemovePreview("the menu was closed for a minute");
					g_previewHold = false;
					g_previewPull = false;
				}
			}
			const auto now = Clock::now();
			float      dt = std::chrono::duration<float>(now - g_last).count();
			g_last = now;
			dt = std::clamp(dt, 0.0f, 0.2f);

			const auto& gs = Config::Get();  // the global settings; each character below gets its own copy with the per-NPC files over it
			g_askGlobal = !gs.enabled ? -1 : (g_forms.empty() ? -2 : 0);
			if (!gs.enabled) {
				if (g_wasEnabled.exchange(false)) {
					ShutdownRestore();
				}
				return;
			}
			g_wasEnabled = true;
			if (g_forms.empty()) {
				return;
			}
			Refresh(now);
			Npc::Poll(now);

			std::vector<Diag> diag;
			for (auto& h : g_actors) {
				auto  ptr = h.get();
				auto* a = ptr.get();
				if (!a || !a->Get3D()) {
					continue;
				}
				Npc::Info npcInfo;
				const Config::Settings s = Npc::Effective(a, gs, &npcInfo);
				if (npcInfo.Any() && g_npcLogged.insert(a->GetFormID()).second) {
					logger::info("[{}] per-NPC settings: {}{}{}({} value(s); own file {})", a->GetName(), npcInfo.def ? "_default " : "", npcInfo.generic ? "_generic " : "", npcInfo.own ? "own file " : "",
						npcInfo.values, npcInfo.file.empty() ? "none possible" : npcInfo.file);
				}
				if (gs.debug && g_npcKeyLogged.insert(a->GetFormID()).second) {
					const auto* nb = a->GetActorBase();
					logger::info("[{}] per-NPC settings file for this character: {}  ({})", a->GetName(), npcInfo.file.empty() ? "none (created at run time)" : npcInfo.file, nb && nb->IsUnique() ? "unique NPC" : "generic NPC, shares _generic.toml");
				}
				auto*             tongue = FindTongue(a);
				const RE::FormID  id = a->GetFormID();
				auto&             st = g_state[id];
				if (s.debug) {
					WatchMouth(a, SpeechKindOf(id), st.shown, std::chrono::duration<float>(now - st.equippedAt).count());
				}
				// FaceLab can ask for the tongue: a face with the TongueOut channel (1.0 = fully out, more = further). Then the pose / mouth / moan rules do not apply,
				// and if OStim gave her no tongue (a blowjob) we ask OStim to equip one.
				float faceTongue = -1.0f;
				if (const auto* fl = FaceLabAPI::Get(); fl && fl->version >= 5) {
					faceTongue = fl->GetTongue(id);
				}
				const bool inScope = InScope(a, s);
				const bool asked = faceTongue > 0.05f && inScope;
				{
					const int code = faceTongue > 0.05f ? (!inScope ? 1 : (tongue ? 3 : 2)) : 0;
					std::lock_guard lk(g_askMx);
					if (code != 0 || g_askCode.contains(id)) {
						const auto prev = g_askCode.find(id);
						if (prev == g_askCode.end() ? code != 0 : prev->second != code) {  // always logged (not only with Debug): this is the answer to 'why is there no tongue'
							static const char* const why[] = { "no request", "asked, but this character is out of scope (Who settings)", "asked, waiting for a tongue to be put on", "asked, tongue is on" };
							logger::info("[{}] FaceLab tongue request ({:.2f}): {}", a->GetName(), faceTongue, why[std::clamp(code, 0, 3)]);
						}
						g_askCode[id] = code;
					}
				}
				if (asked) {
					st.askedAt = now;
				}
				if (tongue || !asked) {
					st.equipTries = 0;
				}
				if (!tongue && asked && st.equipTries < 3 && now - st.lastEquipTry > std::chrono::seconds(3)) {
					st.lastEquipTry = now;
					++st.equipTries;
					st.weEquipped = true;
					OStimObject("EquipObject", a);
					if (s.debug) {
						logger::info("[{}] a FaceLab face asks for the tongue ({:.2f}) and she has none: asking OStim to equip one", a->GetName(), faceTongue);
					}
				}
				const int wantModel = g_modelOverride.load() >= 0 ? g_modelOverride.load() : (s.tongueUse > 0 ? s.tongueUse - 1 : s.tongueModel);
				if (st.directItem != 0 && st.directModel != wantModel && asked) {  // another model was picked: take ours off, the code below puts the new one on
					auto* old = RE::TESForm::LookupByID<RE::TESObjectARMO>(st.directItem);
					auto* omgr = RE::ActorEquipManager::GetSingleton();
					if (old && omgr) {
						omgr->UnequipObject(a, old, nullptr, 1, nullptr, false, true, false, true);
						a->RemoveItem(old, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
						QueueNiNodeUpdate(a);  // a paused game (a menu is open) keeps the old model on screen until it is told to rebuild
					}
					logger::info("[{}] another tongue model was picked ({} -> {}): swapping", a->GetName(), st.directModel, wantModel);
					st.directItem = 0;
					st.lastDirectTry = Clock::time_point{};
					st.lastEquipTry = Clock::time_point{};
				}
				// No OStim scene (a screenshot pose, a preset applied from FaceLab's menu): OStim has nothing to equip, so after its one try we put a tongue on her ourselves,
				// the way the Tuning page's preview does, and take it off again 2.5 s after the face is over. Only a tongue WE added is ever removed.
				if (!tongue && asked && st.equipTries >= 1 && st.directItem == 0 && now - st.lastEquipTry > std::chrono::milliseconds(1500) && now - st.lastDirectTry > std::chrono::seconds(4)) {
					st.lastDirectTry = now;
					const auto forms = SortedForms();
					auto*      mgr = RE::ActorEquipManager::GetSingleton();
					auto*      armor = !forms.empty() ? RE::TESForm::LookupByID<RE::TESObjectARMO>(forms[static_cast<std::size_t>(std::clamp(wantModel, 0, static_cast<int>(forms.size()) - 1))]) : nullptr;
					if (armor && mgr && a->Get3D()) {
						a->AddObjectToContainer(armor, nullptr, 1, nullptr);
						mgr->EquipObject(a, armor, nullptr, 1, nullptr, false, true, false, true);
						st.directItem = armor->GetFormID();
						st.directModel = wantModel;
						st.directDiagDone = false;
						logger::info("[{}] no OStim scene: put a tongue on her ourselves ({} {:08X})", a->GetName(), armor->GetName(), armor->GetFormID());
					} else {
						logger::warn("[{}] a FaceLab face asks for the tongue but there is none to put on (tongue forms found: {})", a->GetName(), forms.size());
					}
				}
				// we put a tongue on her ourselves and 1.2 s later nothing is worn: say what the engine did (once per attempt)
				if (st.directItem != 0 && asked && !tongue && !st.directDiagDone && now - st.lastDirectTry > std::chrono::milliseconds(1200)) {
					st.directDiagDone = true;
					auto*       armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(st.directItem);
					std::size_t inInv = 0;
					if (armor) {
						const auto fid = st.directItem;
						inInv = a->GetInventory([fid](RE::TESBoundObject& o) { return o.GetFormID() == fid; }).size();
					}
					auto&       biped = a->GetBiped();
					std::string slots;
					if (biped) {
						for (auto idx : g_slots) {
							if (idx < static_cast<std::size_t>(RE::BIPED_OBJECTS::kTotal)) {
								auto& o = biped->objects[idx];
								slots += std::format(" [slot {}: item {:08X}, mesh {}]", idx, o.item ? o.item->GetFormID() : 0u, o.partClone ? "yes" : "no");
							}
						}
					}
					logger::warn("[{}] our tongue {} ({:08X}) is not showing 1.2 s after we equipped it: in her inventory {}, biped {}, slots:{}", a->GetName(), armor ? armor->GetName() : "?", st.directItem, inInv, biped ? "yes" : "NO", slots);
					QueueNiNodeUpdate(a);  // a paused game (a menu is open) does not build the model by itself
					logger::info("[{}] asked the engine to build her tongue's model now (it was not built, probably because the game is paused)", a->GetName());
				}
				if (st.directItem != 0 && !asked && now - st.askedAt > std::chrono::milliseconds(2500)) {
					auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(st.directItem);
					auto* mgr = RE::ActorEquipManager::GetSingleton();
					if (armor && mgr) {
						mgr->UnequipObject(a, armor, nullptr, 1, nullptr, false, true, false, true);
						a->RemoveItem(armor, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
						logger::info("[{}] the face is over: took our tongue off again", a->GetName());
					}
					st.directItem = 0;
				}
				// The tongue we asked OStim for is NOT taken off again when the face ends: equipping or removing armor makes the game rebuild her body (a visible flash),
				// the next scene may want that same tongue (masturbation equips one itself), and OStim removes its equip objects at scene end anyway.
				// ---- the scene swap: "Tongue to use" ----------------------------------------------------------------------------------------------------
				// A scene (OStim, SLO VE) equips ITS tongue. When a tongue was picked in the menu and the character wears a different one, we equip the picked one:
				// the engine puts the scene's tongue aside (it stays in her inventory, unworn) and the scene removes its own item when it ends. Ours is taken off once the
				// scene's item is gone, or when something else takes the slot. If another mod keeps putting its tongue back, we stop for a minute rather than fight it.
				if (s.tongueUse > 0 || st.swapItem != 0) {  // nothing to do for everyone else: this runs for every character every frame
					auto* swapMgr = RE::ActorEquipManager::GetSingleton();
					const auto forms = SortedForms();
					const int  wantIdx = (s.tongueUse > 0 && inScope && s.tongueUse - 1 < static_cast<int>(forms.size())) ? s.tongueUse - 1 : -1;
					const RE::FormID worn = WornTongueId(a);
					if (st.swapItem != 0 && now - st.lastSwapCheck > std::chrono::milliseconds(400)) {
						st.lastSwapCheck = now;
						const bool fromHere = st.swapFrom != 0 && PlayerHas(a, st.swapFrom);
						if (s.debug) {
							logger::info("[{}] swap check: worn {:08X}, ours {:08X}, scene's {:08X} {}", a->GetName(), worn, st.swapItem, st.swapFrom, fromHere ? "still in her inventory" : "gone from her inventory");
						}
						const char* why = nullptr;
						const bool  building = (worn == 0 && now - st.lastSwapTry < std::chrono::seconds(6)) || now - st.lastSwapTry < std::chrono::seconds(4);  // the new model is not built yet (nothing, or the old tongue, is still worn for a moment)
						if (building) {
							// wait
						} else if (worn != st.swapItem) {
							why = "something else took the slot (our tongue is no longer worn)";
							if (worn == 0) {
								why = "nothing is worn any more (the scene took the tongue off)";
							} else if (now - st.swapFightAt > std::chrono::seconds(30)) {
								st.swapFights = 0;
							}
							if (worn != 0) {  // only another tongue counts as a fight, not the scene taking everything off
								st.swapFightAt = now;
								++st.swapFights;
							}
							if (st.swapFights >= 2 && worn != 0) {
								st.swapBlockUntil = now + std::chrono::seconds(60);
								logger::warn("[{}] another mod keeps putting its own tongue on: not swapping her tongue for a minute", a->GetName());
							}
						} else if (st.swapFrom != 0 && !fromHere) {
							why = "the scene's own tongue is gone (the scene is over)";
						} else if (wantIdx < 0) {
							why = "no tongue is picked any more";
						}
						if (why) {
							const bool restore = wantIdx < 0 && fromHere && worn == st.swapItem;  // the pick was cleared mid-scene: give her the scene's tongue back
							DropOurTongue(a, st.swapItem);
							logger::info("[{}] took our swapped-in tongue off again: {}", a->GetName(), why);
							if (restore && swapMgr) {
								if (auto* back = RE::TESForm::LookupByID<RE::TESObjectARMO>(st.swapFrom)) {
									swapMgr->EquipObject(a, back, nullptr, 1, nullptr, false, true, false, true);
								}
							}
							st.swapItem = 0;
							st.swapFrom = 0;
							st.swapModel = -1;
							continue;
						}
					}
					const bool ownPreview = worn != 0 && worn == g_prevItem.load();  // the Position / Size page's preview tongue is not a scene tongue
					if (tongue && wantIdx >= 0 && swapMgr && st.directItem == 0 && !ownPreview && now >= st.swapBlockUntil && now - st.lastSwapTry > std::chrono::seconds(3)) {
						const RE::FormID wantId = forms[static_cast<std::size_t>(wantIdx)];
						if (st.swapItem != 0 && worn == st.swapItem && st.swapModel != wantIdx) {  // she wears ours but another one was picked since: change it
							auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(wantId);
							const auto oldOurs = st.swapItem;
							if (armor && wantId == st.swapFrom) {  // the pick is the very tongue the scene wore: put that one back and let go
								st.lastSwapTry = now;
								swapMgr->EquipObject(a, armor, nullptr, 1, nullptr, false, true, false, true);
								DropOurTongue(a, oldOurs);
								logger::info("[{}] the pick is the scene's own tongue: gave it back and took ours off", a->GetName());
								st.swapItem = 0;
								st.swapFrom = 0;
								st.swapModel = -1;
								continue;
							}
							if (armor) {
								st.lastSwapTry = now;
								a->AddObjectToContainer(armor, nullptr, 1, nullptr);
								swapMgr->EquipObject(a, armor, nullptr, 1, nullptr, false, true, false, true);
								if (oldOurs != wantId) {
									DropOurTongue(a, oldOurs);
								}
								st.swapItem = wantId;
								st.swapModel = wantIdx;
								logger::info("[{}] another tongue was picked: {:08X} -> {:08X}", a->GetName(), oldOurs, wantId);
							}
							continue;
						}
						if (worn != 0 && worn != wantId && worn != st.swapItem) {  // the scene's tongue is on, and it is not the one picked
							auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(wantId);
							if (armor) {
								st.lastSwapTry = now;
								st.swapFrom = worn;
								st.swapItem = wantId;
								st.swapModel = wantIdx;
								st.lastSwapCheck = now;
								a->AddObjectToContainer(armor, nullptr, 1, nullptr);
								swapMgr->EquipObject(a, armor, nullptr, 1, nullptr, false, true, false, true);
								logger::info("[{}] scene swap: she wears {:08X} ({}), the pick is {:08X} ({}): equipped the pick", a->GetName(), worn, TongueName(a), wantId, armor->GetName());
								continue;  // the model is rebuilt: the next tick works with the new tongue node
							}
						}
					}
				}
				if (!tongue) {
					if (st.wasPresent && s.debug) {
						logger::info("[{}] tongue gone (OStim took it off)", a->GetName());
					}
					st.wasPresent = false;
					st.shown = false;
					st.openFor = st.closedFor = 0.0f;
					st.checkedFor = nullptr;
					st.faceSeen = false;
					st.anchor = nullptr;
					st.slide = 0.0f;
					continue;
				}
				ApplyVisual(tongue, s, id);
				if (tongue != st.checkedFor) {  // a new tongue node: find out whether it can slide
					st.checkedFor = tongue;
					st.equippedAt = now;
					st.lastTry = now;
					st.reported = false;
					st.anchor = FindAnchor(tongue);
					st.slide = 0.0f;
					if (st.anchor) {
						st.restPos = st.anchor->local.translate;
						WorkOutAxis(st, a->GetName());
					}
					if (s.debug) {
						logger::info("[{}] tongue can{} slide  -  {}", a->GetName(), st.anchor ? "" : "not (yet)", TongueName(a));
					}
				} else if (!st.anchor && now - st.lastTry > std::chrono::milliseconds(250) && now - st.equippedAt < std::chrono::seconds(120)) {
					// the skin's bone pointers are filled in a little after the armor appears: keep looking for a while
					st.lastTry = now;
					st.anchor = FindAnchor(tongue);
					if (st.anchor) {
						st.restPos = st.anchor->local.translate;
						WorkOutAxis(st, a->GetName());
						if (st.shown) {
							st.slide = 1.0f;  // it was already out (shown without sliding): do not pull it back in just to slide out again
						}
						if (s.debug) {
							logger::info("[{}] tongue can slide (found {:.1f}s after it was equipped)", a->GetName(), std::chrono::duration<float>(now - st.equippedAt).count());
						}
					} else if (!st.reported && now - st.equippedAt > std::chrono::seconds(3)) {
						st.reported = true;
						logger::info("[{}] no slide bone on this tongue: {}", a->GetName(), Describe(tongue));
					}
				}
				if (!st.wasPresent) {
					st.wasPresent = true;
					st.shown = false;
					st.openFor = st.closedFor = 0.0f;
					if (s.debug) {
						logger::info("[{}] OStim equipped a tongue: held in until the mouth is open", a->GetName());
					}
				}
				if (!InScope(a, s)) {
					tongue->SetAppCulled(false);  // not ours to manage: OStim's behaviour
					if (st.anchor) {
						st.anchor->local.translate = st.restPos;
					}
					continue;
				}

				// Just after OStim equips the tongue the mouth can still hold the previous pose for a split second (the tongue would flash out
				// and back in), and a sliding tongue's bone is only ready 0.3-3 s later (without it the tongue pops in instead of sliding).
				// So keep it in for a moment, and for a sliding tongue until its bone is there, but never longer than the wait below.
				{
					const float sinceEquip = std::chrono::duration<float>(now - st.equippedAt).count();
					if (sinceEquip < 0.5f || (s.slide && !st.anchor && sinceEquip < s.slideWait)) {
						st.shown = false;
						st.openFor = st.closedFor = 0.0f;
						st.slide = 0.0f;
						tongue->SetAppCulled(true);
						{
							Diag dg{ a->GetName(), true, false, false, false, 0.0f, false, 0.0f };
							dg.id = a->GetFormID();
							diag.push_back(dg);
						}
						continue;
					}
				}

				const MouthInfo mi = Mouth(a, s.mouthSource, s.poseLevel);
				const int       kind = SpeechKindOf(id);
				const bool  speaking = (kind == 2 && s.hideDuringLines) || (kind == 1 && s.hideDuringMoans);
				if (speaking) {
					st.lastSpeech = now;
				}
				bool climax = false;
				{
					std::lock_guard lk(g_climaxMx);
					auto            it = g_climax.find(id);
					climax = it != g_climax.end() && now < it->second;
				}

				// OStim's own tongue expression is BigAah 100 + Eee 80. Moans and talking open the mouth with other shapes, so by default only
				// that pose counts (unless it is the climax window, when ahegao style expressions use other shapes).
				const bool externalTongue = g_externalForms.contains(WornTongueId(a));
				float      mouth = (s.mouthRule == 1 && !externalTongue && !(climax && s.climaxAnyMouth)) ? mi.pose : mi.open;

				bool wantOut = true;
				if (climax && s.climaxMode == 2) {
					wantOut = false;
				} else if (!(climax && s.climaxMode == 1)) {
					const bool recent = st.lastSpeech.time_since_epoch().count() != 0 && std::chrono::duration<float>(now - st.lastSpeech).count() < s.speechGrace;
					if (speaking || recent) {
						wantOut = false;
						st.speechHold = true;
					}
				}

				// FaceLab draws the visible mouth with its own morphs, which the MFG phonemes above know nothing about: it closes the mouth at a facial
				// climax and gives it back to a voice line. Once it has drawn this face, the tongue is only out while that mouth is open.
				float faceMouth = -1.0f;
				if (s.faceFollow && !asked) {
					if (const auto* fl = FaceLabAPI::Get(); fl && fl->version >= 3) {
						faceMouth = fl->GetMouthOpen(id);
						// follow FaceLab's mouth only while it is drawing a face for her; once the face is released the scene's own mouth and tongue pose rule again
						st.faceSeen = faceMouth >= 0.0f;
						if (st.faceSeen) {
							faceMouth = std::max(faceMouth, 0.0f);
							// the 'goes in' value is kept at least 0.15 below the 'comes out' value: a mouth value hovering between two almost equal limits would flip the tongue in and out for ever
							if (faceMouth < (st.shown ? std::min(s.faceClose, s.faceOpen - 0.15f) : s.faceOpen)) {
								wantOut = false;
							}
						}
					}
				}
				// A moan is lip-synced by a voice mod through the same phonemes the tongue pose is read from, so while one plays the pose flickers: a moan
				// leaves the tongue exactly as it is (FaceLab's mouth and the other rules above can still pull it in).
				if (asked) {  // the face wants the tongue out: nothing about the pose or the mouth matters (a spoken line still pulls it in, above)
					mouth = 1.0f;
				}
				const bool moanHold = kind == 1 && !s.hideDuringMoans && !asked;
				const bool before = st.shown;
				if (st.shown) {
					if (!wantOut) {
						st.shown = false;
						st.closedFor = 0.0f;
					} else if (!moanHold && mouth < std::min(s.closeThreshold, s.openThreshold - 0.10f)) {
						st.closedFor += dt;
						if (st.closedFor >= s.hideDelay) {
							st.shown = false;
							st.closedFor = 0.0f;
						}
					} else {
						st.closedFor = 0.0f;
					}
					st.openFor = 0.0f;
				} else {
					if (wantOut && !moanHold && mouth >= s.openThreshold) {
						st.openFor += dt;
						if (st.openFor >= s.showDelay) {
							st.shown = true;
							st.openFor = 0.0f;
						}
					} else {
						st.openFor = 0.0f;
					}
				}
				// A tongue that has just come out (or gone in) stays that way for a moment: a mouth value hovering around a threshold otherwise makes it flutter in and out.
				// A spoken line, or a face asking for the tongue, still acts at once.
				if (before != st.shown) {
					if (!speaking && !asked && st.changedAt.time_since_epoch().count() != 0 && std::chrono::duration<float>(now - st.changedAt).count() < 0.8f) {
						st.shown = before;
					} else {
						st.changedAt = now;
					}
				}
				if (s.debug && now - st.lastBeat > std::chrono::seconds(1)) {
					st.lastBeat = now;
					logger::info("BEAT [{}] shown={} wantOut={} mouth={:.2f} (open>={:.2f}) pose={:.2f} faceLab={:.2f} seen={} speaking={} recent={} anchor={} sinceEquip={:.1f}s", a->GetName(), st.shown, wantOut, mouth, s.openThreshold, mi.pose, faceMouth, st.faceSeen, speaking, st.lastSpeech.time_since_epoch().count() != 0 && std::chrono::duration<float>(now - st.lastSpeech).count() < s.speechGrace, st.anchor != nullptr, std::chrono::duration<float>(now - st.equippedAt).count());
				}
				if (g_previewHold.load()) {
					st.shown = true;  // testing aid: keep it out while the sliders are being tried
					st.speechHold = false;
				} else if (g_previewPull.load()) {
					st.shown = false;  // testing aid: sit at the pulled-back place, visible, so 'pulls back into the mouth by' can be seen while it is dragged
					st.speechHold = false;
					st.slide = 0.0f;
				}
				bool visible = st.shown;
				FreezeForEdit(a, st);
				if (st.anchor && s.slide) {
					// slide: out while it is wanted, back in otherwise; the node moves along the head's -Y, i.e. into the mouth
					const float inSec = st.speechHold ? s.speechIn : s.slideIn;
					const float outSec = st.speechHold ? s.speechOut : s.slideOut;
					const float step = dt / std::max(0.02f, st.shown ? outSec : inSec);
					st.slide = st.shown ? std::min(1.0f, st.slide + step) : std::max(0.0f, st.slide - step);
					if (st.shown && st.slide >= 1.0f) {
						st.speechHold = false;
					}
					visible = st.shown || st.slide > 0.0f;
					{  // a tongue a FaceLab face asks for goes only as far as that face says (and further above 1.0); eased so it never jumps
						const float k = std::min(1.0f, dt / 0.3f);
						st.reach += ((asked ? std::clamp(faceTongue, 0.0f, 1.0f) : 1.0f) - st.reach) * k;
						st.over += ((asked ? std::max(0.0f, faceTongue - 1.0f) : 0.0f) - st.over) * k;
						st.askBlend += ((asked ? 1.0f : 0.0f) - st.askBlend) * k;
					}
					const float eased = Ease(st.slide, s.slideEase) * st.reach;
					const float back = 1.0f - eased;
					const float out = eased;  // fully out = 1: the "extended" offsets apply, fully in = the "retracted" ones
					st.anchor->local.translate = st.restPos + AxisMap(st, s.extendX * out + s.slideX * back + s.askX * st.askBlend * eased,
						s.extendY * out - s.slideDistance * back + (st.over * s.askOver + s.askY * st.askBlend) * eased,
						s.extendZ * out + s.slideZ * back + s.askZ * st.askBlend * eased);
					PushIfEditing(st.anchor, tongue);
				} else if (st.anchor) {
					st.anchor->local.translate = st.restPos + AxisMap(st, s.extendX, s.extendY, s.extendZ);
					PushIfEditing(st.anchor, tongue);
					st.slide = st.shown ? 1.0f : 0.0f;
				}
				if (g_previewPull.load() && !g_previewHold.load()) {
					visible = true;
				}
				if (!st.anchor && !visible && now - st.equippedAt < std::chrono::seconds(9)) {
					// A tongue that is hidden (held in until the mouth opens) is never bound to its skeleton by the engine: its skin has no bones, so the slide bone cannot be
					// found, and a mouth that stays shut for a while leaves the tongue popping instead of sliding for good. So show it for a moment, a few times, right after it
					// appears: the bone is there about 0.2 s later and the slide works whenever the mouth does open.
					if (now >= st.nextPeek && now >= st.peekUntil) {
						st.peekUntil = now + std::chrono::milliseconds(450);
						st.nextPeek = now + std::chrono::milliseconds(2500);
					}
					if (now < st.peekUntil) {
						visible = true;
					}
				}
				tongue->SetAppCulled(!visible);

				if (s.debug && before != st.shown) {
					logger::info("[{}] tongue {} (judged {:.2f}: open {:.2f} pose {:.2f} [BigAah {:.2f} Eee {:.2f} Aah {:.2f}] other-source {:.2f}, speaking={} kind={}, climax={}, slide={}, faceLab mouth={:.2f}{})", a->GetName(), st.shown ? "OUT" : "IN", mouth, mi.open, mi.pose, mi.bigAah, mi.eee, mi.aah, mi.other, speaking, kind, climax, st.anchor ? "yes" : "no", faceMouth, st.faceSeen ? " (followed)" : "");
				}
				{
					Diag dg{ a->GetName(), true, st.shown, speaking, climax, mouth, st.anchor != nullptr, st.slide };
					dg.noSlideBone = st.reported && st.anchor == nullptr;
					dg.faceMouth = faceMouth;
					dg.faceFollowed = st.faceSeen;
					dg.asked = asked;
					dg.id = id;
					diag.push_back(dg);
				}
			}
			std::lock_guard lk(g_diagMx);
			g_diag = std::move(diag);
		}

		class EventSink : public RE::BSTEventSink<SKSE::ModCallbackEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
			{
				if (a_event && std::string_view(a_event->eventName.c_str()) == "ostim_actor_orgasm"sv) {
					if (auto* a = a_event->sender ? a_event->sender->As<RE::Actor>() : nullptr) {
						std::lock_guard lk(g_climaxMx);
						g_climax[a->GetFormID()] = Clock::now() + std::chrono::milliseconds(static_cast<int>(Config::Get().climaxSeconds * 1000.0f));
						if (Config::Get().debug) {
							logger::info("[{}] orgasm: climax window opened", a->GetName());
						}
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		EventSink g_sink;
	}

	void ShutdownRestore()
	{
		for (auto& h : g_actors) {
			if (auto ptr = h.get()) {
				if (auto* t = FindTongue(ptr.get())) {
					t->SetAppCulled(false);
					auto& st = g_state[ptr->GetFormID()];
					if (st.anchor && st.checkedFor == t) {
						st.anchor->local.translate = st.restPos;
					}
				}
			}
		}
		for (auto& [id, st] : g_state) {
			st.shown = false;
		}
	}

	std::vector<Nearby> NearbyList()
	{
	std::lock_guard lk(g_nearMx);
	return g_near;
	}

	std::vector<Diag> Snapshot()
	{
		std::lock_guard lk(g_diagMx);
		return g_diag;
	}

	// Does the tongue's mesh file carry the slide bone ("tong1 1", the one TongueLab moves)? Read from the file once per tongue: Halo's tongues have it, OStim's own stock
	// tongues do not (they are physics tongues on the head bone), so those can only pop in and out. -1 = could not tell (a mesh packed in an archive).
	static int MeshHasSlideBone(const RE::TESObjectARMO* a_armor)
	{
		static std::mutex                           s_mx;
		static std::unordered_map<RE::FormID, int> s_cache;
		if (!a_armor) {
			return -1;
		}
		std::lock_guard lk(s_mx);
		if (const auto it = s_cache.find(a_armor->GetFormID()); it != s_cache.end()) {
			return it->second;
		}
		int result = -1;
		for (auto* arma : a_armor->armorAddons) {
			if (!arma || result != -1) {
				continue;
			}
			for (int sex = 0; sex < 2 && result == -1; ++sex) {
				const char* model = arma->bipedModels[sex].GetModel();
				if (!model || !*model) {
					continue;
				}
				std::ifstream f(std::string("Data/Meshes/") + model, std::ios::binary);
				if (!f) {
					continue;
				}
				const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
				result = bytes.find("tong1 1") != std::string::npos ? 1 : 0;
			}
		}
		s_cache[a_armor->GetFormID()] = result;
		return result;
	}

	std::vector<std::string> TongueChoices()
	{
		std::vector<std::string> names;
		int                      n = 0;
		for (auto id : SortedForms()) {
			const auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(id);
			std::string nm = armor && armor->GetName() && *armor->GetName() ? armor->GetName() : "(unnamed)";
			if (MeshHasSlideBone(armor) == 0) {
				nm += "  (stock mesh: cannot slide)";
			}
			names.push_back(std::format("{}: {}", ++n, nm));
		}
		return names;
	}

	std::vector<TongueChoice> TongueChoiceList()
	{
		std::vector<TongueChoice> out;
		int                       n = 0, idx = 0;
		for (auto id : SortedForms()) {
			const auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(id);
			std::string nm = armor && armor->GetName() && *armor->GetName() ? armor->GetName() : "(unnamed)";
			if (armor && (!armor->GetName() || !*armor->GetName())) {
				// no display name (SLO VE's tongue armors): say which mod and which mesh instead
				const auto  key = TongueMeshKey(const_cast<RE::TESObjectARMO*>(armor));
				const auto* file = armor->GetFile(0);
				const auto  slash = key.rfind('\\');
				std::string mesh = key.empty() ? std::string{} : key.substr(slash == std::string::npos ? 0 : slash + 1);
				if (mesh.size() > 4 && mesh.ends_with(".nif")) {
					mesh.resize(mesh.size() - 4);
				}
				nm = std::string(file ? file->GetFilename() : "?") + " " + (mesh.empty() ? "tongue" : mesh);
			}
			out.push_back({ std::format("{}: {}", ++n, nm), idx++, MeshHasSlideBone(armor) != 0 });
		}
		return out;
	}

	int WornTongueIndex(std::uint32_t a_actorId)
	{
		auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_actorId);
		if (!actor) {
			return -1;
		}
		const auto id = WornTongueId(actor);
		if (!id) {
			return -1;
		}
		const auto forms = SortedForms();
		for (std::size_t i = 0; i < forms.size(); ++i) {
			if (forms[i] == id) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	int PreviewIndex()
	{
		const auto id = g_prevItem.load();
		if (!id) {
			return -1;
		}
		const auto forms = SortedForms();
		for (std::size_t i = 0; i < forms.size(); ++i) {
			if (forms[i] == id) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void PreviewTongue(bool a_on, int a_index, std::uint32_t a_actorId)
	{
		SKSE::GetTaskInterface()->AddTask([a_on, a_index, a_actorId]() {
			RemovePreview("changed");
			if (!a_on) {
				return;
			}
			const auto forms = SortedForms();
			RE::Actor*  pc = a_actorId == 0x14 ? RE::PlayerCharacter::GetSingleton() : RE::TESForm::LookupByID<RE::Actor>(a_actorId);
			auto*      mgr = RE::ActorEquipManager::GetSingleton();
			if (forms.empty() || a_index < 0 || a_index >= static_cast<int>(forms.size()) || !pc || !mgr || !pc->Get3D()) {
				logger::warn("preview tongue: nothing to put on (tongues found: {}, picked {})", forms.size(), a_index);
				return;
			}
			auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(forms[a_index]);
			if (!armor) {
				return;
			}
			if (!PlayerHas(pc, forms[a_index])) {
				pc->AddObjectToContainer(armor, nullptr, 1, nullptr);
			}
			mgr->EquipObject(pc, armor, nullptr, 1, nullptr, false, true, false, true);
			g_prevActor = pc->GetFormID();
			g_prevItem = forms[a_index];
			g_prevOn = true;
			logger::info("preview tongue put on {}: {} ({:08X})", pc->GetName(), armor->GetName(), forms[a_index]);
			if (a_actorId != 0x14) {
				// another character's model is not built while the menu has the game paused: build it a moment later (the player's is built at once)
				std::thread([a_actorId]() {
					std::this_thread::sleep_for(std::chrono::milliseconds(1200));
					SKSE::GetTaskInterface()->AddTask([a_actorId]() {
						if (auto* who = RE::TESForm::LookupByID<RE::Actor>(a_actorId); who && g_prevOn.load() && g_prevActor.load() == a_actorId) {
							QueueNiNodeUpdate(who);
						}
					});
				}).detach();
			}
		});
	}

	int AskStatus(unsigned int a_formID)
	{
	if (const int g = g_askGlobal.load(); g < 0) {
		return g;
	}
	std::lock_guard lk(g_askMx);
	const auto it = g_askCode.find(a_formID);
	return it == g_askCode.end() ? 0 : it->second;
	}

	void SetModelOverride(int a_index)
	{
	if (g_modelOverride.exchange(a_index) != a_index) {
		logger::info("tongue model asked for by another plugin: {}", a_index);
	}
	}

	int ModelOverride()
	{
	return g_modelOverride.load();
	}

	bool PreviewTongueOn()
	{
		return g_prevOn.load();
	}

	void SetPreviewPull(bool a_on)
	{
	if (g_previewPull.exchange(a_on) != a_on) {
		logger::info("preview pull-back {}: every equipped tongue is {}", a_on ? "ON" : "off", a_on ? "held at its pulled-back place" : "back to the normal rules");
	}
	if (a_on) {
		g_previewHold = false;
	}
	}

	bool PreviewPull()
	{
	return g_previewPull.load();
	}

	void SetPreviewHold(bool a_on)
	{
		if (a_on) {
			g_previewPull = false;
		}
		if (g_previewHold.exchange(a_on) != a_on) {
			logger::info("preview hold {}: every equipped tongue is {}", a_on ? "ON" : "off", a_on ? "held out" : "back to the normal rules");
		}
	}

	bool PreviewHold()
	{
		return g_previewHold.load();
	}

	void NoteEditing(std::uint32_t a_character)
	{
		g_editChar = a_character;
		g_editingMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
	}

	VisualInfo Visual()
	{
		return { g_visOk.load(), g_visFail.load() };
	}

	std::size_t FormCount()
	{
		return g_forms.size();
	}

	void Init()
	{
		ScanEquipObjects();
		FindSpeechProviders();
		if (auto* src = SKSE::GetModCallbackEventSource()) {
			src->AddEventSink(&g_sink);
		}
		g_last = Clock::now();
		g_ticker = std::jthread([](std::stop_token a_stop) {
			while (!a_stop.stop_requested()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
				if (!g_queued.exchange(true)) {
					SKSE::GetTaskInterface()->AddTask([]() { Tick(); });
				}
			}
		});
	}
}

// Exports for other plugins (FaceLab's Tongue assist): the list of tongue models and which one TongueLab puts on a character itself.
extern "C" __declspec(dllexport) int TongueLab_TongueCount()
{
	return static_cast<int>(Tongue::FormCount());
}

extern "C" __declspec(dllexport) int TongueLab_TongueName(int a_index, char* a_out, int a_cap)
{
	const auto names = Tongue::TongueChoices();
	if (a_index < 0 || a_index >= static_cast<int>(names.size()) || !a_out || a_cap < 1) {
		return 0;
	}
	std::snprintf(a_out, static_cast<std::size_t>(a_cap), "%s", names[static_cast<std::size_t>(a_index)].c_str());
	return 1;
}

extern "C" __declspec(dllexport) void TongueLab_SetTongueModel(int a_index)
{
	Tongue::SetModelOverride(a_index);
}

extern "C" __declspec(dllexport) int TongueLab_GetTongueModel()
{
	return Tongue::ModelOverride();
}

// What TongueLab makes of FaceLab's tongue request for this character (FaceLab shows it under its tongue button). See g_askCode in Tongue.cpp for the codes.
extern "C" __declspec(dllexport) int TongueLab_AskStatus(unsigned int a_formID)
{
	return Tongue::AskStatus(a_formID);
}

// the generic name a face mod looks for (it scans the loaded modules, so it never needs to know this mod's name): same answer as TongueLab_AskStatus
extern "C" __declspec(dllexport) int FaceLab_TongueStatus(unsigned int a_formID)
{
	return Tongue::AskStatus(a_formID);
}
