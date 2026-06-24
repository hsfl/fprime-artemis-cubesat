# F Prime v4 On ISIS iOBC: Decision Memo

## Bottom-Line Recommendation

If you keep the ISIS iOBC class hardware and need a defensible mission decision now, choose `F Prime v3.6.x`.

If you strongly want `F Prime v4`, it is not impossible, but it is not the default safe choice on this hardware. The reasonable path is:

1. Do a time-boxed `v4` feasibility spike on a target-correct `ARM926EJ-S` build.
2. Only continue with `v4` if the final stripped programmed image lands comfortably below the `1 MB` NOR budget.
3. If `v4` remains tight after real size work, change flight computer before changing framework.

I would not recommend a whole new software framework as the first response to this problem. Between the options available today:

- lowest risk: `v3.6.x` on the iOBC
- best long-term cleanliness if `v4` matters: change to a roomier flight computer
- highest schedule risk: force `v4` onto the iOBC and invent a new platform/size strategy under flight pressure

## Direct Answer To Your Question

### Is `v4` on the iOBC possible?

Yes, technically possible.

### Is it doable in a reasonable way?

Conditionally yes, but only if you accept all of the following:

- a target-specific `ARM9` port and build effort
- a very stripped-down topology, not the stock starter deployment
- aggressive size-first compiler and linker settings
- a release process that strips the image
- a pass/fail gate based on measured final flash image size

### Is compression alone the answer?

No.

Compression helps distribution size, but it is not the main answer for flight viability here. The main answer is reducing the actual linked code footprint.

### Do you need a whole different framework?

Probably not.

If the real choice is between:

- `v3.6.x` on this iOBC
- `v4` on a slightly larger or more modern OBC
- abandoning F Prime entirely

then abandoning F Prime is the least attractive of those three unless you already know you want a radically smaller, more custom, less component-heavy software architecture.

## Concise v3 vs v4 Software Difference

At a high level, `v4` is the newer and broader F Prime line, while `v3.6.x` is the more conservative choice for a constrained mission.

- `v3.6.x` is the lower-risk branch for a small flight computer because it has a smaller measured baseline in this study and aligns better with a "minimum viable flight stack" mindset.
- `v4` brings a broader modern framework surface and more built-in capability, which is attractive for long-term maintainability and future features, but that broader surface tends to cost code space and integration headroom.
- For your decision, the important difference is not that `v4` is "better" in the abstract. It is that `v4` asks more of the platform budget, while `v3.6.x` is easier to justify on tight heritage hardware.
- If hardware stays fixed, `v3.6.x` is the pragmatic software choice. If you want the newer F Prime line for programmatic reasons, the cleaner move is usually to give it more hardware margin instead of forcing it into a flash-constrained box.

## Findings

### 1. Current measured data says `v4` is close, not hopeless

From the existing ARMv6 Pi-style study in this repo:

- `v4.0.0` stripped binary: `1,022,504` bytes
- `1 MB` NOR assumed as `1,048,576` bytes
- current stripped margin: `26,072` bytes

That is too little margin to approve, but it is close enough that `v4` is not automatically dead.

Reference:

- [PI_ZERO_W_MINIMAL_FPRIME_SIZE_COMPARISON.md](PI_ZERO_W_MINIMAL_FPRIME_SIZE_COMPARISON.md)

### 2. Architecture mismatch is real

The measured binary today is not the final target class:

- measured binary: `ARMv6KZ` Linux PIE
- final CPU family: `ARM926EJ-S`, `ARMv5TEJ`

Microchip documents the `ARM926EJ-S` as an `ARM9` core implementing `ARM architecture version 5TEJ`, with support for both `ARM` and `THUMB` instruction sets.

That matters because:

- the code generator will change
- available instructions change
- code density changes
- runtime model changes
- Linux-vs-baremetal assumptions change

So the current measurement is directionally useful, but not final.

### 3. The current `v4` image is not dominated by Linux packaging overhead

From the measured `v4` ELF section data, the obvious dynamic/PIE sections total about `58 KB`.

That means:

- moving from Linux PIE to a target-correct non-PIE build may save meaningful space
- but it will not magically recover hundreds of kilobytes by itself

In other words, `v4` cannot be rescued by one trick. It needs real topology and framework trimming.

### 4. F Prime itself gives you real size levers

NASA's F Prime configuration guide explicitly exposes compile-time size levers, including:

