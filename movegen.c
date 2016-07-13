#include "movegen.h"

int piece_moves(board *b, coord c, move *list, bool captures_only);
int diagonal_moves(board *b, coord c, move *list, int steps, bool captures_only);
int horizontal_moves(board *b, coord c, move *list, int steps, bool captures_only);
int pawn_moves(board *b, coord c, move *list, bool captures_only);
int castle_moves(board *b, coord c, move *list);
int slide_moves(board *b, coord orig_c, move *list, int dx, int dy, int steps, bool captures_only);
piece puts_in_check_radiate_helper(board *b, coord square, coord king_loc);

// Adds a move at the front of the list if it's pseudolegal
static inline bool add_move(move m, move *list, bool captures_only) {
	if (captures_only && p_eq(m.captured, no_piece)) return false;
	else list[0] = m;
	return true;
}

static const char promo_p[] = {'Q', 'N', 'B', 'R'}; // promotion targets

// Generates pseudo-legal moves for a player.
// (Does not exclude moves that put the king in check.)
// Generates an array of valid moves, and populates the count.
// TODO: Optimize to use hashsets of piece locations.
move *board_moves(board *b, int *count, bool captures_only) {
	bool white = !b->black_to_move;
	move *moves = malloc(sizeof(move) * max_moves_in_list);
	*count = 0;
	for (uint8_t i = 0; i < 8; i++) { // col
		for (uint8_t j = 0; j < 8; j++) { // row
			if (p_eq(b->b[j][i], no_piece) || b->b[j][i].white != white) continue;
			(*count) += piece_moves(b, (coord){j, i}, moves + (*count), captures_only);
		}

	}
	// TODO does 
	//moves = realloc(moves, sizeof(move) * ((*count) + 1)); // extra slot for working space
	return moves;
}

bool is_legal_move(board *b, move m) {
	move moves[50]; // Up to 50 moves supported
	int count = piece_moves(b, m.from, moves, false);
	bool result = move_arr_contains(moves, m, count);
	return result;
}

// Writes all legal moves for a piece to an array starting at index 0; 
// returns the number of items added.
int piece_moves(board *b, coord c, move *list, bool captures_only) {
	int added = 0;
	if (p_eq(at(b, c), no_piece)) return 0;
	switch(at(b, c).type) {
		case 'P':
			added += pawn_moves(b, c, list, captures_only);
		break;
		case 'N':
			for (int i = -2; i <= 2; i += 4) {
				for (int j = -1; j <= 1; j += 2) {
					added += slide_moves(b, c, list + added, i, j, 1, captures_only);
					added += slide_moves(b, c, list + added, j, i, 1, captures_only);
				}
			}
		break;
		case 'B':
			added += diagonal_moves(b, c, list, 8, captures_only);
		break;
		case 'R':
			added += horizontal_moves(b, c, list, 8, captures_only);
		break;
		case 'Q':
			added += diagonal_moves(b, c, list, 8, captures_only);
			added += horizontal_moves(b, c, list + added, 8, captures_only);
		break;
		case 'K':
			added += diagonal_moves(b, c, list, 1, captures_only);
			added += horizontal_moves(b, c, list + added, 1, captures_only);
			if (!captures_only) added += castle_moves(b, c, list + added);
		break;
		default: assert(false);
	}
	return added;
}

int diagonal_moves(board *b, coord c, move *list, int steps, bool captures_only) {
	int added = 0;
	added += slide_moves(b, c, list, 1, 1, steps, captures_only);
	added += slide_moves(b, c, list + added, 1, -1, steps, captures_only);
	added += slide_moves(b, c, list + added, -1, 1, steps, captures_only);
	added += slide_moves(b, c, list + added, -1, -1, steps, captures_only);
	return added;
}

int horizontal_moves(board *b, coord c, move *list, int steps, bool captures_only) {
	int added = 0;
	added += slide_moves(b, c, list, 1, 0, steps, captures_only);
	added += slide_moves(b, c, list + added, -1, 0, steps, captures_only);
	added += slide_moves(b, c, list + added, 0, 1, steps, captures_only);
	added += slide_moves(b, c, list + added, 0, -1, steps, captures_only);
	return added;
}

