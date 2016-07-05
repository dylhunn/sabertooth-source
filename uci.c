#include "uci.h"

void process_command(char *command_str);
void *search_entrypoint(void *param);
void *timeout_entrypoint(int *time);
void kill_workers(bool print);
void print_pv(board *b_orig, int maxdepth);

// Called after the engine recieves the string "uci."
// Configures the engine with the GUI and loops, waiting for commands.
void enter_uci() {
	// Assume a new game is beginning for noncompilant engines (that don't send ucinewgame)
	tt_init();
	reset_board(&uciboard);

	// initilize logging
	logstr = fopen("ucilog.txt", "a");
	if (logstr == NULL) {
    	printf("info string error opening ucilog file!\n");
    	exit(1);
	}
	/* print some text */
	fprintf(logstr, "Starting log\n");

	while (true) {
		char command[max_input_string_length]; // read the input string (ends in \n\0)
		char *result = fgets(command, max_input_string_length - 1, stdin);
		process_command(result);
		fflush(stdout);
	}
}

void process_command(char *command_str) {
	fprintf(logstr, "Received command: %s\n", command_str);
	fflush(logstr);
	char *first_token = strtok(command_str, token_sep);

	if (strcmp(first_token, "isready") == 0) {
		stdout_fprintf(logstr, "readyok\n");

	} else if (strcmp(first_token, "uci") == 0) {
		stdout_fprintf(logstr, "id name %s %s\n", engine_name, engine_version);
		stdout_fprintf(logstr, "id author %s\n", author_name);
		stdout_fprintf(logstr, "info string loading %s%s\n", engine_name, engine_version);
		// Assume a new game is beginning for noncompilant engines (that don't send ucinewgame)
		tt_init();
		reset_board(&uciboard);
		stdout_fprintf(logstr, "uciok\n");

	} else if (strcmp(first_token, "ucinewgame") == 0) { // a new game is starting
		reset_board(&uciboard);
		tt_init();

	} else if (strcmp(first_token, "quit") == 0) { // terminate engine
		exit(0);

	} else if (strcmp(first_token, "position") == 0) { // configure the board
		char *mode = strtok(NULL, token_sep);
		if (strcmp(mode, "startpos") == 0) {
			reset_board(&uciboard);
		} else if (strcmp(mode, "fen") == 0) {
			stdout_fprintf(logstr, "info string FEN positions not yet supported\n");
			return;
		} else {
			stdout_fprintf(logstr, "info string unknown \"position\" option \"%s\"\n", mode);
			return;
		}
		char *nextstr = strtok(NULL, token_sep);
		if (nextstr == NULL) {
			return;
		}
		if (!strcmp(nextstr, "moves") == 0) {
			stdout_fprintf(logstr, "info string unknown \"position\" option \"%s\"\n", nextstr);
		}
		// process the moves to modify the board
		char *nextmstr = strtok(NULL, token_sep);
		move nextm;
		while (nextmstr != NULL) {
			if (!string_to_move(&uciboard, nextmstr, &nextm)) {
				stdout_fprintf(logstr, "info string failed to process move \"%s\"\n", nextmstr);
				return;
			}
			uciboard.true_game_ply_clock++;
			apply(&uciboard, nextm);
			nextmstr = strtok(NULL, token_sep);
		}

	} else if (strcmp(first_token, "go") == 0) { // begin the search
		int wtime = -1;
		int btime = -1;
		int winc = -1;
		int binc = -1;
		int movestogo = -1;
		int movetime = -1;
		bool infinite = false;

		char *mode = strtok(NULL, token_sep);
		while (mode != NULL) {
			if (strcmp(mode, "infinite") == 0) {
				stdout_fprintf(logstr, "info string infinite search...\n");
				infinite = true;
			} else if (strcmp(mode, "wtime") == 0) {
				wtime = atoi(strtok(NULL, token_sep));
			} else if (strcmp(mode, "btime") == 0) {
				btime = atoi(strtok(NULL, token_sep));
			} else if (strcmp(mode, "winc") == 0) {
				winc = atoi(strtok(NULL, token_sep));
			} else if (strcmp(mode, "binc") == 0) {
				binc = atoi(strtok(NULL, token_sep));
			} else if (strcmp(mode, "movetime") == 0) {
				movetime = atoi(strtok(NULL, token_sep));
			} else if (strcmp(mode, "movestogo") == 0) {
				movestogo = atoi(strtok(NULL, token_sep));
			} else {
				stdout_fprintf(logstr, "info string unsupported \"go\" option \"%s\"\n", mode);
			}
			mode = strtok(NULL, token_sep);
		}
		// compute the time to be used
		if (!infinite && movetime == -1) {
			int timeleft = uciboard.black_to_move ? btime : wtime;
			int increment = uciboard.black_to_move ? binc : winc;
			movetime = time_use(&uciboard, timeleft, increment, movestogo);
		}

		if (infinite || wtime <= 0 || btime <= 0) {
			movetime = -1;
			infinite = true;
		}

		// stop workers that are already running
		kill_workers(false);

		// spawn the worker thread
		int *arg = malloc(sizeof(int));
		*arg = movetime; // TODO this will leak
		if (movetime == -1) {
			if (pthread_create(&search_worker, NULL, search_entrypoint, NULL) != 0) {
				stdout_fprintf(logstr, "info string failed to spawn infinite search thread\n");
			}
		} else if (pthread_create(&timer_worker, NULL, timeout_entrypoint, arg) != 0) {
			stdout_fprintf(logstr, "info string failed to spawn timed search thread\n");
		}
		
	} else if (strcmp(first_token, "stop") == 0) { // end the search
		kill_workers(true);
		//tt_clear(); // debugging

	} else {
		stdout_fprintf(logstr, "info string unsupported UCI operation \"%s\"\n", first_token);
	}

}

