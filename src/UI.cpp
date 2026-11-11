#include "PCH.h"

#include "Config.h"
#include "Loc.h"
#include "Npc.h"
#include "NpcCore.h"
#include "SKSEMenuFramework.h"
#include "Tongue.h"
#include "UI.h"

#include <atomic>
#include <cstdarg>
#include <chrono>
#include <map>
#include <mutex>

namespace G = ImGuiMCP;

// "TongueLab" page in SKSE Menu Framework. Every change is applied at once and written to TongueLab.toml.
namespace UI
{
	namespace
	{
		int g_saveState = 0;  // 0 nothing, 1 saved, 2 failed

		// ---- what the pages edit: everyone (TongueLab.toml), the generic NPCs (_generic.toml) or one character (her own file) ----------------
		int                    g_scope = 2;           // 0 everyone, 1 generic NPCs, 2 one character (the default: you, until another is picked)
		std::uint32_t          g_charId = 0x14;       // the character of scope 2 (the player by default)
		Config::Settings       g_scratch;             // what the scoped pages edit
		bool                   g_scratchValid = false;
		bool                   g_pageScoped = false;  // each page says whether its widgets edit the scoped copy
		std::atomic<bool>      g_dirtyG{ false };     // TongueLab.toml has unsaved edits
		std::atomic<bool>      g_dirtyS{ false };     // the scoped file has unsaved edits
		std::atomic<long long> g_lastMs{ 0 };
		std::mutex             g_saveMx;

		long long NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		RE::Actor* ScopeActor()
		{
			return RE::TESForm::LookupByID<RE::Actor>(g_charId);
		}

		std::string ScopeStem()
		{
			if (g_scope == 1) {
				return "_generic";
			}
			auto* a = ScopeActor();
			return a ? Npc::KeyOf(a->GetActorBase()) : std::string{};
		}

		bool Scoped(bool glob) { return g_pageScoped && !glob && g_scope != 0; }

		Config::Settings& Edit()
		{
			if (g_pageScoped && g_scope != 0) {
				if (!g_scratchValid) {
					g_scratch = Npc::Build(ScopeActor(), Config::Get(), ScopeStem(), true);
					g_scratchValid = true;
				}
				return g_scratch;
			}
			return Config::Edit();
		}

		// a setting was changed: draw it at once (for the scoped copy through a live layer) and write it once it has been still for a moment
		void Changed(bool glob)
		{
			g_lastMs = NowMs();
			if (!Scoped(glob)) {
				g_dirtyG = true;
				return;
			}
			const auto stem = ScopeStem();
			if (stem.empty()) {
				g_saveState = 2;
				return;
			}
			const auto base = Npc::Build(ScopeActor(), Config::Get(), stem, false);
			Npc::SetLive(stem, NpcCore::Diff(base, g_scratch));
			g_dirtyS = true;
		}

		void FlushNow(bool force)
		{
			if (!g_dirtyG.load() && !g_dirtyS.load()) {
				return;
			}
			if (!force && NowMs() - g_lastMs.load() < 400) {
				return;
			}
			std::lock_guard lk(g_saveMx);
			if (g_dirtyG.exchange(false)) {
				g_saveState = Config::Save() ? 1 : 2;
			}
			if (g_dirtyS.exchange(false)) {
				const auto        stem = ScopeStem();
				auto*             a = ScopeActor();
				const auto        base = Npc::Build(a, Config::Get(), stem, false);
				const std::string name = g_scope == 1 ? "generic NPCs" : (a && a->GetName() ? a->GetName() : "");
				g_saveState = Npc::Save(stem, NpcCore::Diff(base, g_scratch), name) ? 1 : 2;
			}
		}

		// ---- translated drawing: every text goes through Loc (English falls through unchanged) ------------------------------------------
		std::string VFmt(const char* fmt, va_list ap)
		{
			va_list c;
			va_copy(c, ap);
			const int n = std::vsnprintf(nullptr, 0, fmt, c);
			va_end(c);
			std::string out(n > 0 ? static_cast<std::size_t>(n) : 0, '\0');
			if (n > 0) {
				std::vsnprintf(out.data(), out.size() + 1, fmt, ap);
			}
			return out;
		}

