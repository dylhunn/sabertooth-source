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
#define NO_PIECE {'0', false}

// starting size is prime number
#define TT_STARTING_SIZE 15485867

#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define RESET "\x1B[0m"

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

static const piece no_piece = NO_PIECE;
static const move no_move = {NO_COORD, NO_COORD, NO_PIECE, NO_PIECE, N};
static const char promo_p[] = {'Q', 'N', 'B', 'R'}; // promotion targets
static const double tt_max_load = .7; // re-hash at 70% load factor

static const coord wqr = (coord) {0, 0}; // Rook squares
static const coord wkr = (coord) {7, 0};
static const coord bqr = (coord) {0, 7};
static const coord bkr = (coord) {7, 7};

static uint64_t *tt_keys = NULL;
static evaluation *tt_values = NULL;
static uint64_t tt_size = TT_STARTING_SIZE;
static uint64_t tt_count = 0;
static uint64_t tt_rehash_count; // computed based on max_load
static uint64_t zobrist[64][12]; // zobrist table for pieces
static uint64_t zobrist_castle_wq; // removed when castling rights are lost
static uint64_t zobrist_castle_wk;
static uint64_t zobrist_castle_bq;
static uint64_t zobrist_castle_bk;
static uint64_t zobrist_black_to_move;

int repl(void);
void print_moves(board *b);
void print_board(board *b);
void print_analysis(board *b);
int search(board *b, int ply);
int minimax_max(board *b, int ply);
int minimax_min(board *b, int ply);
int evaluate(board *b);
int evaluate_material(board *b);
void reset_board(board *b);
void apply(board *b, move m);
void unapply(board *b, move m);
void update_castling_rights_hash(board *b, move m);
void tt_init(void);
void tt_auto_cleanup(void);
uint64_t tt_hash_position(board *b);
void tt_put(board *b, evaluation e);
evaluation *tt_get(board *b);
void tt_expand(void);
move *board_moves(board *b, int *count);
int piece_moves(board *b, coord c, move *list);
int diagonal_moves(board *b, coord c, move *list, int steps);
int horizontal_moves(board *b, coord c, move *list, int steps);
int pawn_moves(board *b, coord c, move *list);
int castle_moves(board *b, coord c, move *list);
int slide_moves(board *b, coord orig_c, move *list, int dx, int dy, int steps);
bool add_move(board *b, move m, move *list);
char *move_to_string(move m, char str[6]);
bool string_to_move(board *b, char *str, move *m);
bool in_check(int col, int row);
uint64_t rand64(void);

static inline bool p_eq(piece a, piece b) {
	return a.type == b.type && a.white == b.white;
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

int main() {
	repl();
	return 0;
}

int repl(void) {
	tt_init();
	board b; 
	reset_board(&b);
	while (true) {
		print_board(&b);
		printf("Commands: \"e n\" evaluates to depth n; \"m a1a2\" makes a move.\n\n");
		char buffer[100];
		fgets(buffer, 99, stdin);
		int edepth = buffer[2] - '0';
		move m;
		switch(buffer[0]) {
			case 'e':
				search(&b, edepth);
				print_analysis(&b);
				printf("\n");
				break;
			case 'm':
				if (!string_to_move(&b, buffer + 2, &m)) {
					move_to_string(m, buffer);
					printf("Invalid move: %s\n", buffer);

				} else {
					printf("Read move: %s\n", move_to_string(m, buffer));
					apply(&b, m);
				}
				printf("\n");
				break;
			default:
				printf("Unrecognized command.\n\n");
		}
	}

}

void print_moves(board *b) {
	int movec = 0;
	move *list = board_moves(b, &movec);
	printf("%d moves available: ", movec);
	for (int i = 0; i < movec; i++) {
		char moveb[6];
		printf("%s ", move_to_string(list[i], moveb));
	}
	printf("\n");
	free(list);
}

void print_board(board *b) {
	for (int i = 7; i >= 0; i--) {
		printf(BLU "%d " RESET, i+1);
		for (int j = 0; j <= 7; j++) {
			if (p_eq(b->b[j][i], no_piece)) printf(" ");
			else {
				if (b->b[j][i].white) printf(WHT "%c" RESET, b->b[j][i].type);
				else  printf(YEL "%c" RESET, b->b[j][i].type);
			}
		}
		printf("\n");
	}
	printf(BLU "  ABCDEFGH\n\n" RESET);
	printf("%s to move.\n", b->black_to_move ? "Black" : "White");
	print_moves(b);
	printf("\n");
}

void print_analysis(board *b_orig) {
	board b_cpy = *b_orig;
	board *b = &b_cpy;
	printf("%+.2f ", ((double)tt_get(b)->score)/100);
	evaluation *eval = tt_get(b);
	assert(eval != NULL);
	int moveno = 1;
	if (b->black_to_move) {
		printf("1...");
		moveno++;
	}
	do {
		if (!b->black_to_move) printf("%d.", moveno++);
		char move[6];
		printf("%s ", move_to_string(eval->best, move));
		apply(b, eval->best);
		eval = tt_get(b);
	} while (eval != NULL);
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
		uint64_t oldhash = tt_hash_position(b); // debugging
		apply(b, moves[i]);
		int child_val = minimax_min(b, ply - 1);
		if (score < child_val) {
			score = child_val;
			best_move = moves[i];
		}
		unapply(b, moves[i]);
		assert(tt_hash_position(b) == oldhash);
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
		uint64_t oldhash = tt_hash_position(b); // debugging
		//print_board(b);
		apply(b, moves[i]);
		//print_board(b);
		int child_val = minimax_min(b, ply - 1);
		if (score > child_val) {
			score = child_val;
			best_move = moves[i];
		}
		unapply(b, moves[i]);
		/*if (tt_hash_position(b) != oldhash) {
			print_board(b);
			char mv[6];
			move_to_string(moves[i], mv);
			printf("Chosen move: %s\n", mv);
		}*/
		assert(tt_hash_position(b) == oldhash);
	}
	tt_put(b, (evaluation){best_move, score});
	free(moves);
	return score;
}

