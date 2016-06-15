#include "uci.h"

void process_command(char *command_str);
void *search_entrypoint(void *param);

// Called after the engine recieves the string "uci."
// Configures the engine with the GUI and loops, waiting for commands.
void enter_uci() {
	printf("id %s%s\n", engine_name, engine_version);

	// Assume a new game is beginning for noncompilant engines (that don't send ucinewgame)
	tt_init();

	printf("uciok\n");

	while (true) {
		char command[max_input_string_length]; // read the input string (ends in \n\0)
		char *result = fgets(command, max_input_string_length - 1, stdin);
		process_command(result);
	}
}

void process_command(char *command_str) {
	char *first_token = strtok(command_str, token_sep);

	if (strcmp(first_token, "isready") == 0) {
		printf("readyok\n");

	} else if (strcmp(first_token, "ucinewgame") == 0) { // a new game is starting
		tt_init();
		tt_clear();

	} else if (strcmp(first_token, "quit") == 0) { // terminate engine
		exit(0);

	} else if (strcmp(first_token, "position") == 0) { // configure the board
		char *mode = strtok(NULL, token_sep);
		if (strcmp(mode, "startpos") == 0) {
			reset_board(&uciboard);
		} else if (strcmp(mode, "fen") == 0) {
			printf("info string FEN positions not yet supported\n");
			return;
		} else {
			printf("info string unknown \"position\" option \"%s\"\n", mode);
			return;
		}
		// process the moves to modify the board
		char *nextmstr = strtok(NULL, token_sep);
		move nextm;
		while (nextmstr != NULL) {
			if (!string_to_move(&uciboard, nextmstr, &nextm)) {
				printf("info string failed to process move \"%s\"\n", nextmstr);
				return;
			}
			uciboard.true_game_ply_clock++;
			apply(&uciboard, nextm);
			nextmstr = strtok(NULL, token_sep);
		}

	} else if (strcmp(first_token, "go") == 0) { // begin the search
		char *mode = strtok(NULL, token_sep);
		while (mode != NULL) {
			if (strcmp(mode, "infinite") == 0) {
				printf("info string infinite search...\n");
				// spawn the worker thread
				if (pthread_create(&search_worker, NULL, search_entrypoint, NULL) != 0) {
					printf("info string failed to spawn infinite search thread\n");
				}
				return;
			} else {
				printf("info string unsupported \"go\" option \"%s\"\n", mode);
			}
			printf("info sting bare \"go\" command is currently unsupported; use \"go infinite\"\n");
			mode = strtok(NULL, token_sep);
		}

		
	} else if (strcmp(first_token, "stop") == 0) { // end the search
		if (pthread_cancel(search_worker) != 0) {
			printf("info string failed to terminate search thread\n");
		}
		char buffer[6];
		printf("bestmove %s", move_to_string(tt_get(&uciboard)->best, buffer));
	} else {
		printf("info string unsupported UCI operation \"%s\"\n", first_token);
	}

}

// Prints the space-separated moves in the PV, followed by a space.
// Will print no more than maxdepth moves.
void print_pv(int maxdepth) {
	/*do {
		char move[6];
		printf("%s ", move_to_string(eval->best, move));
		apply(b, eval->best);
		eval = tt_get(b);
	} while (eval != NULL && !m_eq(eval->best, no_move) && curr_depth-- > 0);*/
}

// TODO - prevent thread from dying during tt_put
// TODO - ensure cancellation doesn't call the cleanup function
// The entrypoint for a multithreaded position search.
// This function will perform iterative deepening forever (or until its thread is killed).
void *search_entrypoint(void *param) { 
	board working_copy = uciboard; // killing the worker thread might destroy our board
	for (int i = 1; true; i++) {
		search(&working_copy, i);
		print_pv(i);
	}
}
