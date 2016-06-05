//#define NDEBUG // Disable assertions

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NO_COORD {255, 255}
#define NO_PIECE {'0', false, false}

typedef enum castle {N, K, Q} castle; // castle directions
typedef struct piece {char type; bool white; bool moved;} piece;
typedef struct coord {uint8_t col; uint8_t row;} coord;
typedef struct move {coord from; coord to; piece captured; piece promote_to; castle c; bool flipped_moved;} move;
typedef struct board {piece b[8][8]; uint64_t hash;} boardd; // cols then rows

const piece no_piece = NO_PIECE;
const move no_move = {NO_COORD, NO_COORD, NO_PIECE, NO_PIECE};
const char promo_p[] = {'Q', 'N', 'B', 'R'}; // promotion targets
const int ttable_size = 10^5;

uint64_t tt_keys[ttable_size] = {0};
move tt_values[ttable_size];
uint64_t zobrist[64][12];

int main(int argc, char *argv[]);
void reset_board(boardd *b);
void apply(boardd *b, move m);
void unapply(boardd *b, move m);
void tt_init();
inline uint64_t tt_pieceval(boardd *b, coord c);
move *board_moves(boardd *b, bool white, int *count);
int piece_moves(boardd *b, coord c, move *list);
int diagonal_moves(boardd *b, coord c, move *list, int steps);
int horizontal_moves(boardd *b, coord c, move *list, int steps);
int pawn_moves(boardd *b, coord c, move *list);
int castle_moves(boardd *b, coord c, move *list);
int moves_slide(boardd *b, coord orig_c, move *list, int dx, int dy, uint8_t steps);
bool add_move(boardd *b, move m, move *list);
bool in_check(int col, int row);
uint64_t rand64();

static inline bool p_eq(piece a, piece b) {return a.type == b.type && a.white == b.white && a.moved == b.moved;}
static inline bool c_eq(coord a, coord b) {return a.col == b.col && a.row == b.row;}
static inline bool m_eq(move a, move b) {return c_eq(a.from, b.from) && c_eq(a.to, b.to) 
				&& p_eq(a.captured, b.captured) && p_eq(a.promote_to, b.promote_to);}
static inline bool in_bounds(coord c) {return c.row <= 7 && c.col <= 7;} // coordinates are unsigned
static inline piece at(boardd *b, coord c) {return b->b[c.col][c.row];}
static inline void set(boardd *b, coord c, piece p) {b->b[c.col][c.row] = p;}

int main(int argc, char *argv[]) {
	//tt_init();
	boardd b; 
	reset_board(&b);
	int moves;
	move *list = board_moves(&b, true, &moves);
	printf("Moves available: %d\n", moves);
	set(&b, (coord){0, 1}, no_piece);
	move *list2 = board_moves(&b, true, &moves);
	printf("Moves available: %d\n", moves);
	//free(list);
	//free(list2);
}

void reset_board(boardd *b) {
	b->hash = 0;
	for (int i = 16; i < 48; i++) b->b[i%8][i/8] = no_piece; // empty squares
	for (int i = 0; i < 8; i++) b->b[i][1] = (piece){'P', true, false}; // white pawns
	b->b[0][0] = b->b[7][0] = (piece){'R', true, false}; // white rooks
	b->b[1][0] = b->b[6][0] = (piece){'N', true, false}; // white knights
	b->b[2][0] = b->b[5][0] = (piece){'B', true, false}; // white bishops
	b->b[3][0] = (piece){'Q', true, false}; // white queen
	b->b[4][0] = (piece){'K', true, false}; // white king
	for (int i = 0; i < 8; i++) b->b[i][6] = (piece){'P', false, false}; // black pawns
	b->b[0][7] = b->b[7][7] = (piece){'R', false, false}; // black rooks
	b->b[1][7] = b->b[6][7] = (piece){'N', false, false}; // black knights
	b->b[2][7] = b->b[5][7] = (piece){'B', false, false}; // black bishops
	b->b[3][7] = (piece){'Q', false, false}; // black queen
	b->b[4][7] = (piece){'K', false, false}; // black king
}

