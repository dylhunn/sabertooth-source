#ifndef UCI_H
#define UCI_H

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "util.h"
#include "movegen.h"
#include "search.h"

#define max_input_string_length 2000

static const char *token_sep = " \t\n"; // characters that can separate tokens in a UCI input string

static board uciboard; // the last known board loaded with the position command

static pthread_t search_worker;

// Called after the engine recieves the string "uci"
// Configures the engine with the GUI and loops, waiting for commands.
void enter_uci(void) __attribute__ ((noreturn));

#endif
