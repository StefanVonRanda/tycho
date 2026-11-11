/* PPMd7 (var.H) and PPMd8 (var.I) from the 7-Zip 24.09 SDK, public domain.
 * One translation unit: the two codecs share macro names, so they are
 * undefined between the includes. */
#include "Ppmd7.c"
#include "Ppmd7Dec.c"
#include "Ppmd7Enc.c"
#undef MAX_FREQ
#undef UNIT_SIZE
#undef U2B
#undef U2I
#undef I2U
#undef REF
#undef STATS_REF
#include "Ppmd8.c"
#include "Ppmd8Dec.c"
#include "Ppmd8Enc.c"
#undef MAX_FREQ
#undef UNIT_SIZE
#undef U2B
#undef U2I
#undef I2U
#undef REF
#undef STATS_REF