// Statically evaluates a board position.
// Positive numbers indicate white advantage.
// Returns result in centipawns.
int evaluate(board *b) {
	int score = 0;
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
}

void apply(board *b, move m) {
	// Information
	piece moved_piece = at(b, m.from);
	piece new_piece = p_eq(m.promote_to, no_piece) ? at(b, m.from) : m.promote_to;

	// Transform board and hash
	b->hash ^= tt_pieceval(b, m.from);
	b->hash ^= tt_pieceval(b, m.to);
	set(b, m.to, new_piece);
	set(b, m.from, no_piece);
	b->hash ^= tt_pieceval(b, m.to);
	b->hash ^= zobrist_black_to_move;
	b->black_to_move = !b->black_to_move;
	b->last_move_ply++;

	// Manually move rook for castling
	if (m.c != N) { // Manually move rook
		uint8_t rook_from_col = ((m.c == K) ? 7 : 0);
		uint8_t rook_to_col = ((m.c == K) ? 5 : 3);
		b->hash ^= tt_pieceval(b, (coord){rook_from_col, m.to.row});
		b->b[rook_to_col][m.to.row] = (piece){'R', at(b, m.to).white};
		b->b[rook_from_col][m.to.row] = no_piece;
		b->hash ^= tt_pieceval(b, (coord){rook_to_col, m.to.row});
	}

	// King moves always strip castling rights
	if (moved_piece.white && moved_piece.type == 'K') {
		if (b->castle_rights_wk) {
			b->hash ^= zobrist_castle_wk;
			b->castle_wk_lost_on_ply = b->last_move_ply;
			b->castle_rights_wk = false;
		}
		if (b->castle_rights_wq) {
			b->hash ^= zobrist_castle_wq;
			b->castle_wq_lost_on_ply = b->last_move_ply;
			b->castle_rights_wq = false;
		}
	} else if (!moved_piece.white && moved_piece.type == 'K') {
		if (b->castle_rights_bk) {
			b->hash ^= zobrist_castle_bk;
			b->castle_bk_lost_on_ply = b->last_move_ply;
			b->castle_rights_bk = false;
		}
		if (b->castle_rights_bq) {
			b->hash ^= zobrist_castle_bq;
			b->castle_bq_lost_on_ply = b->last_move_ply;
			b->castle_rights_bq = false;
		}
	}

	// Moves involving rook squares always strip castling rights
	if (c_eq(m.from, wqr) && b->castle_rights_wq) {
		b->castle_rights_wq = false;
		b->hash ^= zobrist_castle_wq;
		b->castle_wq_lost_on_ply = b->last_move_ply;
	}
	if (c_eq(m.from, wkr) && b->castle_rights_wk) {
		b->castle_rights_wk = false;
		b->hash ^= zobrist_castle_wk;
		b->castle_wk_lost_on_ply = b->last_move_ply;
	}
	if (c_eq(m.from, bqr) && b->castle_rights_bq) {
		b->castle_rights_bq = false;
		b->hash ^= zobrist_castle_bq;
		b->castle_bq_lost_on_ply = b->last_move_ply;
	}
	if (c_eq(m.from, bkr) && b->castle_rights_bk) {
		b->castle_rights_bk = false;
		b->hash ^= zobrist_castle_bk;
		b->castle_bk_lost_on_ply = b->last_move_ply;
	}

	assert(b->hash == tt_hash_position(b));
}

