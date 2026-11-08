# Versions

Each version of WHY? has a code name. The project owner chooses it and decides when it changes.

| Code name | Full name | Version | Tag | Released |
| --- | --- | --- | --- | --- |
| Vigil | WHY - Vigil | 2.0.0 | `v2.0.0-vigil` | Not released yet |
| Triage | WHY - Triage | 0.1.0 | `v0.1.0-triage` | 2026-11-03 |

The jump from 0.1.0 to 2.0.0 is deliberate: Vigil is the second named version, and its major number says so.

## Getting a specific version

Download the build as it was released:

```bash
gh release download v0.1.0-triage --repo DevToccus/WHY
```

Each release holds `why.exe` and its SHA-256 checksum, under **Releases** on GitHub.

Or get the source of that version and build it:

```bash
git clone --branch v0.1.0-triage https://github.com/DevToccus/WHY.git
```

Then follow "Building" in the README.

## How versions are named

Tags have the form `v<version>-<code name>`, in lower case: `v2.0.0-vigil`.

| Part | Where it is set | When it changes |
| --- | --- | --- |
| Code name | `WHY_CODENAME` in `CMakeLists.txt` | When the owner names a new version |
| Version | `project(WHY VERSION ...)` in `CMakeLists.txt` | With every release |

A fix to a named version keeps its code name and raises the version: `v2.0.1-vigil`.

## Publishing a release

1. Set the version, and the code name if it changes, in `CMakeLists.txt`.
2. Add a row to the table above and commit to `main`.
3. Push the tag:

```bash
git tag -a v2.0.0-vigil -m "WHY - Vigil 2.0.0"
git push origin v2.0.0-vigil
```

The `Release` workflow then checks that the tag matches `CMakeLists.txt`, builds from that exact commit, runs the tests and publishes the release. A tag that does not match publishes nothing.

Published tags are never moved or reused. To correct a release, publish a new version.

## Fixing an older version

Start a branch from its latest tag, fix it there, then tag and push as above:

```bash
git switch -c release/triage v0.1.0-triage
```