- using `FW_FILEID_ASSERT` instead of filename asserts, which the docs say saves a lot of code space
- disabling `FW_PORT_SERIALIZATION` for single-node deployments
- disabling `FW_ENABLE_TEXT_LOGGING`
- disabling `FW_OBJECT_NAMES`
- disabling `FW_OBJECT_TO_STRING`, `FW_SERIALIZABLE_TO_STRING`, and `FW_ARRAY_TO_STRING`
- disabling `FW_OBJECT_REGISTRATION` and `FW_QUEUE_REGISTRATION`

Those are legitimate framework-supported reductions, not hacks.

### 5. GCC and binutils give you more legitimate levers

Official GCC/binutils documentation supports several standard size-reduction techniques:

- `-Os` optimizes for size
- `-Oz` optimizes more aggressively for size on newer GCC versions
- `-flto` enables link-time optimization
- `-ffunction-sections -fdata-sections` plus linker `--gc-sections` can reduce statically linked executables after stripping
- `-mthumb` is available on ARM targets and may improve code density on `ARM926EJ-S`
- `-fno-rtti` can save some space if RTTI is not needed, but GCC warns that mixing RTTI and non-RTTI objects may fail
- `-fno-threadsafe-statics` reduces code size slightly when thread-safe local static initialization is not needed
- `strip --strip-unneeded` removes symbols not needed for relocation processing

These are all reasonable things to try in a size campaign.

### 6. F Prime on this target will be your platform responsibility

The F Prime supported-platforms page does not list the `AT91SAM9G20` or an `ARM9` Linux/FreeRTOS platform as a maintained supported platform.

F Prime does document baremetal support and says baremetal systems should prefer passive or queued components over active components, but that is still a platform effort you would own.

This is the biggest non-size risk in the `v4 on iOBC` path:

- not just fitting the image
- also owning the platform port and maintenance burden

### 7. Compression is a secondary tactic, not the primary plan

UPX documents that it can compress supported executables substantially and that Linux executables are among the supported formats.

However, for flight software I would treat executable packing as a last-mile packaging tactic, not the primary design answer, because it adds:

- boot/runtime complexity
- decompression path risk
- verification burden
- less transparent fault isolation

If you need compression to make the difference between success and failure, your margin is already too thin.

## My Recommendation By Scenario

### Scenario A: hardware stays iOBC, schedule matters, flight risk matters

Choose `F Prime v3.6.x`.

This is the correct conservative call.

### Scenario B: hardware stays iOBC, but you really want `v4`

Do not commit to `v4` yet, but do not reject it blindly either.

Run a short feasibility gate with these assumptions:

- target CPU setting: `-mcpu=arm926ej-s`
- size-first optimization: `-Os` first, `-Oz` if toolchain supports it
- link-time optimization: `-flto`
- section GC: `-ffunction-sections -fdata-sections` and `--gc-sections`
- build stripped release images
- single-node deployment, so disable port serialization
- disable text logging, object names, object/queue registries, and toString features unless needed
- prefer passive/queued topology where possible
- do not include file handling, data products, or service-rich starter topology unless mission needs them at boot

Pass/fail rule:

- continue with `v4` only if the final programmed image is below about `800-850 KB`
- if it only barely slips under `1 MB`, reject it for flight use on this OBC

That margin rule is stricter than bare fit because you still need room for growth, integration error, and late mission logic.

### Scenario C: you want `v4` and hardware can still change

Change flight computer instead of changing framework.

This is the cleanest route if `v4` matters strategically.

Why:

- you avoid fighting the `1 MB` NOR ceiling
- you avoid heroic size tuning becoming part of normal development
- you keep room for mission code growth
- you reduce the chance that every later feature becomes a code-space negotiation

If I had to choose between:

- `v4` on too-small heritage hardware
- or `v4` on a slightly larger OBC with enough flash margin

I would choose the larger OBC.

### Scenario D: you are considering a whole new framework

Do not do that as the first move.

A new framework only makes sense if one of these is true:

- you need a radically smaller baremetal control loop architecture
- you no longer want F Prime's component model, tooling, or operational concepts
- your team is already prepared to build and maintain much more custom infrastructure

Otherwise, changing framework now is likely more expensive than either:

- staying on `v3.6.x`
- or changing hardware to support `v4`

### Scenario E: iOBC is fixed and `v4` is fixed