		void Text(const char* fmt, ...)
		{
			va_list ap;
			va_start(ap, fmt);
			const auto s = VFmt(Loc::T(fmt), ap);
			va_end(ap);
			G::TextUnformatted(s.c_str());
		}

		void TextWrapped(const char* fmt, ...)
		{
			va_list ap;
			va_start(ap, fmt);
			const auto s = VFmt(Loc::T(fmt), ap);
			va_end(ap);
			G::TextWrapped("%s", s.c_str());
		}

		void TextCol(const G::ImVec4 col, const char* fmt, ...)
		{
			va_list ap;
			va_start(ap, fmt);
			const auto s = VFmt(Loc::T(fmt), ap);
			va_end(ap);
			G::TextColored(col, "%s", s.c_str());
		}

		void SeparatorText(const char* label) { G::SeparatorText(Loc::T(label)); }
		bool Button(const char* label) { return G::Button(Loc::L(label)); }
		bool Checkbox(const char* label, bool* v) { return G::Checkbox(Loc::L(label), v); }

		// items are English texts: translated
		bool Combo(const char* label, int* cur, const char* const* items, int n)
		{
			std::vector<const char*> t;
			for (int i = 0; i < n; ++i) {
				t.push_back(Loc::T(items[i]));
			}
			return G::Combo(Loc::L(label), cur, t.data(), n);
		}

		// items are names that come from the game (characters, tongues): only the label is translated
		bool ComboNames(const char* label, int* cur, const char* const* items, int n) { return G::Combo(Loc::L(label), cur, items, n); }

		void Tip(const char* a_text)
		{
			if (a_text && *a_text && G::IsItemHovered()) {
				G::SetTooltip("%s", Loc::T(a_text));
			}
		}

		void Check(const char* a_label, bool& v, const char* a_tip, bool glob = false)
		{
			if (Checkbox(a_label, &v)) {
				Changed(glob);
			}
			Tip(a_tip);
		}

		void Slider(const char* a_label, float& v, float lo, float hi, const char* a_fmt, const char* a_tip, bool glob = false)
		{
			G::PushItemWidth(320.0f);
			if (G::SliderFloat(Loc::L(a_label), &v, lo, hi, Loc::T(a_fmt))) {
				Changed(glob);
			}
			G::PopItemWidth();
			Tip(a_tip);
		}

		void Choice(const char* a_label, int& v, const char* const* a_items, int a_count, const char* a_tip, bool glob = false)
		{
			G::PushItemWidth(320.0f);
			if (Combo(a_label, &v, a_items, a_count)) {
				Changed(glob);
			}
			G::PopItemWidth();
			Tip(a_tip);
		}

		void SaveNote()
		{
			FlushNow(false);
			if (g_saveState == 1) {
				TextCol(G::ImVec4{ 0.45f, 0.90f, 0.45f, 1.0f }, "Saved.");
			} else if (g_saveState == 2) {
				TextCol(G::ImVec4{ 1.0f, 0.4f, 0.4f, 1.0f }, "Could not write the settings file (see TongueLab.log).");
			}
		}

		void SetScope(int a_scope, std::uint32_t a_id)
		{
			FlushNow(true);
			Npc::ClearLive(ScopeStem());
			g_scope = a_scope;
			g_charId = a_id;
			g_scratchValid = false;
		}

