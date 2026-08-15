# Support and stability

## Versions

Tycho is **0.x**. Until 1.0, a minor release may break source compatibility, and
one has: 0.7.0 stopped compiling a copied `handle` and a copied channel, because
both were live memory errors. Every 0.x release is published as a prerelease.

**From 1.0 onward, only a major release may break source compatibility.** Minor
and patch releases will not require you to change working code.

Read the breaking sections of [CHANGELOG.md](CHANGELOG.md) before upgrading; each
states what you wrote and what you write instead.

## Deprecations

A deprecated function keeps working and tells you so: a `# deprecated:` marker in
its doc comment, a compiler warning at every call site naming the replacement, a
`CHANGELOG.md` entry, and a named removal version.

**A deprecation survives until the next major release.** Nothing is removed
without having warned in a released version first.

**That is the promise from 1.0 onward.** While Tycho is 0.x a breaking change may
ship with a changelog entry and no deprecation period at all — 0.7.0 did — so the
paragraph above describes the mechanism you will see used, not a window you can
yet rely on.

## Platforms

**Linux x86_64 is the supported platform. Everything else is best-effort.**

That is a statement about what is promised, not about what works — several of the
others are well exercised:

| platform | what backs it |
|---|---|
| **Linux x86_64** — supported | the full suite, 720 fixtures under ASan/UBSan/LeakSanitizer, plus every gate lane |
| Windows x86_64 — best-effort | six wine lanes including a trap-mode UBSan sweep over the corpus; no native CI |
| macOS x86_64 / arm64 — best-effort | cross-compiled; **never executed on the platform** |
| Linux arm64 — best-effort | cross-compiled; **never executed on the platform** |
| Windows arm64 — best-effort | cross-compiled; **never executed on the platform** |

This is one person's project, which is the real reason the supported list has one
row on it: Linux x86_64 is the machine the work happens on. Artifacts that have
never run on their platform say so in an `UNTESTED-PLATFORM.txt` inside the
tarball. Bug reports from any platform are
welcome; only Linux x86_64 carries a promise.

## Old versions

**There is no support window.** Fixes land on the current release. Older versions
get nothing, including security fixes — if you need a fix, upgrade.

## Security

See [SECURITY.md](SECURITY.md). There has been no third-party audit.