// Precondition: the piece at c is a pawn.
int pawn_moves(board *b, coord c, move *list, bool captures_only) {
	assert(at(b, c).type == 'P');
	int added = 0;
	piece curr_p = at(b, c);
	bool unmoved = (c.row == 1 && curr_p.white) || (c.row == 6 && !curr_p.white);
	int8_t dy = curr_p.white ? 1 : -1;
	bool promote = (c.row + dy == 0 || c.row + dy == 7); // next move is promotion
	if (p_eq(at(b, (coord){c.col, c.row + dy}), no_piece) && !captures_only) { // front is clear, and we may make non-capturing moves
		if (promote) {
			for (int i = 0; i < 4; i++)
				if (add_move((move){c, (coord){c.col, c.row + dy}, no_piece, 
								(piece){promo_p[i], curr_p.white}, N}, list + added, captures_only)) added++;
		} else {
			if (add_move((move){c, {c.col, c.row + dy}, no_piece, no_piece, N}, list + added, captures_only)) added++;
			if (unmoved && p_eq(at(b, (coord){c.col, c.row + dy + dy}), no_piece)) // double move
				if (add_move((move){c, (coord){c.col, c.row + dy + dy}, 
							no_piece, no_piece, N}, list + added, captures_only)) added++;
		}
	}

	for (int8_t dx = -1; dx <= 1; dx += 2) { // both capture directions
		coord cap = {c.col + dx, c.row + dy};
		if (!in_bounds(cap)) continue;
		piece cap_p = at(b, cap);
		if (p_eq(cap_p, no_piece) || cap_p.white == curr_p.white) continue;
		if (promote) {
			for (int i = 0; i < 4; i++) { // promotion types
				if (add_move((move){c, (coord){c.col + dx, c.row + dy}, cap_p, 
								(piece){promo_p[i], curr_p.white}, N}, list + added, captures_only)) added++;
			}
		} else if (add_move((move){c, (coord){c.col + dx, c.row + dy}, at(b, cap), no_piece, N}, list + added, captures_only)) added++;
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
	for (int i = 5; i <= 6; i++) { // rely on the search to avoid moving into check
		if (!p_eq(b->b[i][row], no_piece) || in_check(b, i, row, b->black_to_move)) {
			k_r_path_clear = false;
			//break;
		}
	}
	for (int i = 3; i >= 2; i--) {
		if (!p_eq(b->b[i][row], no_piece) || in_check(b, i, row, b->black_to_move)) {
			q_r_path_clear = false;
			break;
		}
	}
	if (k_r_path_clear) {
		if (isWhite && b->castle_rights_wk) {
			if (add_move((move){c, (coord){6, row}, no_piece, no_piece, K}, list + added, false)) added++;
		} else if (!isWhite && b->castle_rights_bk) {
			if (add_move((move){c, (coord){6, row}, no_piece, no_piece, K}, list + added, false)) added++;
		}
	}
	if (q_r_path_clear) {
		if (isWhite && b->castle_rights_wq) {
			if (add_move((move){c, (coord){2, row}, no_piece, no_piece, Q}, list + added, false)) added++;
		} else if (!isWhite && b->castle_rights_bq) {
			if (add_move((move){c, (coord){2, row}, no_piece, no_piece, Q}, list + added, false)) added++;
		}
	}
	return added;
}

// Writes all legal moves to an array by sliding from a given origin with some dx and dy for n steps.
// Stops on capture, board bounds, or blocking.
int slide_moves(board *b, coord orig_c, move *list, int dx, int dy, int steps, bool captures_only) {
	int added = 0;
	coord curr_c = {orig_c.col, orig_c.row};
	for (uint8_t s = 1; s <= steps; s++) {
		curr_c.col += dx;
		curr_c.row += dy;
		if (!in_bounds(curr_c)) break; // left board bounds
		piece curr_p = at(b, curr_c);
		bool is_blocked = !p_eq(curr_p, no_piece);
		if (is_blocked && (curr_p.white == at(b, orig_c).white)) break; // blocked
		if (add_move((move){orig_c, curr_c, curr_p, no_piece, N}, list + added, captures_only)) added++; // freely move
		if (is_blocked) break; // captured
	}
	return added;
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
	if (!in_bounds(m->from)) return false;
	if (!in_bounds(m->to)) return false;
	m->captured = at(b, m->to);
	switch(str[4]) {
		case '\0':
			m->promote_to = no_piece;
			break;
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
			return false;
	}

	if (at(b, m->from).type == 'K') {
		if (m->to.col == m->from.col + 2) m->c = K;
		else if (m->to.col == m->from.col - 2) m->c = Q;
		else m->c = N;
	} else {
		m->c = N;
	}

	bool found = false;
	int nMoves;
	move *moves;
	moves = board_moves(b, &nMoves, false);
	for (int i = 0; i < nMoves; i++) {
		if (m_eq(moves[i], *m)) {
			found = true;
			break;
		}
	}
	free(moves);

	return found;
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
	// todo en passant check
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

// checks if a given move, ALREADY applied, has put the specified color's king in check
// this is slightly more efficient than in_check
// precondition: the specified King was NOT already in check
// TODO This is buggy and needs to be fixed before using
bool puts_in_check(board *b, move m, bool white_king) {
	coord king_loc = white_king ? b->white_king : b->black_king;

	// We can't handle king moves any more efficiently
	if (at(b, m.to).type == 'K') return in_check(b, king_loc.col, king_loc.row, !white_king);

	piece moved_p = at(b, m.to);
	bool piece_is_white = moved_p.white;

	// Step 1: Discovered attacks as a result of the move

	// Did the move create any discovered checks?
	piece assailant = puts_in_check_radiate_helper(b, m.from, king_loc);

	if (p_eq(assailant, no_piece)) goto step2; // No piece along line
	if (assailant.white == white_king) goto step2; // Friendly piece along line

	switch(assailant.type) {
		case 'B':
		case 'R':
		case 'Q':
			return true;
		default: ; // nothing
	}

	// Step 2: Direct attacks as a result of the move
	step2:

	// did the arrival square put the king in check?
	if (moved_p.white == white_king) return false; // a teammate cannot check the king

	// direction of radiation for direct check
	assailant = puts_in_check_radiate_helper(b, m.to, king_loc);

	if (p_eq(assailant, no_piece)) goto step3; // No piece along line
	if (assailant.white == white_king) goto step3; // Friendly piece along line

	switch(assailant.type) {
		case 'B':
		case 'R':
		case 'Q':
			return true;
		default: ; // nothing
	}

	// Step 3: Direct knight attacks (discovery is impossible)
	step3:
	if (moved_p.type != 'N') goto step4;
	int xdist = abs(m.to.col - king_loc.col);
	int ydist = abs(m.to.row - king_loc.row);
	if (((xdist == 1) && (ydist == 2)) || ((xdist == 2) && (ydist == 1))) return true;

	// Step 4: Direct pawn attacks (discovery is impossible)
	step4:
	if (moved_p.type != 'P') goto done;
	if (abs(m.to.col - king_loc.col) != 1) goto done; // pawns must attack diagonally
	int dy_from_king;
	dy_from_king = (piece_is_white) ? -1 : 1; 
	if (king_loc.row + dy_from_king == m.to.row) return true; 

	done:
	return false;
}

// See if a given coordinate falls along a straight line with the king
// If so, see what piece could deliver check from that direction
piece puts_in_check_radiate_helper(board *b, coord square, coord king_loc) {
	// direction of radiation for check
	int dx = 0;
	int dy = 0;
	// did the coordinate expose the king to discovered or direct attack in its direction?
	if (square.col == king_loc.col) {
		dy = (square.row > king_loc.row) ? 1 : -1;
	} else if (square.row == king_loc.row) {
		dx = (square.col > king_loc.col) ? 1 : -1;
	} else if (abs(king_loc.col - square.col) == abs(king_loc.row - square.row)) {
		dy = (square.row > king_loc.row) ? 1 : -1;
		dx = (square.col > king_loc.col) ? 1 : -1;
	}

	if (dx == 0 && dy == 0) return no_piece;

	piece found = no_piece;
	int ii = king_loc.col + dx;
	for (int j = king_loc.row + dy; in_bounds((coord){ii, j}); j += dy) {
		found = at(b, (coord){ii, j});
		if (!p_eq(found, no_piece)) break; // find the first piece in the way
		ii += dx;
	}
	return found;
}
