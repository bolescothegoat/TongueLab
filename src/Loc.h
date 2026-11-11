#pragma once

// Translations for the in-game menu. The English text in the source IS the key: Loc::T("English") gives the translated text, or the English
// itself when there is no translation, so nothing ever shows up empty. Language files are TOML, one per language:
//   Data/SKSE/Plugins/<Mod>/Languages/<name>.toml      with a [strings] table:   "English text" = "Translated text"
// English.toml is the template (every string of the menu). Copy it, rename it (german.toml ...), translate the right hand sides.
// An empty or missing entry falls back to English. Placeholders (%s %d %.2f {}) must stay: a translation whose placeholders differ is ignored.
namespace Loc
{
	// folder: where the language files are (Data/SKSE/Plugins/<Mod>/Languages); setting: "auto" (the game's language) or a file name without .toml
	void Init(const std::string& folder, const std::string& setting);

	// switch language at run time (the menu's language picker). Returns false when the file could not be used (English stays).
	bool Set(const std::string& setting);

	[[nodiscard]] const char* T(const char* english);       // text: translation or the English itself
	[[nodiscard]] const char* L(const char* english);       // widget label: translation + a hidden id taken from the English, so ids never change with the language

	[[nodiscard]] std::vector<std::string> Available();     // "english" + every language file found, sorted
	[[nodiscard]] std::string              Current();       // the language in use ("english" when none)
	[[nodiscard]] int                      Count();         // how many strings the current language translates
	// "" when the letters of the current language will show; otherwise the font SKSE Menu Framework has to be set to (its Settings > Font) for them to show
	[[nodiscard]] std::string              FontAdvice();
	void                                   SetLogMissing(bool on);  // Debug: log every string a non-English language has no entry for (once each)
}
