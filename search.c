#include "search.h"

// Search statistics; set by last call to search()
searchstats sstats;

// Local functions
int mtd_f(board *b, int ply);
int abq_multithread(board *b, int alpha, int beta, int ply, int centiply_extension, bool allow_extensions, bool side_to_move_in_check);
void *abq_multithread_entrypoint(void *param);
int abq(board *b, int alpha, int beta, int ply, int centiply_extension, bool allow_extensions, bool side_to_move_in_check);
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

// Compute the amount of time to spend on the next move
// Some parameters might be -1 if they do not apply
// TODO - this algorithm is naive
int time_use(board *b, int time_left, int increment, int movestogo) {
	if (time_left <= 0) return 100; // Oops!
	// Assume the game is 70 moves long, but never use more than 1/10th of the remaining time
	int moves_left_guess = (movestogo == -1) ? max(10, 70 - b->last_move_ply) : movestogo + 1;
	return time_left / moves_left_guess;
}

void search(board *b, int ply) {
	clear_stats(); // Stats for search
	sstats.depth = ply;
	// Start timer for the search
	struct timeval t1, t2;
   	gettimeofday(&t1, NULL);
   	int result;
	if (use_mtd_f) result = mtd_f(b, ply);
	else {
		coord king_loc = b->black_to_move ? b->black_king : b->white_king;
		bool side_to_move_in_check = in_check(b, king_loc.col, king_loc.row, b->black_to_move);
		result = abq_multithread(b, NEG_INFINITY, POS_INFINITY, ply, 0, true, side_to_move_in_check);
	}
	gettimeofday(&t2, NULL);
	// Compute and print the elapsed time in millisec
	double search_millisec = (t2.tv_sec - t1.tv_sec) * 1000.0; // sec to ms
	search_millisec += (t2.tv_usec - t1.tv_usec) / 1000.0; // us to ms
	sstats.time = search_millisec;
}

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
	coord king_loc = board->black_to_move ? board->black_king : board->white_king;
	bool side_to_move_in_check = in_check(board, king_loc.col, king_loc.row, board->black_to_move);
	while (lower_bound < upper_bound) {
		if (search_terminate_requested) return 0;
		int beta;
		if (g == lower_bound) beta = g+1;
		else beta = g;
		g = abq_multithread(board, beta-1, beta, ply, 0, true, side_to_move_in_check);
		if (g < beta) upper_bound = g;
		else lower_bound = g;
	}
	return g;
}

int abq_multithread(board *b, int alpha, int beta, int ply, int centiply_extension, bool allow_extensions, bool side_to_move_in_check) {
	pthread_t workers[num_search_threads];
	for (int i = 0; i < num_search_threads; i++) {
		search_worker_thread_args *args = malloc(sizeof(search_worker_thread_args));
		search_worker_thread_args param_list = {.b = b, .alpha = alpha, .beta = beta, .ply = ply, 
			.centiply_extension = centiply_extension, .allow_extensions = allow_extensions, 
			.side_to_move_in_check = side_to_move_in_check};
		*args = param_list;
		if (pthread_create(&workers[i], NULL, abq_multithread_entrypoint, args)) {
			stdout_fprintf(logstr, "info string error creating worker thread\n");
			free(args);
		}
	}
	// Wait for the workers to finish
	void *return_val;
	for (int i = 0; i < num_search_threads; i++) {
       pthread_join(workers[i], &return_val);
	}
	return (int)return_val;
}

void *abq_multithread_entrypoint(void *param) {
	search_worker_thread_args *args = param;
	abq(args->b, args->alpha, args->beta, args->ply, args->centiply_extension, args->allow_extensions, args->side_to_move_in_check);
	free(param);
	return NULL;
}