		// Who the Size, Timing and Position pages edit is picked here, on the Tongue Select page
		void ScopePicker()
		{
			g_pageScoped = true;
			static const char* items[] = { "One character", "Everyone", "Generic NPCs" };
			static const int   toScope[] = { 2, 0, 1 };  // menu order -> the scope numbers used inside (0 everyone, 1 generic, 2 one character)
			int                shownIdx = g_scope == 2 ? 0 : (g_scope == 0 ? 1 : 2);
			G::PushItemWidth(320.0f);
			const bool scopeChanged = Combo("Who", &shownIdx, items, 3);
			const int  sc = toScope[std::clamp(shownIdx, 0, 2)];
			G::PopItemWidth();
			Tip("Everyone = TongueLab.toml. Generic NPCs (guards, bandits) share one file. One character gets a file of her own.");
			std::uint32_t id = g_charId;
			if (sc == 2) {
				std::vector<std::pair<std::string, std::uint32_t>> chars;
				auto*                                              pc = RE::PlayerCharacter::GetSingleton();
				chars.push_back({ std::string(pc && pc->GetName() ? pc->GetName() : Loc::T("You")) + Loc::T(" (you)"), 0x14 });
				for (auto& d : Tongue::Snapshot()) {
					if (d.id && d.id != 0x14) {
						chars.push_back({ d.name, d.id });
					}
				}
				for (auto& n : Tongue::NearbyList()) {  // everyone near you, tongue or not
					if (n.id && n.id != 0x14 && std::ranges::none_of(chars, [&](const auto& c) { return c.second == n.id; })) {
						chars.push_back({ n.name, n.id });
					}
				}
				int idx = -1;
				for (int i = 0; i < static_cast<int>(chars.size()); ++i) {
					if (chars[i].second == g_charId) {
						idx = i;
					}
				}
				if (idx < 0) {
					idx = 0;
					id = 0x14;
				}
				std::vector<const char*> ptrs;
				for (auto& c : chars) {
					ptrs.push_back(c.first.c_str());
				}
				G::PushItemWidth(420.0f);
				if (ComboNames("Character", &idx, ptrs.data(), static_cast<int>(ptrs.size()))) {
					id = chars[idx].second;
				}
				G::PopItemWidth();
				Tip("You, anyone near you, or anyone with a tongue on now.");
			}
			if (scopeChanged || id != g_charId) {
				SetScope(sc, id);
			}
			if (g_scope != 0 && ScopeStem().empty()) {
				TextCol(G::ImVec4{ 1.0f, 0.4f, 0.4f, 1.0f }, "This character has no plugin, so she cannot have settings of her own.");
			}
		}

		// top line of Size, Timing and Position: who they are editing
		void EditingLine()
		{
			g_pageScoped = true;
			auto*       who = g_scope == 2 ? ScopeActor() : nullptr;
			std::string name = g_scope == 0 ? Loc::T("everyone") : (g_scope == 1 ? Loc::T("generic NPCs") : (who && who->GetName() && *who->GetName() ? who->GetName() : Loc::T("one character")));
			TextCol(G::ImVec4{ 0.55f, 0.85f, 1.0f, 1.0f }, "Editing: %s", name.c_str());
			G::Separator();
		}

