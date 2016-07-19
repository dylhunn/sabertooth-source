#ifndef UCI_H
#define UCI_H

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "settings.h"
#include "types.h"
#include "util.h"
#include "movegen.h"
#include "search.h"
#include "ttable.h"

static const char *token_sep = " \t\n"; // characters that can separate tokens in a UCI input string

static board uciboard; // the last known board loaded with the position command

static bool search_running = false;
static move last_tt_pv_move;
extern pthread_t search_worker;
extern pthread_t timer_worker;

// Called after the engine recieves the string "uci"
// Configures the engine with the GUI and loops, waiting for commands.
void enter_uci(void) __attribute__ ((noreturn));

void read_from_fen(board *b);

#endif