If both decisions are effectively locked, then the most reasonable workaround is a staged boot architecture:

- keep a small trusted image in NOR
- keep the larger `v4` application image on SD
- copy the selected SD image into RAM at boot
- fall back to the NOR-resident image if SD boot fails

This is feasible, but only if you treat it as a deliberate system architecture, not as a shortcut.

Recommended split:

- NOR:
  - immutable first-stage boot code
  - minimal safe-mode or recovery image
  - image verification and rollback entry logic
- FRAM:
  - active image slot
  - boot-attempt counters
  - rollback flags
  - health state and last-failure reason
- SD:
  - primary and backup `v4` application images
  - manifest or fixed image slots
  - read-only in nominal operations if practical

My recommendation is to avoid relying on a simple FAT32 filepath lookup as the primary boot contract. A fixed-slot or manifest-driven A/B image scheme is more robust and easier to reason about during recovery.

This workaround is reasonable only if all of the following are true:

- the NOR image is sufficient to boot safely without SD
- the SD-loaded `v4` image is treated as stage-2 application software, not the only survival path
- image integrity is verified before execution
- rollback state is persisted in FRAM
- boot failure returns the system to a known-safe NOR-resident mode

This is the best workaround if `v4` and iOBC are both mandatory, but it is still a higher-risk path than:

- `v3.6.x` directly on the iOBC
- or `v4` on a flight computer with more code-space margin

## Practical Technical Plan If You Want To Fight For `v4`

This is the only version of the `v4` plan I would consider reasonable.

### Step 1: build the right artifact

Produce a target-correct `ARM926EJ-S` image, not another Pi-style Linux proxy.

### Step 2: make `v4` smaller using supported knobs first

Use:

- F Prime config reductions
- stripped release image
- `-Os` or `-Oz`
- `-flto`
- `-ffunction-sections -fdata-sections`
- `--gc-sections`
- `-mthumb`

Only after that should you consider riskier flags like:

- `-fno-rtti`
- `-fno-threadsafe-statics`
- `-fno-exceptions`

And `-fno-exceptions` should only be used after auditing the actual build and runtime assumptions carefully.

### Step 3: build the right topology

Do not use the stock starter as your flight architecture baseline.

Build a core-flight subset only:

- scheduler / rate group
- command ingest if required
- minimal telemetry/events
- watchdog / fault handling
- board interfaces
- mission-critical control logic

Move everything else out of the initial boot image if possible.

### Step 4: reject magic answers

Do not base the mission on:

- UPX as the primary solution
- "it barely fits when stripped"
- assuming Linux packaging will somehow hide the real size problem

## Final Advice

Your in-house advice to prefer `v3.6.x` is sound.

My expert recommendation is:

- if you must keep the iOBC, choose `v3.6.x`
- if you strongly want `v4`, change hardware before changing framework
- only pursue `v4` on the iOBC if you can afford a focused feasibility effort and are willing to kill it quickly if it does not produce a comfortable margin

That is the honest middle ground:

- `v4` is possible
- `v4` on this iOBC is not the reasonable baseline
- `v3.6.x` is the safer software choice
- a larger flight computer is the safer hardware choice if `v4` is strategically important

## Sources

- NASA F Prime configuration guide: [Configuring F´](https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/configuring-fprime/)
- NASA F Prime baremetal guide: [F´ on Baremetal Systems](https://fprime.jpl.nasa.gov/devel/docs/user-manual/framework/run-baremetal/)
- NASA F Prime supported platforms: [Supported Platforms](https://fprime.jpl.nasa.gov/devel/docs/user-manual/framework/supported-platforms/)
- Microchip ARM926EJ-S reference: [ARM926EJ-S Processor](https://onlinedocs.microchip.com/oxy/GUID-B822915F-C375-4172-91BD-AB6F326EB783-en-US-1/GUID-18DFE37F-F488-4BE7-9CEF-E083646FB79F.html)
- Microchip AT91SAM9G20 summary datasheet: [AT91SAM9G20 Summary PDF](https://ww1.microchip.com/downloads/en/DeviceDoc/6384s.pdf)
- GCC optimization options: [Optimize Options](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)
- GCC ARM options: [ARM Options](https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html)
- GCC C++ dialect options: [C++ Dialect Options](https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html)
- GNU binutils strip: [strip](https://sourceware.org/binutils/docs/binutils/strip.html)
- UPX project: [UPX](https://upx.github.io/)
