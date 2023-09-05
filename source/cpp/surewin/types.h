#ifndef TYPES_H_INCLUDED
#define TYPES_H_INCLUDED

#include <stdlib.h>
#include <inttypes.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define IS_SIX_WINS 1
#define PLY_SUREWIN 1
#define PLY 4
#define EXTREME_PLY_EXT 0

typedef uint64_t Key;
extern bool print;

#endif // #ifndef TYPES_H_INCLUDED
