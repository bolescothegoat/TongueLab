#pragma once

namespace UI
{
	void Register();
	void Flush();  // writes menu edits that have not changed for a moment (called from the game thread tick)
}
