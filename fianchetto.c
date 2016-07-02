/* Optimization TODO list:
 * - Store sets of pieces by color for use in:
 *   - board_moves
 *   - evaluate_material
 * - Separate move generator for quiescence
 *
 * Other TODO items:
 * - Identify game-ending conditions
 * - En passant
 * - No castling through check
 * - Search function should account for 50-move and repeated-move draws
 */

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "types.h"
#include "util.h"
#include "search.h"
#include "movegen.h"
#include "ttable.h"
#include "uci.h"

// limit for how many moves print_analysis should print
static const int depth_limit = 50;

int repl(void);
void print_moves(board *b);
void print_board(board *b);
void print_analysis(board *b);
void iterative_deepen(board *b, int max_depth);

int main() {
	repl();
	return 0;
}

int repl(void) {
	tt_init();
	board b; 
	reset_board(&b);
	system("clear");
	printf("%s %s by Dylan D. Hunn\nConsole Analysis Interface\n\n", engine_name, engine_version);
	while (true) {
		print_board(&b);
		printf("Commands: \"e3\" evaluates to depth 3; \"ma1a2\" makes the move a1a2; \"q\" quits; \"u\" or \"uci\" enters UCI mode.\n\n");
		char buffer[100];
		fgets(buffer, 99, stdin);
		int edepth = buffer[1] - '0';
		if (buffer[2] != '\n') edepth = 10 * edepth + (buffer[2] - '0');
		move m;
		switch(buffer[0]) {
			case 'e':
				printf("Calculating...\n");
				system("clear");
				iterative_deepen(&b, edepth);
				printf("\n");
				break;
			case 'm':
				system("clear");
				if (!string_to_move(&b, buffer + 1, &m)) {
					move_to_string(m, buffer);
					printf("Invalid move: %s\n", buffer);

				} else {
					printf("Read move: %s\n", move_to_string(m, buffer));
					b.true_game_ply_clock++;
					apply(&b, m);
				}
				printf("\n");
				break;
			case 'q':
				exit(0);
			case 'u':
				enter_uci();
			default:
				system("clear");
				printf("Unrecognized command.\n\n");
		}
	}

}

void print_moves(board *b) {
	int movec = 0;
	move *list = board_moves(b, &movec);
	printf("%d moves available: ", movec);
	for (int i = 0; i < movec; i++) {
		char moveb[6];
		printf("%s ", move_to_string(list[i], moveb));
	}
	printf("\n");
	free(list);
}

void print_board(board *b) {
	for (int i = 7; i >= 0; i--) {
		printf(cBLU "%d " cRESET, i+1);
		for (int j = 0; j <= 7; j++) {
			if (p_eq(b->b[j][i], no_piece)) printf(" ");
			else {
				if (b->b[j][i].white) printf(cWHT "%c" cRESET, b->b[j][i].type);
				else printf(cYEL "%c" cRESET, b->b[j][i].type);
			}
		}
		printf("\n");
	}
	printf(cBLU "  ABCDEFGH\n" cRESET);
	printf("Castling rights: ");
	if (b->castle_rights_wq) printf("white queenside; ");
	if (b->castle_rights_wk) printf("white kingside; ");
	if (b->castle_rights_bq) printf("black queenside; ");
	if (b->castle_rights_bk) printf("black kingside; ");

	printf("\n%s to move.\n\n", b->black_to_move ? "Black" : "White");
	print_moves(b);
	printf("\n");
}

void print_analysis(board *b_orig) {
	int curr_depth = depth_limit;
	board b_cpy = *b_orig;
	board *b = &b_cpy;
	evaluation *eval = tt_get(b);
	printf("d%d [%+.2f]: ", eval->depth, ((double)eval->score)/100); // Divide centipawn score
	assert(eval != NULL);
	int moveno = (b->last_move_ply+2)/2;
	if (b->black_to_move) {
		printf("%d...", moveno);
		moveno++;
	}
	do {
		assert(!m_eq(eval->best, no_move));
		if (!b->black_to_move) printf("%d.", moveno++);
		char move[6];
		if (eval->type == qexact) printf("(q) ");
		printf("%s ", move_to_string(eval->best, move));
		apply(b, eval->best);
		eval = tt_get(b);
	} while (eval != NULL && /*!m_eq(eval->best, no_move) &&*/ curr_depth-- > 0);
	printf("\n\t(%llu new nodes, %llu new qnodes, %llu qnode aborts, %.0fms)", 
		sstats.nodes_searched, sstats.qnodes_searched, sstats.qnode_aborts, sstats.time);
	printf("\n");
}

void iterative_deepen(board *b, int max_depth) {
	printf("Iterative Deepening Analysis Results (including cached analysis)\n");
	for (int i = 1; i <= max_depth; i++) {
		clear_stats();
		printf("Searching at depth %d... ", i);
		fflush(stdout);
		search(b, i);
		print_analysis(b);
	}
}
