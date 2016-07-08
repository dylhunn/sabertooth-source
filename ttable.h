#ifndef TTABLE_H
#define TTABLE_H

#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "search.h"
#include "types.h"
#include "util.h"

/**
 * Transposition Table Public API
 *
 * The Transposition Table is a hashtable that stores previously evaluated positions.
 */

// Fetch usage data about the table
uint64_t get_tt_count(void);
uint64_t get_tt_size(void);

// Return the percentage of the table that is used
double tt_load(void);

// Initialize the transposition table. Must be called before use.
void tt_init(void);

// Generate the expected hash value of a board.
// Typically, use the board struct's hash field instead.
uint64_t tt_hash_position(board *b);

// Add or update a board in the transposition table.
void tt_put(board *b, evaluation e);

// Fetch an evaluation from the table. Returns NULL if not found.
// Do not write to this pointer. This pointer may expire after
// performing another tt_put(), and using it then has undefined behavior.
evaluation *tt_get(board *b);

// Clears the transposition table (by resetting it).
void tt_clear(void);

// Get the Zobrist hash value of a piece.
uint64_t tt_pieceval(board *b, coord c);

/**
 * Constants and variables
 */

// starting size of table, and whether expansion is allowed
#define TT_MEGABYTES_DEFAULT 500
static bool allow_tt_expansion = false;

extern int tt_megabytes;

// To control access
extern pthread_mutex_t tt_writing_lock;

// Re-hash at 70% load factor
static const double tt_max_load = .7;

// Nodes that haven't been accessed in this many moves are ancient and might be removed
static const int remove_at_age = 2; // TODO dynamically select? Consider levels as low as 1.

static bool is_initialized = false;

// Randomly selected zobrist values used to hash board state
extern uint64_t zobrist[64][12]; // zobrist table for pieces
extern uint64_t zobrist_castle_wq; // removed when castling rights are lost
extern uint64_t zobrist_castle_wk;
extern uint64_t zobrist_castle_bq;
extern uint64_t zobrist_castle_bk;
extern uint64_t zobrist_black_to_move;

#endif