		// ---- 1. Tongue Select: who, which tongue, preview -----------------------------------------------------------------------------------
		void __stdcall PageSelect()
		{
			ScopePicker();
			auto& s = Edit();
			Tongue::NoteEditing();
			std::vector<Tongue::TongueChoice> list = Tongue::TongueChoiceList();
			for (auto& t : list) {
				if (!t.slides) {
					t.name += Loc::T("  (cannot slide)");
				}
			}
			if (list.empty()) {
				TextWrapped("No tongues were found. Install Halo's HDT Tongues (see the README).");
				SaveNote();
				return;
			}
			const std::uint32_t prevId = (g_scope == 2 && g_charId != 0) ? g_charId : 0x14;
			std::string         prevName = Loc::T("my character");
			if (prevId != 0x14) {
				if (auto* act = RE::TESForm::LookupByID<RE::Actor>(prevId); act && act->GetName() && *act->GetName()) {
					prevName = act->GetName();
				}
			}
			// the preview shows the tongue picked here; with none picked, the one the character wears now, else the first
			const auto previewIndex = [&]() {
				if (s.tongueUse > 0) {
					return std::clamp(s.tongueUse - 1, 0, static_cast<int>(list.size()) - 1);
				}
				const int worn = Tongue::WornTongueIndex(prevId);
				return worn >= 0 ? worn : 0;
			};
			const bool previewOn = Tongue::PreviewTongueOn();

			SeparatorText("Tongue");
			std::vector<std::string> shown{ Loc::T("Not chosen (the scene's own tongue)") };
			for (auto& t : list) {
				shown.push_back(t.name);
			}
			std::vector<const char*> ptrs;
			for (auto& n : shown) {
				ptrs.push_back(n.c_str());
			}
			int pick = std::clamp(s.tongueUse, 0, static_cast<int>(shown.size()) - 1);
			G::PushItemWidth(320.0f);
			if (ComboNames("Tongue", &pick, ptrs.data(), static_cast<int>(ptrs.size()))) {
				s.tongueUse = pick;
				Changed(false);
				if (previewOn) {
					Tongue::PreviewTongue(true, previewIndex(), prevId);
				}
			}
			G::PopItemWidth();
			Tip("In a scene this tongue replaces the one OStim or SLO VE equips. With no scene it is the tongue FaceLab shows.");

			SeparatorText("Preview");
			bool        on = previewOn;
			std::string chkText = Loc::T("Show this tongue on {}");
			if (const auto at = chkText.find("{}"); at != std::string::npos) {
				chkText.replace(at, 2, prevName);
			}
			chkText += "###prev";
			if (G::Checkbox(chkText.c_str(), &on)) {
				Tongue::PreviewTongue(on, previewIndex(), prevId);
				Tongue::SetPreviewHold(on);
			}
			Tip("A preview only: it comes off when you untick it or a minute after you close the menu. Untick it before you save.");
			{
				bool hold = Tongue::PreviewHold();
				if (Checkbox("Keep the tongue out while I edit", &hold)) {
					Tongue::SetPreviewHold(hold);
				}
				bool pull = Tongue::PreviewPull();
				if (Checkbox("Keep the tongue in the mouth while I edit", &pull)) {
					Tongue::SetPreviewPull(pull);
				}
				Tip("Testing aids, not saved.");
			}
			{
				auto& s = Config::Edit();  // the general switches are global, not the picked character's
				SeparatorText("General");
				Check("Take over the OStim tongue", s.enabled, "Off = OStim shows and hides the tongue itself.", true);
				Check("The player", s.player, "", true);
				G::SameLine();
				Check("Everyone else", s.others, "", true);
				Check("Female actors", s.females, "", true);
				G::SameLine();
				Check("Male actors", s.males, "", true);
				{
					// language: "Automatic" follows the game's language; the rest are the files in TongueLab/Languages
					static std::vector<std::string>              langs;
					static std::chrono::steady_clock::time_point lastScan{};
					const auto                                   nowT = std::chrono::steady_clock::now();
					if (langs.empty() || nowT - lastScan > std::chrono::seconds(3)) {
						langs = Loc::Available();
						lastScan = nowT;
					}
					std::vector<std::string> shown{ Loc::T("Automatic (the game's language)") };
					shown.insert(shown.end(), langs.begin(), langs.end());
					int pick = 0;
					if (Config::Get().language != "auto") {
						for (std::size_t i = 0; i < langs.size(); ++i) {
							if (langs[i] == Config::Get().language) {
								pick = static_cast<int>(i) + 1;
							}
						}
					}
					std::vector<const char*> ptrs;
					for (auto& n : shown) {
						ptrs.push_back(n.c_str());
					}
					G::PushItemWidth(320.0f);
					if (ComboNames("Language", &pick, ptrs.data(), static_cast<int>(ptrs.size()))) {
						s.language = pick == 0 ? std::string("auto") : langs[static_cast<std::size_t>(pick) - 1];
						Loc::Set(s.language);
						Changed(true);
					}
					G::PopItemWidth();
					Tip("Languages are the .toml files in Data/SKSE/Plugins/TongueLab/Languages. Automatic uses the game's language.");
					if (const auto need = Loc::FontAdvice(); !need.empty()) {
						TextCol(G::ImVec4{ 1.0f, 0.8f, 0.3f, 1.0f }, "This language needs the font '%s' in SKSE Menu Framework (its settings page, Font), otherwise its letters show as ????. Restart the game after changing it.", need.c_str());
					}
				}
				static bool resetArmed = false;
				if (!resetArmed) {
					if (Button("Reset ALL settings to default")) {
						resetArmed = true;
					}
				} else {
					TextCol(G::ImVec4{ 1.0f, 0.7f, 0.3f, 1.0f }, "This puts every TongueLab setting back to the shipped defaults.");
					if (Button("Yes, reset everything")) {
						s = Config::Settings{};
						resetArmed = false;
						Changed(true);
					}
					G::SameLine();
					if (Button("Cancel")) {
						resetArmed = false;
					}
				}
			}
			SaveNote();
		}

