# TongueLab

An SKSE plugin for Skyrim SE / AE that takes control of OStim's tongue. OStim equips a tongue the instant it picks an expression, before the mouth has opened, and takes it off after the mouth has closed. TongueLab holds the tongue back until the mouth is really open, slides it out and back in, and lets you move, resize and reshape it live in a menu.

- Nexus page: (add the link when the page is up)
- Source: https://github.com/bolescothegoat/TongueLab
- License: GNU GPL v3 or later (see `LICENSE`)
- Build instructions: `BUILDING.md`
- Development timeline: `DEVELOPMENT_HISTORY.md`

## What it does

- Waits for the mouth: the tongue comes out when the mouth is open and goes in as it starts to close, judged from the mouth as drawn.
- Slides: with slide-capable tongue files (`patcher/` makes them from your own copy of Halo's tongues; it contains none of the meshes) the tongue slides out of and back into the mouth.
- Live tuning: where the tongue sits (pulled back, fully out, for a tongue a FaceLab face asks for), and its size, length, width, thickness and tilt. A preview tongue can be put on your own character without a scene.
- Per-character settings: `_default`, `_generic` and one file per NPC.
- Voice mods: any DLL that exports `extern "C" int TongueLab_SpeechKind(std::uint32_t formID)` (0 silent, 1 moan, 2 spoken line) tells TongueLab when someone is speaking.
- FaceLab (optional): a face can ask for the tongue (`TongueOut`), and the tongue can follow the mouth FaceLab draws.

## Built with AI assistance

The C++ was written by Claude (Anthropic) through Claude Code, from my designs and my in-game testing. The mod contains no AI-made art, meshes, textures or sounds.

## Credits

Halo's tongues (the meshes TongueLab works with, not included), OStim Standalone, CommonLibSSE-NG (alandtse), SKSE Menu Framework (Thiago099 / QTR-Modding). Full third-party licences: `THIRD_PARTY_NOTICES.txt`.

## Issues

Bug reports are welcome. Please attach `TongueLab.log` from your `Documents/My Games/Skyrim Special Edition/SKSE` folder (tick "Debug log" on the General page first).
