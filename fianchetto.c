#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define NO_COORD {255, 255}
#define NO_PIECE {'0', false, false}

typedef enum castle {N, K, Q} castle; // castle directions
typedef struct piece {char type; bool white; bool moved;} piece;
typedef struct coord {uint8_t col; uint8_t row;} coord;
typedef struct move {coord from; coord to; piece captured; piece promote_to; castle c; bool flipped_moved;} move;

const piece no_piece = NO_PIECE;
const move no_move = {NO_COORD, NO_COORD, NO_PIECE, NO_PIECE};
const int ttable_size = 10^5;
const char promo_p[] = {'Q', 'N', 'B', 'R'}; // promotion targets

uint64_t keys[ttable_size];
uint64_t values[ttable_size];

int main(int argc, char *argv[]);
void reset_board(piece board[8][8]);
void apply(piece board[8][8], move m);
void unapply(piece board[8][8], move m);
move *board_moves(piece board[8][8], bool white, int *count);
int piece_moves(piece board[8][8], coord c, move *list);
int diagonal_moves(piece board[8][8], coord c, move *list, int steps);
int horizontal_moves(piece board[8][8], coord c, move *list, int steps);
int pawn_moves(piece b[8][8], coord c, move *list);
int castle_moves(piece b[8][8], coord c, move *list);
int moves_slide(piece b[8][8], coord orig_c, move *list, int dx, int dy, uint8_t steps);
bool add_move(piece b[8][8], move m, move *list);
bool in_check(int col, int row);

static inline bool p_eq(piece a, piece b) {return a.type == b.type && a.white == b.white && a.moved == b.moved;}
static inline bool c_eq(coord a, coord b) {return a.col == b.col && a.row == b.row;}
static inline bool m_eq(move a, move b) {return c_eq(a.from, b.from) && c_eq(a.to, b.to) 
				&& p_eq(a.captured, b.captured) && p_eq(a.promote_to, b.promote_to);}
static inline bool in_bounds(coord c) {return c.row <= 7 && c.col <= 7;} // coordinates are unsigned
static inline piece at(piece board[8][8], coord c) {return board[c.col][c.row];}
static inline void set(piece board[8][8], coord c, piece p) {board[c.col][c.row] = p;}

int main(int argc, char *argv[]) {
	piece board[8][8]; // cols then rows
	reset_board(board);
	int moves;
	move *list = board_moves(board, true, &moves);
	printf("Moves available: %d\n", moves);
	set (board, (coord){0, 1}, no_piece);
	move *list2 = board_moves(board, true, &moves);
	printf("Moves available: %d\n", moves);
}

void reset_board(piece board[8][8]) {
	for (int i = 16; i < 48; i++) board[i%8][i/8] = no_piece; // empty squares
	for (int i = 0; i < 8; i++) board[i][1] = (piece){'P', true, false}; // white pawns
	board[0][0] = board[7][0] = (piece){'R', true, false}; // white rooks
	board[1][0] = board[6][0] = (piece){'N', true, false}; // white knights
	board[2][0] = board[5][0] = (piece){'B', true, false}; // white bishops
	board[3][0] = (piece){'Q', true, false}; // white queen
	board[4][0] = (piece){'K', true, false}; // white king
	for (int i = 0; i < 8; i++) board[i][6] = (piece){'P', false, false}; // black pawns
	board[0][7] = board[7][7] = (piece){'R', false, false}; // black rooks
	board[1][7] = board[6][7] = (piece){'N', false, false}; // black knights
	board[2][7] = board[5][7] = (piece){'B', false, false}; // black bishops
	board[3][7] = (piece){'Q', false, false}; // black queen
	board[4][7] = (piece){'K', false, false}; // black king
}

void apply(piece b[8][8], move m) {
	set(b, m.to, p_eq(m.promote_to, no_piece) ? at(b, m.from) : m.promote_to);
	set(b, m.from, no_piece);
	if (!at(b, m.to).moved) m.flipped_moved = true;
	else m.flipped_moved = false;
	b[m.to.col][m.to.row].moved = true;
	if (m.c == K) { // castle
		b[5][m.to.row] = (piece){'R', at(b, m.to).white, true};
		b[7][m.to.row] = no_piece;
	} else if (m.c == Q) {
		b[3][m.to.row] = (piece){'R', at(b, m.to).white, true};
		b[0][m.to.row] = no_piece;
	}
}

