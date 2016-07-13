#include "search.h"

// Search statistics; set by last call to search()
searchstats sstats;

// Local functions
int mtd_f(board *b, int ply);
int abq(board *b, int alpha, int beta, int ply);
int relative_evaluation(board *b);
int capture_move_comparator(const board *board, const move *a, const move *b);

void clear_stats() {
	sstats.time = 0;
	sstats.depth = 0;
	sstats.nodes_searched = 0;
	sstats.qnodes_searched = 0;
	sstats.qnode_aborts = 0;
	sstats.ttable_inserts = 0;
	sstats.ttable_insert_failures = 0;
	sstats.ttable_hits = 0;
	sstats.ttable_misses = 0;
	sstats.ttable_overwrites = 0;
}

// Some parameters might be -1 if they do not apply
int time_use(board *b, int time_left, int increment, int movestogo) {
	if (time_left < 5000) return time_left / 10; // always freak out if we are almost out of time
	// Assume the game is 45 moves long, but never use more than 1/5th of the remaining time
	int moves_left_guess = (movestogo == -1) ? max(5, 45 - b->last_move_ply) : movestogo;
	return time_left / moves_left_guess;
}

int search(board *b, int ply) {
	clear_stats(); // Stats for search
	sstats.depth = ply;
	// Start timer for the search
	struct timeval t1, t2;
    gettimeofday(&t1, NULL);
	//int result = mtd_f(b, ply);
	int result = abq(b, NEG_INFINITY, POS_INFINITY, ply);
	gettimeofday(&t2, NULL);
    // Compute and print the elapsed time in millisec
    double search_millisec = (t2.tv_sec - t1.tv_sec) * 1000.0; // sec to ms
    search_millisec += (t2.tv_usec - t1.tv_usec) / 1000.0; // us to ms
    sstats.time = search_millisec;
    return result;
}

// NOT currently in usegi
int mtd_f(board *board, int ply) {
	int g; // First guess of evaluation
	evaluation *stored = tt_get(board); // Use last pass in Transposition Table
	if (stored != NULL) g = stored->score; // If not present, use static evaluation as guess
	else {
		g = evaluate(board);
		if (board->black_to_move) g = -g;
	}
	int upper_bound = POS_INFINITY;
	int lower_bound = NEG_INFINITY;
	while (lower_bound < upper_bound) {
		int beta;
		if (g == lower_bound) beta = g+1;
		else beta = g;
		//g = negamax(board, beta-1, beta, ply, !board->black_to_move);
		if (g < beta) upper_bound = g;
		else lower_bound = g;
	}
	return g;
}

// Unified alpha-beta and quiescence search
int abq(board *b, int alpha, int beta, int ply) {
	pthread_testcancel(); // To allow search worker thread termination
	bool quiescence = (ply <= 0);

	// Generate all possible moves for the quiscence search or normal search, and compute the
	// static evaluation if applicable.
	move *moves = NULL;
	int num_available_moves = 0;
	if (quiescence) moves = board_moves(b, &num_available_moves, true); // Generate only captures
	else moves = board_moves(b, &num_available_moves, false); // Generate all moves
	if (quiescence && !useqsearch) return relative_evaluation(b); // If qsearch is turned off

	// Abort if the quiescence search is too deep (currently 45 plies)
	if (ply < -quiesce_ply_cutoff) { 
		sstats.qnode_aborts++;
		return relative_evaluation(b);
	}

	// Allow the quiescence search to generate cutoffs
	if (quiescence) {
		int score = relative_evaluation(b);
		alpha = max(alpha, score);
		if (alpha >= beta) return score;
	}

	// Update search stats
	if (quiescence) sstats.qnodes_searched++;
	else sstats.nodes_searched++;

	// Search hueristic: sort exchanges using MVV-LVA
	if (quiescence && mvvlva) nlopt_qsort_r(moves, num_available_moves, sizeof(move), b, &capture_move_comparator);

	move best_move_yet = no_move;
	int best_score_yet = NEG_INFINITY;
	int num_moves_actually_examined = 0; // We might end up in checkmate
	for (int i = num_available_moves - 1; i >= 0; i--) { // Iterate backwards to match MVV-LVA sort order
		apply(b, moves[i]);
		// never move into check
		coord king_loc = b->black_to_move ? b->white_king : b->black_king; // for side that just moved
		if (in_check(b, king_loc.col, king_loc.row, !(b->black_to_move))) {
			unapply(b, moves[i]);
			continue;
		}
		int score = -abq(b, -beta, -alpha, ply - 1);
		num_moves_actually_examined++;
		unapply(b, moves[i]);
		if (score >= best_score_yet) {
			best_score_yet = score;
			best_move_yet = moves[i];
		}
		alpha = max(alpha, best_score_yet);
		if (alpha >= beta) break;
	}

	// We have no available moves (or captures) that don't leave us in check
	// This means checkmate or stalemate in normal search
	// It might mean no captures are available in quiescence search
	if (num_moves_actually_examined == 0) {
		if (quiescence) return relative_evaluation(b); // TODO: qsearch doesn't understand stalemate or checkmate
		coord king_loc = b->black_to_move ? b->black_king : b->white_king;
		if (in_check(b, king_loc.col, king_loc.row, b->black_to_move)) return NEG_INFINITY; // checkmate
		else return 0; // stalemate
	}

	// record the selected move in the transposition table
	evaltype type = (quiescence) ? qexact : exact;
	evaluation eval = {.best = best_move_yet, .score = best_score_yet, .type = type, .depth = ply};
	tt_put(b, eval);
	return best_score_yet;
}