// Unified alpha-beta and quiescence search
int abq(board *b, int alpha, int beta, int ply, int centiply_extension, bool allow_extensions, bool side_to_move_in_check) {
	if (search_terminate_requested) return 0; // Check for search termination

	int alpha_orig = alpha; // For use in later TT storage
	bool quiescence = (ply <= 0);

	// Retrieve the value from the transposition table, if appropriate
	evaluation *stored = tt_get(b);
	if (stored != NULL && stored->depth >= ply && use_ttable) {
		if (stored->type == qexact || stored->type == exact) return stored->score;
		if (stored->type == qlowerbound || stored->type == lowerbound) alpha = max(alpha, stored->score);
		else if (stored->type == qupperbound || stored->type == upperbound) beta = min(beta, stored->score);
		if (alpha >= beta) return stored->score;
	}

	// Generate all possible moves for the quiscence search or normal search, and compute the
	// static evaluation if applicable.
	move *moves = NULL;
	int num_available_moves = 0;
	if (quiescence) moves = board_moves(b, &num_available_moves, true); // Generate only captures
	else moves = board_moves(b, &num_available_moves, false); // Generate all moves
	if (quiescence && !use_qsearch) {
		free(moves);
		return relative_evaluation(b); // If qsearch is turned off
	}

	// Abort if the quiescence search is too deep (currently 45 plies)
	if (ply < -quiesce_ply_cutoff) { 
		sstats.qnode_aborts++;
		free(moves);
		return relative_evaluation(b);
	}

	int quiescence_stand_pat;
	// Allow the quiescence search to generate cutoffs
	if (quiescence) {
		quiescence_stand_pat = relative_evaluation(b);
		alpha = max(alpha, quiescence_stand_pat);
		if (alpha >= beta) {
			free(moves);
			return quiescence_stand_pat;
		}
	} else if (stored != NULL && use_tt_move_hueristic) {
		assert(is_legal_move(b, stored->best)); // TODO
		// For non-quiescence search, use the TT entry as a hueristic
		moves[num_available_moves] = stored->best;
		num_available_moves++;
	}

	// Update search stats
	if (quiescence) sstats.qnodes_searched++;
	else sstats.nodes_searched++;

	// Search hueristic: sort exchanges using MVV-LVA
	if (quiescence && mvvlva) nlopt_qsort_r(moves, num_available_moves, sizeof(move), b, &capture_move_comparator);

	// Search extensions
	bool no_more_extensions = false;

	// Extend the search if we are in check
	//coord king_loc = b->black_to_move ? b->black_king : b->white_king;
	bool currently_in_check = side_to_move_in_check; //in_check(b, king_loc.col, king_loc.row, b->black_to_move);
	if (check_extend && currently_in_check && ply <= check_extend_threshold && !quiescence && allow_extensions) { // only extend in shallow non-quiescence situations 
		centiply_extension += check_extension_centiply;
		no_more_extensions = true;
	}

	// Process any extensions
	if (allow_extensions && centiply_extension >= 100) {
		centiply_extension -= 100;
		ply += 1;
	} else if (allow_extensions && centiply_extension <= -100) {
		centiply_extension += 100;
		ply -= 1;
	}

	if (no_more_extensions) allow_extensions = false; // Only allow one check extension

	move best_move_yet = no_move;
	int best_score_yet = NEG_INFINITY; 
	int num_moves_actually_examined = 0; // We might end up in checkmate
	for (int i = num_available_moves - 1; i >= 0; i--) { // Iterate backwards to match MVV-LVA sort order
		apply(b, moves[i]);
		bool we_moved_into_check;
		// Choose the more efficient version if possible
		// If we were already in check, we need to do the expensive search
		if (side_to_move_in_check) {
			coord king_loc = b->black_to_move ? b->white_king : b->black_king; // for side that just moved
			we_moved_into_check = in_check(b, king_loc.col, king_loc.row, !(b->black_to_move));
		} else we_moved_into_check = puts_in_check(b, moves[i], !b->black_to_move);
		// Never move into check
		if (we_moved_into_check) {
			unapply(b, moves[i]);
			continue;
		}
		bool opponent_in_check = puts_in_check(b, moves[i], b->black_to_move);
		/*coord opp_king_loc = b->black_to_move ? b->black_king : b->white_king;
		bool opponent_in_check = in_check(b, opp_king_loc.col, opp_king_loc.row, (b->black_to_move));*/
		int score = -abq(b, -beta, -alpha, ply - 1, centiply_extension, allow_extensions, opponent_in_check);
		num_moves_actually_examined++;
		unapply(b, moves[i]);
		if (score > best_score_yet) {
			best_score_yet = score;
			best_move_yet = moves[i];
		}
		alpha = max(alpha, best_score_yet);
		if (alpha >= beta) break;
	}
	free(moves); // We are done with the array

	// We have no available moves (or captures) that don't leave us in check
	// This means checkmate or stalemate in normal search
	// It might mean no captures are available in quiescence search
	if (num_moves_actually_examined == 0) {
		if (quiescence) return quiescence_stand_pat; // TODO: qsearch doesn't understand stalemate or checkmate
		// This seems paradoxical, but the +1 is necessary so we pick some move in case of checkmate
		if (currently_in_check) return NEG_INFINITY + 1; // checkmate
		else return 0; // stalemate
	}

	if (quiescence && best_score_yet < quiescence_stand_pat) return quiescence_stand_pat; // TODO experimental stand pat

	if (search_terminate_requested) return 0; // Search termination preempts tt_put

	// Record the selected move in the transposition table
	evaltype type;
	if (best_score_yet <= alpha_orig) type = (quiescence) ? qupperbound : upperbound;
	else if (best_score_yet >= beta) type = (quiescence) ? qlowerbound : lowerbound;
	else type = (quiescence) ? qexact : exact;
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

