//#define NDEBUG // Disable assertions

/* Optimization TODO list:
 * - Store sets of pieces by color for use in:
 *   - board_moves
 *   - evaluate_material
 */

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NO_COORD {255, 255}
#define NO_PIECE {'0', false, false}

// starting size is prime number
#define TT_STARTING_SIZE 15485867

typedef enum castle {
	N, K, Q // castle directions
} castle;

typedef struct piece {
	char type;
	bool white;
	bool moved;
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
	bool flipped_moved;
} move;

typedef struct board {
	piece b[8][8]; // cols then rows
	uint64_t hash;
	bool black_to_move;
} board;

typedef struct evaluation {
	move best;
	int score;
} evaluation;

const piece no_piece = NO_PIECE;
const move no_move = {NO_COORD, NO_COORD, NO_PIECE, NO_PIECE};
const char promo_p[] = {'Q', 'N', 'B', 'R'}; // promotion targets
const double tt_max_load = .7; // re-hash at 70% load factor

uint64_t *tt_keys = NULL;
evaluation *tt_values = NULL;
uint64_t tt_size = TT_STARTING_SIZE;
uint64_t tt_count = 0;
uint64_t tt_rehash_count; // computed based on max_load
uint64_t zobrist[64][12]; // zobrist table for pieces
uint64_t zobrist_castle_wq; // removed when castling rights are lost
uint64_t zobrist_castle_wk;
uint64_t zobrist_castle_bq;
uint64_t zobrist_castle_bk;
uint64_t zobrist_black_to_move;

int main(int argc, char *argv[]);
void print_analysis(board *b);
int evaluate(board *b);
int evaluate_material(board *b);
void reset_board(board *b);
void apply(board *b, move m);
void unapply(board *b, move m);
void update_castling_rights_hash(board *b, move m);
void tt_init();
void tt_auto_cleanup();
uint64_t tt_hash_position(board *b);
void tt_put(board *b, evaluation e);
evaluation *tt_get(board *b);
void tt_expand();
move *board_moves(board *b, int *count);
int piece_moves(board *b, coord c, move *list);
int diagonal_moves(board *b, coord c, move *list, int steps);
int horizontal_moves(board *b, coord c, move *list, int steps);
int pawn_moves(board *b, coord c, move *list);
int castle_moves(board *b, coord c, move *list);
int slide_moves(board *b, coord orig_c, move *list, int dx, int dy, uint8_t steps);
bool add_move(board *b, move m, move *list);
void move_to_string(move m, char str[6]);
bool in_check(int col, int row);
uint64_t rand64();

static inline bool p_eq(piece a, piece b) {
	return a.type == b.type && a.white == b.white && a.moved == b.moved;
}

static inline bool c_eq(coord a, coord b) {
	return a.col == b.col && a.row == b.row;
}

static inline bool m_eq(move a, move b) {
	return c_eq(a.from, b.from) && c_eq(a.to, b.to) && p_eq(a.captured, b.captured) && p_eq(a.promote_to, b.promote_to);
}

static inline bool in_bounds(coord c) {
	return c.row <= 7 && c.col <= 7; // coordinates are unsigned
}

static inline piece at(board *b, coord c) {
	return b->b[c.col][c.row];
}

static inline void set(board *b, coord c, piece p) {
	b->b[c.col][c.row] = p;
}

static inline int square_code(coord c) {
	return (c.col-1)*8+c.row;
}

static inline uint64_t tt_pieceval(board *b, coord c) {
	int piece_code = 0;
	piece p = at(b, c);
	if (p_eq(p, no_piece)) return 0;
	if (p.white) piece_code += 6;
	if (p.type == 'N') piece_code += 1;
	else if (p.type == 'B') piece_code += 2;
	else if (p.type == 'R') piece_code += 3;
	else if (p.type == 'Q') piece_code += 4;
	else piece_code += 5;
	return zobrist[square_code(c)][piece_code];
}

static inline uint64_t tt_index(board *b) {
	uint64_t idx = b->hash % tt_size;
	while (tt_keys[idx] != 0 && tt_keys[idx] != b->hash) idx = (idx + 1) % tt_size;
	return idx;
}

int main(int argc, char *argv[]) {
	tt_init();
	board b; 
	reset_board(&b);
	int moves = 0;
	move *list = board_moves(&b, &moves);
	search(&b, 3);
	printf("Moves available: %d\n", moves);
	print_analysis(&b);
	set(&b, (coord){0, 1}, no_piece);
	move *list2 = board_moves(&b, &moves);
	search(&b, 3);
	printf("Moves available: %d\n", moves);
	print_analysis(&b);
	free(list);
	free(list2);
}