void unapply(board *b, move m) {
	// Information
	piece old_piece = p_eq(m.promote_to, no_piece) ? at(b, m.to) : (piece){'P', at(b, m.to).white};

	// Transform board and hash
	b->hash ^= tt_pieceval(b, m.to);
	set(b, m.from, old_piece);
	set(b, m.to, m.captured);
	b->hash ^= tt_pieceval(b, m.from);
	b->hash ^= tt_pieceval(b, m.to);
	b->hash ^= zobrist_black_to_move;
	b->black_to_move = !b->black_to_move;
	b->last_move_ply--;
	
	// Manually move rook for castling
	if (m.c != N) { // Manually move rook
		uint8_t rook_to_col = ((m.c == K) ? 7 : 0);
		uint8_t rook_from_col = ((m.c == K) ? 5 : 3);
		b->hash ^= tt_pieceval(b, (coord){rook_from_col, m.to.row});
		b->b[rook_to_col][m.to.row] = (piece){'R', at(b, m.to).white};
		b->b[rook_from_col][m.to.row] = no_piece;
		b->hash ^= tt_pieceval(b, (coord){rook_to_col, m.to.row});
	}

	// Restore castling rights
	if (b->castle_wq_lost_on_ply == b->last_move_ply + 1) {
		b->castle_rights_wq = true;
		b->hash ^= zobrist_castle_wq;
		b->castle_wq_lost_on_ply = -1;
	}
	if (b->castle_wk_lost_on_ply == b->last_move_ply + 1) {
		b->castle_rights_wk = true;
		b->hash ^= zobrist_castle_wk;
		b->castle_wk_lost_on_ply = -1;
	}
	if (b->castle_bq_lost_on_ply == b->last_move_ply + 1) {
		b->castle_rights_bq = true;
		b->hash ^= zobrist_castle_bq;
		b->castle_bq_lost_on_ply = -1;
	}
	if (b->castle_bk_lost_on_ply == b->last_move_ply + 1) {
		b->castle_rights_bk = true;
		b->hash ^= zobrist_castle_bk;
		b->castle_bk_lost_on_ply = -1;
	}

	assert(b->hash == tt_hash_position(b));
}

