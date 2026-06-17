---
name: Bug report
about: A miscompile, crash, wrong output, or build failure
title: ""
labels: bug
assignees: ""
---

**What happened**
A clear description of the bug.

**Minimal repro**
The smallest `.hi` program that triggers it (paste it inline — this is the single
most useful thing you can include):

```
fn main():
    ...
```

How you compiled/ran it (e.g. `./hierc bug.hi && ./bug`, or `make ci`):

```
$ ...
```

**Expected vs. actual**
- Expected: …
- Actual: … (paste the exact output / error / sanitizer report)

**Does it reproduce in both compilers?** (helps a lot, if you can check)
- [ ] `./hierc bug.hi` (the C compiler)
- [ ] via the self-hosted compiler — `./hierc bug.hi --bundle | <hierc0>` or `make fixpoint`
- [ ] not sure

**Environment**
- OS / arch: (e.g. Linux x86_64, macOS arm64)
- C compiler: (`cc --version` first line)
- Hier commit / tag: (`git rev-parse --short HEAD` or the release tag)
