---
name: fprime-docs-search
description: "Search and retrieve F´ (F Prime) official documentation from NASA JPL website. Use when you need to look up framework components (Drv, Svc), design patterns, build system details, GDS features, FPP language specs, or any F´ concepts not covered in local skills. Provides categorized documentation index and WebFetch guidance."
---

# F´ Documentation Search Skill

**Prefer repo-local docs first.** This repo has a pinned F' submodule at `ArtemisRpiTeensy_N2/lib/fprime/docs` — these docs exactly match the version the project builds against. Use `rg` under that path to find specifics before falling back to web docs. Only use the web URLs below when the local docs are missing a topic.

## When to Use

Use this skill when:
- You need official F´ framework documentation
- Looking up specific components (Drv.*, Svc.*, Fw.*)
- Need details on build system, CMake, or toolchains
- Want to understand design patterns (rate groups, hub pattern, etc.)
- Need GDS (Ground Data System) documentation
- Looking for FPP (F Prime Prime) language specifications
- Troubleshooting issues not covered in local skills
- Need API reference or component SDDs (Software Design Documents)

## How to Use

1. **Check local docs first:** `rg -l <topic> ArtemisRpiTeensy_N2/lib/fprime/docs`
2. **If not found, identify topic category** from the index below
3. **Find relevant URL(s)** for your question
4. **Use WebFetch tool** to retrieve and search documentation:

```
WebFetch(
  url: "<documentation-url>",
  prompt: "What does this page say about <your-specific-question>?"
)
```

## Documentation Index

### 🚀 Getting Started

**Installation & Setup:**
- Main docs: `https://fprime.jpl.nasa.gov/latest/docs/`
- Getting started: `https://fprime.jpl.nasa.gov/latest/docs/getting-started/`
- Installing F´: `https://fprime.jpl.nasa.gov/latest/docs/getting-started/installing-fprime/`

**Tutorials:**
- All tutorials: `https://fprime.jpl.nasa.gov/latest/docs/tutorials/`
- Cross-compilation: `https://fprime.jpl.nasa.gov/latest/docs/tutorials/cross-compilation/`

### 📚 User Manual - Overview

**Core Concepts:**
- Full introduction: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/01-full-intro/`
- F´ architecture: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/02-fprime-architecture/`
- Ports, components, topologies: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/03-port-comp-top/`
- Commands, events, channels, parameters: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/04-cmd-evt-chn-prm/`
- Enums, arrays, serializables: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/05-enum-arr-ser/`

**Project Structure:**
- Projects & deployments: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/proj-dep/`
- Source tree layout: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/source-tree/`
- Development practices: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/development-practice/`
- Unit testing: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/unit-testing/`

**GDS Introduction:**
- GDS overview: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/gds-introduction/`

### 🔧 User Manual - Framework

**Framework Features:**
- Building topologies: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/building-topology/`
- Autocoded functions: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/autocoded-functions/`
- Ground interface: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/ground-interface/`
- State machines: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/state-machines/`
- Data products: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/data-products/`

**Configuration & Platforms:**
- Configuring F´: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/configuring-fprime/`
- Supported platforms: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/supported-platforms/`
- Baremetal/multicore: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/baremetal-multicore/`
- Dynamic memory: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/dynamic-memory/`
- Assert handling: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/assert/`

### 🏗️ User Manual - Build System

**CMake Build System:**
- CMake introduction: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/01-cmake-intro/`
- CMake API: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-api/`
- Settings: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/settings/`
- Targets: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-targets/`
- Platforms: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-platforms/`
- Toolchains: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-toolchains/`
- Unit tests: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-uts/`
- Customization: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-customization/`
- Implementations: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-implementations/`

### 🎨 User Manual - Design Patterns

**Common Patterns:**
- Rate groups: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/rate-group/`
- Hub pattern: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/hub-pattern/`
- Manager-worker: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/manager-worker/`
- Health checking: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/health-checking/`
- Common port patterns: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/common-port-patterns/`
- App-manager-driver: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/app-man-drv/`
- Subtopologies: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/subtopologies/`

### 🖥️ User Manual - GDS (Ground Data System)

