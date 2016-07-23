#include "util.h"

const piece no_piece = NO_PIECE;
const move no_move = NO_MOVE;
const evaluation no_eval = {NO_MOVE, 0, 0 , 0, 0};

const char *engine_name = ENGINE_NAME;
const char *engine_version = ENGINE_VERSION;
const char *author_name = AUTHOR_NAME;
FILE *logstr;

uint64_t rand64(void) {
    uint64_t r = 0;
    for (int i = 0; i < 5; ++i) {
        r = (r << 15) | (rand() & 0x7FFF);
    }
    return r & 0xFFFFFFFFFFFFFFFFULL;
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
	b->true_game_ply_clock = 0;
	b->white_king = (coord){4, 0};
	b->black_king = (coord){4, 7};
	b->en_passant_pawn_push_col_history = malloc(sizeof(int8_t) * 400); // TODO - leak
	b->en_passant_pawn_push_col_history[0] = -1;
}

bool move_arr_contains(move *moves, move move, int arrlen) {
	for (int i = 0; i < arrlen; i++) {
		if (m_eq(moves[i], move)) {
			return true;
		}
	}
	return false;
}

int min(int a, int b) {
	return a < b ? a : b;
}

int max(int a, int b) {
	return a > b ? a : b;
}

void stdout_fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    va_start(ap, fmt);
    if (use_log_file) vfprintf(f, fmt, ap);
    va_end(ap);
    fflush(stdout);
    if (use_log_file) fflush(f);
}

// qsort_r implementation from nlopt on Github
// Because glibc and BSD use incompatible implementations!
static void swap(void *a_, void *b_, size_t size) {
     if (a_ == b_) return;
     {
          size_t i, nlong = size / sizeof(long);
          long *a = (long *) a_, *b = (long *) b_;
          for (i = 0; i < nlong; ++i) {
               long c = a[i];
               a[i] = b[i];
               b[i] = c;
          }
	  a_ = (void*) (a + nlong);
	  b_ = (void*) (b + nlong);
     }
     {
          size_t i;
          char *a = (char *) a_, *b = (char *) b_;
          size = size % sizeof(long);
          for (i = 0; i < size; ++i) {
               char c = a[i];
               a[i] = b[i];
               b[i] = c;
          }
     }
}

void nlopt_qsort_r(void *base_, size_t nmemb, size_t size, void *thunk,
		   int (*compar)(void *, const void *, const void *)) {
     char *base = (char *) base_;
     if (nmemb < 10) { /* use O(nmemb^2) algorithm for small enough nmemb */
	  size_t i, j;
	  for (i = 0; i+1 < nmemb; ++i)
	       for (j = i+1; j < nmemb; ++j)
		    if (compar(thunk, base+i*size, base+j*size) > 0)
			 swap(base+i*size, base+j*size, size);
     }
     else {
	  size_t i, pivot, npart;
	  /* pick median of first/middle/last elements as pivot */
	  {
	       const char *a = base, *b = base + (nmemb/2)*size, 
		    *c = base + (nmemb-1)*size;
	       pivot = compar(thunk,a,b) < 0
		    ? (compar(thunk,b,c) < 0 ? nmemb/2 :
		       (compar(thunk,a,c) < 0 ? nmemb-1 : 0))
		    : (compar(thunk,a,c) < 0 ? 0 :
		       (compar(thunk,b,c) < 0 ? nmemb-1 : nmemb/2));
	  }
	  /* partition array */
	  swap(base + pivot*size, base + (nmemb-1) * size, size);
	  pivot = (nmemb - 1) * size;
	  for (i = npart = 0; i < nmemb-1; ++i)
	       if (compar(thunk, base+i*size, base+pivot) <= 0)
		    swap(base+i*size, base+(npart++)*size, size);
	  swap(base+npart*size, base+pivot, size);
	  /* recursive sort of two partitions */
	  nlopt_qsort_r(base, npart, size, thunk, compar);
	  npart++; /* don't need to sort pivot */
	  nlopt_qsort_r(base+npart*size, nmemb-npart, size, thunk, compar);
     }
}
