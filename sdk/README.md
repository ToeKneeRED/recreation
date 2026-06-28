# Recreation SDK (`sdk/`)

The C# world the engine hosts: the API mods are written against, the multiplayer
platform, the per-game rulesets, and the host entrypoint the native engine calls.
This used to live at `engine/script/managed/`; it now sits at the top level
because it is a public-facing artifact (mod authors build against it), not an
engine internal. The native bridge (`engine/script/host/`) stays engine-side.

## Layout

| Path | What it is | Audience |
|------|------------|----------|
| `Engine/`, `Modding/`, `Interop/` | the SDK surface: `Game`/`Form`/`Actor`/`Quest`, the modding framework (`IMod`, `EventBus`, `GameBehaviour`, `ModHost`), the native marshalling types | mod authors |
| `Net/`, `Teams/`, `Economy/`, `Admin/`, `Persistence/`, `Voice/`, `Map/`, `Hud/`, `Chat/`, `Social/`, `Scoreboard/`, `Browser/`, `Entities/`, `Ui/` | the multiplayer platform (`Recreation.Net`) | mixed: API + impl |
| `default_gamemodes/{Skyrim,Fallout,Starfield}` | per-game soft-logic, optional pre-provided content (still namespaced `Recreation.Games.*`) | examples |
| `Samples/` | example gamemodes (wire-sync, roleplay) | examples |
| `ScriptHost.cs`, `SdkInfo.cs` | the managed `Main` entrypoint + version accessor | engine glue |
| `tests/` | the dependency-free test runner (`dotnet run`) | contributors |
| `templates/mod/` | copy-out starter for a drop-in mod | mod authors |

The SDK proper compiles into one assembly, `Recreation.Scripting`. The default
gamemodes build as their own assemblies loaded at runtime (see below). Splitting
the stable contract into its own `Recreation.Sdk` assembly is the remaining step
(Roadmap).

## Building

```sh
# from the nix dev shell (dotnet is only on PATH there)
./tools/build_managed.sh            # -> build/managed/Recreation.Scripting.dll
                                    #    + build/managed/gamemodes/Recreation.{Skyrim,Fallout,Starfield}.dll
RECREATION_SCRIPTING_DIR=build/managed ./run-local.sh ...
```

Tests: `cd sdk/tests && dotnet run -c Release` (also wired into `ctest` as
`managed_scripting_tests` when a .NET SDK is on the configure PATH).

## Default gamemodes

The per-game rulesets in `default_gamemodes/` build as **separate assemblies**
(`Recreation.Skyrim/Fallout/Starfield`), not part of the core SDK. At boot the
host preloads the `gamemodes/` directory beside `Recreation.Scripting.dll`, so
each ruleset loads and registers exactly like a built-in mod did — but it is now
optional content:

- Delete a DLL from `gamemodes/` to drop that game.
- `RECREATION_NO_GAMEMODES=1` skips them all (a barebones session).
- `RECREATION_GAMEMODES_DIR=<dir>` loads them from elsewhere.

Each game references the SDK compile-time-only, so the DLLs carry no SDK copy and
bind to the engine's loaded one (same rule as any mod). A ruleset only activates
when its game is the primary domain, so loading all three is harmless.

## Versioning

The SDK is versioned with **SemVer**, set once in `Directory.Build.props`
(`<Version>`) and surfaced at runtime via `SdkInfo.Version` (logged at boot).

- **Additive** change (new API, no break) -> bump **minor** (`1.2` -> `1.3`).
- **Breaking** change (removed/changed public API) -> bump **major** (`1.x` -> `2.0`).

A mod built against SDK **X.Y runs on any engine shipping SDK X.>=Y**. A major
bump is the only thing that breaks an existing mod. This promise is only as
trustworthy as the contract is clean, which is the motivation for Stage 2: while
game logic shares the assembly, every `public` change there is technically an API
change. Keep game-internal types `internal`.

### The two boundaries

- **C# API** (mod ↔ SDK): the SemVer contract above. This is what a mod author
  cares about.
- **Native ABI** (SDK ↔ engine): `engine/script/host/bridge.h`, a fixed-layout
  POD struct mirrored byte-for-byte in `Interop/`. Mods never touch it; only the
  SDK does, and the SDK + engine are always built and shipped together, so they
  are always matched. Bump it independently when the struct layout changes.

## Building a stable mod

Copy `templates/mod/` out of the repo and build it. The key detail: the SDK is a
**compile-time-only** reference (`ExcludeAssets=runtime`, `PrivateAssets=all`), so
`Recreation.Scripting.dll` is **not** copied next to your mod. At runtime the mod
binds to the engine's in-memory SDK; shipping your own copy would split assembly
identity and the mod would silently fail to load.

```sh
dotnet pack -c Release sdk/Recreation.Scripting.csproj   # -> Recreation.Scripting.<ver>.nupkg
# point a local nuget source at it, then build the mod against the pinned version
dotnet build -c Release            # in your mod folder
cp bin/Release/net9.0/MyMod.dll  $RECREATION_MODS_DIR/
```

Pin the SDK version in the mod's csproj; the mod keeps working across engine
updates until the next major SDK bump. See `MODDING.md` for the streaming and
multiplayer workflow.

## Roadmap

- **Stage 1 (done):** relocated to `sdk/`, single versioned assembly, packable as
  a NuGet (`dotnet pack`), `SdkInfo.Version` at boot, mod template.
- **Stage 2 (done):** the default gamemodes build as separate, droppable
  assemblies loaded from `gamemodes/` at boot, out of the core SDK.
- **Stage 3 (when contract churn bites):** carve `Recreation.Sdk` (the
  `Engine/ Modding/ Interop/` contract + platform public API) out of
  `Recreation.Scripting`, leaving the platform impl and samples behind it. This
  makes the version number trustworthy: the changelog stops being game-logic noise.