**GDS Tools:**
- GDS CLI: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/gds/gds-cli/`
- GDS development guide: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/gds/gds-dev-guide/`
- Dashboard reference: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/gds/gds-dashboard-reference/`
- Custom dashboards: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/gds/gds-custom-dashboards/`
- Test API guide: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/gds/gds-test-api-guide/`
- Sequence generator: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/gds/seqgen/`

### 🔌 Framework Components

**Drv (Drivers):**
- All drivers: `https://fprime.jpl.nasa.gov/latest/docs/Drv/`

**Svc (Services):**
- All services: `https://fprime.jpl.nasa.gov/latest/docs/Svc/`
- Framer: `https://fprime.jpl.nasa.gov/latest/docs/Svc/Framer/`
- Framer SDD: `https://fprime.jpl.nasa.gov/latest/docs/Svc/Framer/docs/sdd.md`
- Deframer: `https://fprime.jpl.nasa.gov/latest/docs/Svc/Deframer/`
- Deframer SDD: `https://fprime.jpl.nasa.gov/latest/docs/Svc/Deframer/docs/sdd.md`

**Subtopologies:**
- All subtopologies: `https://fprime.jpl.nasa.gov/latest/docs/Svc/Subtopologies/`
- CdhCore: `https://fprime.jpl.nasa.gov/latest/docs/Svc/Subtopologies/CdhCore`
- ComCcsds: `https://fprime.jpl.nasa.gov/latest/docs/Svc/Subtopologies/ComCcsds`
- ComFprime: `https://fprime.jpl.nasa.gov/latest/docs/Svc/Subtopologies/ComFprime/`
- DataProducts: `https://fprime.jpl.nasa.gov/latest/docs/Svc/Subtopologies/DataProducts`
- FileHandling: `https://fprime.jpl.nasa.gov/latest/docs/Svc/Subtopologies/FileHandling`

### 📖 How-To Guides

**Development Guides:**
- All how-tos: `https://fprime.jpl.nasa.gov/latest/docs/how-to/`
- Device driver development: `https://fprime.jpl.nasa.gov/latest/docs/how-to/develop-device-driver/`
- F´ libraries: `https://fprime.jpl.nasa.gov/latest/docs/how-to/develop-fprime-libraries/`
- Subtopologies: `https://fprime.jpl.nasa.gov/latest/docs/how-to/develop-subtopologies/`
- GDS plugins: `https://fprime.jpl.nasa.gov/latest/docs/how-to/develop-gds-plugins/`
- State machines: `https://fprime.jpl.nasa.gov/latest/docs/how-to/define-state-machines/`
- Test-driven development: `https://fprime.jpl.nasa.gov/latest/docs/how-to/test-driven-development/`

**Integration & Porting:**
- External libraries: `https://fprime.jpl.nasa.gov/latest/docs/how-to/integrate-external-libraries/`
- Porting guide: `https://fprime.jpl.nasa.gov/latest/docs/how-to/porting-guide/`
- Custom framing: `https://fprime.jpl.nasa.gov/latest/docs/how-to/custom-framing/`
- Derive channels on ground: `https://fprime.jpl.nasa.gov/latest/docs/how-to/derive-channels-on-ground/`

### 🔐 Security

**Security Documentation:**
- Software bill of materials: `https://fprime.jpl.nasa.gov/latest/docs/user-manual/security/software-bill-of-materials/`

### 📋 API Reference

**API Documentation:**
- API reference: `https://fprime.jpl.nasa.gov/latest/docs/reference/`

### 🌐 General Information

**Overview Pages:**
- Main overview: `https://fprime.jpl.nasa.gov/overview/`
- Powerful SWA: `https://fprime.jpl.nasa.gov/overview/powerful-swa/`
- Streamline SD: `https://fprime.jpl.nasa.gov/overview/streamline-sd/`

## Common Search Patterns

### Pattern 1: Component Lookup

**Question:** "How does the Framer component work?"

**Action:**
```
WebFetch(
  url: "https://fprime.jpl.nasa.gov/latest/docs/Svc/Framer/docs/sdd.md",
  prompt: "Explain how the Framer component works, its ports, and typical usage"
)
```

