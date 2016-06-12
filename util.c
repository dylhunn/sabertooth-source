#include "util.h"

const piece no_piece = NO_PIECE;
const move no_move = {NO_COORD, NO_COORD, NO_PIECE, NO_PIECE, N};

uint64_t rand64(void) {
    uint64_t r = 0;
    for (int i = 0; i < 5; ++i) {
        r = (r << 15) | (rand() & 0x7FFF);
    }
    return r & 0xFFFFFFFFFFFFFFFFULL;
}
