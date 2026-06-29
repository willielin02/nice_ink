# Nice Ink

Nice Ink is an Unreal Engine 5.7 prototype for a supernatural tattoo parlor game. The current prototype focuses on a playable tattoo workflow: a player-controlled needle, ink palettes, runtime tattoo strokes, save/load support, a prototype parlor scene, and multiplayer replication scaffolding.

## Project Layout

- `Source/NiceInk/` - C++ gameplay module.
- `Content/Maps/L_NiceInk_Prototype.umap` - prototype map.
- `Content/Materials/` - prototype tattoo and parlor materials.
- `Config/` - startup map, input, and project defaults.
- `SPEC.md` - product and system planning notes.
- `WORKFLOW.md` - implementation workflow and task status.

## Local Requirements

- Unreal Engine 5.7.
- Git LFS for Unreal binary assets.
- The project currently references the local editor tooling plugins in `Plugins/VibeUE` and `Plugins/UnrealClaude`.

## Verified Build

The current state has been smoke-tested with:

- `NiceInkEditor Win64 Development`
- `NiceInk Win64 Development`
- PIE tattoo paint/save/load checks
- 2-player listen-server PIE replication check
- Windows package smoke build

