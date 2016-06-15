CFLAGS = -O0 -Weverything -Wno-padded -Wno-unused-parameter -Wno-unused-variable -Wno-sign-conversion -ggdb
all: fianchetto.o util.o ttable.o movegen.o evaluate.o search.o uci.o
	clang $(CFLAGS) $^ -o fianchetto
clean:
	rm -f fianchetto
	rm -f *.o
	rm -f *.gch
	rm -rf fianchetto.dSYM
%.o: %.c
	clang -c $(CFLAGS) $< -o $@
fianchetto.o: fianchetto.c
ttable.o: ttable.h ttable.c
movegen.o: movegen.h movegen.c
util.o: util.h util.c
evaluate.o: evaluate.h evaluate.c
search.o: search.h search.c
uci.o: uci.h uci.c