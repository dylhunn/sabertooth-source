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
	int8_t c; // castle; structure packing
	bool en_passant_capture;
} move;

typedef enum evaltype {
	upperbound,
	lowerbound,
	qupperbound,
	qlowerbound,
	exact,
	qexact
} evaltype;

typedef struct evaluation {
	move best;
	int16_t score;
	uint16_t last_access_move; // used for age to delete ancient entries; set automatically by the TT
	int8_t depth;
	int8_t type; // evaltype; structure packing
} evaluation;

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
	uint16_t true_game_ply_clock;
	coord white_king;
	coord black_king;

	// Array of double pawn push history indexed by move ply
	int8_t *en_passant_pawn_push_col_history;
} board;

typedef struct searchstats {
	int depth; // the depth of the current search
	double time; // time at this depth in ms
	uint64_t nodes_searched; // non-quiescence, non-TT nodes considered
	uint64_t qnodes_searched;
	uint64_t qnode_aborts; // quiescence search abandoned due to depth
	uint64_t ttable_inserts;
	uint64_t ttable_insert_failures;
	uint64_t ttable_hits;
	uint64_t ttable_misses;
	uint64_t ttable_overwrites;
} searchstats;

typedef struct search_worker_thread_args {
	board *b;
	int alpha;
	int beta;
	int ply;
	int centiply_extension;
	bool allow_extensions;
	bool side_to_move_in_check;
} search_worker_thread_args;

#endif
