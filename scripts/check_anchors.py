#!/usr/bin/env python3
"""Resolve every `](path#fragment)` in a tracked .md against the target's headings.

check_links.sh strips the fragment and checks only that the FILE exists, so a link
into a renamed section passed clean -- 9 did, including 5 spelling `#what-1-0-requires`
for `## What 1.0 requires`. GitHub DELETES `.` rather than hyphenating it, which is
exactly the class of slug error nobody catches by eye.

--selfcheck runs the controls: a bogus fragment must be caught, and every real one
must stay clean, or a green run means nothing.
"""
import collections
import os
import re
import subprocess
import sys

FENCE = re.compile(r'\s*```')
HEADING = re.compile(r'\s{0,3}(#{1,6})\s+(.*?)\s*#*\s*$')
HTML_ANCHOR = re.compile(r'<a\s+(?:name|id)="([^"]+)"')
INLINE_CODE = re.compile(r'`[^`]*`')
MD_LINK = re.compile(r'\]\(([^)\s]+)\)')
LINK_TEXT = re.compile(r'!?\[([^\]]*)\]\([^)]*\)')


def slugify(text):
    """GitHub's rule: strip markup, lowercase, delete punctuation, spaces to hyphens."""
    text = LINK_TEXT.sub(r'\1', text).replace('`', '')
    return re.sub(r'[^\w\- ]', '', text.lower(), flags=re.UNICODE).replace(' ', '-')


def anchors_of(path):
    """Every fragment the target file offers: heading slugs plus explicit HTML anchors."""
    seen = collections.Counter()
    out = set()
    fence = False
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            if FENCE.match(line):
                fence = not fence
                continue
            if fence:
                continue
            for m in HTML_ANCHOR.finditer(line):
                out.add(m.group(1))
            m = HEADING.match(line)
            if not m:
                continue
            slug = slugify(m.group(2))
            n = seen[slug]
            seen[slug] += 1
            out.add(slug if n == 0 else '%s-%d' % (slug, n))
    return out


def links_of(md):
    """Each (line, target) whose target carries a fragment, outside fences and code spans."""
    fence = False
    with open(md, encoding='utf-8', errors='replace') as fh:
        for i, line in enumerate(fh, 1):
            if FENCE.match(line):
                fence = not fence
                continue
            if fence:
                continue
            for m in MD_LINK.finditer(INLINE_CODE.sub('', line)):
                t = m.group(1)
                if '#' in t and not t.startswith(('http:', 'https:', 'mailto:')):
                    yield i, t


def check(files, cache):
    dead, total = [], 0
    for md in files:
        for lineno, target in links_of(md):
            path, _, frag = target.partition('#')
            if not frag:
                continue
            tgt = md if path == '' else os.path.normpath(os.path.join(os.path.dirname(md), path))
            if not tgt.endswith('.md'):
                continue
            total += 1
            if tgt not in cache:
                cache[tgt] = anchors_of(tgt) if os.path.exists(tgt) else None
            # a missing FILE is check_links.sh's job, not this leg's
            if cache[tgt] is not None and frag not in cache[tgt]:
                dead.append((md, lineno, target, tgt))
    return dead, total


def tracked():
    out = subprocess.run(['git', 'ls-files', '*.md'], capture_output=True, text=True).stdout
    return [f for f in out.split() if '/content/' not in f]


def selfcheck(files):
    """[1] a bogus fragment must be CAUGHT, [2] the tree as it stands must be clean."""
    import tempfile
    ok = True
    with tempfile.TemporaryDirectory() as d:
        probe = os.path.join(d, 'probe.md')
        with open(probe, 'w') as fh:
            fh.write('# Real Heading\n\n[live](#real-heading) [dead](#no-such-heading)\n')
        dead, total = check([probe], {})
        hit = [x for x in dead if 'no-such-heading' in x[2]]
        miss = [x for x in dead if 'real-heading' in x[2]]
        print('  [c1] bogus fragment caught: %s' % ('yes' if hit else 'NO -- leg is dead'))
        print('  [c2] real fragment left alone: %s' % ('yes' if not miss else 'NO -- false positive'))
        print('  [c3] of %d links in the probe, %d scored' % (2, total))
        ok = bool(hit) and not miss and total == 2
    dead, total = check(files, {})
    print('  [c4] tree clean: %s (%d links, %d dead)' % ('yes' if not dead else 'NO', total, len(dead)))
    return ok and not dead


def main():
    os.chdir(subprocess.run(['git', 'rev-parse', '--show-toplevel'],
                            capture_output=True, text=True).stdout.strip())
    files = tracked()
    if '--selfcheck' in sys.argv:
        print('anchor check selfcheck:')
        good = selfcheck(files)
        print('selfcheck: %s' % ('ok' if good else 'FAILED'))
        return 0 if good else 1
    dead, total = check(files, {})
    for md, lineno, target, tgt in dead:
        print('DEAD ANCHOR  %s:%d  ->  %s   (%s has no such heading)' % (md, lineno, target, tgt))
    if dead:
        print('anchor check: FAILED (%d of %d anchor links name no heading)' % (len(dead), total))
        return 1
    print('anchor check: ok (%d anchor links resolve to a heading in their target)' % total)
    return 0


if __name__ == '__main__':
    sys.exit(main())