// Kill a search, if it is running, and print the bestmove.
void kill_workers(bool print) {
	if (!search_running) return;
	pthread_mutex_lock(&tt_writing_lock);
	if (pthread_cancel(timer_worker) != 0) {
		stdout_fprintf(logstr, "info string failed to terminate timer thread (perhaps it has already been killed?) \n");
	}
	if (pthread_cancel(search_worker) != 0) {
		stdout_fprintf(logstr, "info string failed to terminate search thread (perhaps it has already been killed?) \n");
	}
	pthread_mutex_unlock(&tt_writing_lock);
	char buffer[6];
	if (print) stdout_fprintf(logstr, "bestmove %s\n", move_to_string(tt_get(&uciboard)->best, buffer));
	search_running = false;
}

// Prints the space-separated moves in the PV, followed by a space.
// Will print no more than maxdepth moves.
// Also stores the bestmove for later printing.
void print_pv(board *b_orig, int maxdepth) {
	int curr_depth = maxdepth; // output depth limit
	board b_cpy = *b_orig;
	board *b = &b_cpy;
	evaluation *eval = tt_get(b);
	lastbestmove = eval->best;
	do {
		char move[6];
		stdout_fprintf(logstr, "%s ", move_to_string(eval->best, move));
		apply(b, eval->best);
		eval = tt_get(b);
	} while (eval != NULL && !m_eq(eval->best, no_move) && curr_depth-- > 0);
}

// TODO - ensure cancellation doesn't call the cleanup function
// The entrypoint for a multithreaded position search.
// This function will perform iterative deepening forever (or until its thread is killed).
void *search_entrypoint(void *param) { 
	search_running = true;
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	board working_copy = uciboard; // killing the worker thread might destroy our board
	for (int i = 2; i <= iterative_deepening_cutoff; i += 2) { // TODO debug: only even depths
		search(&working_copy, i);
		evaluation *eval = tt_get(&working_copy);

		stdout_fprintf(logstr, "info depth %d time %d nodes %llu score cp %d\n", sstats.depth, (int) sstats.time, sstats.nodes_searched + sstats.qnodes_searched, eval->score);
		stdout_fprintf(logstr, "info pv ");
		print_pv(&working_copy, pv_printing_cutoff);
		stdout_fprintf(logstr, "\n");
		fflush(stdout);
	}
	return NULL;
}

// spawns a search worker, then kills it after the elapsed time in ms and prints the bestmove
void *timeout_entrypoint(int *time) {
	lastbestmove = no_move;
	search_running = true;
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	// spawn the worker thread
	if (pthread_create(&search_worker, NULL, search_entrypoint, NULL) != 0) {
		stdout_fprintf(logstr, "info string failed to spawn timed search thread\n");
	}

	// Wait in microseconds
	usleep((*time) * 1000);

	pthread_mutex_lock(&tt_writing_lock);
	if (pthread_cancel(search_worker) != 0) {
		stdout_fprintf(logstr, "info string failed to terminate search thread (perhaps it has already been killed?) \n");
	}
	pthread_mutex_unlock(&tt_writing_lock);
	char buffer[6];
	if (m_eq(lastbestmove, no_move)) { // Panic! The search wasn't long enough to complete depth one. Choose a random legal move.
		stdout_fprintf(logstr, "info string search depth 1 timeout; choosing random move\n");
		int c;
		move *moves = board_moves(&uciboard, &c);
		lastbestmove =  moves[0];
		free(moves);
	}
	stdout_fprintf(logstr, "bestmove %s\n", move_to_string(lastbestmove, buffer));
	search_running = false;
	return NULL;
}

