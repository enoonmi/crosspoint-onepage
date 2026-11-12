# CI workflows (pending install)

These two workflow files are stored here instead of `.github/workflows/`
because the GitHub token used for the pushes in this session has **no `workflow`
scope**. GitHub restricts writes to `.github/workflows/` separately from normal
repo content, and the write returns `404 Not Found` rather than `403`:

```
PUT /repos/enoonmi/crosspoint-onepage/contents/.github/workflows/onepage-build.yml
→ 404 Not Found
```

Writing the same content to `ci/onepage-build.yml` succeeded, which confirms it
is the path and not the content or the branch.

## What these do

| File | Repo | Purpose |
|---|---|---|
| `onepage-build.yml` | `crosspoint-onepage` | Builds `pio run -e onepage` for real, plus runs the 317 host assertions under g++ |
| `onepage-analyze.yml` | `anx-reader` | `dart analyze` on the progress-merge patch |

These close the two verification gaps that could not be closed on this machine:

- the firmware for OnePage has **never** been built in CI (upstream CI builds the
  default X4/C3 env on `master`; this fork targets `-e onepage` on `main`)
- the Flutter patch has **never** been through `dart analyze` (no Dart SDK here)

## Activating

Run these on a machine with a token that has the `workflow` scope:

### crosspoint-onepage

```bash
cd crosspoint-onepage
mkdir -p .github/workflows
git mv ci/onepage-build.yml .github/workflows/
git commit -m "ci: activate the OnePage build workflow"
git push
```

### anx-reader

```bash
cd anx-reader
mkdir -p .github/workflows
git mv ci/onepage-analyze.yml .github/workflows/
git commit -m "ci: activate the analyze workflow"
git push
```

After the first push, both workflows also run on PRs and can be started by hand
from the Actions tab (`workflow_dispatch`).

## What to expect on the first run

**`onepage-build.yml` will probably fail the first time, and that is useful.**
The firmware has never been compiled, so `lib/AnxSyncIo/` and the two UI
activities may contain errors that the MSVC-stub check could not see: real
ESP-IDF header names, platform macros under `ONEPAGE_C61`, link order. The build
log will say exactly which line.

The `host-tests` job is expected to pass — the same 317 assertions already pass
locally under MSVC with `/W4 /WX /permissive-`, and the sources have no Windows
dependencies.

**`onepage-analyze.yml` may need one adjustment.** `dart run build_runner build`
generates `sync.g.dart` and the Riverpod outputs; if the upstream tree needs a
different generator invocation, the log will show it. `flutter gen-l10n` is
already tolerated as optional (`|| true`).

## Why not just fix the token

Creating a GitHub token with the `workflow` scope is an account-level action and
needs the user's own credentials. It is one checkbox on
<https://github.com/settings/tokens> for a classic token, or granting
"Workflows: Read and write" for a fine-grained token on these two repos.
