#ifndef EVALUATE_H
#define EVALUATE_H

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "types.h"
#include "util.h"

int evaluate(board *b);
int evaluate_material(board *b);

#endif
