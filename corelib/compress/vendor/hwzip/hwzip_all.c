/* hwzip 2.4 (public domain): Shrink, Reduce and Implode. The three sources
 * have private helpers with the same names; rename them per source. */
#define max_len implode_max_len
#define max_dist implode_max_dist
#define lit_callback implode_lit_callback
#define backref_callback implode_backref_callback
#include "implode.c"
#undef max_len
#undef max_dist
#undef lit_callback
#undef backref_callback

#define max_len reduce_max_len
#define max_dist reduce_max_dist
#define lit_callback reduce_lit_callback
#define backref_callback reduce_backref_callback
#define min reduce_min
#include "reduce.c"
#undef max_len
#undef max_dist
#undef lit_callback
#undef backref_callback
#undef min

#include "shrink.c"
#define min lz77_min
#include "lz77.c"
#undef min
#include "huffman.c"
#include "tables.c"
