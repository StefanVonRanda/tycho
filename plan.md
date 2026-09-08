# Swarm — close all 18 admitted gaps, each with a failing gate

## Contract

- [x] 0.1 Write CONTRACT.md — files: CONTRACT.md — done when: every component lists owned files no other component touches, plus a runnable done-when check
  - Scope: ownership map for phases 1–7; checks from G1–G18 in FINDINGS.md
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`
  - Not: `make test`, `make ci`

## Components


  - Scope: `compiler/emit/emit.ty:279`, `compiler/emit/emit.ty:4869`
  - Verify: new accept/reject fixture pair; revert-each reddens; iterate up to 3 times, unticked on failure
  - Gates: `make parse-check`, `make tychoc1-check`, `make test`
  - Not: `make ci`
- [ ] 1.2 critic emit — files: CONTRACT.md — done when: re-ran 1.1 checks; PASS or numbered failures with file:line
- [ ] 2.1 parse recovery trio — files: compiler/parse/parse.ty — done when: all three sub-gaps closed or split with a failing gate each
  - Scope: `compiler/parse/parse.ty:25`, `compiler/parse/parse.ty:120`, `compiler/parse/parse.ty:1959`
  - Verify: fixture per sub-gap; revert-each reddens; iterate up to 3 times, unticked on failure
  - Gates: `make parse-check`, `make test`
  - Not: `make ci`
- [ ] 2.2 critic parse — files: CONTRACT.md — done when: re-ran 2.1 checks; PASS or numbered failures with file:line
- [ ] 3.1 driver flags + resolve hint — files: compiler/driver/driver.ty, compiler/types/resolve.ty — done when: spaced path builds; hint present on resolve path
  - Scope: `compiler/driver/driver.ty:168`, `compiler/types/resolve.ty:893`
  - Verify: path-with-space build; hint diagnostic output; iterate up to 3 times, unticked on failure
  - Gates: `make tychoc1-check`, `make test`
  - Not: `make ci`
- [ ] 3.2 critic driver-resolve — files: CONTRACT.md — done when: re-ran 3.1 checks; PASS or numbered failures with file:line
- [ ] 4.1 tychoc imports + spelling gate — files: src/tychoc.c — done when: sibling free-ride caught regardless of parse order; unused import flagged per file; spelling gate exists
  - Scope: `src/tychoc.c:2662`, `src/tychoc.c:5367`, `src/tychoc.c:5852`
  - Verify: order-swapped two-file fixture set; revert-each reddens; iterate up to 3 times, unticked on failure
  - Gates: `make test`
  - Not: `make corelib`, `make ci`
- [ ] 4.2 critic tychoc — files: CONTRACT.md — done when: re-ran 4.1 checks; PASS or numbered failures with file:line
- [ ] 5.1 os shim batch + windows — files: corelib/os/os_shim.c — done when: batch files refused or covered by test; Windows path covered or loud-skipped
  - Scope: `corelib/os/os_shim.c:225`, `corelib/os/os_shim.c:380`
  - Verify: `make shim-check`; skip line loud on hosts without target; iterate up to 3 times, unticked on failure
  - Gates: `make shim-check`, `make corelib`
  - Not: `make test`, `make ci`
- [ ] 5.2 critic os-shim — files: CONTRACT.md — done when: re-ran 5.1 checks; PASS or numbered failures with file:line
- [ ] 6.1 httpd ceiling + debug drive case — files: corelib/httpd/httpd.ty, tools/tycho-debug/main.ty — done when: ceiling named in diagnostic; drive-letter case handled or refused
  - Scope: `corelib/httpd/httpd.ty:145`, `tools/tycho-debug/main.ty:208`
  - Verify: over-ceiling input names limit; mixed-case drive input test; iterate up to 3 times, unticked on failure
  - Gates: `make corelib`, `make debug-check`, `sh scripts/entrypoints.sh`
  - Not: `make test`, `make ci`
- [ ] 6.2 critic httpd-debug — files: CONTRACT.md — done when: re-ran 6.1 checks; PASS or numbered failures with file:line
- [ ] 7.1 tycho-make subset — files: tools/tycho-make/graph/graph.ty, tools/tycho-make/build/build.ty — done when: each subset refusal named; non-text target refused by name or supported
  - Scope: `tools/tycho-make/graph/graph.ty:82`, `tools/tycho-make/graph/graph.ty:84`, `tools/tycho-make/graph/graph.ty:87`, `tools/tycho-make/build/build.ty:11`
  - Verify: fixture per refusal; re-record cannot bless a dropped edge; iterate up to 3 times, unticked on failure
  - Gates: `make make-check`, `sh scripts/entrypoints.sh`
  - Not: `make test`, `make ci`
- [ ] 7.2 critic tycho-make — files: CONTRACT.md — done when: re-ran 7.1 checks; PASS or numbered failures with file:line

## Integration

- [ ] 8.1 assemble + re-run all component checks — files: CONTRACT.md — done when: every builder check re-run green plus doc gates green
  - Verify: `python3 scripts/check_citations.py`, `sh scripts/check_links.sh`, each phase gate in order
  - Not: `make ci`
- [ ] 8.2 final critic — files: CONTRACT.md — done when: verdict against outcome (all 18 gaps closed, each with a failing gate); PASS or failures
