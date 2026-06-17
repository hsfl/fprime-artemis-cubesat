---
name: student-git-handoff
description: "Use when helping non-technical students use Codex, Claude Code, or another coding agent with GitHub: choosing a Software Dev Lead-approved base branch, creating a student feature branch, completing work, inspecting changes, committing, pushing, and preparing a GitHub pull request for instructor or lead review. Also use for safe student Git workflows, commit/push handoff, branch hygiene, pull request summaries, and avoiding destructive Git commands."
---

# Student Git Handoff

## Operating Style

Keep the workflow simple, explicit, and student-safe. Use BLUF first, plain language, and short steps. Prefer GitHub terminology: say "pull request", not "merge request". Use feature branches only; do not introduce formal Gitflow, release branches, hotfix branches, or history rewriting unless the user explicitly asks.

Assume the student may not understand Git. Explain what each Git step does before running it when it changes repository state.

## Default Workflow

1. Confirm repository state.
2. Ask the student to get the Software Dev Lead-approved base branch.
3. Create a `student/<short-task-name>` feature branch from that exact base branch.
4. Complete the requested task.
5. Show changed files and summarize them in plain language.
6. Commit only task-relevant files with a simple message.
7. Push the student branch.
8. Provide a pull request handoff with branch, base branch, commit, push status, and PR text.

## Before Branching

Do not assume the base branch is `main`, `master`, or `develop`. Many student repos have multiple active branches.

Before creating a feature branch:

1. Inspect the repo:
   ```bash
   git status --short
   git branch --show-current
   git remote -v
   git fetch --all --prune
   ```
2. Tell the student to ask the Software Dev Lead which branch to start from.
3. Wait for the base branch name before branching.
4. Create the feature branch from the exact approved base:
   ```bash
   git switch <approved-base-branch>
   git pull --ff-only
   git switch -c student/<short-task-name>
   ```

If the approved base exists only on the remote, create from `origin/<approved-base-branch>`:

```bash
git switch -c student/<short-task-name> origin/<approved-base-branch>
```

Prefer `student/<short-task-name>` branch names. Keep names lowercase, short, and hyphen-separated, such as `student/add-login-page` or `student/fix-grade-calculator`.

## Student-Safe Git Rules

Stop and ask before:

- choosing a base branch without Software Dev Lead confirmation
- running `git reset --hard`
- deleting branches
- force pushing
- rebasing public or shared branches
- resolving merge conflicts when the intended code is unclear
- committing unrelated changed files

Never commit secrets, `.env` files, credentials, tokens, dependency caches, build artifacts, or generated files that the project normally ignores.

If the repo is dirty before starting, separate:

- pre-existing changes that were already there
- new changes made for the current task

Stage only files relevant to the student's task.

## Commit And Push

Before committing, inspect and summarize:

```bash
git status --short
git diff --stat
git diff
```

Use simple commit messages:

- `Add <thing>`
- `Fix <thing>`
- `Update <thing>`

Commit and push:

```bash
git add <task-relevant-files>
git commit -m "Add <thing>"
git push -u origin student/<short-task-name>
```

If there are no changes to commit, say that clearly and do not create an empty commit unless the user explicitly asks.

## Pull Request Handoff

After pushing, give the student:

- base branch used
- feature branch name
- commit hash from `git rev-parse --short HEAD`
- push status
- GitHub pull request URL if available, otherwise instructions to open a PR from the pushed branch
- short PR text they can paste into GitHub

Use this PR body:

```markdown
## What changed
- 

## How I tested it
- 

## Notes for reviewer
- Base branch: 
```

Avoid overclaiming. If tests were not run, write:

```text
Not run; no test command was provided or discovered.
```

## Final Response Shape

Keep the final student handoff concise:

```markdown
BLUF: Your work is pushed and ready for a GitHub pull request.

Base branch: <approved-base-branch>
Student branch: student/<short-task-name>
Commit: <short-hash>
Push: <success or issue>

PR text:
<paste-ready PR body>

Next step: Open a GitHub pull request from `student/<short-task-name>` into `<approved-base-branch>` and send it to your Software Dev Lead or instructor for review.
```
