#include "ttable.h"

void tt_auto_cleanup(void);
bool tt_expand(void);

// Zobrist table data for hashing board positions
uint64_t zobrist[64][12]; // zobrist table for pieces
uint64_t zobrist_castle_wq; // removed when castling rights are lost
uint64_t zobrist_castle_wk;
uint64_t zobrist_castle_bq;
uint64_t zobrist_castle_bk;
uint64_t zobrist_black_to_move;
uint64_t zobrist_en_passant_files[8];

// How large should the table be?
int tt_megabytes = TT_MEGABYTES_DEFAULT;

// Table data
static uint64_t *tt_keys = NULL;
static evaluation *tt_values = NULL;
static pthread_mutex_t *tt_locks = NULL;
static uint8_t *tt_node_thread_counts = NULL;
static uint64_t tt_size;
static uint64_t tt_count = 0;
static uint64_t tt_rehash_count; // When to perform a rehash; computed based on max_load
static bool is_initialized = false;

// To prevent the search worker from being killed while writing to the table
bool search_terminate_requested = false;

// The index of a given board coordinate in the Zobrist table
static inline int square_code(coord c) {
	return (c.col)*8+c.row;
}

// The percentage load on the table
double tt_load() {
	assert(is_initialized);
	return 100 * ((double) tt_count) / tt_size;
}

uint64_t get_tt_count() {
	return tt_count;
}

uint64_t get_tt_size() {
	return tt_size;
}

// Invoke to prepare transposition table
void tt_init(void) {
	// First, compute size from memory use
	const uint64_t bytes_in_mb = 1000000;
	tt_size = (uint64_t) (ceil(((double) (tt_megabytes * bytes_in_mb)) / 
		(sizeof(evaluation) + sizeof(uint64_t) + sizeof(pthread_mutex_t))));
	uint64_t check_mb_size = (uint64_t) ((double) tt_size * (sizeof(evaluation) + sizeof(uint64_t))) / bytes_in_mb;
	printf("info string initializing ttable with %llu slots for total size %llumb\n", tt_size, check_mb_size);

	if (tt_keys != NULL) free(tt_keys);
	if (tt_values != NULL) free(tt_values);
	if (tt_locks != NULL) free(tt_locks);
	if (tt_node_thread_counts != NULL) free(tt_node_thread_counts);
	tt_keys = malloc(sizeof(uint64_t) * tt_size);
	assert(tt_keys != NULL);
	memset(tt_keys, 0, tt_size * sizeof(uint64_t));
	tt_values = malloc(sizeof(evaluation) * tt_size);
	assert(tt_values != NULL);
	tt_locks = malloc(sizeof(pthread_mutex_t) * tt_size);
	assert(tt_locks != NULL);
	//tt_node_thread_counts = malloc(sizeof(uint8_t) * tt_size);
	//memset(tt_node_thread_counts, 0, tt_size * sizeof(uint8_t));
	//assert(tt_node_thread_counts != NULL);
	tt_count = 0;
	tt_rehash_count = (uint64_t) (ceil(tt_max_load * tt_size));

	for (int i = 0; i < tt_size; i++) {
		pthread_mutex_init(tt_locks + i, NULL);
	}

	// Populate Zobrist data
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
	for (int i = 0; i < 8; i++) zobrist_en_passant_files[i] = rand64();
	zobrist_black_to_move = rand64();
	atexit(tt_auto_cleanup);
	is_initialized = true;
}

// Automatically called
void tt_auto_cleanup(void) {
	// Commented out in the interm for the sake of killing the worker thread
	/*if (tt_keys) free(tt_keys);
	tt_keys = NULL;
	if (tt_values) free(tt_values);
	tt_values = NULL;
	if (tt_threads) free(tt_values);
	tt_values = NULL;*/
}

// Hash a board position.
// Usually, you should use the board's "hash" field instead, which is updated incrementally.
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

