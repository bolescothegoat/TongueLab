#pragma once

namespace Tongue
{
	struct Diag
	{
		std::string name;
		bool        present{ false };
		bool        shown{ false };
		bool        speaking{ false };
		bool        climax{ false };
		float       mouth{ 0.0f };
		bool        canSlide{ false };
		float       slide{ 0.0f };
		std::uint32_t id{ 0 };
		bool        noSlideBone{ false };  // TongueLab looked for the slide bone for a while and there is none (stock mesh, or Halo's without the patch)
	};

	// the preview: put one of the tongues on the player without a scene (for the alignment sliders), and take it off again
	[[nodiscard]] std::vector<std::string> TongueChoices();   // names of the tongue armors found, in a fixed order
	struct TongueChoice
	{
		std::string name;      // "<number>: <name>" (the number is the tongue's place in the full list)
		int         index{ 0 };  // index into the full list (what PreviewTongue / the model setting take)
		bool        slides{ true };  // its mesh has the slide bone (Halo's tongues); false = a stock mesh that can only pop in and out
	};
	[[nodiscard]] std::vector<TongueChoice> TongueChoiceList();  // the same tongues, each with whether it can slide
	[[nodiscard]] int PreviewIndex();  // index of the tongue the preview has on now, -1 = none
	void PreviewTongue(bool a_on, int a_index, std::uint32_t a_actorId = 0x14);  // on the player (0x14) or on another loaded character
	// which tongue MODEL TongueLab puts on a character itself (no scene, a FaceLab face asked for the tongue): an index into TongueChoices(); -1 = the per-character / global setting.
	// Another plugin (FaceLab's Tongue assist) sets it through the TongueLab_SetTongueModel export. A tongue already put on this way is swapped when it changes.
	[[nodiscard]] int AskStatus(unsigned int a_formID);  // see g_askCode in Tongue.cpp
	void SetModelOverride(int a_index);
	[[nodiscard]] int ModelOverride();
	[[nodiscard]] bool PreviewTongueOn();
	void SetPreviewPull(bool a_on);   // testing aid: every equipped tongue is held visible at its pulled-back place (to tune how far it pulls back); excludes the 'held out' one
	[[nodiscard]] bool PreviewPull();
	void SetPreviewHold(bool a_on);   // testing aid: every tongue that is equipped is held out (nothing saved)
	[[nodiscard]] bool PreviewHold();
	void NoteEditing(std::uint32_t a_character = 0);  // the Tuning / Visual page calls this every frame it is drawn (a_character: the one character being edited, 0 = everyone)
	void Init();                     // kDataLoaded: resolves OStim's tongue forms, hooks events, starts the ticker
	void ShutdownRestore();          // un-hide everything (mod switched off)
	std::vector<Diag> Snapshot();    // for the menu (characters that have a tongue on right now)
	struct Nearby
	{
	std::string  name;
	std::uint32_t id{ 0 };
	};
	std::vector<Nearby> NearbyList();  // the five characters nearest the player, tongue or not: the menu's Character list, so a character can be picked before she has a tongue
	[[nodiscard]] std::size_t FormCount();
	// the Visual page: skin data of tongues that were recognised and changeable / that could not be recognised
	struct VisualInfo
	{
		int recognised{ 0 };
		int failed{ 0 };
	};
	VisualInfo Visual();
}
