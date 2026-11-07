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
	};

	// the preview: put one of the tongues on the player without a scene (for the alignment sliders), and take it off again
	[[nodiscard]] std::vector<std::string> TongueChoices();   // names of the tongue armors found, in a fixed order
	void PreviewTongue(bool a_on, int a_index);
	[[nodiscard]] bool PreviewTongueOn();
	void SetPreviewHold(bool a_on);   // testing aid: every tongue that is equipped is held out (nothing saved)
	[[nodiscard]] bool PreviewHold();
	void NoteEditing();               // the Tuning page calls this every frame it is drawn
	void Init();                     // kDataLoaded: resolves OStim's tongue forms, hooks events, starts the ticker
	void ShutdownRestore();          // un-hide everything (mod switched off)
	std::vector<Diag> Snapshot();    // for the menu
	[[nodiscard]] std::size_t FormCount();
	// the Visual page: skin data of tongues that were recognised and changeable / that could not be recognised
	struct VisualInfo
	{
		int recognised{ 0 };
		int failed{ 0 };
	};
	VisualInfo Visual();
}
