#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

/**
 * Types
 *
 * This file provides various basic types. Change with caution; many are static-initialized
 * in an order-dependent capacity.
 */

typedef enum castle {
	N, K, Q // castle directions
} castle;

typedef struct piece {
	char type;
	bool white;
} piece;

typedef struct coord {
	uint8_t col;
	uint8_t row;
} coord;

typedef struct move {
	coord from;
	coord to;
	piece captured;
	piece promote_to;
	castle c;
} move;

typedef struct board {
	piece b[8][8]; // cols then rows
	uint64_t hash;
	bool black_to_move;
	bool castle_rights_wq; // can castle on this side
	bool castle_rights_wk;
	bool castle_rights_bq;
	bool castle_rights_bk;

	// below fields do not affect board equality (or hashing)
	int castle_wq_lost_on_ply;
	int castle_wk_lost_on_ply;
	int castle_bq_lost_on_ply;
	int castle_bk_lost_on_ply;
	int last_move_ply; // the ply number of the last move applied
} board;

typedef struct evaluation {
	move best;
	int score;
} evaluation;

#endif
