CFLAGS = -Ofast -flto -ffast-math -Weverything -Wno-padded -Wno-sign-conversion -Wno-conversion -Wno-comment -Wno-format-nonliteral -ggdb
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
util.o: settings.h util.h util.c
evaluate.o: evaluate.h evaluate.c
search.o: search.h search.c
uci.o: uci.h uci.c
