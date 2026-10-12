title: The Arena Model
date: 1700000000
author: ada
---
# The Arena Model

Every value in Tycho lives in the arena of the scope that created it. When
the scope exits, the whole arena is reclaimed at once — nothing to free by
hand, and no collector running in the background.

## What this means for you

- allocating is just bumping a pointer
- returning a value copies it into the caller's arena
- keeping data past the scope is explicit: copy it somewhere that outlives it

Memory management you never have to think about, until you want to.