void print_analysis(board *b_orig) {
	board b_cpy = *b_orig;
	board *b = &b_cpy;
	printf("%+.2f ", ((double)tt_get(b)->score)/100);
	evaluation *next = tt_get(b);
	int moveno = 1;
	if (b->black_to_move) {
		printf("1...");
		moveno++;
	}
	while (next != NULL) {
		if (!b->black_to_move) printf("%d.", moveno++);
		char move[6];
		move_to_string(tt_get(b)->best, move);
		printf("%s ", move);
		next = tt_get(b);
		apply(b, next->best);
	}
	printf("\n");
}

int search(board *b, int ply) {
	if (b->black_to_move) return minimax_min(b, ply);
	return minimax_max(b, ply);
}

int minimax_max(board *b, int ply) {
	if (ply == 0) return evaluate(b);
	int num_children;
	move best_move;
	move *moves = board_moves(b, &num_children);
	assert(num_children > 0);
	int score = INT_MIN;
	for (int i = 0; i < num_children; i++) {
		apply(b, moves[i]);
		int child_val = minimax_min(b, ply - 1);
		if (score < child_val) {
			score = child_val;
			best_move = moves[i];
		}
		unapply(b, moves[i]);
	}
	tt_put(b, (evaluation){best_move, score});
	free(moves);
	return score;
}

int minimax_min(board *b, int ply) {
	if (ply == 0) return evaluate(b);
	int num_children;
	move best_move;
	move *moves = board_moves(b, &num_children);
	assert(num_children > 0);
	int score = INT_MAX;
	for (int i = 0; i < num_children; i++) {
		apply(b, moves[i]);
		int child_val = minimax_min(b, ply - 1);
		if (score > child_val) {
			score = child_val;
			best_move = moves[i];
		}
		unapply(b, moves[i]);
	}
	tt_put(b, (evaluation){best_move, score});
	free(moves);
	return score;
}

// Statically evaluates a board position.
// Positive numbers indicate white advantage.
// Returns result in centipawns.
int evaluate(board *b) {
	float score = 0;
	score += evaluate_material(b);
	return score;
}

int evaluate_material(board *b) {
	int eval = 0;
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < 8; j++) {
			if (p_eq(no_piece, b->b[i][j])) continue;
			float piece_val = 0;
			switch (b->b[i][j].type) {
				case 'P': piece_val = 100; break;
				case 'N': piece_val = 320; break;
				case 'B': piece_val = 330; break;
				case 'R': piece_val = 500; break;
				case 'Q': piece_val = 900; break;
				case 'K': piece_val = 20000; break;
				default: assert(false);
			}
			if (!b->b[i][j].white) piece_val = -piece_val;
			eval += piece_val;
		}
	}
	return eval;
}

void reset_board(board *b) {
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
	b->hash = tt_hash_position(b);
	b->black_to_move = false;
}

void apply(board *b, move m) {
	b->hash ^= tt_pieceval(b, m.from);
	b->hash ^= tt_pieceval(b, m.to);
	piece new_piece = p_eq(m.promote_to, no_piece) ? at(b, m.from) : m.promote_to;
	set(b, m.to, new_piece);
	set(b, m.from, no_piece);
	if (!at(b, m.to).moved) m.flipped_moved = true;
	else m.flipped_moved = false;
	b->b[m.to.col][m.to.row].moved = true;
	if (m.c == K) { // castle
		b->hash ^= tt_pieceval(b, (coord){7, m.to.row});
		b->b[5][m.to.row] = (piece){'R', at(b, m.to).white, true};
		b->b[7][m.to.row] = no_piece;
		b->hash ^= tt_pieceval(b, (coord){5, m.to.row});
	} else if (m.c == Q) {
		b->hash ^= tt_pieceval(b, (coord){0, m.to.row});
		b->b[3][m.to.row] = (piece){'R', at(b, m.to).white, true};
		b->b[0][m.to.row] = no_piece;
		b->hash ^= tt_pieceval(b, (coord){3, m.to.row});
	}
	update_castling_rights_hash(b, m);
	b->hash ^= zobrist_black_to_move;
	b->black_to_move = !b->black_to_move;
	b->hash ^= tt_pieceval(b, m.to);
}

