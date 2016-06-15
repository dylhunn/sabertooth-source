#include "evaluate.h"

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
				case 'K': piece_val = 30000; break;
				default: assert(false);
			}
			if (!b->b[i][j].white) piece_val = -piece_val;
			eval += piece_val;
		}
	}
	return eval;
}