/* 
 * Returns a relative evaluation of the board position from the perspective of the side about to move.
 */
int relative_evaluation(board *b) {
	int evaluation = evaluate(b);
	if (b->black_to_move) evaluation = -evaluation;
	return evaluation;
}

// An array sorting comparator for capture moves, for use with qsort_r.
// Sorts by MVV/LVA (Most Valuable Victim/Least Valuable Attacker) in REVERSE order.
// Returns -1 if the first argument should come first, etc.
// Uses reverse order because of implementation details above.
// COMPATIBILITY WARNING: When compiling with GNU libraries (Linux), the argument order
// is silently permuted! This uses the BSD/OS X ordering.
int capture_move_comparator(const board *board, const move *a, const move *b) {
	int a_victim_value;
	switch(a->captured.type) {
		case 'P': a_victim_value = 1; break;
		case 'N': a_victim_value = 3; break;
		case 'B': a_victim_value = 3; break;
		case 'R': a_victim_value = 5; break;
		case 'Q': a_victim_value = 9; break;
		case 'K': a_victim_value = 200; break;
		case '0': a_victim_value = 0; break; // In case we attack an empty square (no_piece)
		default: assert(false);
	}
	int b_victim_value;
	switch(b->captured.type) {
		case 'P': b_victim_value = 1; break;
		case 'N': b_victim_value = 3; break;
		case 'B': b_victim_value = 3; break;
		case 'R': b_victim_value = 5; break;
		case 'Q': b_victim_value = 9; break;
		case 'K': b_victim_value = 200; break;
		case '0': b_victim_value = 0; break; // In case we attack an empty square (no_piece)
		default: assert(false);
	}
	int a_attacker_value;
	switch(at(board, a->from).type) {
		case 'P': a_attacker_value = 1; break;
		case 'N': a_attacker_value = 3; break;
		case 'B': a_attacker_value = 3; break;
		case 'R': a_attacker_value = 5; break;
		case 'Q': a_attacker_value = 9; break;
		case 'K': a_attacker_value = 20; break;
		default: assert(false);
	}
	int b_attacker_value;
	switch(at(board, b->from).type) {
		case 'P': b_attacker_value = 1; break;
		case 'N': b_attacker_value = 3; break;
		case 'B': b_attacker_value = 3; break;
		case 'R': b_attacker_value = 5; break;
		case 'Q': b_attacker_value = 9; break;
		case 'K': b_attacker_value = 20; break;
		default: assert(false);
	}
	return ((a_victim_value << 2) - a_attacker_value) - ((b_victim_value << 2) - b_attacker_value);
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
		b->hash ^= tt_pieceval(b, (coord){rook_from_col, m.from.row});
		b->b[rook_to_col][m.to.row] = (piece){'R', at(b, m.to).white}; // !!
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

	if (moved_piece.type == 'K') {
		if (moved_piece.white) b->white_king = m.to;
		else b->black_king = m.to;
	}

	// Moves involving rook squares always strip castling rights
	if ((c_eq(m.from, wqr) || c_eq(m.to, wqr)) && b->castle_rights_wq) {
		b->castle_rights_wq = false;
		b->hash ^= zobrist_castle_wq;
		b->castle_wq_lost_on_ply = b->last_move_ply;
	}
	if ((c_eq(m.from, wkr) || c_eq(m.to, wkr)) && b->castle_rights_wk) {
		b->castle_rights_wk = false;
		b->hash ^= zobrist_castle_wk;
		b->castle_wk_lost_on_ply = b->last_move_ply;
	}
	if ((c_eq(m.from, bqr) || c_eq(m.to, bqr)) && b->castle_rights_bq) {
		b->castle_rights_bq = false;
		b->hash ^= zobrist_castle_bq;
		b->castle_bq_lost_on_ply = b->last_move_ply;
	}
	if ((c_eq(m.from, bkr) || c_eq(m.to, bkr)) && b->castle_rights_bk) {
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

	if (old_piece.type == 'K') {
		if (old_piece.white) b->white_king = m.from;
		else b->black_king = m.from;
	}
	
	// Manually move rook for castling
	if (m.c != N) { // Manually move rook
		uint8_t rook_to_col = ((m.c == K) ? 7 : 0);
		uint8_t rook_from_col = ((m.c == K) ? 5 : 3);
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

