# Artemis CubeSat Kit SDD

Version 1.0 is the living design baseline for the Artemis FSW port. It describes
the target Teensy/Zephyr bus authority, Pi/Linux payload deployment, and Yamcs
ground interface. It is a design target; it does not claim those allocations
are implemented or verified in this repository.

## Package contents

- `Artemis_CubeSat_Kit_SDD_v1.0.docx`: team-facing document.
- `source/Artemis_SDD.md`: editable document source.
- `source/build_docx.py`: rebuilds the DOCX from the Markdown and diagram PNGs.
- `source/build_diagrams.py`: regenerates the editable SVG, diagrams.net, and
  layout sources from the original geometry.
- `diagrams/`: editable `.drawio` and `.svg` sources, `.layout.json` geometry,
  and PNG previews embedded in the DOCX.

The interview decisions are in [`../ARTEMIS_SDD_INTERVIEW_DECISIONS.md`](../ARTEMIS_SDD_INTERVIEW_DECISIONS.md).

## Editing

For a text change, edit `source/Artemis_SDD.md`, then rebuild the DOCX from this
directory with `python3 source/build_docx.py` (requires `python-docx`). Check
page breaks and contents-page numbering after rebuilding.

For a diagram change, edit the `.drawio` file in diagrams.net and export a PNG
with the same basename to keep the DOCX preview synchronized. Keep the SVG and
layout source synchronized as well. `source/build_diagrams.py` regenerates the
original diagram geometry and will overwrite manual layout changes.

## Status

This is a design baseline for implementation, not completed flight software,
hardware-in-the-loop evidence, or flight qualification. The open allocations
and verification matrix in the SDD define closure work. Keep this document,
the FPP model, component documentation, ICDs, and tests aligned as the port
progresses.

The 22 September 2026 Pi power correction selects the PDU-regulated `BUS_5V`
feeding the OBC U2 Pi load switch, controlled by Teensy pin 36. The as-built
power path and loaded behavior remain open under SDD item O2.
