# Changelog

All notable changes to Recreation are documented here. The main menu's NEWS rail
shows the most recent entries (the first bullet of each release is its headline).

## [0.3.0] - 2026-06-23
### Added
- Cinematic main menu is live
- Full-bleed three-pane front screen with a procedural 3D hero object per universe
- Per-universe atmospheric backdrops generated at runtime, no external art
- Functional social/settings shortcuts on the bottom bar
### Fixed
- UI canvas tracks the swapchain extent, fixing the half-screen black bar
- Cursor mapping scaled to the backbuffer so menu clicks land precisely

## [0.2.0] - 2026-05-30
### Added
- Cross-game NPC reaction layer
- Wanted levels, guard response, daily routines and bystander alarm
- Damage resistance, sneak/crit and legendary layers for Fallout 4 and Starfield
### Changed
- Quest-driven world edits now replicate across multiplayer sessions

## [0.1.0] - 2026-04-18
### Added
- Native quest graph engine
- Quest graph IR with a condition layer, a superset of Skyrim QUST
- C# scripting host bridged to the ultragui runtime
- Placed NPCs load as ECS entities with authoritative client activation
