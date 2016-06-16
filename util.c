#include "util.h"

const piece no_piece = NO_PIECE;
const move no_move = {NO_COORD, NO_COORD, NO_PIECE, NO_PIECE, N};

const char *engine_name = "Fianchetto";
const char *engine_version = "0.1a";

bool p_eq(piece a, piece b) {
	return a.type == b.type && a.white == b.white;
}

bool c_eq(coord a, coord b) {
	return a.col == b.col && a.row == b.row;
}

bool m_eq(move a, move b) {
	return c_eq(a.from, b.from) && c_eq(a.to, b.to) && p_eq(a.captured, b.captured) 
		&& p_eq(a.promote_to, b.promote_to) && a.c == b.c;
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

void reset_board(board *b) {
	for (int i = 16; i < 48; i++) b->b[i%8][i/8] = no_piece; // empty squares
	for (int i = 0; i < 8; i++) b->b[i][1] = (piece){'P', true}; // white pawns
	b->b[0][0] = b->b[7][0] = (piece){'R', true}; // white rooks
	b->b[1][0] = b->b[6][0] = (piece){'N', true}; // white knights
	b->b[2][0] = b->b[5][0] = (piece){'B', true}; // white bishops
	b->b[3][0] = (piece){'Q', true}; // white queen
	b->b[4][0] = (piece){'K', true}; // white king
	for (int i = 0; i < 8; i++) b->b[i][6] = (piece){'P', false}; // black pawns
	b->b[0][7] = b->b[7][7] = (piece){'R', false}; // black rooks
	b->b[1][7] = b->b[6][7] = (piece){'N', false}; // black knights
	b->b[2][7] = b->b[5][7] = (piece){'B', false}; // black bishops
	b->b[3][7] = (piece){'Q', false}; // black queen
	b->b[4][7] = (piece){'K', false}; // black king
	b->black_to_move = false;
	b->castle_rights_wq = true;
	b->castle_rights_wk = true;
	b->castle_rights_bq = true;
	b->castle_rights_bk = true;
	b->castle_wq_lost_on_ply = -1;
	b->castle_wk_lost_on_ply = -1;
	b->castle_bq_lost_on_ply = -1;
	b->castle_bk_lost_on_ply = -1;
	b->last_move_ply = 0;
	b->hash = tt_hash_position(b);
	b->true_game_ply_clock = 0;
}

bool move_arr_contains(move *moves, move move, int arrlen) {
	for (int i = 0; i < arrlen; i++) {
		if (m_eq(moves[i], move)) {
			return true;
		}
	}
	return false;
}
