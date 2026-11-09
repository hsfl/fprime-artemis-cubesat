---
name: fprime-docs-search
description: "Search and retrieve F´ (F Prime) documentation. Use when you need to look up framework components (Drv, Svc, Fw), design patterns, build system/CMake details, GDS features, FPP language syntax/specs, or any F´ concept not covered in local skills. Local pinned docs first, then official web docs."
---

# F´ Documentation Search

## Priority Order

1. **Repo-local pinned docs.** Search the selected deployment's F′ checkout first when present. For the Pi deployment, use `ArtemisRpiTeensy_N2/lib/fprime/docs`.
2. **FPP language docs** — separate site: `https://nasa.github.io/fpp/fpp-users-guide.html` (syntax, how-to) and `https://nasa.github.io/fpp/fpp-spec.html` (formal spec). FPP is NOT covered on the main F' docs site.
3. **Official web docs.** Use them when local docs are unavailable or lack the topic.

**Version drift warning:** web URLs are `/latest/`; the submodule is pinned. Check the pinned version with `git -C ArtemisRpiTeensy_N2/lib/fprime describe --tags`. When web and local docs disagree, trust local — it matches what actually compiles here.

## Local Search Recipes

```bash
# Topic search across framework docs
rg -il '<topic>' ArtemisRpiTeensy_N2/lib/fprime/docs

# Component SDDs (design docs live next to the code, not under docs/)
rg --files ArtemisRpiTeensy_N2/lib/fprime | rg '/docs/.*\.md$' | rg -i '<component>'

# Direct SDD paths: Svc/<Comp>/docs/sdd.md, Drv/<Comp>/docs/sdd.md, Fw/..., Os/..., Utils/...
```

Key local entry points (all under `ArtemisRpiTeensy_N2/lib/fprime/docs/`):

| Need | Path |
|------|------|
| Index / manual / tutorials | `index.md`, `user-manual/index.md`, `tutorials/index.md` |
| Architecture, ports/components/topologies | `user-manual/overview/02-fprime-architecture.md`, `03-port-comp-top.md` |
| Commands/events/channels/params | `user-manual/overview/04-cmd-evt-chn-prm.md` |
| Unit testing / TDD | `user-manual/overview/unit-testing.md`, `how-to/test-driven-development.md` |
| Build system / CMake / toolchains | `user-manual/build-system/*.md` |
| Topology assembly | `user-manual/framework/building-topology.md` |
| State machines | `how-to/define-state-machines.md`, `user-manual/framework/state-machines.md` |
| Design patterns (rate group, hub, manager-worker, health) | `user-manual/design-patterns/*.md` |
| GDS (CLI, plugins, test API, dashboards) | `user-manual/gds/*.md`, `how-to/develop-gds-plugins.md` |
| Ground interface / framing | `user-manual/framework/ground-interface.md`, `how-to/custom-framing.md` |
| Communication adapter, JSON dict, numerical types | `reference/*.md` |

## Web Fallback

Use the host's available web search or fetch capability. Start at the [F′ documentation index](https://fprime.jpl.nasa.gov/latest/docs/) for the user manual, tutorials, component docs, and API reference. Open the relevant page and cite it. If a topic or component moved between releases, inspect the [NASA F′ source](https://github.com/nasa/fprime) at the selected deployment's pinned revision.

## Tips

- SDDs (`docs/sdd.md` next to component source) carry the implementation-level detail; user-manual pages carry concepts.
- Use web docs for "what/why", repo-local skills (`fprime-swe`, `fprime-cross-compilation`) for "how in this repo".
- For FPP syntax errors, go straight to the FPP User's Guide (`nasa.github.io/fpp`), not the main docs site.
