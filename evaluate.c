#include "evaluate.h"

// transform coordinates to access piece tables
static inline int ptw(int c, int r) {
	return c+56-(r*8);
}

static inline int ptb(int c, int r) {
	return c+(r*8);
}

int piece_val(piece p);
int square_by_square(board *b);
int piece_square_val(piece p, int c, int r);

// Piece-Square tables, from white's perspective

static int ptable_pawn[64] = {  
   0,  0,  0,  0,  0,  0,  0,  0,
  20, 21, 22, 25, 25, 22, 21, 20,
  10, 10, 20, 20, 20, 20, 10, 10,
   5,  7, 16, 17, 17, 16,  7,  5,
   3,  0, 14, 15, 15, 14,  0,  3,
   0,  4,  3, 10, 10,  3,  4,  0,
   5,  5,  5,  5,  5,  5,  5,  5,
   0,  0,  0,  0,  0,  0,  0,  0
};

static int ptable_knight[64] = {  
   -20,  0,  0,  0,  0,  0,  0,-20,
   -15,  5,  6,  7,  7,  6,  5,-15,
   -10,  7, 16, 20, 20, 16,  7,-10,
    -5,  7, 15, 30, 30, 15,  7, -5,
    -5,  7, 15, 25, 25, 15,  7, -5,
   -10,  5, 10, 15, 15, 10,  5,-10,
   -15,  3,  5,  7,  7,  5,  3,-15,
   -20,  0,  0,  0,  0,  0,  0,-20
};

static int ptable_bishop[64] = {  
   0,  0,  0,  0,  0,  0,  0,  0,
   0, 10, 10, 20, 20, 10, 10,  0,
   0, 10, 30, 30, 30, 30, 10,  0,
   0, 10, 20, 30, 30, 20, 10,  0,
   0, 10, 30, 30, 30, 30, 10,  0,
   0, 15, 30, 10, 10, 30, 15,  0,
   0, 22,  0,  5,  5,  0, 22,  0,
   0,  0,  0,  0,  0,  0,  0,  0
};

static int ptable_rook[64] = {  
   0,  0,  0,  0,  0,  0,  0,  0,
   0, 10, 10, 10, 10, 10, 10,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  5,  0,  0,  5,  0,  0
};

static int ptable_queen[64] = {  
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0, 10, 10,  0,  0,  0,
   0,  0,  0, 10, 10,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0, 10,  0,  0,  0,  0
};

static int ptable_king[64] = {  
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0, 10, 10,  0,  0,  0,
   0,  0,  0, 10, 10,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0, 15,  0, 10,  0, 15,  0
};

// Statically evaluates a board position.
// Positive numbers indicate white advantage.
// Returns result in centipawns.
int evaluate(board *b) {
	int score = 0;
	score += square_by_square(b);
	return score;
}

int square_by_square(board *b) {
   uint8_t black_pawns_by_col[8] = {0};
   uint8_t white_pawns_by_col[8] = {0};
   uint8_t black_bishops = 0;
   uint8_t white_bishops = 0;

	int eval = 0;
	for (int c = 0; c < 8; c++) { // cols
		for (int r = 0; r < 8; r++) { // rows
			piece p = b->b[c][r];
         if (p.type == 'P') {
            if (p.white) white_pawns_by_col[c]++;
            else black_pawns_by_col[c]++;
         } else if (p.type == 'B') {
            if (p.white) white_bishops++;
            else black_bishops++;
         }
			if (p_eq(no_piece, p)) continue;
			eval += piece_val(p);
			eval += piece_square_val(p, c, r);
		}
	}
   for (int i = 0; i < 8; i++) {
      if (black_pawns_by_col[i] > 1) eval += doubled_pawn_penalty;
      if (white_pawns_by_col[i] > 1) eval -= doubled_pawn_penalty;
   }
   if (black_bishops == 2) eval -= bishop_pair_bonus;
   if (white_bishops == 2) eval += bishop_pair_bonus;
	return eval;
}

int piece_val(piece p) {
	int piece_val;
	switch (p.type) {
		case 'P': piece_val = 100; break;
		case 'N': piece_val = 320; break;
		case 'B': piece_val = 325; break;
		case 'R': piece_val = 500; break;
		case 'Q': piece_val = 900; break;
		case 'K': piece_val = 30000; break;
		default: assert(false);
	}
	if (!p.white) piece_val = -piece_val;
	return piece_val;
}

int piece_square_val(piece p, int c, int r) {
	int tableidx = (p.white) ? ptw(c, r) : ptb(c, r);
	int table_val;
	switch (p.type) {
		case 'P': table_val = ptable_pawn[tableidx]; break;
		case 'N': table_val = ptable_knight[tableidx]; break;
		case 'B': table_val = ptable_bishop[tableidx]; break;
		case 'R': table_val = ptable_rook[tableidx]; break;
		case 'Q': table_val = ptable_queen[tableidx]; break;
		case 'K': table_val = ptable_king[tableidx]; break;
		default: assert(false);
	}
	if (!p.white) table_val = -table_val;
	return table_val;
}
