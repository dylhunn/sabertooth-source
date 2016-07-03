#include "ttable.h"

void tt_auto_cleanup(void);
bool tt_expand(void);

uint64_t zobrist[64][12]; // zobrist table for pieces
uint64_t zobrist_castle_wq; // removed when castling rights are lost
uint64_t zobrist_castle_wk;
uint64_t zobrist_castle_bq;
uint64_t zobrist_castle_bk;
uint64_t zobrist_black_to_move;

static uint64_t *tt_keys = NULL;
static evaluation *tt_values = NULL;
static uint64_t tt_size;
static uint64_t tt_count = 0;
static uint64_t tt_rehash_count; // computed based on max_load

static inline int square_code(coord c) {
	return (c.col)*8+c.row;
}

static inline uint64_t tt_index(board *b) {
	uint64_t idx = b->hash % tt_size;
	while (tt_keys[idx] != 0 && tt_keys[idx] != b->hash) idx = (idx + 1) % tt_size;
	return idx;
}

// Invoke to prepare transposition table
void tt_init(void) {
	// first, compute size from memory use
	const uint64_t bytes_in_mb = 1000000;
	tt_size = (TT_MEGABYTES * bytes_in_mb) / (sizeof(evaluation) + sizeof(uint8_t));

	if (tt_keys != NULL) free(tt_keys);
	if (tt_values != NULL) free(tt_values);
	tt_keys = malloc(sizeof(uint64_t) * tt_size);
	assert(tt_keys != NULL);
	memset(tt_keys, 0, tt_size * sizeof(uint64_t));
	tt_values = malloc(sizeof(evaluation) * tt_size);
	assert(tt_values != NULL);
	tt_count = 0;
	tt_rehash_count = (uint64_t) (ceil(tt_max_load * tt_size));

	srand((unsigned int) time(NULL));
	for (int i = 0; i < 64; i++) {
		for (int j = 0; j < 12; j++) {
			zobrist[i][j] = rand64();
		}
	}
	zobrist_castle_wq = rand64();
	zobrist_castle_wk = rand64();
	zobrist_castle_bq = rand64();
	zobrist_castle_bk = rand64();
	zobrist_black_to_move = rand64();
	atexit(tt_auto_cleanup);
	is_initialized = true;
}

// Automatically called
void tt_auto_cleanup(void) {
	if (tt_keys) free(tt_keys);
	tt_keys = NULL;
	if (tt_values) free(tt_values);
	tt_values = NULL;
}

uint64_t tt_hash_position(board *b) {
	assert(is_initialized);
	uint64_t hash = 0;
	for (uint8_t i = 0; i < 8; i++) {
		for (uint8_t j = 0; j < 8; j++) {
			hash ^= tt_pieceval(b, (coord){i, j});
		}
	}
	if (b->black_to_move) hash ^= zobrist_black_to_move;
	if (b->castle_rights_wq) hash ^= zobrist_castle_wq;
	if (b->castle_rights_wk) hash ^= zobrist_castle_wk;
	if (b->castle_rights_bq) hash ^= zobrist_castle_bq;
	if (b->castle_rights_bk) hash ^= zobrist_castle_bk;
	return hash;
}

// TODO - permit storing both upper and lower bounds in an inexact node
void tt_put(board *b, evaluation e) {
	assert(is_initialized);
	if (tt_count >= tt_rehash_count) {
		if (allow_tt_expansion && !tt_expand()) {
			printf("ERROR: Failed to expand transposition table from %llu entries; clearing.\n", tt_count);
			tt_clear();
			return;
		}
		if (!allow_tt_expansion) {
			printf("Transposition table filled; clearing.\n");
			tt_clear();
			return;
		}
	}

	uint64_t idx = b->hash % tt_size;
	while (tt_keys[idx] != 0 && tt_keys[idx] != b->hash) {
		if (b->true_game_ply_clock - tt_values[idx].last_access_move >= remove_at_age) {
			tt_count--;
			tt_keys[idx] = 0;
			break;
		}
		idx = (idx + 1) % tt_size;
	}

	if (tt_keys[idx] == 0) tt_count++;
	// Never replace exact with inexact, or we could easily lose the PV.
	if (tt_values[idx].type == exact && e.type != exact) return;
	if (tt_values[idx].type == qexact && e.type != qexact) return;
	// Always replace inexact with exact;
	// otherwise, we might fail to replace a cutoff with a "shallow" ending of a PV.
	if (tt_values[idx].type != exact && e.type == exact) goto skipchecks;
	if (tt_values[idx].type != qexact && e.type == qexact) goto skipchecks;
	// Otherwise, prefer deeper entries; replace if equally deep due to aspiration windows
	if (e.depth < tt_values[idx].depth) return;
	skipchecks:
	tt_keys[idx] = b->hash;
	e.last_access_move = b->true_game_ply_clock;
	tt_values[idx] = e;
}

// Returns NULL if entry is not found.
evaluation *tt_get(board *b) {
	assert(is_initialized);
	uint64_t idx = tt_index(b);
	if (tt_keys[idx] == 0) return NULL;
	tt_values[idx].last_access_move = b->true_game_ply_clock;
	return tt_values + idx;
}

// Clear the transposition table (by resetting it).
void tt_clear() {
	assert(is_initialized);
	tt_init();
}

bool tt_expand(void) {
	assert(is_initialized);
	printf("Expanding transposition table...\n");
	uint64_t new_size = tt_size * 2;
	uint64_t *new_keys = malloc(sizeof(uint64_t) * new_size);
	evaluation *new_values = malloc(sizeof(evaluation) * new_size);
	if (new_keys == NULL || new_values == NULL) return false;
	memset(new_keys, 0, new_size * sizeof(uint64_t)); // zero out keys
	for (uint64_t i = 0; i < tt_size; i++) { // for every old index
		if (tt_keys[i] == 0) continue; // skip empty slots
		uint64_t new_idx = tt_keys[i] % new_size;
		new_keys[new_idx] = tt_keys[i];
		new_values[new_idx] = tt_values[i];
	}
	free(tt_keys);
	free(tt_values);
	tt_keys = new_keys;
	tt_values = new_values;
	tt_size = new_size;
	tt_rehash_count = (uint64_t) (ceil(tt_max_load * new_size));
	return true;
}

uint64_t tt_pieceval(board *b, coord c) {
	assert(is_initialized);
	int piece_code = 0;
	piece p = at(b, c);
	if (p_eq(p, no_piece)) return 0;
	if (!p.white) piece_code += 6;
	if (p.type == 'P') ; // do nothing
	else if (p.type == 'N') piece_code += 1;
	else if (p.type == 'B') piece_code += 2;
	else if (p.type == 'R') piece_code += 3;
	else if (p.type == 'Q') piece_code += 4;
	else if (p.type == 'K') piece_code += 5;
	else assert(false);
	return zobrist[square_code(c)][piece_code];
}