		// ---- 2. Size ----------------------------------------------------------------------------------------------------------------------------
		void __stdcall PageSize()
		{
			EditingLine();
			auto& s = Edit();
			auto& g = Config::Edit();
			Tongue::NoteEditing(g_scope == 2 ? g_charId : 0);
			Check("Change the tongue's shape", g.visual, "Off = the tongue exactly as shipped.", true);
			Slider("Size", s.visSize, -60.0f, 100.0f, "%+.0f %%", "");
			Slider("Length", s.visLength, -60.0f, 150.0f, "%+.0f %%", "");
			Slider("Width", s.visWidth, -60.0f, 100.0f, "%+.0f %%", "");
			Slider("Thickness", s.visThickness, -60.0f, 100.0f, "%+.0f %%", "");
			Slider("Tilt", s.visTilt, -45.0f, 45.0f, "%+.0f deg", "Tip up (+) or down (-).");
			if (Button("Reset")) {
				s.visSize = s.visLength = s.visWidth = s.visThickness = s.visTilt = 0.0f;
				Changed(false);
			}
			G::SameLine();
			if (Button("HALOS preset")) {
				s.visSize = 0.0f;
				s.visLength = -21.0f;
				s.visWidth = -5.0f;
				s.visThickness = -27.0f;
				s.visTilt = 0.0f;
				Changed(false);
			}
			Tip("The HALOS author's size preset.");
			SaveNote();
		}

