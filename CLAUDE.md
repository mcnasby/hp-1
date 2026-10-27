# HP-1: working on an issue or a feature

The steps for every GitHub issue or new feature, from the first look to the release.

## 1. Understand it

- `gh issue view N --json title,body,comments` (plain `gh issue view` can print nothing here).
- Find the code: the instrument and its music (scales, layouts, the second pan) are
  `firmware/src/dsp/handpan.{c,h}`; the panel and the screen are `firmware/src/app/ui.c`. The host
  simulator and the web app build the same sources.
- When the request leaves a real choice open (behavior, naming, the default), ask before building.

## 2. Build it on a branch

- `git checkout -b <short-name> main`. Never commit to `main` directly.
- Match the code around it: terse comments in the same voice, the same naming, C99, float only, no
  libm in the engine.
- Saved projects must keep loading. Add new enum values and parameters at the end
  (`project.c` resets any value that is out of range to its default).

## 3. Test it

- Unit tests go in `tests/host/handpan_test.c`. Whole-app behavior goes in a scenario,
  `tests/scenarios/<name>.kal` (`tap`, `spin`, `tapkey`, `expect`, `shot`): add one for a new feature.
- Run `tests/run_tests.sh`. Everything must pass (update-loader is skipped locally without
  `AC79_SDK`). When a change moves existing behavior, update the scenarios that assumed the old way.
- Make sure a new check can fail: break one expectation, see it fail, put it back.
- Screen changes: look at the screenshots in `build/scenarios/*/`. To see a release build's
  version, build the host with `-DOM_VERSION=\"0.1.0 BETA\"` (see `host/build_host.sh`).

## 4. Document it

- `README.md`: the section and the control tables it touches, and one line in the history list
  ("In rough order") for anything a player would notice.
- `web/emu/index.html`: the help text, when controls change.
- `docs/img/*.png`: copy the regenerated screenshots from `build/scenarios/` when the screen changed
  (`browser.png` is a screenshot of the page itself, retaken by hand).

## 5. Version and changelog (in the same PR)

- Bump `VERSION`: the patch number for fixes and small features, the minor number for bigger ones.
- Add a `## X.Y.Z` section at the top of `CHANGELOG.md`, written for players, in the style of the
  existing ones. It becomes the release notes. A PR with a new `VERSION` and no section fails CI.
- Leave `VERSION` alone for changes that don't need a release (CI or docs only), or when told not
  to release.

## 6. Commit, PR, CI

- Commit subject: `Area: what changed` (e.g. `Setup: the version, in the knob row's empty fourth
  place`). The body says what and why. `Closes #N` for an issue. End with the co-author line.
- `git push -u origin <branch>`, then `gh pr create`: what changed for a player, how it was tested,
  `Closes #N`.
- `gh pr checks N --watch` until CI passes. Fix failures on the branch.
- The user merges PRs: don't merge them yourself unless told to for that PR. Rebase merges keep
  `main` linear.
- Before pushing a follow-up to a PR's branch, check it's still open (`gh pr view N --json state`).
  The user may have merged (and so released) it already: then branch from `main` again, bump
  `VERSION` again, and open a new PR.

## 7. Release (automatic)

- Merging to `main` with a new `VERSION` releases it, all in that one run: the release build, the tag
  `vX.Y.Z`, the GitHub Release with the CHANGELOG section, and the site at
  https://mcnasby.github.io/hp-1/ with the new firmware in its installer.
- Never push tags by hand, never create releases with `gh release create`, and never re-run the
  Pages job. GitHub Pages keeps only the first deployment of a commit, and a tag pushed by a run
  starts no other run.
- Afterwards, check: `gh release view vX.Y.Z`, and
  `curl -s https://mcnasby.github.io/hp-1/firmware/latest.json` shows the new version.
