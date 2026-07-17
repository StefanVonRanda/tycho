date: 2026-07-16
# Streaming I/O without a GC

`core:io` grew a bounded-memory line reader: `open_lines` / `read_line` /
`close_lines`. Peak memory is **O(longest line)**, not O(file), so you can chew
through a log larger than RAM.

1. Open the reader.
2. Pull one line at a time.
3. Aggregate in place.

See the `weblog` example for the whole story.
