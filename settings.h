#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "stdbool.h"
#include "types.h"

/*
 * Search settings
 */
static const bool use_mtd_f = true; // Use MTD-F optimization on top of alpha-beta search; use_ttable must also be on
static const int quiesce_ply_cutoff = 45; // Quiescence search will cut off after this many plies
static const bool mvvlva = true; // Capture hueristic
static const bool use_qsearch = true; // Quiescence search
static const bool clear_tt_every_move = false; // Clear the transposition table after each search completes
static const bool use_ttable = true; // Should the transposition table be used to generate search cutoffs?
static const bool use_tt_move_hueristic = true; // Use the last move stored in the TT as a "best-first" hueristic
static const bool check_extend = false; // Extend the search by one ply in case of check
static const int check_extension_centiply = 100; // Centiply to extend in case of check
static const int check_extend_threshold = 2; // In the final n plies of regular search
static const bool use_log_file = false;
static const bool always_use_debug_mode = false;
static const int num_search_threads = 1; // Parallel search

/*
 * Evaluation settings
 */
static const int doubled_pawn_penalty = 9; // Evaluation penalties for doubled pawns

/*
 * Engine settings
 */
#define ENGINE_NAME "Sabertooth"
#define ENGINE_VERSION "0.2"
#define AUTHOR_NAME "Dylan D. Hunn"

/*
 * UCI protocol settings
 */
#define max_input_string_length 2000
#define iterative_deepening_cutoff 40 // Cutoff is necessary to prevent very deep sarches in the event of mate
#define pv_printing_cutoff 40 // Cutoff is nessary to avoid printing cyclic PVs forever

#endif

