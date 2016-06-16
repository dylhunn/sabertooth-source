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

	// the true ply number of the game, which has no bearing on the current board state
	// used only for disposing of ancient entries in the transposition table
	int true_game_ply_clock; 
} board;

typedef enum evaltype {
	at_least,
	at_most,
	exact
} evaltype;

typedef struct evaluation {
	move best;
	int score;
	evaltype type;
	uint8_t depth;
	uint16_t last_access_move; // used for age to delete ancient entries; set automatically by the TT
} evaluation;

typedef struct searchstats {
	int depth; // the depth of the current search
	double time; // time at this depth in ms
	uint64_t nodes_searched; // non-quiescence, non-TT nodes considered
	uint64_t qnodes_searched;
	uint64_t qnode_aborts; // quiescence search abandoned due to depth
} searchstats;

#endif