### Pattern 2: Concept Deep Dive

**Question:** "What are rate groups and how do I use them?"

**Action:**
```
WebFetch(
  url: "https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/rate-group/",
  prompt: "Explain rate groups: what they are, how to configure them, and common usage patterns"
)
```

### Pattern 3: Build System Question

**Question:** "How do I configure CMake toolchains?"

**Action:**
```
WebFetch(
  url: "https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-toolchains/",
  prompt: "How do I configure custom CMake toolchains for F´ projects?"
)
```

### Pattern 4: GDS Usage

**Question:** "How do I use the GDS test API?"

**Action:**
```
WebFetch(
  url: "https://fprime.jpl.nasa.gov/latest/docs/user-manual/gds/gds-test-api-guide/",
  prompt: "Explain how to use the GDS test API for integration testing, with examples"
)
```

### Pattern 5: FPP Language Features

**Question:** "How do I define state machines in FPP?"

**Action:**
```
WebFetch(
  url: "https://fprime.jpl.nasa.gov/latest/docs/how-to/define-state-machines/",
  prompt: "How do I define and implement state machines in F´ using FPP?"
)
```

### Pattern 6: Architecture Understanding

**Question:** "What's the overall F´ architecture?"

**Action:**
```
WebFetch(
  url: "https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/02-fprime-architecture/",
  prompt: "Explain the F´ architecture: layers, component model, and how pieces fit together"
)
```

### Pattern 7: Multiple Related Pages

**Question:** "How do commands, events, and telemetry work together?"

**Action (fetch multiple):**
```
# First get the overview
WebFetch(
  url: "https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/04-cmd-evt-chn-prm/",
  prompt: "Explain commands, events, channels, and parameters in F´"
)

# Then get ground interface details if needed
WebFetch(
  url: "https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/ground-interface/",
  prompt: "How does the ground interface handle commands, events, and telemetry?"
)
```

## Topic Quick Reference

**When you need info about...**

| Topic | URL Category | Start Here |
|-------|--------------|------------|
| Getting started | Getting Started | `docs/getting-started/` |
| Component basics | User Manual - Overview | `03-port-comp-top/` |
| Commands/Events/Telemetry | User Manual - Overview | `04-cmd-evt-chn-prm/` |
| Topology wiring | User Manual - Framework | `building-topology/` |
| Build errors | User Manual - Build System | `01-cmake-intro/`, `settings/` |
| Rate groups | Design Patterns | `rate-group/` |
| GDS commands | User Manual - GDS | `gds-cli/` |
| Testing | User Manual - Overview | `unit-testing/`, How-To `test-driven-development/` |
| Cross-compilation | Tutorials | `cross-compilation/` |
| Drivers (GPIO, SPI, etc.) | Framework Components | `docs/Drv/` |
| Services (Framer, etc.) | Framework Components | `docs/Svc/` |
| Custom components | How-To Guides | `develop-device-driver/`, `develop-fprime-libraries/` |
| State machines | How-To Guides | `define-state-machines/` |
| Integration tests | User Manual - GDS | `gds-test-api-guide/` |
| API reference | API Reference | `docs/reference/` |

## Usage Tips

1. **Local docs first:** `rg -l <keyword> ArtemisRpiTeensy_N2/lib/fprime/docs` before hitting the web
2. **Start broad, then narrow:** Begin with overview pages, then dive into specifics
3. **Use WebFetch prompts to filter:** Be specific in your prompt to get relevant info
4. **Cross-reference:** Related topics often span multiple pages
5. **Check SDDs for components:** Software Design Documents have implementation details
6. **Combine with local skills:** Use docs for "what/why", local skills for "how in this repo"

## Notes

- Documentation is versioned (`/latest/` in URLs) - these links are for latest version
- Some pages are long - use specific WebFetch prompts to extract relevant sections
- SDD (Software Design Document) pages have detailed component specifications
- Official tutorials are separate from how-to guides (tutorials are complete walkthroughs)
- When stuck, start with overview pages to build mental model, then dive deeper
