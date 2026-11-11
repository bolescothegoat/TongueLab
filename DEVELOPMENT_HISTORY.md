# Development history

Dates come from my own file timestamps and project notes on my PC; they are not third-party timestamps.

| Date | Event |
|---|---|
| 2026-10-18 | First version: judges the mouth OStim draws and shows or hides OStim's tongue armor accordingly. |
| 2026-10-18 | Slide: a patched copy of Halo's tongues, bound to a movable bone, lets the tongue slide out of and into the mouth. Confirmed in game. |
| 2026-10-26 | Renamed TongueLab. Mouth follows FaceLab's face; a face can ask for the tongue (`TongueOut`); shape sliders (Visual page); per-character settings. |
| 2026-11-05 | Slide-bone patcher built as a separate tool: it contains only a table of changed bytes, never the meshes. |
| 2026-11-07 | Live realign inside the menu and a preview tongue; a generic voice hook; ported to CommonLibSSE-NG 15.5.0 (one DLL for SE and AE); 1.0.0 prepared. |
| 2026-11-08 | A tongue put on another character while a menu has the game paused is built at once (a model update is requested), so it shows with the menu open. Tuning previews only tongues that can slide; the Visual page previews every tongue; stock OStim tongues are marked as unable to slide. The Visual and Tuning pages freeze the tongue physics the same way. A setting for the tongue FaceLab puts on when no scene is running. A face mod finds TongueLab through an exported function (`FaceLab_TongueStatus`). |
| 2026-11-10 | SLO VE tongues recognised by their meshes and listed in the pickers; menu translation layer (Languages folder). |
| 2026-11-11 | Tongue Select page (pick, preview, scope), the picked tongue swapped in during scenes, menu redesigned into six pages, slide bone bound again when the tongue starts out of view, per-frame cost of the swap code removed; 1.2.0. |

From the first commit of this repository onward, git history is the record.
