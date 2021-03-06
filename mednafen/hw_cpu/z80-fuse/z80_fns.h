#include "../../mednafen-types.h"

void (*z80_writebyte)(uint16_t a, uint8_t b);
uint8_t (*z80_readbyte)(uint16_t a);
void (*z80_writeport)(/*uint16_t a, uint8_t b*/);

/* This is what everything acts on! */
struct processor z80;

uint64_t z80_tstates;
uint64_t last_z80_tstates;
uint8_t sz53_table[0x100]; /* The S, Z, 5 and 3 bits of the index */
uint8_t parity_table[0x100]; /* The parity of the lookup value */
uint8_t sz53p_table[0x100]; /* OR the above two tables together */

/* Similarly, overflow can be determined by looking at the 7th bits; again
   the hash into this table is r12 */
const uint8_t overflow_add_table[] = { 0, 0, 0, Z80_FLAG_V, Z80_FLAG_V, 0, 0, 0 };
const uint8_t overflow_sub_table[] = { 0, Z80_FLAG_V, 0, 0, 0, 0, Z80_FLAG_V, 0 };

/* Whether a half carry occurred or not can be determined by looking at
   the 3rd bit of the two arguments and the result; these are hashed
   into this table in the form r12, where r is the 3rd bit of the
   result, 1 is the 3rd bit of the 1st argument and 2 is the
   third bit of the 2nd argument; the tables differ for add and subtract
   operations */
const uint8_t halfcarry_add_table[] =
  { 0, Z80_FLAG_H, Z80_FLAG_H, Z80_FLAG_H, 0, 0, 0, Z80_FLAG_H };
const uint8_t halfcarry_sub_table[] =
  { 0, 0, Z80_FLAG_H, 0, Z80_FLAG_H, 0, Z80_FLAG_H, Z80_FLAG_H };

