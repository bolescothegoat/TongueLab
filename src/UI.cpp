#include "PCH.h"

#include "Config.h"
#include "Npc.h"
#include "NpcCore.h"
#include "SKSEMenuFramework.h"
#include "Tongue.h"
#include "UI.h"

#include <atomic>
#include <chrono>
#include <mutex>

namespace G = ImGuiMCP;

// "TongueLab" page in SKSE Menu Framework. Every change is applied at once and written to TongueLab.toml.
namespace UI
{
	namespace
	{
		int g_saveState = 0;  // 0 nothing, 1 saved, 2 failed

		// ---- what the pages edit: everyone (TongueLab.toml), the generic NPCs (_generic.toml) or one character (her own file) ----------------
		int                    g_scope = 0;           // 0 everyone, 1 generic NPCs, 2 one character
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

		void Tip(const char* a_text)
		{
			if (a_text && *a_text && G::IsItemHovered()) {
				G::SetTooltip("%s", a_text);
			}
		}

		void Check(const char* a_label, bool& v, const char* a_tip, bool glob = false)
		{
			if (G::Checkbox(a_label, &v)) {
				Changed(glob);
			}
			Tip(a_tip);
		}

		void Slider(const char* a_label, float& v, float lo, float hi, const char* a_fmt, const char* a_tip, bool glob = false)
		{
			G::PushItemWidth(320.0f);
			if (G::SliderFloat(a_label, &v, lo, hi, a_fmt)) {
				Changed(glob);
			}
			G::PopItemWidth();
			Tip(a_tip);
		}

		void Choice(const char* a_label, int& v, const char* const* a_items, int a_count, const char* a_tip, bool glob = false)
		{
			G::PushItemWidth(320.0f);
			if (G::Combo(a_label, &v, a_items, a_count)) {
				Changed(glob);
			}
			G::PopItemWidth();
			Tip(a_tip);
		}