// Put a new entry in the transposition table.
// Only replaces under certain conditions, to avoid overwriting a principal variation (PV).
// Overwrites ancient entries.
void tt_put(board *b, evaluation e) {
	assert(is_initialized);

	// If we wanted to clear on the next move
	if (tt_clear_scheduled && (b->true_game_ply_clock != tt_clear_scheduled_on_move)) {
		tt_clear();
		tt_clear_scheduled = false;
		stdout_fprintf(logstr, "info string transposition table clear performed.\n");
	}

	if (tt_count >= tt_rehash_count && !tt_clear_scheduled) { // The ttable is overflowing and no clear is already scheduled
		if (allow_tt_expansion && !tt_expand()) {
			stdout_fprintf(logstr, "info string failed to expand transposition table from %llu entries; clearing.\n", tt_count);
			tt_clear();
			return;
		}
		if (!allow_tt_expansion) {
			stdout_fprintf(logstr, "info string transposition table filled; scheduling a clear\n");
			tt_clear_scheduled = true;
			tt_clear_scheduled_on_move = b->true_game_ply_clock;
		}
	}

	uint64_t idx = b->hash % tt_size;
	bool overwriting = false;

	// Because we are going to clear the table regardless, we switch to an "always-overwrite" strategy
	// Specifically, we overwrite any non-enpty slot. We don't touch empty slots to avoid very long tt_get() operations.
	// This might overwrite random things and destroy the table or PV, but that's OK - presumably
	// this is only called at deep depths, and the search() caller should handle the starting position
	// not being in the table.
	if (tt_clear_scheduled) {
		if (tt_keys[idx] != 0) goto skipchecks;
		else {
			return;
		}
	}

	while (tt_keys[idx] != 0 && tt_keys[idx] != b->hash) {
		if (b->true_game_ply_clock - tt_values[idx].last_access_move >= remove_at_age) {
			overwriting = true;
			break;
		}
		idx = (idx + 1) % tt_size;
	}

	// We found our entry; lock it
	pthread_mutex_lock(tt_locks + idx);

	// If it is a new entry, skip the replacement checks
	if (tt_keys[idx] == 0 || overwriting) goto skipchecks;

	// TODO did it play better with this commented out?
	// Never replace exact with inexact, or we could easily lose the PV.
	if (tt_values[idx].type == exact && e.type != exact) {
		sstats.ttable_insert_failures++;
		pthread_mutex_unlock(tt_locks + idx);
		return;
	}
	// only replace qexact with other qexact or exact
	if (tt_values[idx].type == qexact) {
		if (e.type != qexact && e.type != exact) {
			sstats.ttable_insert_failures++;
			pthread_mutex_unlock(tt_locks + idx);
			return;
		}
	}
	// Always replace inexact with exact;
	// otherwise, we might fail to replace a cutoff with a "shallow" ending of a PV.
	if (tt_values[idx].type != exact && e.type == exact) goto skipchecks;
	if (tt_values[idx].type != qexact && e.type == qexact) goto skipchecks;
	if (tt_values[idx].type != qexact && e.type == exact) goto skipchecks;
	// Otherwise, prefer deeper entries; replace if equally deep due to aspiration windows
	if (e.depth < tt_values[idx].depth) {
		//sstats.ttable_insert_failures++; 
		// TODO keeping the deepest entry aappears to caue blunders? Maybe collisions are responsible? Really odd.
		pthread_mutex_unlock(tt_locks + idx);
		return;
	}
	skipchecks:
	e.last_access_move = b->true_game_ply_clock;
	tt_values[idx] = e;
	if (!overwriting) tt_count++;
	else sstats.ttable_overwrites++;
	sstats.ttable_inserts++;
	tt_keys[idx] = b->hash; // Write the key at the end so tt_get will never read an incomplete entry
	pthread_mutex_unlock(tt_locks + idx);
}

// Fetch an entry from the transposition table.
void tt_get(board *b, evaluation *result) {
	assert(is_initialized);
	uint64_t idx = b->hash % tt_size;
	while (tt_keys[idx] != 0 && tt_keys[idx] != b->hash) {
		idx = (idx + 1) % tt_size;
	}
	if (tt_keys[idx] == 0) {
		sstats.ttable_misses++;
		*result = no_eval;
		return;
	}
	// TODO is locking necessary?
	pthread_mutex_lock(tt_locks + idx);
	tt_values[idx].last_access_move = b->true_game_ply_clock;
	sstats.ttable_hits++;
	*result = tt_values[idx];
	pthread_mutex_unlock(tt_locks + idx);
}

// Clear the transposition table (by resetting it).
void tt_clear() {
	assert(is_initialized);
	tt_init();
}

bool tt_try_to_claim_node(board *b, int *id) {
	assert(is_initialized);
	uint64_t idx = b->hash % tt_size;
	while (tt_keys[idx] != 0 && tt_keys[idx] != b->hash) {
		idx = (idx + 1) % tt_size;
	}
	pthread_mutex_lock(tt_locks + idx);
	//bool success = __sync_bool_compare_and_swap(tt_node_thread_counts + idx, zero, one);
	if (tt_node_thread_counts[idx] != 0) {
		pthread_mutex_unlock(tt_locks + idx);
		return false;
	}
	tt_node_thread_counts[idx]++;
	*id = idx;
	return true;
}

void tt_always_claim_node(board *b, int *id) {
	assert(is_initialized);
	uint64_t idx = b->hash % tt_size;
	while (tt_keys[idx] != 0 && tt_keys[idx] != b->hash) {
		idx = (idx + 1) % tt_size;
	}
	pthread_mutex_lock(tt_locks + idx);
	tt_node_thread_counts[idx]++;
	*id = idx;
}

// Unclaims a node for a given id.
void tt_unclaim_node(int id) {
	tt_node_thread_counts[id]--;
	pthread_mutex_unlock(tt_locks + id);
}

// Expand the table. This won't be called unless the appropriate setting is activated in the .h file.
bool tt_expand(void) {
	assert(false); // TODO this no longer works
	assert(is_initialized);
	stdout_fprintf(logstr, "expanding transposition table...\n");
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

// Get the Zobrist hash value of a piece at a board location.
uint64_t tt_pieceval(board *b, coord c) {
	assert(is_initialized);
	int piece_code = 0;
	piece p = at(b, c);
	if (p_eq(p, no_piece)) return 0;
	if (!p.white) piece_code += 6;
	switch(p.type) {
		case 'P': break; // do nothing
		case 'N': piece_code += 1; break;
		case 'B': piece_code += 2; break;
		case 'R': piece_code += 3; break;
		case 'Q': piece_code += 4; break;
		case 'K': piece_code += 5; break;
		default: assert(false);
	}
	return zobrist[square_code(c)][piece_code];
}