void unapply(board *b, move m) {
	b->hash ^= tt_pieceval(b, m.to);
	update_castling_rights_hash(b, m);
	b->hash ^= zobrist_black_to_move;
	b->black_to_move = !b->black_to_move;
	piece old_piece = p_eq(m.promote_to, no_piece) ? at(b, m.to) : (piece){'P', at(b, m.to).white, true};
	set(b, m.from, old_piece);
	set(b, m.to, m.captured);
	if (m.flipped_moved) b->b[m.from.col][m.from.row].moved = false;
	if (m.c == K) { // castle
		b->hash ^= tt_pieceval(b, (coord){5, m.from.row});
		b->b[7][m.from.row] = (piece){'R', at(b, m.from).white, false};
		b->b[5][m.from.row] = no_piece;
		b->hash ^= tt_pieceval(b, (coord){7, m.from.row});
	} else if (m.c == Q) {
		b->hash ^= tt_pieceval(b, (coord){3, m.from.row});
		b->b[0][m.from.row] = (piece){'R', at(b, m.from).white, false};
		b->b[3][m.from.row] = no_piece;
		b->hash ^= tt_pieceval(b, (coord){0, m.from.row});
	}
	b->hash ^= tt_pieceval(b, m.from);
	b->hash ^= tt_pieceval(b, m.to);
}

// If m was the first move of a rook or king, revokes castling rights in the hash.
// Call this AFTER applying m to b, or BEFORE m is unapplied.
// Relies on the flipped_moved field.
void update_castling_rights_hash(board *b, move m) {
	if (!m.flipped_moved) return;
	bool isKing = at(b, m.to).type == 'K';
	bool isRook = at(b, m.to).type == 'R';
	if (!isKing && !isRook) return;
	bool isWhite = at(b, m.to).white;
	// castling strips rights
	if (m.c == Q && isWhite) b->hash ^= zobrist_castle_wq;
	if (m.c == K && isWhite) b->hash ^= zobrist_castle_wk;
	if (m.c == Q && !isWhite) b->hash ^= zobrist_castle_bq;
	if (m.c == K && !isWhite) b->hash ^= zobrist_castle_bk;
	if (isKing && isWhite) {
		b->hash ^= zobrist_castle_wq;
		b->hash ^= zobrist_castle_wk;
	} else if (isKing) {
		b->hash ^= zobrist_castle_bq;
		b->hash ^= zobrist_castle_bk;
	} else if (isRook && isWhite && m.from.col == 0) b->hash ^= zobrist_castle_wq;
	else if (isRook && !isWhite && m.from.col == 0) b->hash ^= zobrist_castle_bq;
	else if (isRook && isWhite) b->hash ^= zobrist_castle_wk; 
	else if (isRook && !isWhite) b->hash ^= zobrist_castle_bk;
}

// Invoke to prepare transposition table
void tt_init() {
	if (tt_keys != NULL) free(tt_keys);
	if (tt_values != NULL) free(tt_values);
	tt_keys = malloc(sizeof(uint64_t) * tt_size);
	memset(tt_keys, 0, tt_size);
	tt_values = malloc(sizeof(evaluation) * tt_size);
	tt_count = 0;
	tt_rehash_count = ceil(tt_max_load * tt_size);

	srand(time(NULL));
	for (int i = 0; i < 64; i++) {
		for (int j = 0; j < 12; j++) {
			zobrist[i][j] = rand64();
		}
	}
	zobrist_castle_wq = rand64();
	zobrist_castle_wk = rand64();
	zobrist_castle_bq = rand64();
	zobrist_castle_bk = rand64();
	zobrist_black_to_move = rand64();
	atexit(tt_auto_cleanup);
}

// Automatically called
void tt_auto_cleanup() {
	free(tt_keys);
	free(tt_values);
}

uint64_t tt_hash_position(board *b) {
	uint64_t hash = 0;
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < 8; j++) {
			hash ^= tt_pieceval(b, (coord){i, j});
		}
	}
	if (b->black_to_move) hash ^= zobrist_black_to_move;

	// white castling
	if (b->b[4][0].type == 'K' && !b->b[4][0].moved) {
		if (b->b[0][0].type == 'R' && !b->b[0][0].moved) {
			hash ^= zobrist_castle_wq;
		}
		if (b->b[7][0].type == 'R' && !b->b[7][0].moved) {
			hash ^= zobrist_castle_wk;
		}
	}
	// black castling
	if (b->b[4][7].type == 'K' && !b->b[4][7].moved) {
		if (b->b[0][7].type == 'R' && !b->b[0][7].moved) {
			hash ^= zobrist_castle_bq;
		}
		if (b->b[7][7].type == 'R' && !b->b[7][7].moved) {
			hash ^= zobrist_castle_bk;
		}
	}
	return hash;
}