		void SaveNote()
		{
			FlushNow(false);
			if (g_saveState == 1) {
				G::TextColored(G::ImVec4{ 0.45f, 0.90f, 0.45f, 1.0f }, "Saved.");
			} else if (g_saveState == 2) {
				G::TextColored(G::ImVec4{ 1.0f, 0.4f, 0.4f, 1.0f }, "Could not write the settings file (see TongueLab.log).");
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

		// top of the Timing, Tuning and Visual pages: who the sliders below are for
		void ScopeBar()
		{
			g_pageScoped = true;
			static const char* items[] = { "Everyone (TongueLab.toml)", "Generic NPCs (guards, bandits ... they share one file)", "One character" };
			int                sc = g_scope;
			G::PushItemWidth(420.0f);
			const bool scopeChanged = G::Combo("These settings are for", &sc, items, 3);
			G::PopItemWidth();
			std::uint32_t id = g_charId;
			if (sc == 2) {
				std::vector<std::pair<std::string, std::uint32_t>> chars;
				auto*                                              pc = RE::PlayerCharacter::GetSingleton();
				chars.push_back({ std::string(pc && pc->GetName() ? pc->GetName() : "You") + " (you)", 0x14 });
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
				if (G::Combo("Character", &idx, ptrs.data(), static_cast<int>(ptrs.size()))) {
					id = chars[idx].second;
				}
				G::PopItemWidth();
				Tip("You, everyone near you, and every character that has a tongue on right now. Settings saved for a character apply whenever she has a tongue. To see a change on someone with no tongue yet, press Show the tongue on her in FaceLab (look at her first).");
			}
			if (scopeChanged || id != g_charId) {
				SetScope(sc, id);
			}
			if (g_scope == 0) {
				G::TextWrapped("Applies to everyone. Settings saved for a character or for the generic NPCs win over these.");
			} else {
				const auto stem = ScopeStem();
				if (stem.empty()) {
					G::TextColored(G::ImVec4{ 1.0f, 0.4f, 0.4f, 1.0f }, "This character was made while playing and has no plugin: it cannot have a file of its own.");
				} else {
					G::TextWrapped("Saved to Data/SKSE/Plugins/TongueLab/%s.toml. Only the values you change are written; everything else stays as set under 'Everyone'. Delete the file to go back.", stem.c_str());
				}
			}
			G::Separator();
		}

		void __stdcall PageGeneral()
		{
			g_pageScoped = false;
			auto& s = Config::Edit();
			Check("Take over the OStim tongue", s.enabled, "Off = OStim shows and hides the tongue the way it always did.");
			G::TextWrapped("OStim puts the tongue on the instant it picks an expression, before the mouth has opened, and takes it off after the mouth has closed. This mod holds the tongue back until the mouth is actually open, pulls it in as the mouth starts to close, and keeps it in while a voice mod is saying a line.");
			G::Separator();
			G::Text("Who it applies to");
			Check("The player", s.player, "");
			G::SameLine();
			Check("Everyone else", s.others, "");
			Check("Female actors", s.females, "");
			G::SameLine();
			Check("Male actors", s.males, "");
			G::Separator();
			Check("Debug log", s.debug, "Writes every tongue decision to TongueLab.log (Documents/My Games/Skyrim Special Edition/SKSE).");
			G::Text("Tongue armors found: %d", static_cast<int>(Tongue::FormCount()));
			if (::GetModuleHandleA("FaceLab.dll")) {
				// only shown when FaceLab is installed: it is the tongue FaceLab's Show tongue button puts on when no OStim scene is running
				const auto names = Tongue::TongueChoices();
				if (!names.empty()) {
					G::Separator();
					G::SeparatorText("Tongue for FaceLab and screenshots");
					G::TextWrapped("In a scene, OStim chooses the tongue. When you use FaceLab's Show tongue button with no scene running (a screenshot, a posed face), nothing supplies one, so this is the tongue FaceLab puts on.");
					std::vector<const char*> ptrs;
					for (auto& n : names) {
						ptrs.push_back(n.c_str());
					}
					auto& cfg = Config::Edit();
					int   pick = std::clamp(cfg.tongueModel, 0, static_cast<int>(ptrs.size()) - 1);
					G::PushItemWidth(320.0f);
					if (G::Combo("Tongue to show", &pick, ptrs.data(), static_cast<int>(ptrs.size()))) {
						cfg.tongueModel = pick;
						Changed(true);
					}
					G::PopItemWidth();
					Tip("Changing it swaps the tongue she has now. Tongues marked 'stock mesh' cannot slide, they just appear.");
				}
			}
			G::Separator();
			static bool resetArmed = false;
			if (!resetArmed) {
				if (G::Button("Reset ALL settings to default")) {
					resetArmed = true;
				}
			} else {
				G::TextColored(G::ImVec4{ 1.0f, 0.7f, 0.3f, 1.0f }, "This puts every TongueLab setting (all pages) back to the shipped defaults.");
				if (G::Button("Yes, reset everything")) {
					s = Config::Settings{};
					resetArmed = false;
					Changed(true);
				}
				G::SameLine();
				if (G::Button("Cancel")) {
					resetArmed = false;
				}
			}
			SaveNote();
		}

		void __stdcall PageTiming()
		{
			ScopeBar();
			auto& s = Edit();
			auto& g = Config::Edit();
			static const char* source[] = { "As drawn on screen (recommended)", "As requested by OStim or a voice mod" };
			Choice("Mouth is measured", g.mouthSource, source, 2, "Which mouth value decides when the tongue comes out. If the tongue never appears, try the second option and tell me.", true);
			static const char* rule[] = { "Any open mouth", "Only OStim's tongue pose (ignores moans and talking)" };
			Choice("Tongue comes out for", g.mouthRule, rule, 2, "The tongue pose is the mouth OStim makes when it wants the tongue out. Moans and talking use other mouth shapes, so they no longer bring the tongue out.", true);
			Check("At climax any open mouth counts", g.climaxAnyMouth, "Ahegao style expressions use other mouth shapes than OStim's tongue pose.", true);
			Slider("Size of OStim's tongue pose", g.poseLevel, 0.05f, 1.0f, "%.2f", "How open OStim's tongue pose really is on screen (BigAah, Eee). The two sliders below are fractions of this, so they stay on a normal scale. Lower it if another mod makes OStim's tongue pose small.", true);
			Slider("Tongue comes out when the mouth is", s.openThreshold, 0.05f, 1.0f, "%.2f open", "The mouth must reach this opening before the tongue appears.");
			Slider("Tongue goes in when the mouth is below", s.closeThreshold, 0.0f, 1.0f, "%.2f open", "Higher = the tongue retreats earlier, while the mouth is still closing.");
			Check("Follow the mouth FaceLab draws", s.faceFollow, "Once FaceLab has drawn this actor's face in a scene, the tongue is only out while FaceLab's mouth is open: it goes in when the mouth closes (climax) or yields to a voice line, and waits for the mouth to open again.");
			Slider("FaceLab mouth open enough to come out", s.faceOpen, 0.1f, 1.5f, "%.2f", "1.0 = a full RaceMenu weight. Higher = the tongue waits until the mouth is wider.");
			Slider("FaceLab mouth below this: tongue goes in", s.faceClose, 0.0f, 1.5f, "%.2f", "Keep this lower than the value above.");
			Slider("Wait before it comes out", s.showDelay, 0.0f, 1.5f, "%.2f s", "The mouth has to stay open this long first.");
			Slider("Wait before it goes in", s.hideDelay, 0.0f, 1.5f, "%.2f s", "The mouth has to stay below the 'goes in' value this long.");
			G::Separator();
			G::Text("Slide (needs the 'HDT Tongues - Slide Bone' meshes)");
			Check("Slide the tongue out of and into the mouth", s.slide, "Off = the tongue pops in and out. Tongues without the slide meshes always pop.");
			Slider("Slide out takes", s.slideOut, 0.05f, 5.0f, "%.2f s", "");
			Slider("Slide in takes", s.slideIn, 0.05f, 5.0f, "%.2f s", "");
			Slider("Wait for the slide bone", g.slideWait, 0.0f, 6.0f, "%.1f s", "The slide bone shows up 0.3 to 6 s after OStim equips the tongue. Until then the tongue is kept in so it can slide out; if it is not there by this time the tongue comes out without sliding. 0 = never wait.", true);
			static const char* ease[] = { "Linear", "Smooth", "Fast then slow", "Slow then fast" };
			Choice("Motion curve", s.slideEase, ease, 4, "How the speed changes during the slide.");
			if (s.closeThreshold > s.openThreshold) {
				G::TextColored(G::ImVec4{ 1.0f, 0.8f, 0.3f, 1.0f }, "The 'goes in' value is above the 'comes out' value: the tongue will flicker. Keep it lower.");
			}
			SaveNote();
		}

		int g_pickSlide = 0;  // the preview pickers' choices (an index into their own list)
		int g_pickStock = 0;

		// A preview tongue on the character these settings are for (one selected above), otherwise on you. a_sliding: the list of tongues that have the slide bone
		// (Halo's, for the Tuning page, whose position sliders only work on those); false: every tongue (for the Visual page: the shape sliders work on all of them).
		void PreviewPicker(bool a_slidingOnly, const char* a_combo, int& a_pick)
		{
			std::vector<Tongue::TongueChoice> list;
			for (auto& t : Tongue::TongueChoiceList()) {
				if (!a_slidingOnly || t.slides) {
					list.push_back(t);
					if (!a_slidingOnly && !t.slides) {
						list.back().name += "  (cannot slide)";
					}
				}
			}
			if (list.empty()) {
				G::TextWrapped("%s", a_slidingOnly ? "No tongue with a slide bone was found. Install Halo's HDT Tongues and run the Slide Bone Patcher (see the README)." : "No tongues were found.");
				return;
			}
			std::vector<const char*> ptrs;
			for (auto& t : list) {
				ptrs.push_back(t.name.c_str());
			}
			a_pick = std::clamp(a_pick, 0, static_cast<int>(list.size()) - 1);
			const std::uint32_t prevId = (g_scope == 2 && g_charId != 0) ? g_charId : 0x14;
			std::string         prevName = "my character";
			if (prevId != 0x14) {
				if (auto* act = RE::TESForm::LookupByID<RE::Actor>(prevId); act && act->GetName() && *act->GetName()) {
					prevName = act->GetName();
				}
			}
			const int curIdx = Tongue::PreviewIndex();
			bool      on = Tongue::PreviewTongueOn() && std::ranges::any_of(list, [curIdx](const Tongue::TongueChoice& t) { return t.index == curIdx; });
			if (G::Checkbox(std::format("Show a tongue on {} (preview)##{}", prevName, a_slidingOnly ? "s" : "x").c_str(), &on)) {
				Tongue::PreviewTongue(on, list[static_cast<std::size_t>(a_pick)].index, prevId);
				Tongue::SetPreviewHold(on);
			}
			Tip("Puts the tongue picked below on the character these settings are for (you, if none is selected above) without a scene and keeps it out, so you can try the sliders right here. It is taken off again when you untick this, or a minute (two on someone else) after this menu is closed. Untick it before you save the game.");
			G::PushItemWidth(320.0f);
			if (G::Combo(a_combo, &a_pick, ptrs.data(), static_cast<int>(ptrs.size())) && on) {
				Tongue::PreviewTongue(true, list[static_cast<std::size_t>(a_pick)].index, prevId);
			}
			G::PopItemWidth();
		}

		void __stdcall PageTuning()
		{
			ScopeBar();
			auto& s = Edit();
			Tongue::NoteEditing();
			for (const auto& t : Tongue::Snapshot()) {
				if (t.noSlideBone) {
					G::TextColored(G::ImVec4{ 1.0f, 0.8f, 0.3f, 1.0f }, "%s's tongue cannot slide (a stock OStim tongue, or Halo's tongue without the Slide Bone Patch): the position sliders below do nothing to it. Pick a Halo tongue and run the Slide Bone Patcher (see the README).", t.name.c_str());
				}
			}
			{
				bool hold = Tongue::PreviewHold();
				if (G::Checkbox("Keep the tongue out while I edit (testing)", &hold)) {
					Tongue::SetPreviewHold(hold);
				}
				Tip("Start a scene once so the tongue is equipped, then tick this: the tongue stays out and every change below is drawn as you drag it. Not saved; it is off every time the game starts. Untick it when you are done.");
				bool pull = Tongue::PreviewPull();
				if (G::Checkbox("Show the tongue pulled back while I edit (testing)", &pull)) {
					Tongue::SetPreviewPull(pull);
				}
				Tip("'Keep the tongue out' hides the effect of 'Pulls back into the mouth by': that value only matters while the tongue is pulled in. Tick this one instead and the tongue sits visibly at its pulled-back place, so dragging that slider (and the Retracted ones) moves the whole tongue where you can see it. The tongue has to be equipped first (start a scene, or use Show a tongue on ... below). Not saved; off every time the game starts.");
			}
			G::Separator();
			G::TextWrapped("Where the tongue is. Units are game units; a tongue is about 3 long. Timing (when it comes out and how fast) is on the Timing page.");
			G::TextWrapped("Preview: only the tongues that can slide are listed (Halo's, with the Slide Bone Patch). OStim's own stock tongues cannot slide, so these position sliders do nothing to them.");
			PreviewPicker(true, "Preview tongue (can slide)", g_pickSlide);
			G::Separator();
			Slider("Pulls back into the mouth by", s.slideDistance, 0.0f, 15.0f, "%.1f", "How far the tongue travels back. About 6 is the length of the tongue.");
			G::Text("Where the tongue sits when fully OUT (relative to its normal place)");
			Slider("Out: sideways", s.extendX, -6.0f, 6.0f, "%.1f", "Shifts the extended tongue sideways.");
			Slider("Out: forward / back", s.extendY, -6.0f, 6.0f, "%.1f", "Pushes the extended tongue further out (+) or pulls it back (-).");
			Slider("Out: up / down", s.extendZ, -6.0f, 6.0f, "%.1f", "Raises (+) or lowers (-) the extended tongue.");
			G::Text("A tongue a FaceLab face asks for (TongueOut), added to the place above");
			Slider("Asked: sideways", s.askX, -10.0f, 10.0f, "%.1f", "In a blowjob the normal place is where the shaft is: move the tongue beside it.");
			Slider("Asked: forward / back", s.askY, -10.0f, 10.0f, "%.1f", "");
			Slider("Asked: up / down", s.askZ, -10.0f, 10.0f, "%.1f", "Lower (-) puts the tongue under the shaft.");
			Slider("Asked: further per 1.0 above 1.0", s.askOver, 0.0f, 10.0f, "%.1f", "TongueOut 150 = 0.5 x this further out.");
			G::Text("Where it sits when pulled back in");
			Slider("Retracted position: sideways", s.slideX, -6.0f, 6.0f, "%.1f", "Shifts the tongue sideways while it is pulled back.");
			Slider("Retracted position: up / down", s.slideZ, -6.0f, 6.0f, "%.1f", "Shifts the tongue up (+) or down (-) while it is pulled back.");
			SaveNote();
		}

		void __stdcall PageVisual()
		{
			ScopeBar();
			auto& s = Edit();
			Tongue::NoteEditing();  // the Visual page edits the tongue like the Tuning page does: same frozen physics, so the tongue looks the same on both pages
			G::SeparatorText("Preview a tongue");
			G::TextWrapped("The shape sliders below work on every tongue: Halo's and OStim's own. Halo's tongues can also slide (position sliders on the Tuning page); OStim's stock tongues only pop in and out.");
			PreviewPicker(false, "Preview tongue", g_pickStock);
			G::Separator();
			auto& g = Config::Edit();
			Check("Change the shape of the tongue", g.visual, "Off = the tongue mesh exactly as it was shipped.", true);
			G::TextWrapped("The same sliders as the HALOS Human HDT Tongueslide BodySlide set (size, length, width, thickness, tilt), but live and with no BodySlide or mesh rebuild. Each value is the percent change from the tongue as shipped, applied to the whole tongue about its centre. Where the tongue sits is on the Tuning page.");
			G::Separator();
			Slider("Size (all directions)", s.visSize, -60.0f, 100.0f, "%+.0f %%", "Grows or shrinks the tongue in every direction. The other three multiply with this.");
			Slider("Length (front to back)", s.visLength, -60.0f, 150.0f, "%+.0f %%", "A longer tongue reaches further out of the mouth.");
			Slider("Width (side to side)", s.visWidth, -60.0f, 100.0f, "%+.0f %%", "");
			Slider("Thickness (up / down)", s.visThickness, -60.0f, 100.0f, "%+.0f %%", "");
			Slider("Tilt: tip up (+) / down (-)", s.visTilt, -45.0f, 45.0f, "%+.0f deg", "Turns the whole tongue about its centre, side to side axis.");
			G::Separator();
			if (G::Button("Back to the tongue as shipped")) {
				s.visSize = s.visLength = s.visWidth = s.visThickness = s.visTilt = 0.0f;
				Changed(false);
			}
			G::SameLine();
			if (G::Button("The HALOS author's preset")) {
				// Size Small 20, X Size Big 10, Z Size Small 30 of "[AtomicSkull]HALOS Tongue Preset", turned into per-axis percentages with the measurements of the HALOS tongue mesh
				s.visSize = 0.0f;
				s.visLength = -21.0f;
				s.visWidth = -5.0f;
				s.visThickness = -27.0f;
				s.visTilt = 0.0f;
				Changed(false);
			}
			Tip("Its Back 15 and Up 32 are positions, not shape: use the Tuning page for those. The shipped slider data does not fit most of the ten tongue meshes exactly, so this is the intent of the preset, not its noise.");
			const auto info = Tongue::Visual();
			G::Separator();
			if (info.failed > 0) {
				G::TextColored(G::ImVec4{ 1.0f, 0.4f, 0.4f, 1.0f }, "%d tongue skin data could not be recognised and was left alone. See TongueLab.log, lines starting 'Visual:'.", info.failed);
			} else if (info.recognised > 0) {
				G::TextColored(G::ImVec4{ 0.45f, 0.90f, 0.45f, 1.0f }, "Working: %d tongue skin data recognised.", info.recognised);
			} else {
				G::Text("Nothing yet: the shape is applied the first time a tongue is on screen.");
			}
			SaveNote();
		}

		void __stdcall PageVoice()
		{
			g_pageScoped = false;
			auto& s = Config::Edit();
			Check("Tongue stays in during spoken lines", s.hideDuringLines, "Needs a voice mod that reports its lines to TongueLab (see the README). Without one nothing happens.");
			Check("Tongue stays in during moans", s.hideDuringMoans, "Off = moans do not affect the tongue. Needs a voice mod that reports its moans to TongueLab.");
			Slider("Goes in when a line starts, takes", s.speechIn, 0.02f, 1.0f, "%.2f s", "Shorter = the tongue is gone by the time the lips move.");
			Slider("Comes out after a line, takes", s.speechOut, 0.05f, 2.0f, "%.2f s", "Longer = it follows the mouth opening again instead of leading it.");
			Slider("Stays in after a line ends", s.speechGrace, 0.0f, 2.0f, "%.2f s", "Stops the tongue flicking out between two lines.");
			G::Separator();
			G::Text("Climax (for expression mods like Ahegao)");
			static const char* climax[] = { "Normal rules", "Tongue may stay out while speaking", "Tongue always in" };
			Choice("At climax", s.climaxMode, climax, 3, "Expression mods put the tongue out at climax. 'May stay out' lets it show even while a moan plays.");
			Slider("Climax lasts", s.climaxSeconds, 1.0f, 30.0f, "%.0f s", "How long after an orgasm the climax rule applies.");
			SaveNote();
		}

		void __stdcall PageLive()
		{
			G::TextWrapped("Actors that currently have an OStim tongue on. 'Drawn mouth' is the value the tongue is judged by.");
			G::Separator();
			const auto list = Tongue::Snapshot();
			if (list.empty()) {
				G::Text("None right now.");
			}
			for (auto& d : list) {
				G::Text("%s  -  tongue %s   mouth %.2f   slide %s %.2f%s%s", d.name.c_str(), d.shown ? "OUT" : "in", d.mouth, d.canSlide ? "yes" : "no (stock mesh)", d.slide, d.speaking ? "   speaking" : "", d.climax ? "   climax" : "");
			}
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
		SKSEMenuFramework::AddSectionItem("General", PageGeneral);
		SKSEMenuFramework::AddSectionItem("Timing", PageTiming);
		SKSEMenuFramework::AddSectionItem("Tuning", PageTuning);
		SKSEMenuFramework::AddSectionItem("Visual", PageVisual);
		SKSEMenuFramework::AddSectionItem("Voice and climax", PageVoice);
		SKSEMenuFramework::AddSectionItem("Live", PageLive);
		SKSEMenuFramework::AddEvent(OnMenuEvent, 0.0f);
		logger::info("SKSE Menu Framework pages registered (section: TongueLab)");
	}
}
