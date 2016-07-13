#ifndef SEARCH_H
#define SEARCH_H

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "types.h"
#include "util.h"
#include "evaluate.h"
#include "movegen.h"

/*
 * Constants
 */
static const coord wqr = (coord) {0, 0}; // Rook squares
static const coord wkr = (coord) {7, 0};
static const coord bqr = (coord) {0, 7};
static const coord bkr = (coord) {7, 7};
// Because negating INT_MIN has awful consequences
// Ensure these are always the same number, so negating scores doesn't produce unpredictable results
static const int POS_INFINITY = 999999;
static const int NEG_INFINITY = -999999;

/*
 * Search settings
 */
static const int quiesce_ply_cutoff = 45; // Quiescence search will cut off after this many plies
static const bool mvvlva = true; // Capture hueristic
static const bool useqsearch = true; // Quiescence search
static const int clear_tt_every_move = true;

/*
 * Search statistics
 */
 extern searchstats sstats;

/*
 * Public API
 */
// Clear the search stats struct, for use between search() calls
void clear_stats(void);
// Computes how much time should be used to search the next move, all units in ms
int time_use(board *b, int time_left, int increment, int movestogo);
// Perform a search and store the results in the transposition table
int search(board *b, int ply);
// Apply and unapply a move to the board, updating the hash
void apply(board *b, move m);
void unapply(board *b, move m);

#endif
