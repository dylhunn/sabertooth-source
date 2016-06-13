#include "util.h"

const piece no_piece = NO_PIECE;
const move no_move = {NO_COORD, NO_COORD, NO_PIECE, NO_PIECE, N};

bool p_eq(piece a, piece b) {
	return a.type == b.type && a.white == b.white;
}

bool c_eq(coord a, coord b) {
	return a.col == b.col && a.row == b.row;
}

bool m_eq(move a, move b) {
	return c_eq(a.from, b.from) && c_eq(a.to, b.to) && p_eq(a.captured, b.captured) && p_eq(a.promote_to, b.promote_to);
}

bool in_bounds(coord c) {
	return c.row <= 7 && c.col <= 7; // coordinates are unsigned
}

piece at(board *b, coord c) {
	return b->b[c.col][c.row];
}

void set(board *b, coord c, piece p) {
	b->b[c.col][c.row] = p;
}

uint64_t rand64(void) {
    uint64_t r = 0;
    for (int i = 0; i < 5; ++i) {
        r = (r << 15) | (rand() & 0x7FFF);
    }
    return r & 0xFFFFFFFFFFFFFFFFULL;
}
