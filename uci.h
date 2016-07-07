#ifndef UCI_H
#define UCI_H

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "types.h"
#include "util.h"
#include "movegen.h"
#include "search.h"
#include "ttable.h"

#define max_input_string_length 2000

// Cutoff is necessary to prevent very deep sarches in the event of mate
#define iterative_deepening_cutoff 40 

// Cutoff is nessary to avoid printing cyclic PVs forever
#define pv_printing_cutoff 40

static const char *token_sep = " \t\n"; // characters that can separate tokens in a UCI input string

static board uciboard; // the last known board loaded with the position command

static bool search_running = false;

static move lastbestmove;

pthread_t search_worker;
pthread_t timer_worker;

// Called after the engine recieves the string "uci"
// Configures the engine with the GUI and loops, waiting for commands.
void enter_uci(void) __attribute__ ((noreturn));

#endif
