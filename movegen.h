#ifndef MOVEGEN_H
#define MOVEGEN_H

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "types.h"
#include "util.h"

/**
 * Move Generator Public API
 *
 * The Move Generator produces a list of pseudo-legal moves, given a board position.
 */

// Generates pseudo-legal moves for a player.
// (Does not exclude moves that put the king in check.)
// Generates an array of valid moves, and populates the count.
// (The malloc'ed memory will have a blank slot at the end for ease of move ordering,
// but this is not reflected in the count.)
move *board_moves(board *b, int *count, bool captures_only);

bool is_legal_move(board *b, move m);

// Fill a provided buffer with a move's string.
char *move_to_string(move m, char str[6]);

// Parses a string into a move object; returns success.
// (Ensures the move is valid on the board.)
bool string_to_move(board *b, char *str, move *m);

// Determines if a specific square is under attack.
bool in_check(board *b, int col, int row, bool by_white);

// checks if a given move, ALREADY applied, has put the specified color's king in check
// this is slightly more efficient than in_check
// precondition: the specified King was not already in check
bool puts_in_check(board *b, move m, bool white_king);

// The maximum number of moves that can be stored in a move array
// If any position results in more moves than this, a segfault will occur
static int max_moves_in_list = 150; 

#endif