void apply(boardd *b, move m) {
	set(b, m.to, p_eq(m.promote_to, no_piece) ? at(b, m.from) : m.promote_to);
	set(b, m.from, no_piece);
	if (!at(b, m.to).moved) m.flipped_moved = true;
	else m.flipped_moved = false;
	b->b[m.to.col][m.to.row].moved = true;
	if (m.c == K) { // castle
		b->b[5][m.to.row] = (piece){'R', at(b, m.to).white, true};
		b->b[7][m.to.row] = no_piece;
	} else if (m.c == Q) {
		b->b[3][m.to.row] = (piece){'R', at(b, m.to).white, true};
		b->b[0][m.to.row] = no_piece;
	}
}

void unapply(boardd *b, move m) {
	set(b, m.from, p_eq(m.promote_to, no_piece) ? at(b, m.to) : (piece){'P', at(b, m.to).white, true});
	set(b, m.to, m.captured);
	if (m.flipped_moved) b->b[m.from.col][m.from.row].moved = false;
	if (m.c == K) { // castle
		b->b[7][m.from.row] = (piece){'R', at(b, m.from).white, false};
		b->b[5][m.from.row] = no_piece;
	} else if (m.c == Q) {
		b->b[0][m.from.row] = (piece){'R', at(b, m.from).white, false};
		b->b[3][m.from.row] = no_piece;
	}
}

void tt_init() {
	srand(time(NULL));
	for (int i = 0; i < 64; i++) {
		for (int j = 0; j < 12; j++) {
			zobrist[i][j] = rand64();
		}
	}
}

inline uint64_t tt_pieceval(boardd *b, coord c) {
	const int square_code = (c.col-1)*8+c.row;
	int piece_code = 0;
	piece p = at(b, c);
	assert(!p_eq(p, no_piece));
	if (p.white) piece_code += 6;
	if (p.type == 'N') piece_code += 1;
	else if (p.type == 'B') piece_code += 2;
	else if (p.type == 'R') piece_code += 3;
	else if (p.type == 'Q') piece_code += 4;
	else piece_code += 5;
	return zobrist[square_code][piece_code];
}

// Generates pseudo-legal moves for a player.
// (Does not exclude moves that put the king in check.)
// Generates an array of valid moves, and populates the count.
move *board_moves(boardd *b, bool white, int *count) {
	move *moves = malloc(sizeof(move) * 120); // Up to 120 moves supported
	*count = 0;
	for (uint8_t i = 0; i < 8; i++) { // col
		for (uint8_t j = 0; j < 8; j++) { // row
			if (p_eq(b->b[j][i], no_piece) || b->b[j][i].white != white) continue;
			(*count) += piece_moves(b, (coord){j, i}, moves + (*count));
		}

	}
	realloc(moves, sizeof(move) * (*count));
	return moves;
}

// Writes all legal moves for a piece to an array starting at index 0; 
// returns the number of items added.
int piece_moves(boardd *b, coord c, move *list) {
	int added = 0;
	if (p_eq(at(b, c), no_piece)) return 0;
	if (at(b, c).type == 'P') {
		added += pawn_moves(b, c, list);
	} else if (at(b, c).type == 'B') {
		added += diagonal_moves(b, c, list, 8);
	} else if (at(b, c).type == 'N') {
		for (int i = -2; i <= 2; i += 4) {
			for (int j = -1; j <= 1; j += 2) {
				added += moves_slide(b, c, list + added, i, j, 1);
				added += moves_slide(b, c, list + added, j, i, 1);
			}
		}
	} else if (at(b, c).type == 'R') {
		added += horizontal_moves(b, c, list, 8);
	} else if (at(b, c).type == 'Q') {
		added += diagonal_moves(b, c, list, 8);
		added += horizontal_moves(b, c, list + added, 8);

	} else if (at(b, c).type == 'K') {
		added += diagonal_moves(b, c, list, 1);
		added += horizontal_moves(b, c, list + added, 1);
		added += castle_moves(b, c, list + added);
	}
	return added;
}

int diagonal_moves(boardd *b, coord c, move *list, int steps) {
	int added = 0;
	added += moves_slide(b, c, list, 1, 1, steps);
	added += moves_slide(b, c, list + added, 1, -1, steps);
	added += moves_slide(b, c, list + added, -1, 1, steps);
	added += moves_slide(b, c, list + added, -1, -1, steps);
	return added;
}

int horizontal_moves(boardd *b, coord c, move *list, int steps) {
	int added = 0;
	added += moves_slide(b, c, list, 1, 0, steps);
	added += moves_slide(b, c, list + added, -1, 0, steps);
	added += moves_slide(b, c, list + added, 0, 1, steps);
	added += moves_slide(b, c, list + added, 0, -1, steps);
	return added;
}

