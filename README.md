# fianchetto
Fianchetto/Sabertooth is a lightweight UCI-compatible chess engine written in C.

It should work with any UCI chess interface, although it was tested with the OS X version of XBoard.

It plays at a reasonable strength; it can hold its own against a moderate-to-strong club player. There are a couple of known bugs that cause rare blunders; this is currently the primary source of its weaknesses.

You can clone and run the engine in debug mode with:
Specifically:

    git clone https://github.com/dylhunn/fianchetto.git
    cd fianchetto
    make
    ./fianchetto -d
After performing `make`, you can use the binary with an arbitrary UCI chess interface.

The engine is currently only compatible with Mac and Linux systems, due to the use of `pthreads`.

