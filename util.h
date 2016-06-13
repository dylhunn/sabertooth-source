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

bool p_eq(piece a, piece b);

bool c_eq(coord a, coord b);

bool m_eq(move a, move b);

bool in_bounds(coord c);

piece at(board *b, coord c);

void set(board *b, coord c, piece p);

extern uint64_t rand64(void);

#endif

