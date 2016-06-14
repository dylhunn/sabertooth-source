/* Optimization TODO list:
 * - Store sets of pieces by color for use in:
 *   - board_moves
 *   - evaluate_material
 * - Update TT to provide alpha and beta for non-exact nodes
 * - MTD(f)
 */

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "types.h"
#include "util.h"
#include "movegen.h"
#include "ttable.h"

static const coord wqr = (coord) {0, 0}; // Rook squares
static const coord wkr = (coord) {7, 0};
static const coord bqr = (coord) {0, 7};
static const coord bkr = (coord) {7, 7};

// limit for how many moves print_analysis should print
static const int depth_limit = 50;

static move pv[50];

// set by last call to search()
static uint64_t nodes_searched;
static double search_millisec;

int repl(void);
void print_moves(board *b);
void print_board(board *b);
void print_analysis(board *b);
void iterative_deepen(board *b, int max_depth);
int search(board *b, int ply);
int ab_max(board *b, int alpha, int beta, int ply);
int ab_min(board *b, int alpha, int beta, int ply);
int evaluate(board *b);
int evaluate_material(board *b);
void reset_board(board *b);
void apply(board *b, move m);
void unapply(board *b, move m);

int main() {
	repl();
	return 0;
}

int repl(void) {
	tt_init();
	true_game_ply_clock = 0;
	board b; 
	reset_board(&b);
	system("clear");
	printf("Fianchetto 0.1a Console Analysis Interface\n\n");
	while (true) {
		print_board(&b);
		printf("Commands: \"e3\" evaluates to depth 3; \"ma1a2\" makes the move a1a2; \"q\" quits.\n\n");
		char buffer[100];
		fgets(buffer, 99, stdin);
		int edepth = buffer[1] - '0';
		move m;
		switch(buffer[0]) {
			case 'e':
				printf("Calculating...\n");
				system("clear");
				iterative_deepen(&b, edepth);
				printf("\n");
				break;
			case 'm':
				system("clear");
				if (!string_to_move(&b, buffer + 1, &m)) {
					move_to_string(m, buffer);
					printf("Invalid move: %s\n", buffer);

				} else {
					printf("Read move: %s\n", move_to_string(m, buffer));
					true_game_ply_clock++;
					apply(&b, m);
				}
				printf("\n");
				break;
			case 'q':
				exit(0);
			default:
				system("clear");
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
		printf(cBLU "%d " cRESET, i+1);
		for (int j = 0; j <= 7; j++) {
			if (p_eq(b->b[j][i], no_piece)) printf(" ");
			else {
				if (b->b[j][i].white) printf(cWHT "%c" cRESET, b->b[j][i].type);
				else printf(cYEL "%c" cRESET, b->b[j][i].type);
			}
		}
		printf("\n");
	}
	printf(cBLU "  ABCDEFGH\n" cRESET);
	printf("Castling rights: ");
	if (b->castle_rights_wq) printf("white queenside; ");
	if (b->castle_rights_wk) printf("white kingside; ");
	if (b->castle_rights_bq) printf("black queenside; ");
	if (b->castle_rights_bk) printf("black kingside; ");

	printf("\n%s to move.\n\n", b->black_to_move ? "Black" : "White");
	print_moves(b);
	printf("\n");
}

void print_analysis(board *b_orig) {
	int curr_depth = depth_limit;
	board b_cpy = *b_orig;
	board *b = &b_cpy;
	evaluation *eval = tt_get(b);
	printf("d%d [%+.2f]: ", eval->depth, ((double)eval->score)/100); // Divide centipawn score
	assert(eval != NULL);
	int moveno = (b->last_move_ply+2)/2;
	if (b->black_to_move) {
		printf("%d...", moveno);
		moveno++;
	}
	do {
		if (!b->black_to_move) printf("%d.", moveno++);
		char move[6];
		printf("%s ", move_to_string(eval->best, move));
		apply(b, eval->best);
		eval = tt_get(b);
	} while (eval != NULL && !m_eq(eval->best, no_move) && curr_depth-- > 0);
	printf("(%llu new nodes in %.0fms)", nodes_searched, search_millisec);
	printf("\n");
}

void iterative_deepen(board *b, int max_depth) {
	printf("Iterative Deepening Analysis Results (including cached analysis)\n");
	for (int i = 1; i <= max_depth; i++) {
		printf("Searching at depth %d... ", i);
		fflush(stdout);
		search(b, i);
		print_analysis(b);
	}
}

int search(board *b, int ply) {
	struct timeval t1, t2;
    // start timer
    gettimeofday(&t1, NULL);
	nodes_searched = 0;
	int result;
	if (b->black_to_move) result = ab_min(b, INT_MIN, INT_MAX, ply);
	else result = ab_max(b, INT_MIN, INT_MAX, ply);
	gettimeofday(&t2, NULL);
    // compute and print the elapsed time in millisec
    search_millisec = (t2.tv_sec - t1.tv_sec) * 1000.0;      // sec to ms
    search_millisec += (t2.tv_usec - t1.tv_usec) / 1000.0;   // us to ms
    return result;

}

int ab_max(board *b, int alpha, int beta, int ply) {
	evaluation *stored = tt_get(b);
	if (stored != NULL && stored->depth >= ply) {
		if (stored->type == at_least) {
			if (stored->score >= beta) return beta;
		} else if (stored->type == at_most) {
			//if (stored->score <= alpha) return alpha;
		} else { // exact
			if (stored->score >= beta) return beta; // respect fail-hard cutoff
			if (stored->score < alpha) return alpha; // alpha cutoff
			return stored->score;
		}
	}	

	if (ply == 0) return evaluate(b);
	int num_children;
	move chosen_move = no_move;
	move *moves = board_moves(b, &num_children);
	assert(num_children > 0);

	// start with the move from the transposition table
	if (stored != NULL) {
		assert(!m_eq(stored->best, no_move));
		moves[num_children] = stored->best;
		num_children++;
	}

	int localbest = INT_MIN;
	for (int i = num_children - 1; i >= 0; i--) {
		apply(b, moves[i]);
		nodes_searched++;
		int score = ab_min(b, alpha, beta, ply - 1);
		if (score >= beta) {
			unapply(b, moves[i]);
			tt_put(b, (evaluation){moves[i], score, at_least, ply});
			free(moves);
			return beta; // fail-hard
		}
		if (score > localbest) {
			localbest = score;
			chosen_move = moves[i];
			if (score > alpha) alpha = score;
		}
		unapply(b, moves[i]);
	}
	tt_put(b, (evaluation){chosen_move, alpha, exact, ply});
	free(moves);
	return alpha;
}

int ab_min(board *b, int alpha, int beta, int ply) {
	evaluation *stored = tt_get(b);
	if (stored != NULL && stored->depth >= ply) {
		if (stored->type == at_least) {
			//if (stored->score >= beta) return beta;
		} else if (stored->type == at_most) {
			if (stored->score <= alpha) return alpha;
		} else { // exact
			if (stored->score <= alpha) return alpha; // respect fail-hard cutoff
			if (stored->score > beta) return beta; // alpha cutoff
			return stored->score;
		}
	}

	if (ply == 0) return evaluate(b);
	int num_children;
	move chosen_move = no_move;
	move *moves = board_moves(b, &num_children);
	assert(num_children > 0);

	// start with the move from the transposition table
	if (stored != NULL) {
		assert(!m_eq(stored->best, no_move));
		moves[num_children] = stored->best;
		num_children++;
	}

	int localbest = INT_MAX;
	for (int i = num_children - 1; i >= 0; i--) {
		apply(b, moves[i]);
		nodes_searched++;
		int score = ab_max(b, alpha, beta, ply - 1);
		if (score <= alpha) {
			unapply(b, moves[i]);
			tt_put(b, (evaluation){moves[i], score, at_most, ply});
			free(moves);
			return alpha; // fail-hard
		}
		if (score < localbest) {
			localbest = score;
			chosen_move = moves[i];
			if (score < beta) beta = score;
		}
		unapply(b, moves[i]);
	}
	tt_put(b, (evaluation){chosen_move, beta, exact, ply});
	free(moves);
	return beta;
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
				case 'K': piece_val = 60000; break;
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
		uint8_t rook_to_col = ((m.c == K) ? 5 : 2);
		b->hash ^= tt_pieceval(b, (coord){rook_from_col, m.from.row});
		b->b[rook_to_col][m.to.row] = (piece){'R', at(b, m.to).white};
		b->b[rook_from_col][m.from.row] = no_piece;
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
		uint8_t rook_from_col = ((m.c == K) ? 5 : 2);
		b->hash ^= tt_pieceval(b, (coord){rook_from_col, m.from.row});
		b->b[rook_to_col][m.to.row] = (piece){'R', at(b, m.from).white};
		b->b[rook_from_col][m.from.row] = no_piece;
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
}
