#ifndef UTIL_H
#define UTIL_H

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "settings.h"
#include "stdbool.h"
#include "types.h"
#include "ttable.h"

#define NO_COORD {255, 255}
#define NO_PIECE {'0', false}
#define NO_MOVE {NO_COORD, NO_COORD, NO_PIECE, NO_PIECE, N}

#define cYEL   "\x1B[33m"
#define cBLU   "\x1B[34m"
#define cWHT   "\x1B[37m"
#define cRESET "\x1B[0m"

extern const piece no_piece;
extern const move no_move;
extern const evaluation no_eval;

extern const char *engine_name;
extern const char *engine_version;
extern const char *author_name;

extern FILE *logstr;

static inline bool p_eq(piece a, piece b) {
	return a.type == b.type && a.white == b.white;
}

static inline bool c_eq(coord a, coord b) {
	return a.col == b.col && a.row == b.row;
}

static inline bool m_eq(move a, move b) {
	return c_eq(a.from, b.from) && c_eq(a.to, b.to) && p_eq(a.captured, b.captured) 
		&& p_eq(a.promote_to, b.promote_to) && a.c == b.c && a.en_passant_capture == b.en_passant_capture;
}

static inline bool m_eq_without_en_passant(move a, move b) {
	return c_eq(a.from, b.from) && c_eq(a.to, b.to) && p_eq(a.captured, b.captured) 
		&& p_eq(a.promote_to, b.promote_to) && a.c == b.c;
}

static inline bool e_eq(evaluation a, evaluation b) {
	return m_eq(a.best, b.best) && a.score == b.score && a.last_access_move == b.last_access_move 
		&& a.depth == b.depth && a.type == b.type;
}

static inline bool in_bounds(coord c) {
	return c.row <= 7 && c.col <= 7; // coordinates are unsigned
}

static inline piece at(const board *b, coord c) {
	return b->b[c.col][c.row];
}

static inline void set(board *b, coord c, piece p) {
	b->b[c.col][c.row] = p;
}

extern uint64_t rand64(void);

void reset_board(board *b);

bool move_arr_contains(move *moves, move move, int arrlen);

int min(int a, int b);

int max(int a, int b);

void stdout_fprintf(FILE * f, const char * fmt, ...);

void nlopt_qsort_r(void *base_, size_t nmemb, size_t size, void *thunk,
		   int (*compar)(void *, const void *, const void *));

#endif