void unapply(piece b[8][8], move m) {
	set(b, m.from, p_eq(m.promote_to, no_piece) ? at(b, m.to) : (piece){'P', at(b, m.to).white, true});
	set(b, m.to, m.captured);
	if (m.flipped_moved) b[m.from.col][m.from.row].moved = false;
	if (m.c == K) { // castle
		b[7][m.from.row] = (piece){'R', at(b, m.from).white, false};
		b[5][m.from.row] = no_piece;
	} else if (m.c == Q) {
		b[0][m.from.row] = (piece){'R', at(b, m.from).white, false};
		b[3][m.from.row] = no_piece;
	}
}

// Generates pseudo-legal moves for a player.
// (Does not exclude moves that put the king in check.)
// Generates an array of valid moves, and populates the count.
move *board_moves(piece board[8][8], bool white, int *count) {
	move *moves = malloc(sizeof(move) * 120); // Up to 120 moves supported
	*count = 0;
	for (uint8_t i = 0; i < 8; i++) { // col
		for (uint8_t j = 0; j < 8; j++) { // row
			if (p_eq(board[j][i], no_piece) || board[j][i].white != white) continue;
			(*count) += piece_moves(board, (coord){j, i}, moves + (*count));
		}

	}
	realloc(moves, sizeof(move) * (*count));
	return moves;
}

// Writes all legal moves for a piece to an array starting at index 0; 
// returns the number of items added.
int piece_moves(piece board[8][8], coord c, move *list) {
	int added = 0;
	if (p_eq(board[c.col][c.row], no_piece)) return 0;
	if (board[c.col][c.row].type == 'P') {
		added += pawn_moves(board, c, list);
	} else if (board[c.col][c.row].type == 'B') {
		added += diagonal_moves(board, c, list, 8);
	} else if (board[c.col][c.row].type == 'N') {
		for (int i = -2; i <= 2; i += 4) {
			for (int j = -1; j <= 1; j += 2) {
				added += moves_slide(board, c, list + added, i, j, 1);
				added += moves_slide(board, c, list + added, j, i, 1);
			}
		}
	} else if (board[c.col][c.row].type == 'R') {
		added += horizontal_moves(board, c, list, 8);
	} else if (board[c.col][c.row].type == 'Q') {
		added += diagonal_moves(board, c, list, 8);
		added += horizontal_moves(board, c, list + added, 8);

	} else if (board[c.col][c.row].type == 'K') {
		added += diagonal_moves(board, c, list, 1);
		added += horizontal_moves(board, c, list + added, 1);
		added += castle_moves(board, c, list + added);
	}
	return added;
}

int diagonal_moves(piece board[8][8], coord c, move *list, int steps) {
	int added = 0;
	added += moves_slide(board, c, list, 1, 1, steps);
	added += moves_slide(board, c, list + added, 1, -1, steps);
	added += moves_slide(board, c, list + added, -1, 1, steps);
	added += moves_slide(board, c, list + added, -1, -1, steps);
	return added;
}

int horizontal_moves(piece board[8][8], coord c, move *list, int steps) {
	int added = 0;
	added += moves_slide(board, c, list, 1, 0, steps);
	added += moves_slide(board, c, list + added, -1, 0, steps);
	added += moves_slide(board, c, list + added, 0, 1, steps);
	added += moves_slide(board, c, list + added, 0, -1, steps);
	return added;
}

// Precondition: the piece at c is a pawn.
int pawn_moves(piece b[8][8], coord c, move *list) {
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
int castle_moves(piece b[8][8], coord c, move *list) {
	int added = 0;
	int row = at(b, c).white ? 0 : 7;
	if (at(b, c).moved || /*c != (coord){4, row} || */in_check(c.col, c.row)) return 0;
	bool k_r_path_clear = true;
	bool q_r_path_clear = true;
	for (int i = 5; i <= 6; i++) if (!p_eq(b[i][row], no_piece) || in_check(i, row)) {k_r_path_clear = false; break;};
	for (int i = 3; i >= 1; i--) if (!p_eq(b[i][row], no_piece) || in_check(i, row)) {q_r_path_clear = false; break;};
	if (!at(b, (coord){7, row}).moved && k_r_path_clear) 
		if (add_move(b, (move){c, (coord){6, row}, no_piece, no_piece, K}, list + added)) added++;
	if (!at(b, (coord){0, row}).moved && q_r_path_clear) 
		if (add_move(b, (move){c, (coord){2, row}, no_piece, no_piece, Q}, list + added)) added++;
	return added;
}

// Writes all legal moves to an array by sliding from a given origin with some dx and dy for n steps.
// Stops on capture, board bounds, or blocking.
int moves_slide(piece b[8][8], coord orig_c, move *list, int dx, int dy, uint8_t steps) {
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
bool add_move(piece b[8][8], move m, move *list) {
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