#include "search.h"

// set by last call to search()
searchstats sstats;

int mtd_f(board *b, int ply);
int quiesce(board *b, int alpha, int beta, int ply);
int capture_move_comparator(const board *board, const move *a, const move *b);

void clear_stats() {
	sstats.time = 0;
	sstats.depth = 0;
	sstats.nodes_searched = 0;
	sstats.qnodes_searched = 0;
	sstats.qnode_aborts = 0;
}

int search(board *b, int ply) {
	clear_stats();
	sstats.depth = ply;
	struct timeval t1, t2;
    // start timer
    gettimeofday(&t1, NULL);
	int result;
	//if (b->black_to_move) result = ab_min(b, NEG_INFINITY, POS_INFINITY, ply);
	//else result = ab_max(b, NEG_INFINITY, POS_INFINITY, ply);
	result = mtd_f(b, ply);

	gettimeofday(&t2, NULL);
    // compute and print the elapsed time in millisec
    double search_millisec = (t2.tv_sec - t1.tv_sec) * 1000.0; // sec to ms
    search_millisec += (t2.tv_usec - t1.tv_usec) / 1000.0; // us to ms
    sstats.time = search_millisec;
    return result;
}

int mtd_f(board *board, int ply) {
	int f; // first guess of evaluation
	evaluation *stored = tt_get(board); // Use last pass in TT
	if (stored != NULL) f = stored->score; // If not present, use static eval guess
	else f = evaluate(board);
	int g = f;
	int upper_bound = POS_INFINITY;
	int lower_bound = NEG_INFINITY;
	while (lower_bound < upper_bound) {
		int b = g > lower_bound+1 ? g : lower_bound+1;
		if (board->black_to_move) g = -ab(board, -b, -(b-1), ply);
		else g = ab(board, b-1, b, ply);
		if (g < b) upper_bound = g;
		else lower_bound = g;
	}
	return g;
}

int ab(board *b, int alpha, int beta, int ply) {
	evaluation *stored = tt_get(b);
	if (stored != NULL) assert(!m_eq(stored->best, no_move));
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

	if (ply == 0) return quiesce(b, alpha, beta, ply);

	sstats.nodes_searched++;

	int num_children = 0;
	move chosen_move = no_move;
	move *moves = board_moves(b, &num_children);
	assert(num_children > 0);

	// start with the move from the transposition table
	if (stored != NULL) {
		assert(!m_eq(stored->best, no_move));
		// check that the move is valid, in case of a hash collision
		// TODO is this necessary?
		if (is_legal_move(b, stored->best)) {
			moves[num_children] = stored->best;
			num_children++;
		} else {
			char buffer[6];
			printf("Erroneous move in TT: %s\n", move_to_string(stored->best, buffer));
		}
	}

	int localbest = NEG_INFINITY;
	for (int i = num_children - 1; i >= 0; i--) {
		uint64_t old_hash = b->hash; // for debugging
		apply(b, moves[i]);
		//int score = ab_min(b, alpha, beta, ply - 1);
		int score = -ab(b, -beta, -alpha, ply - 1);
		unapply(b, moves[i]);
		if (score >= beta) {
			assert (old_hash == b->hash);
			tt_put(b, (evaluation){moves[i], score, at_least, ply, 0});
			free(moves);
			return beta; // fail-hard
		}
		if (score >= localbest) {
			localbest = score;
			chosen_move = moves[i];
			if (score > alpha) alpha = score;
		}
		assert (old_hash == b->hash);
	}
	tt_put(b, (evaluation){chosen_move, localbest, exact, ply, 0});
	free(moves);
	return alpha;
}

int quiesce(board *b, int alpha, int beta, int ply) {
	evaluation *stored = tt_get(b);
	// Any non-quiescence node would have continued through the quiescence search.
	// Any shallower quiescence node will have a later abort point. (TODO: can we remove the depth check?)
	if (stored != NULL) assert(!m_eq(stored->best, no_move));
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

	sstats.qnodes_searched++;

	int stand_pat = evaluate(b);
	if (b->black_to_move) stand_pat = -stand_pat; // always from perspective of player
	if (stand_pat >= beta) return beta;
	if (alpha < stand_pat) alpha = stand_pat;
	if (ply < -quiesce_ply_cutoff) {
		sstats.qnode_aborts++;
		return stand_pat;
	}

	int num_children = 0;
	move *moves = board_moves(b, &num_children);

	// Sort exchanges using MVV-LVA
	qsort_r(moves, num_children, sizeof(move), b, &capture_move_comparator);
	move localbestmove = no_move;
	int localbest = POS_INFINITY;
	for (int i = 0; i < num_children; i++) {
		if (p_eq(moves[i].captured, no_piece)) continue;
		apply(b, moves[i]);
		int child_score = -quiesce(b, -beta, -alpha, ply-1);
		unapply(b, moves[i]);
		if (child_score >= beta) {
			tt_put(b, (evaluation){moves[i], child_score, at_least, ply, 0});
			free(moves);
			return beta;
		}
		if (child_score > localbest) {
			localbest = child_score;
			localbestmove = moves[i];
			if (child_score > alpha) alpha = child_score;
		}
	}
	if (!m_eq(no_move, localbestmove)) { // This should only fail for terminal nodes (with no captures)
		tt_put(b, (evaluation){localbestmove, localbest, exact, ply, 0});
	}
	free(moves);
	return alpha;
}

// An array sorting comparator for capture moves, for use with qsort_r.
// Sorts by MVV/LVA (Most Valuable Victim/Least Valuable Attacker).
// Returns -1 if the first argument should come first, etc.
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
		case 'K': a_victim_value = 2000; break;
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
	return ((b_victim_value << 2) - b_attacker_value) - ((a_victim_value << 2) - a_attacker_value);
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

