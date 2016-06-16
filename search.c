#include "search.h"

// set by last call to search()
searchstats sstats;

int mtd_f(board *b, int ply);
int quiesce(board *b, int alpha, int beta, int ply);

int clear_stats() {
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
	if (b->black_to_move) result = ab_min(b, NEG_INFINITY, POS_INFINITY, ply);
	else result = ab_max(b, NEG_INFINITY, POS_INFINITY, ply);
	//mtd_f(b, ply);

	gettimeofday(&t2, NULL);
    // compute and print the elapsed time in millisec
    double search_millisec = (t2.tv_sec - t1.tv_sec) * 1000.0; // sec to ms
    search_millisec += (t2.tv_usec - t1.tv_usec) / 1000.0; // us to ms
    sstats.time = search_millisec;
    return result;

}

int mtd_f(board *board, int ply) {
	int f; // first guess of evaluation
	evaluation *stored = tt_get(board);
	if (stored != NULL) f = stored->score;
	else f = evaluate(board);
	int g = f;
	int upper_bound = POS_INFINITY;
	int lower_bound = NEG_INFINITY;
	while (lower_bound < upper_bound) {
		int b = g > lower_bound+1 ? g : lower_bound+1;
		if (board->black_to_move) g = ab_min(board, b-1, b, ply);
		else g = ab_max(board, b-1, b, ply);
		if (g < b) upper_bound = g;
		else lower_bound = g;
	}
	return g;
}

// DUPLICATE, DO NOT EDIT
void print_board_d(board *b) {
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
			printf("Board state at time of error: \n");
			print_board_d(b);
		}
	}

	int localbest = NEG_INFINITY;
	for (int i = num_children - 1; i >= 0; i--) {
		uint64_t old_hash = b->hash; // for debugging
		apply(b, moves[i]);
		int score = ab_min(b, alpha, beta, ply - 1);
		if (score >= beta) {
			unapply(b, moves[i]);
			assert (old_hash == b->hash);
			tt_put(b, (evaluation){moves[i], score, at_least, ply});
			free(moves);
			return beta; // fail-hard
		}
		if (score >= localbest) {
			localbest = score;
			chosen_move = moves[i];
			if (score > alpha) alpha = score;
		}
		unapply(b, moves[i]);
		assert (old_hash == b->hash);
	}
	tt_put(b, (evaluation){chosen_move, alpha, exact, ply});
	free(moves);
	return alpha;
}

int ab_min(board *b, int alpha, int beta, int ply) {
	evaluation *stored = tt_get(b);
	if (stored != NULL && stored->depth >= ply) {
		if (stored->type == at_least) {
			if (stored->score >= beta) return beta;
		} else if (stored->type == at_most) {
			//if (stored->score <= alpha) return alpha;
		} else { // exact
			if (stored->score <= alpha) return alpha; // respect fail-hard cutoff
			if (stored->score > beta) return beta; // alpha cutoff
			return stored->score;
		}
	}

	if (ply == 0) return -quiesce(b, -beta, -alpha, ply);

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
			printf("Board state at time of error: \n");
			print_board_d(b);
		}
	}

	int localbest = POS_INFINITY;
	for (int i = num_children - 1; i >= 0; i--) {
		uint64_t old_hash = b->hash; // for debugging
		apply(b, moves[i]);
		int score = ab_max(b, alpha, beta, ply - 1);
		if (score <= alpha) {
			unapply(b, moves[i]);
			assert (old_hash == b->hash);
			tt_put(b, (evaluation){moves[i], score, at_most, ply});
			free(moves);
			return alpha; // fail-hard
		}
		if (score <= localbest) {
			localbest = score;
			chosen_move = moves[i];
			if (score < beta) beta = score;
		}
		unapply(b, moves[i]);
		assert (old_hash == b->hash);
	}
	tt_put(b, (evaluation){chosen_move, beta, exact, ply});
	free(moves);
	return beta;
}

int quiesce(board *b, int alpha, int beta, int ply) {
	sstats.qnodes_searched++;

	int stand_pat = evaluate(b);
	if (b->black_to_move) stand_pat = -stand_pat;
	//return stand_pat;
	if (stand_pat >= beta) return beta;
	if (alpha < stand_pat) alpha = stand_pat;
	if (ply < -quiesce_ply_cutoff) {
		sstats.qnode_aborts++;
		return stand_pat;
	}

	int num_children = 0;
	move *moves = board_moves(b, &num_children);
	for (int i = 0; i < num_children; i++) {
		if (p_eq(moves[i].captured, no_piece)) continue;
		apply(b, moves[i]);
		int child_score = quiesce(b, -beta, -alpha, ply-1);
		unapply(b, moves[i]);
		if (child_score >= beta) return beta;
		if (child_score > alpha) alpha = child_score;
	}
	return alpha;
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

