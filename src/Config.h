#pragma once

namespace Config
{
	struct Settings
	{
		bool enabled{ true };
		bool debug{ false };
		std::string language{ "auto" };  // menu language: "auto" = the game's language, or the name of a file in TongueLab/Languages (global, never per NPC)

		// when the tongue may come out / must go back in, judged from the mouth that is actually on screen
		int   mouthSource{ 0 };        // 0 = the mouth as drawn (final), 1 = the mouth as requested (what OStim or a voice mod wrote)
		int   mouthRule{ 1 };           // 0 = any open mouth brings the tongue out, 1 = only OStim's tongue pose (BigAah + Eee), so moans and talking do not
		bool  climaxAnyMouth{ true };   // during the climax window any open mouth counts (ahegao expressions use other mouth shapes)
		float poseLevel{ 1.0f };        // how big OStim's tongue pose really is (BigAah / Eee as drawn): the pose is measured as a fraction of this, so the thresholds below stay on a normal 0-1 scale even when the pose is small
		float openThreshold{ 0.55f };   // mouth must be at least this open before the tongue comes out
		float closeThreshold{ 0.40f };  // tongue goes back in once the mouth falls below this (before it has fully shut)
		bool  faceFollow{ true };       // when FaceLab draws the face, the tongue follows the mouth FaceLab draws (needs FaceLab 3+)
		float faceOpen{ 0.8f };         // FaceLab mouth weight the tongue waits for before coming out
		float faceClose{ 0.5f };        // ...and the weight below which it goes back in
		float showDelay{ 0.12f };       // seconds the mouth has to stay open first
		float hideDelay{ 0.0f };        // seconds the mouth has to stay below the close threshold before the tongue goes in

		// slide (needs the tongue meshes from 'HDT Tongues - Slide Bone')
		bool  slide{ true };          // tongue slides out of / back into the mouth instead of popping
		float slideOut{ 0.22f };      // seconds to slide out
		float slideIn{ 0.18f };       // seconds to slide back in
		float slideWait{ 4.0f };      // after OStim equips the tongue: seconds to wait for the slide bone before it may pop in (0 = never wait)
		float slideDistance{ 6.0f };  // how far back into the mouth it goes (game units)
		float extendX{ 0.0f };        // where the tongue sits when fully out, relative to its normal place (head space): sideways
		float extendY{ 0.0f };        // ... forward (+) / back (-)
		float extendZ{ 0.0f };        // ... up (+) / down (-)
		float askX{ 0.0f }, askY{ 0.0f }, askZ{ 0.0f };  // added to the place above for a tongue a FaceLab face asks for (TongueOut): in a blowjob it has to clear the shaft
		float askOver{ 3.0f };      // units further out per 1.0 of TongueOut above 1.0

		// which tongue (linga1..10) this character always gets; 0 = whichever OStim picks. Only set per NPC (see NpcCore.h), never in TongueLab.toml
		int tongueModel{ 0 };
		// the tongue picked in the menu ("Tongue to use"): 0 = not chosen (the first tongue), N = the Nth entry of the list. Saved in TongueLab.toml and per character.
		int tongueUse{ 0 };

		// the tongue's own shape (the HALOS tongue sliders): percent change from the mesh as shipped, whole-tongue transforms about its centre
		bool  visual{ true };
		float visSize{ 0.0f };       // all three directions together, %
		float visLength{ 0.0f };     // along the tongue (front to back), %
		float visWidth{ 0.0f };      // side to side, %
		float visThickness{ 0.0f };  // up and down, %
		float visTilt{ 0.0f };       // tip up (+) / down (-), degrees
		float slideX{ 0.0f };         // extra sideways offset while it is retracted (head space, + = her left)
		float slideZ{ 0.0f };         // extra up/down offset while it is retracted (+ = up)
		int   slideEase{ 1 };         // 0 linear, 1 smooth, 2 starts fast ends slow, 3 starts slow ends fast

		// speech (a voice mod that exports TongueLab_SpeechKind)
		bool  hideDuringLines{ true };   // spoken lines
		bool  hideDuringMoans{ false };  // moans
		float speechIn{ 0.1f };        // seconds the slide-in takes when a spoken line starts (faster than the normal slide-in)
		float speechOut{ 0.5f };       // seconds the slide-out takes after a line (slower, so it follows the mouth reopening)
		float speechGrace{ 0.7f };  // stays in this long after a line ends so it does not flick out between lines

		// climax (Ahegao style expressions)
		int   climaxMode{ 0 };         // 0 = normal rules, 1 = tongue may stay out while speaking during climax, 2 = tongue always in during climax
		float climaxSeconds{ 8.0f };   // how long after an orgasm counts as the climax

		// who
		bool player{ true };
		bool others{ true };
		bool females{ true };
		bool males{ true };
	};

	[[nodiscard]] const Settings& Get();
	[[nodiscard]] Settings&       Edit();
	void                          Load();
	bool                          Save();
}