// Precondition: the piece at c is a pawn.
int pawn_moves(boardd *b, coord c, move *list) {
	assert(at(b, c).type == 'P');
	int added = 0;
	piece cp = at(b, c);
	int dy = cp.white ? 1 : -1;
	bool promote = (c.row + dy == 0 || c.row + dy == 7); // next move is promotion
	if (p_eq(at(b, (coord){c.col, c.row + dy}), no_piece)) { // front is clear
		if (promote) {
			for (int i = 0; i < 4; i++)
				if (add_move(b, (move){c, (coord){c.col, c.row + dy}, no_piece, 
								(piece){promo_p[i], cp.white, true}, N}, list + added)) added++;
		} else {
			if (add_move(b, (move){c, {c.col, c.row + dy}, no_piece, no_piece, N}, list + added)) added++;
			if (!cp.moved && p_eq(at(b, (coord){c.col, c.row + dy + dy}), no_piece)) // double move
				if (add_move(b, (move){c, (coord){c.col, c.row + dy + dy}, 
							no_piece, no_piece, N}, list + added)) added++;
		}
	}
	for (int dx = -1; dx <= 1; dx += 2) { // both capture directions
		coord cap = {c.col + dx, c.row + dy};
		if (!in_bounds(cap) || p_eq(at(b, cap), no_piece)) continue;
		if (promote) {
			for (int i = 0; i < 4; i++)
				if (add_move(b, (move){c, (coord){c.col + dx, c.row + dy}, at(b, cap), 
								(piece){promo_p[i], cp.white, true}, N}, list + added)) added++;
		} else if (add_move(b, (move){c, (coord){c.col + dx, c.row + dy}, at(b, cap), no_piece, N}, list + added)) 
			added++;
	}
	return added;
}

// Precondition: the piece at c is a king.
int castle_moves(boardd *b, coord c, move *list) {
	assert(at(b, c).type == 'K');
	int added = 0;
	int row = at(b, c).white ? 0 : 7;
	if (at(b, c).moved || /*c != (coord){4, row} || */in_check(c.col, c.row)) return 0;
	bool k_r_path_clear = true;
	bool q_r_path_clear = true;
	for (int i = 5; i <= 6; i++) if (!p_eq(b->b[i][row], no_piece) || in_check(i, row)) {k_r_path_clear = false; break;};
	for (int i = 3; i >= 1; i--) if (!p_eq(b->b[i][row], no_piece) || in_check(i, row)) {q_r_path_clear = false; break;};
	if (!at(b, (coord){7, row}).moved && k_r_path_clear) 
		if (add_move(b, (move){c, (coord){6, row}, no_piece, no_piece, K}, list + added)) added++;
	if (!at(b, (coord){0, row}).moved && q_r_path_clear) 
		if (add_move(b, (move){c, (coord){2, row}, no_piece, no_piece, Q}, list + added)) added++;
	return added;
}

// Writes all legal moves to an array by sliding from a given origin with some dx and dy for n steps.
// Stops on capture, board bounds, or blocking.
int moves_slide(boardd *b, coord orig_c, move *list, int dx, int dy, uint8_t steps) {
	int added = 0;
	coord curr_c = {orig_c.col, orig_c.row};
	for (uint8_t s = 1; s <= steps; s++) {
		curr_c.col += dx;
		curr_c.row += dy;
		if (!in_bounds(curr_c)) break; // left board bounds
		if (!p_eq(at(b, curr_c), no_piece) && at(b, curr_c).white == at(b, orig_c).white) break; // blocked
		if (add_move(b, (move){orig_c, curr_c, at(b, curr_c), no_piece, N}, list + added)) added++; // freely move
		if (!p_eq(at(b, curr_c), no_piece)) break; // captured
	}
	return added;
}

// Adds a move at the front of the list if it doesn't put the king in check
bool add_move(boardd *b, move m, move *list) {
	list[0] = m; return true; // todo
	/*apply(b, m);
	bool viable = in_check(king_loc);
	if (viable) list[0] = m;
	unapply(b, m);
	return viable;*/
}

// checks if a given coordinate would be in check on the current board
bool in_check(int col, int row) {
	return false; // todo
}

uint64_t rand64() {
    uint64_t r = 0;
    for (int i = 0; i < 5; ++i) {
        r = (r << 15) | (rand() & 0x7FFF);
    }
    return r & 0xFFFFFFFFFFFFFFFFULL;
}