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

// Fetch an evaluation from the table. Populates with no_eval if not found.
// This pointer does not actually point into the table.
void tt_get(board *b, evaluation *result);

// Clears the transposition table (by resetting it).
void tt_clear(void);

// For parallel search. Marks a node as exclusively belonging to a specific thread.
// Returns true if the node was claimed, and populates the id.
bool tt_try_to_claim_node(board *b, int *id);

bool tt_always_claim_node(board *b, int *id);

// Unclaims a node for a given id.
void tt_unclaim_node(int id);

// Get the Zobrist hash value of a piece.
uint64_t tt_pieceval(board *b, coord c);

/**
 * Constants and settings
 */

// starting size of table, and whether expansion is allowed
#define TT_MEGABYTES_DEFAULT 1000
extern int tt_megabytes; // Don't set a value here 
static bool allow_tt_expansion = false;
extern bool search_terminate_requested;
// Re-hash at 70% load factor
static const double tt_max_load = .75;
// Nodes that haven't been accessed in this many moves are ancient and might be removed
static const int remove_at_age = 3; // TODO dynamically select?

// Randomly selected zobrist values used to hash board state
extern uint64_t zobrist[64][12]; // zobrist table for pieces
extern uint64_t zobrist_castle_wq; // removed when castling rights are lost
extern uint64_t zobrist_castle_wk;
extern uint64_t zobrist_castle_bq;
extern uint64_t zobrist_castle_bk;
extern uint64_t zobrist_black_to_move;

// Used to keep track of when the table will actually be cleared
static int tt_clear_scheduled_on_move = -1;
static bool tt_clear_scheduled = false;


#endif