// Invoke to prepare transposition table
void tt_init(void) {
	if (tt_keys != NULL) free(tt_keys);
	if (tt_values != NULL) free(tt_values);
	tt_keys = malloc(sizeof(uint64_t) * tt_size);
	memset(tt_keys, 0, tt_size);
	tt_values = malloc(sizeof(evaluation) * tt_size);
	tt_count = 0;
	tt_rehash_count = (uint64_t) (ceil(tt_max_load * tt_size));

	srand((unsigned int) time(NULL));
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
void tt_auto_cleanup(void) {
	free(tt_keys);
	free(tt_values);
}

uint64_t tt_hash_position(board *b) {
	uint64_t hash = 0;
	for (uint8_t i = 0; i < 8; i++) {
		for (uint8_t j = 0; j < 8; j++) {
			hash ^= tt_pieceval(b, (coord){i, j});
		}
	}
	if (b->black_to_move) hash ^= zobrist_black_to_move;
	if (b->castle_rights_wq) hash ^= zobrist_castle_wq;
	if (b->castle_rights_wk) hash ^= zobrist_castle_wk;
	if (b->castle_rights_bq) hash ^= zobrist_castle_bq;
	if (b->castle_rights_bk) hash ^= zobrist_castle_bk;
	return hash;
}

void tt_put(board *b, evaluation e) {
	assert(tt_hash_position(b) == b->hash);
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

void tt_expand(void) {
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
	tt_rehash_count = (uint64_t) (ceil(tt_max_load * new_size));
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
	bool unmoved = (c.row == 1 && cp.white) || (c.row == 6 && !cp.white);
	int8_t dy = cp.white ? 1 : -1;
	bool promote = (c.row + dy == 0 || c.row + dy == 7); // next move is promotion
	if (p_eq(at(b, (coord){c.col, c.row + dy}), no_piece)) { // front is clear
		if (promote) {
			for (int i = 0; i < 4; i++)
				if (add_move(b, (move){c, (coord){c.col, c.row + dy}, no_piece, 
								(piece){promo_p[i], cp.white}, N}, list + added)) added++;
		} else {
			if (add_move(b, (move){c, {c.col, c.row + dy}, no_piece, no_piece, N}, list + added)) added++;
			if (unmoved && p_eq(at(b, (coord){c.col, c.row + dy + dy}), no_piece)) // double move
				if (add_move(b, (move){c, (coord){c.col, c.row + dy + dy}, 
							no_piece, no_piece, N}, list + added)) added++;
		}
	}
	for (int8_t dx = -1; dx <= 1; dx += 2) { // both capture directions
		coord cap = {c.col + dx, c.row + dy};
		if (!in_bounds(cap) || p_eq(at(b, cap), no_piece)) continue;
		if (promote) {
			for (int i = 0; i < 4; i++)
				if (add_move(b, (move){c, (coord){c.col + dx, c.row + dy}, at(b, cap), 
								(piece){promo_p[i], cp.white}, N}, list + added)) added++;
		} else if (add_move(b, (move){c, (coord){c.col + dx, c.row + dy}, at(b, cap), no_piece, N}, list + added)) 
			added++;
	}
	return added;
}

// Precondition: the piece at c is a king.
int castle_moves(board *b, coord c, move *list) {
	assert(at(b, c).type == 'K');
	int added = 0;
	uint8_t row = at(b, c).white ? 0 : 7;
	bool isWhite = at(b, c).white;
	if (in_check(c.col, c.row)) return 0;
	bool k_r_path_clear = true;
	bool q_r_path_clear = true;
	for (int i = 5; i <= 6; i++) if (!p_eq(b->b[i][row], no_piece) || in_check(i, row)) {k_r_path_clear = false; break;};
	for (int i = 3; i >= 1; i--) if (!p_eq(b->b[i][row], no_piece) || in_check(i, row)) {q_r_path_clear = false; break;};
	if (k_r_path_clear) {
		if (isWhite && b->castle_rights_wk) {
			if (add_move(b, (move){c, (coord){6, row}, no_piece, no_piece, K}, list + added)) added++;
		} else if (!isWhite && b->castle_rights_bk) 
			if (add_move(b, (move){c, (coord){6, row}, no_piece, no_piece, K}, list + added)) added++;
	}
	if (q_r_path_clear) {
		if (isWhite && b->castle_rights_wq) {
			if (add_move(b, (move){c, (coord){2, row}, no_piece, no_piece, K}, list + added)) added++;
		} else if (!isWhite && b->castle_rights_bq) 
			if (add_move(b, (move){c, (coord){2, row}, no_piece, no_piece, K}, list + added)) added++;
	}
	return added;
}

// Writes all legal moves to an array by sliding from a given origin with some dx and dy for n steps.
// Stops on capture, board bounds, or blocking.
int slide_moves(board *b, coord orig_c, move *list, int dx, int dy, int steps) {
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
// returns a pointer to the provided buffer
char *move_to_string(move m, char str[6]) {
	str[0] = 'a' + m.from.col;
	str[1] = '1' + m.from.row;
	str[2] = 'a' + m.to.col;
	str[3] = '1' + m.to.row;
	if (!p_eq(m.promote_to, no_piece)) {
		str[4] = (char) (tolower(m.promote_to.type));
		str[5] = '\0';
		return str;
	}
	str[4] = '\0';
	return str;
}

// Checks if a move is valid
bool string_to_move(board *b, char *str, move *m) {
	m->from = (coord) {str[0] - 'a', str[1] - '1'};
	m->to = (coord) {str[2] - 'a', str[3] - '1'};
	if (!in_bounds(m->from)) goto fail;
	if (!in_bounds(m->to)) goto fail;
	m->captured = at(b, m->to);
	putchar(str[4]);
	switch(str[4]) {
		case '\n':
			m->promote_to = no_piece;
			break;
		case 'q':
			m->promote_to = (piece){'Q', at(b, m->from).white};
			break;
		case 'n':
			m->promote_to = (piece){'N', at(b, m->from).white};
			break;
		case 'r':
			m->promote_to = (piece){'R', at(b, m->from).white};
			break;
		case 'b':
			m->promote_to = (piece){'B', at(b, m->from).white};
			break;
		default:
			goto fail;
	}

	if (m->to.col == m->from.col + 2) m->c = K;
	else if (m->to.col == m->from.col - 3) m->c = Q;
	else m->c = N;

	bool found = false;
	int nMoves;
	move *moves;
	moves = board_moves(b, &nMoves);
	for (int i = 0; i < nMoves; i++)
		if (m_eq(moves[i], *m)) found = true;
	if (!found) {
		goto fail;
	}
	free(moves);

	return true;
	fail: return false;;
}

// checks if a given coordinate would be in check on the current board
bool in_check(int col, int row) {
	return false; // todo
}

uint64_t rand64(void) {
    uint64_t r = 0;
    for (int i = 0; i < 5; ++i) {
        r = (r << 15) | (rand() & 0x7FFF);
    }
    return r & 0xFFFFFFFFFFFFFFFFULL;
}
