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

static const coord wqr = (coord) {0, 0}; // Rook squares
static const coord wkr = (coord) {7, 0};
static const coord bqr = (coord) {0, 7};
static const coord bkr = (coord) {7, 7};

static const int quiesce_ply_cutoff = 45; // Quiescence search will cut off after this many plies

// Because negating INT_MIN has awful consequences
static const int POS_INFINITY = INT_MAX - 10;
static const int NEG_INFINITY = INT_MIN + 10;

// set by last call to search()
extern searchstats sstats;

void clear_stats(void);

// Computes how much time should be used to search the next move, all units in ms
int time_use(board *b, int time_left, int increment, int movestogo);
int search(board *b, int ply);
int negamax(board *b, int alpha, int beta, int ply, bool actual_white_turn);
void apply(board *b, move m);
void unapply(board *b, move m);

#endif
