# Repo Context and Notes (Tutorial 3)

## Repo layout
- Project root: repo root (this file lives at `.claude/skills/fprime-cross-compilation/references/context.md`)
- Active tutorial project: `MyProject/`
- Tutorial notes: `docs/tutorial-3-cross-compilation/`

## Prior status
- Docker daemon was not running when attempting:
  `docker pull nasafprime/fprime-arm:latest`
- Native build succeeded for `HelloWorldDeployment` (warnings captured in notes).

## Suggested deployment target
- Default deployment for tutorial steps: `MyProject/HelloWorldDeployment`

## Useful commands (host)
- Activate venv: `source MyProject/fprime-venv/bin/activate`
- Native build (if needed): `cd MyProject/HelloWorldDeployment && fprime-util build`

## Useful commands (container)
- Export toolchain path: `export ARM_TOOLS_PATH=/opt/toolchains`
- Generate/build:
  - `fprime-util generate aarch64-linux`
  - `fprime-util build aarch64-linux`
