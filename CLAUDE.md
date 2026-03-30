# CLAUDE.md - EagleWalk (Sphinx) Project Convention

## Project Overview
EagleWalk is a UE 5.6 military simulation project combining:
- **Cesium for Unreal** — 3D globe, real-world terrain tiles
- **GISProcedural** (our plugin) — GIS-driven procedural content generation (buildings, vegetation, roads, water)
- **UltraDynamicSky (UDS)** — dynamic sky, weather, and time-of-day system

Repository structure:
```
/                         ← UE project root (eaglewalk.uproject)
├── Config/               ← UE config
├── Content/              ← UE content (Cesium samples, our content)
├── Plugins/
│   ├── GISProcedural/    ← Our GIS plugin (C++, 46 files)
│   └── UltraDynamicSky/  ← UDS plugin (added by user)
├── Tests/                ← Python test suite + mock data
├── Tools/                ← Python preprocessing pipeline
├── agents/               ← Agent definitions
├── PLAN.md               ← Implementation roadmap
└── README.md
```

## Language
- Code, comments, commit messages: **English**
- Agent communication (SHARED.md), user-facing chat: **Chinese** (unless user switches)

## Code Conventions
- UE5 C++ naming: `U`/`A`/`F`/`E` prefixes per UE convention
- Plugin module dependencies declared in `.Build.cs`
- Header include order: Module header → UE headers → Project headers
- No `using namespace` in headers

## Branch
- Development branch: `claude/integrate-ultradynamicsky-sphinx-MPibj`
- Always push to this branch unless instructed otherwise

## Agents
| Agent | Name | Scope |
|-------|------|-------|
| uds   | 小U  | UltraDynamicSky integration, weather, sky, lighting, time-of-day |

## Commit Convention
- Concise message describing "why" not "what"
- One logical change per commit