		// ---- 3. Timing --------------------------------------------------------------------------------------------------------------------------
		void __stdcall PageTiming()
		{
			EditingLine();
			auto& s = Edit();
			auto& g = Config::Edit();
			SeparatorText("Mouth");
			{
				// what the tongue is judging right now, so the numbers below can be set against something you can see
				const std::uint32_t want = g_scope == 2 ? g_charId : 0;
				bool                found = false;
				for (const auto& d : Tongue::Snapshot()) {
					if (want && d.id != want) {
						continue;
					}
					found = true;
					G::ProgressBar(std::clamp(d.mouth, 0.0f, 1.0f), G::ImVec2{ 320.0f, 0.0f }, std::format("{:.2f}", d.mouth).c_str());
					if (d.asked) {
						Text("%s: a FaceLab face asks for the tongue, so the mouth does not matter.", d.name.c_str());
					} else if (d.faceFollowed) {
						Text("%s: FaceLab draws her face, so its mouth counts: %.2f (the tongue comes out at %.2f or more).", d.name.c_str(), d.faceMouth, s.faceOpen);
					} else {
						Text("%s: mouth %.2f now. Out at %.2f or more, in below %.2f.", d.name.c_str(), d.mouth, s.openThreshold, s.closeThreshold);
					}
					break;
				}
				if (!found) {
					Text("No tongue on right now: start a scene, or tick the preview on Tongue Select, to see the mouth reading here.");
				}
				Text("0 = mouth closed, 1.0 = OStim's full tongue pose (wide open).");
			}
			Slider("Comes out when the mouth is", s.openThreshold, 0.05f, 1.0f, "%.2f open", "");
			Slider("Goes in when the mouth is below", s.closeThreshold, 0.0f, 1.0f, "%.2f open", "Keep this lower than the one above.");
			Slider("Wait before it comes out", s.showDelay, 0.0f, 1.5f, "%.2f s", "");
			Slider("Wait before it goes in", s.hideDelay, 0.0f, 1.5f, "%.2f s", "");
			if (s.closeThreshold > s.openThreshold) {
				TextCol(G::ImVec4{ 1.0f, 0.8f, 0.3f, 1.0f }, "'Goes in' is above 'comes out': the tongue will flicker.");
			}
			SeparatorText("Slide");
			Check("Slide in and out", s.slide, "Off = the tongue pops. Tongues without the slide meshes always pop.");
			Slider("Slide out takes", s.slideOut, 0.05f, 5.0f, "%.2f s", "");
			Slider("Slide in takes", s.slideIn, 0.05f, 5.0f, "%.2f s", "");
			static const char* ease[] = { "Linear", "Smooth", "Fast then slow", "Slow then fast" };
			Choice("Motion", s.slideEase, ease, 4, "");
			if (G::TreeNode(Loc::L("Advanced"))) {
				static const char* source[] = { "As drawn on screen", "As requested by OStim or a voice mod" };
				Choice("Mouth is measured", g.mouthSource, source, 2, "Try the second if the tongue never appears.", true);
				static const char* rule[] = { "Any open mouth", "Only OStim's tongue pose" };
				Choice("Tongue comes out for", g.mouthRule, rule, 2, "'Only OStim's tongue pose' ignores moans and talking.", true);
				Check("At climax any open mouth counts", g.climaxAnyMouth, "", true);
				Slider("Size of OStim's tongue pose", g.poseLevel, 0.05f, 1.0f, "%.2f", "Lower it if another mod makes OStim's tongue pose small.", true);
				Slider("Wait for the slide bone", g.slideWait, 0.0f, 6.0f, "%.1f s", "0 = never wait.", true);
				Check("Follow the mouth FaceLab draws", s.faceFollow, "");
				Slider("FaceLab mouth open enough", s.faceOpen, 0.1f, 1.5f, "%.2f", "");
				Slider("FaceLab mouth closed enough", s.faceClose, 0.0f, 1.5f, "%.2f", "");
				G::TreePop();
			}
			SaveNote();
		}

		// ---- 4. Position: the scene tongue and the FaceLab tongue -------------------------------------------------------------------------------
		void __stdcall PagePosition()
		{
			EditingLine();
			auto& s = Edit();
			Tongue::NoteEditing();
			for (const auto& t : Tongue::Snapshot()) {
				if (t.noSlideBone) {
					TextCol(G::ImVec4{ 1.0f, 0.8f, 0.3f, 1.0f }, "%s's tongue cannot slide: these sliders do nothing to it.", t.name.c_str());
				}
			}
			SeparatorText("Scene Tongue");
			Slider("Pulls back by", s.slideDistance, 0.0f, 15.0f, "%.1f", "About 6 is the length of the tongue.");
			Slider("Out: sideways", s.extendX, -6.0f, 6.0f, "%.1f", "");
			Slider("Out: forward / back", s.extendY, -6.0f, 6.0f, "%.1f", "");
			Slider("Out: up / down", s.extendZ, -6.0f, 6.0f, "%.1f", "");
			Slider("In: sideways", s.slideX, -6.0f, 6.0f, "%.1f", "Where it sits while pulled back.");
			Slider("In: up / down", s.slideZ, -6.0f, 6.0f, "%.1f", "");
			SeparatorText("FaceLab Tongue");
			Slider("Sideways", s.askX, -10.0f, 10.0f, "%.1f", "Added to the Scene Tongue place.");
			Slider("Forward / back", s.askY, -10.0f, 10.0f, "%.1f", "");
			Slider("Up / down", s.askZ, -10.0f, 10.0f, "%.1f", "Lower puts it under the shaft in a blowjob.");
			Slider("Further per 1.0 above 1.0", s.askOver, 0.0f, 10.0f, "%.1f", "TongueOut 150 = half of this further out.");
			SaveNote();
		}

