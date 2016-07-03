#include "movegen.h"

int piece_moves(board *b, coord c, move *list);
int diagonal_moves(board *b, coord c, move *list, int steps);
int horizontal_moves(board *b, coord c, move *list, int steps);
int pawn_moves(board *b, coord c, move *list);
int castle_moves(board *b, coord c, move *list);
int slide_moves(board *b, coord orig_c, move *list, int dx, int dy, int steps);
bool add_move(board *b, move m, move *list);

static const char promo_p[] = {'Q', 'N', 'B', 'R'}; // promotion targets

// Generates pseudo-legal moves for a player.
// (Does not exclude moves that put the king in check.)
// Generates an array of valid moves, and populates the count.
// TODO: Optimize to use hashsets of piece locations.
move *board_moves(board *b, int *count) {
	bool white = !b->black_to_move;
	move *moves = malloc(sizeof(move) * 200); // Up to 200 moves supported
	*count = 0;
	for (uint8_t i = 0; i < 8; i++) { // col
		for (uint8_t j = 0; j < 8; j++) { // row
			if (p_eq(b->b[j][i], no_piece) || b->b[j][i].white != white) continue;
			(*count) += piece_moves(b, (coord){j, i}, moves + (*count));
		}

	}
	moves = realloc(moves, sizeof(move) * ((*count) + 1)); // extra slot for working space
	return moves;
}

bool is_legal_move(board *b, move m) {
	move moves[50]; // Up to 50 moves supported
	int count = piece_moves(b, m.from, moves);
	bool result = move_arr_contains(moves, m, count);
	return result;
}

// Writes all legal moves for a piece to an array starting at index 0; 
// returns the number of items added.
int piece_moves(board *b, coord c, move *list) {
	int added = 0;
	if (p_eq(at(b, c), no_piece)) return 0;
	switch(at(b, c).type) {
		case 'P':
			added += pawn_moves(b, c, list);
		break;
		case 'N':
			for (int i = -2; i <= 2; i += 4) {
				for (int j = -1; j <= 1; j += 2) {
					added += slide_moves(b, c, list + added, i, j, 1);
					added += slide_moves(b, c, list + added, j, i, 1);
				}
			}
		break;
		case 'B':
			added += diagonal_moves(b, c, list, 8);
		break;
		case 'R':
			added += horizontal_moves(b, c, list, 8);
		break;
		case 'Q':
			added += diagonal_moves(b, c, list, 8);
			added += horizontal_moves(b, c, list + added, 8);
		break;
		case 'K':
			added += diagonal_moves(b, c, list, 1);
			added += horizontal_moves(b, c, list + added, 1);
			added += castle_moves(b, c, list + added);
		break;
		default: assert(false);
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
		if (!in_bounds(cap) || p_eq(at(b, cap), no_piece) || at(b, cap).white == cp.white) continue;
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
	if (in_check(b, c.col, c.row, b->black_to_move)) return 0;
	bool k_r_path_clear = true;
	bool q_r_path_clear = true;
	for (int i = 5; i <= 6; i++) {
		if (!p_eq(b->b[i][row], no_piece) || in_check(b, i, row, b->black_to_move)) {
			k_r_path_clear = false;
			break;
		}
	}
	for (int i = 3; i >= 1; i--) {
		if (!p_eq(b->b[i][row], no_piece) || in_check(b, i, row, b->black_to_move)) {
			q_r_path_clear = false;
			break;
		}
	}
	if (k_r_path_clear) {
		if (isWhite && b->castle_rights_wk) {
			if (add_move(b, (move){c, (coord){6, row}, no_piece, no_piece, K}, list + added)) added++;
		} else if (!isWhite && b->castle_rights_bk) {
			if (add_move(b, (move){c, (coord){6, row}, no_piece, no_piece, K}, list + added)) added++;
		}
	}
	if (q_r_path_clear) {
		if (isWhite && b->castle_rights_wq) {
			if (add_move(b, (move){c, (coord){1, row}, no_piece, no_piece, Q}, list + added)) added++;
		} else if (!isWhite && b->castle_rights_bq) {
			if (add_move(b, (move){c, (coord){1, row}, no_piece, no_piece, Q}, list + added)) added++;
		}
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

// Adds a move at the front of the list if it's pseudolegal
bool add_move(board *b, move m, move *list) {
	list[0] = m; return true;
	/*apply(b, m);
	coord king_loc = b->black_to_move ? b->white_king : b->black_king; // for side that just moved
	bool viable = !in_check(b, king_loc.col, king_loc.row, !(b->black_to_move));
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

	if (at(b, m->from).type == 'K') {
		if (m->to.col == m->from.col + 2) m->c = K;
		else if (m->to.col == m->from.col - 3) m->c = Q;
	}
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
	fail: return false;
}

// checks if a given coordinate would be in check on the current board
// not efficient when looping over the board
bool in_check(board *b, int col, int row, bool by_white) {
	// sliders and king
	for (int dc = -1; dc <= 1; dc++) {
		for (int dr = -1; dr <= 1; dr++) {
			if (dc == 0 && dr == 0) continue;
			piece found = no_piece;
			int num_steps = 0;
			int ii = col + dc;
			for (int j = row + dr; in_bounds((coord){ii, j}); j += dr) {
				num_steps++;
				found = at(b, (coord){ii, j});
				if (!p_eq(found, no_piece)) break; // find the first piece in the way
				ii += dc;
			}
			if (p_eq(found, no_piece)) continue;
			if (found.white != by_white) continue;
			switch(found.type) {
				case 'P':
					continue;
				case 'N':
					continue;
				case 'B':
					if (dc != 0 && dr != 0) return true;
					continue;
				case 'R':
					if (dc == 0 || dr == 0) return true;
					continue;
				case 'Q':
					return true;
				case 'K':
					if (num_steps == 1) return true;
					continue;
				default: assert(false);
			}
		}
	}

	// knights
	for (int d1 = -1; d1 <= 1; d1 += 2) {
		for (int d2 = -2; d2 <= 2; d2 += 4) {
			int dc = d1;
			int dr = d2;
			for (int i = 0; i < 2; i++) {
				int temp = dc;
				dc = dr;
				dr = temp;
				coord target = (coord){dc + col, dr + row};
				if (!(in_bounds(target))) continue;
				piece found =  at(b, target);
				if (p_eq(found, no_piece)) continue;
				if (found.white != by_white) continue;
				if (found.type != 'N') continue;
				return true;
			}
			
		}
	}

	// pawns
	int dr = by_white ? -1 : 1;
	for (int dc = -1; dc <= 1; dc += 2) {
		coord target = (coord){dc + col, dr + row};
		if (!(in_bounds(target))) continue;
		piece found = at(b, target);
		if (p_eq(found, no_piece)) continue;
		if (found.white != by_white) continue;
		if (found.type != 'P') continue;
		return true;
	}

	return false;
}
