#include <inttypes.h>
#include "position.h"

#define TT_VALUE_LESS 0 //<= tt value is less then the truth
#define TT_VALUE_MORE 1 //>=
#define TT_VALUE_EXACT 2 // ==

#define SUREWIN_WIN 0
#define SUREWIN_NO_WIN 1

#define INVALID 32001

extern int best_move_safe;
extern int shallow_tt_index;

struct surewin_tt_entry 
{	
	uint64_t hash;
	int16_t val;
	uint16_t bestmove;
	table square[2];
	uint8_t depth;
	bool easy;
	int turn;	
};

struct shallow_tt_entry
{
	uint64_t hash;
	uint64_t is_stable;
};

struct tt_entry 
{	
	uint64_t hash;
	int16_t val;
	uint16_t bestmove;
	int16_t alpha;
	int16_t beta;
	uint8_t depth; 
	uint8_t flag;
	table square[2];
	int turn;	
};

int surewin_tt_setsize (int);
int16_t surewin_tt_probe(Position *, uint16_t *, uint8_t *, bool *); 
void surewin_tt_save(Position *, int16_t, uint16_t, uint8_t, bool);
void surewin_tt_kill();

int shallow_tt_setsize (int);
int16_t shallow_tt_probe(Position *); 
void shallow_tt_save(Position *, int16_t);
void shallow_tt_kill();

int tt_setsize (int);
void tt_probe(Position *, uint16_t *, int16_t *, uint8_t *, uint8_t *); 
void tt_save(Position *, int16_t, uint8_t, uint16_t, uint8_t);
void tt_kill();
void tt_info(Position *);

