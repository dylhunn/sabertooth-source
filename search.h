#ifndef SEARCH_H
#define SEARCH_H

#include <assert.h>
#include <limits.h>
#include <math.h>
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

// set by last call to search()
extern searchstats sstats;

int search(board *b, int ply);
int ab_max(board *b, int alpha, int beta, int ply);
int ab_min(board *b, int alpha, int beta, int ply);
void apply(board *b, move m);
void unapply(board *b, move m);

#endif