		// ---- 5. Voice & Climax ------------------------------------------------------------------------------------------------------------------
		void __stdcall PageVoice()
		{
			g_pageScoped = false;
			auto& s = Config::Edit();
			Check("Tongue stays in during spoken lines", s.hideDuringLines, "Needs a voice mod that reports its lines to TongueLab.");
			Check("Tongue stays in during moans", s.hideDuringMoans, "Needs a voice mod that reports its moans to TongueLab.");
			Slider("Goes in when a line starts, takes", s.speechIn, 0.02f, 1.0f, "%.2f s", "");
			Slider("Comes out after a line, takes", s.speechOut, 0.05f, 2.0f, "%.2f s", "");
			Slider("Stays in after a line ends", s.speechGrace, 0.0f, 2.0f, "%.2f s", "");
			SeparatorText("Climax");
			static const char* climax[] = { "Normal rules", "May stay out while speaking", "Always in" };
			Choice("At climax", s.climaxMode, climax, 3, "");
			Slider("Climax lasts", s.climaxSeconds, 1.0f, 30.0f, "%.0f s", "");
			SaveNote();
		}

		// ---- 6. Debug (with what was the Live page, and the general switches) -----------------------------------------------------------------------
		void __stdcall PageDebug()
		{
			g_pageScoped = false;
			auto& s = Config::Edit();
			Check("Debug log", s.debug, "Writes every decision to TongueLab.log. Leave it off for normal play; turn it on to report a problem.");
			SeparatorText("Live");
			const auto list = Tongue::Snapshot();
			if (list.empty()) {
				Text("No tongue on right now.");
			}
			for (auto& d : list) {
				Text("%s  -  tongue %s   mouth %.2f   slide %s %.2f%s%s", d.name.c_str(), Loc::T(d.shown ? "OUT" : "in"), d.mouth, Loc::T(d.canSlide ? "yes" : "no"), d.slide, d.speaking ? Loc::T("   speaking") : "", d.climax ? Loc::T("   climax") : "");
			}
			Text("Tongue armors found: %d", static_cast<int>(Tongue::FormCount()));
			{
				const auto info = Tongue::Visual();
				if (info.failed > 0) {
					TextCol(G::ImVec4{ 1.0f, 0.4f, 0.4f, 1.0f }, "%d tongue skin data not recognised (see TongueLab.log, 'Visual:').", info.failed);
				} else if (info.recognised > 0) {
					TextCol(G::ImVec4{ 0.45f, 0.90f, 0.45f, 1.0f }, "Size changes working: %d recognised.", info.recognised);
				}
			}
			SaveNote();
		}

		void __stdcall OnMenuEvent(SKSEMenuFramework::Model::EventType a_type)
		{
			if (a_type == SKSEMenuFramework::Model::kCloseMenu) {
				FlushNow(true);
				Npc::ClearLive(ScopeStem());
				g_saveState = 0;
			}
		}
	}

	void Flush()
	{
		FlushNow(false);
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::info("SKSE Menu Framework is not installed: no in-game menu, settings come from TongueLab.toml");
			return;
		}
		SKSEMenuFramework::SetSection("TongueLab");
		SKSEMenuFramework::AddSectionItem(Loc::T("Tongue Select"), PageSelect);
		SKSEMenuFramework::AddSectionItem(Loc::T("Size"), PageSize);
		SKSEMenuFramework::AddSectionItem(Loc::T("Timing"), PageTiming);
		SKSEMenuFramework::AddSectionItem(Loc::T("Position"), PagePosition);
		SKSEMenuFramework::AddSectionItem(Loc::T("Voice & Climax"), PageVoice);
		SKSEMenuFramework::AddSectionItem(Loc::T("Debug"), PageDebug);
		SKSEMenuFramework::AddEvent(OnMenuEvent, 0.0f);
		logger::info("SKSE Menu Framework pages registered (section: TongueLab)");
	}
}
