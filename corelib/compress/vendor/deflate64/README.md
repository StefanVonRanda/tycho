zlib 1.3.1 `contrib/infback9`, the Deflate64 (PKZIP method 9) decoder.

This is a decoder only. `core:compress` supplies the encoder through zlib's
raw-deflate path and verifies that the result is a Deflate64-compatible
subset before returning it. The files keep their zlib copyright notices.
