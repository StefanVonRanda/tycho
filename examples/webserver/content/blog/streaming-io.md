date: 2026-07-16
# Streaming I/O without a GC

Need to read a file bigger than memory? `core:io` has a streaming line
reader — `open_lines`, `read_line`, `close_lines` — that pulls one line at a
time into a single reused buffer.

1. Open the reader.
2. Pull one line at a time.
3. Aggregate in place.

Peak memory stays at **O(longest line)** instead of O(file), so a log larger
than RAM is just another input. See the `weblog` example for the full story.
