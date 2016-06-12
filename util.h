#ifndef UTIL_H
#define UTIL_H

#include <stdlib.h>
#include "stdbool.h"
#include "types.h"

#define NO_COORD {255, 255}
#define NO_PIECE {'0', false}

#define cYEL   "\x1B[33m"
#define cBLU   "\x1B[34m"
#define cWHT   "\x1B[37m"
#define cRESET "\x1B[0m"

extern const piece no_piece;
extern const move no_move;

inline bool p_eq(piece a, piece b) {
	return a.type == b.type && a.white == b.white;
}

inline bool c_eq(coord a, coord b) {
	return a.col == b.col && a.row == b.row;
}

inline bool m_eq(move a, move b) {
	return c_eq(a.from, b.from) && c_eq(a.to, b.to) && p_eq(a.captured, b.captured) && p_eq(a.promote_to, b.promote_to);
}

inline bool in_bounds(coord c) {
	return c.row <= 7 && c.col <= 7; // coordinates are unsigned
}

inline piece at(board *b, coord c) {
	return b->b[c.col][c.row];
}

inline void set(board *b, coord c, piece p) {
	b->b[c.col][c.row] = p;
}

extern uint64_t rand64(void);

#endif