void tt_put(board *b, evaluation e) {
	if (tt_count >= tt_rehash_count) tt_expand();
	uint64_t idx = tt_index(b);
	if (tt_keys[idx] == 0) tt_count++;
	tt_keys[idx] = b->hash;
	tt_values[idx] = e;
}

// Returns NULL if entry is not found.
evaluation *tt_get(board *b) {
	uint64_t idx = tt_index(b);
	if (tt_keys[idx] == 0) return NULL;
	return tt_values + idx;
}

void tt_expand() {
	uint64_t new_size = tt_size * 2;
	uint64_t *new_keys = malloc(sizeof(uint64_t) * new_size);
	evaluation *new_values = malloc(sizeof(uint64_t) * new_size);
	memset(new_keys, 0, new_size); // zero out keys
	for (uint64_t i = 0; i < tt_size; i++) { // for every old index
		if (tt_keys[i] == 0) continue; // skip empty slots
		uint64_t new_idx = tt_keys[i] % new_size;
		new_keys[new_idx] = tt_keys[i];
		new_values[new_idx] = tt_values[i];
	}
	free(tt_keys);
	free(tt_values);
	tt_keys = new_keys;
	tt_values = new_values;
	tt_size = new_size;
	tt_rehash_count = ceil(tt_max_load * new_size);
}

// Generates pseudo-legal moves for a player.
// (Does not exclude moves that put the king in check.)
// Generates an array of valid moves, and populates the count.
move *board_moves(board *b, int *count) {
	bool white = !b->black_to_move;
	move *moves = malloc(sizeof(move) * 120); // Up to 120 moves supported
	*count = 0;
	for (uint8_t i = 0; i < 8; i++) { // col
		for (uint8_t j = 0; j < 8; j++) { // row
			if (p_eq(b->b[j][i], no_piece) || b->b[j][i].white != white) continue;
			(*count) += piece_moves(b, (coord){j, i}, moves + (*count));
		}

	}
	moves = realloc(moves, sizeof(move) * (*count));
	return moves;
}

// Writes all legal moves for a piece to an array starting at index 0; 
// returns the number of items added.
int piece_moves(board *b, coord c, move *list) {
	int added = 0;
	if (p_eq(at(b, c), no_piece)) return 0;
	if (at(b, c).type == 'P') {
		added += pawn_moves(b, c, list);
	} else if (at(b, c).type == 'B') {
		added += diagonal_moves(b, c, list, 8);
	} else if (at(b, c).type == 'N') {
		for (int i = -2; i <= 2; i += 4) {
			for (int j = -1; j <= 1; j += 2) {
				added += slide_moves(b, c, list + added, i, j, 1);
				added += slide_moves(b, c, list + added, j, i, 1);
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

int diagonal_moves(board *b, coord c, move *list, int steps) {
	int added = 0;
	added += slide_moves(b, c, list, 1, 1, steps);
	added += slide_moves(b, c, list + added, 1, -1, steps);
	added += slide_moves(b, c, list + added, -1, 1, steps);
	added += slide_moves(b, c, list + added, -1, -1, steps);
	return added;
}

int horizontal_moves(board *b, coord c, move *list, int steps) {
	int added = 0;
	added += slide_moves(b, c, list, 1, 0, steps);
	added += slide_moves(b, c, list + added, -1, 0, steps);
	added += slide_moves(b, c, list + added, 0, 1, steps);
	added += slide_moves(b, c, list + added, 0, -1, steps);
	return added;
}

// Precondition: the piece at c is a pawn.
int pawn_moves(board *b, coord c, move *list) {
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
int castle_moves(board *b, coord c, move *list) {
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
int slide_moves(board *b, coord orig_c, move *list, int dx, int dy, uint8_t steps) {
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
bool add_move(board *b, move m, move *list) {
	list[0] = m; return true; // todo
	/*apply(b, m);
	bool viable = in_check(king_loc);
	if (viable) list[0] = m;
	unapply(b, m);
	return viable;*/
}

// caller must provide a 6-character buffer
void move_to_string(move m, char str[6]) {
	str[0] = 'a' + m.from.col;
	str[1] = 1 + m.from.row;
	str[2] = 'a' + m.to.col;
	str[3] = 1 + m.to.row;
	if (!p_eq(m.promote_to, no_piece)) {
		str[4] = tolower(m.promote_to.type);
		str[5] = '\0';
		return;
	}
	str[4] = '\0';
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