# SDD diagrams

Each figure has an editable diagrams.net (`.drawio`) source, an SVG, a
`.layout.json` geometry file, and a PNG preview used in the DOCX. Keep these
representations synchronized when editing a figure.

Edit `.drawio` files in diagrams.net. Export a matching PNG for the document
preview. `../source/build_diagrams.py` can regenerate the original geometry,
but overwrites manual layout changes unless those changes are also made in the
builder.
