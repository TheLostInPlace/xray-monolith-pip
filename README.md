# X-Ray Monolith True Picture-in-Picture

Objective-camera picture-in-picture scopes for S.T.A.L.K.E.R. Anomaly, built on the
multithreaded branch of [X-Ray Monolith](https://github.com/themrdemonized/xray-monolith).

> [!IMPORTANT]
> This project is under active development. The engine, bundled `gamedata`, and external
> compatibility patches must come from the same revision. Mixing versions can make scopes
> fall back to the legacy shader path or produce incorrect optic behavior.

## What true PiP means

Traditional shader scopes magnify or distort the main camera image. This project renders a
second view of the world through the physical objective of the optic.

The current implementation provides:

- one entrance-pupil camera for the scoped world and eligible weapon geometry;
- a synchronized weapon pose across the main view and scope view;
- continuous barrel and handguard geometry through the objective;
- authored or measured objective diameter, eye relief, exit pupil, and magnification;
- physical field-stop, pupil, twilight, tunneling, and near-field behavior;
- off-axis deferred reconstruction from the active scope projection;
- separate scope exposure, light capture, bloom, emissive, grass, and LOD controls;
- a hard TAA exclusion stamp for the lens image and reticle;
- objective-only NVG processing without copying the wearer's mask into the scope;
- hybrid magnifier support with reflex-element capture;
- typed Lua-to-engine optic profiles with legacy LTX and script fallbacks;
- diagnostic overlays and structured runtime logging.

`r__svpscope 0` keeps the original non-PiP path. Any positive value selects objective true
PiP; the retired classic mode is not a separate rendering path.

## Compatibility

The engine is designed to preserve the existing Anomaly and Modded Exes ecosystem. Legacy
weapon sections, upgrades, DLTX patches, 3DSS parameters, Mark Switch data, and script-driven
zoom controllers remain supported.

Tested integration targets include:

- 3D Shader Scopes for GAMMA;
- ARC 3DSS content;
- Modular Attachment System;
- Modular Attachment System for GAMMA;
- Pizza's Ultimate Sight Selection;
- hybrid reflex and magnifier combinations.

MAS, MASG, and Pizza are optional. A regular 3DSS setup does not require them.

Third-party mods and their assets are not included in this repository. Shader compatibility
files derived from 3DSS are distributed separately and should be installed as their own MO2
patch above the scope mods they extend.

## Installing a matched build

There is no public release archive yet. For development builds:

1. Install the required 3DSS and weapon-mod dependencies through MO2.
2. Build either `DX11-AVX | x64` or `DX11 | x64`.
3. Copy the matching executable and PDB from `_build/_game/bin_dbg` into the Anomaly `bin`
   directory.
4. Install this repository's `gamedata` as a separate MO2 mod, or package it while preserving
   its directory layout.
5. Install the matching 3DSS PiP compatibility patch at higher MO2 priority than 3DSS.
6. Clear `appdata/shaders_cache` before the first launch after shader changes.

Do not install only the executable. The scripts, configs, and shaders in `gamedata` are part
of the runtime contract.

## Building

Requirements:

- Windows 10 or newer;
- Visual Studio 2022;
- Desktop development with C++;
- current MSVC, Windows SDK, MFC, and ATL components;
- Git submodules initialized recursively.

```powershell
git clone --recursive https://github.com/TheLostInPlace/xray-monolith-pip.git
```

Open `src/engine-vs2022.sln`, select an x64 DX11 configuration, and build the solution. The
current PiP implementation targets DX11.

## Configuration

The normal Modded Exes options page exposes:

- Picture in Picture;
- smooth variable zoom;
- analog zoom input.

The in-game ImGui menu contains the supported optical, rendering, and performance controls.
Experimental invariants are intentionally fixed internally rather than exposed as player
settings.

Important console commands:

| Command | Purpose |
| --- | --- |
| `r__svpscope 0` | Disable PiP and retain the original rendering path |
| `r__svpscope 2` | Enable objective true PiP |
| `r__svp_diag 1` | Enable throttled pass and activation logging |
| `r__svp_cop_diag 1` | Log optic camera and HUD-pose state |
| `r__svp_cop_diag 2` | Add detailed per-frame alignment data |
| `r__svp_report 1` | Emit a one-shot configuration and file report |
| `r__scope_debug 1` to `4` | Show camera, buffer, mask, and geometry diagnostics |
| `svp_dump_optic` | Dump the resolved typed optic profile and provenance |

## Modder integration

Existing weapon and scope definitions remain valid. New physical data can be authored without
replacing the legacy fields used by older engines.

Relevant public integration points:

- `gamedata/configs/pip_optic_profiles.ltx` for per-optic physical profiles;
- `gamedata/configs/pip_hybrid_optics.ltx` for explicit hybrid-reflex state;
- `gamedata/scripts/zzz_extra_scope_features.script` for capability-probed publication;
- `gamedata/scripts/ltx_help_ex.script` for supported configuration fields;
- `svp_dump_optic` for resolved values and their source sections.

The typed optic API is versioned and capability-probed. On an older executable, the script
omits unsupported calls and retains the legacy route instead of assuming fork-only exports.

## Reporting problems

Include:

- the exact weapon and optic section names;
- whether the issue occurs with `r__svpscope 0` or `2`;
- `appdata/logs/pip.log`;
- the current `appdata/logs/xray_*.log`;
- the relevant portion of the MO2 mod list;
- a short video when the problem is motion-dependent.

For activation or zoom problems, test once with `r__svp_diag 1`. For camera or continuity
problems, also enable `r__svp_cop_diag 2`.

## Credits

- [X-Ray Monolith](https://github.com/themrdemonized/xray-monolith) and its contributors;
- the original PiP work in the [gc64 fork](https://github.com/CnRJay/xray-monolith-gc64);
- the authors and maintainers of 3DSS, ARC, MAS, MASG, and the supported weapon packs;
- the OpenXRay, IX-Ray, Call of Chernobyl, and wider Anomaly modding communities.

## License

This repository inherits the upstream X-Ray engine licensing terms. See
[License.txt](License.txt). The original S.T.A.L.K.E.R. engine code is available for
non-commercial use under its applicable terms.
