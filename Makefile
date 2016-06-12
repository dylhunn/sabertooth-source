CFLAGS = -O3 -Weverything -Wno-padded -Wno-unused-parameter -Wno-unused-variable -Wno-sign-conversion
all: fianchetto.o util.o ttable.o movegen.o
	clang $(CFLAGS) -D NDEBUG $^ -o fianchetto
debugf: fianchetto.o ttable.o movegen.o
	clang $(CFLAGS) -g3 $^ -o fianchetto
clean:
	rm *.o
	rm fianchetto
	rm *.gch
	rm -rf fianchetto.dSYM
%.o: %.c
	clang -c $(CFLAGS) $< -o $@
fianchetto.o: fianchetto.c
ttable.o: ttable.h ttable.c
movegen.o: movegen.h movegen.c
util.o: util.h util.c