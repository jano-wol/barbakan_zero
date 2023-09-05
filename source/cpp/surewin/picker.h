#include "position.h"

enum
{
	TT_REGULAR,
	DOUBLE_FOUR_REGULAR,
	FOUR_THREE_REGULAR,
	DOUBLE_THREE_REGULAR,
	DOUBLE_FOUR_REGULAR_OPP,
	FOUR_THREE_REGULAR_OPP,
	INTERESTING_ATTACK_REGULAR,
	DOUBLE_THREE_REGULAR_OPP,	
	INTERESTING_ATTACK_REGULAR_OPP,
	NOT_INTERESTING_ATTACK,
	NOT_INTERESTING_ATTACK_OPP,
	LOCAL1,
	LOCAL2,
	LOCAL3,
	DIST1,
	DIST2,
	DIST3,
	PASSIVE1,
	PASSIVE0,
	END_REGULAR
};

enum
{
	START_ATT,
	TT_ATT,
	DOUBLE_FOUR_ATT,
	FOUR_THREE_ATT,
	INIT_ATT,
	DOUBLE_THREE_ATT,
	INTERESTING_ATT,
	INTERESTING_ATTACKLINE_OK_FT_ATT,
	INTERESTING_ATTACKLINE_OK_TT_ATT,
	NOT_INTERESTING_ATTACKLINE_OK_ATT,
	INTERESTING_NOT_ATTACKLINE_OK_ATT,
	NOT_INTERESTING_NOT_ATTACKLINE_OK_ATT,
	COUNTER_ATTACK_REGULAR_DEFENSE_ATT,
	END_ATT
};

enum
{
	START_DEF,
	TT_DEF,
	DOUBLE_FOUR_DEF,
	FOUR_THREE_DEF,
	FIGHT_BACK_REGULAR_DEF,
	FREE_FOUR_DEF,
	REST_REGULAR_DEF,
	REST_FT_DEF,
	END_DEF
};

enum
{
	START_FOURWIN,
	DOUBLE_FOUR_FOURWIN,
	FOUR_THREE_FOURWIN,
	INTERESTING_FOURWIN,
	INTERESTING_ATTACKLINE_OK_FOURWIN,
	NOT_INTERESTING_ATTACKLINE_OK_FOURWIN,
	INTERESTING_NOT_ATTACKLINE_OK_FOURWIN,
	NOT_INTERESTING_NOT_ATTACKLINE_OK_FOURWIN,
	END_FOURWIN
};

#define ATTACKLINE_PENALITY 4 //should be an even 2k number, if it happens than this line will be searchd with reduced by k depth
#define INTERESTING_PENALITY 6 //same as above
#define YOUNG_LIMIT (300)

class Picker 
{
public:
	Picker(Position *);
	
	table regular;
	table ft;
	table legal;
	table sent;
	table defender_threat;
	table passive[2];
	StateInfo*	help;
	
	int stage;
	int stage_curr;
	int counter;
	int info;
	uint16_t tt_move;
	bool is_extreme_pos;
	
	table picker_defense(Position *);
	table picker(Position *);
};

class Picker_surewin 
{
public:
	Picker_surewin(Position *);
	
	table regular;
	table ft;
	table legal;
	table sent;
	table defender_threat;
	table interesting[4];
	uint16_t tt_move;
	
	int stage;
	int stage_curr;
	bool is_extreme_pos;
	uint8_t depth_left;
	table attackline;
	
	table picker_surewin_attack(Position *);
	table picker_surewin_defense(Position *);
};

class Picker_fourwin
{
public:
	Picker_fourwin(Position *);
	
	table legal;
	table sent;
	table interesting[4];
	
	int stage;
	int stage_curr;
	uint8_t depth_left;
	table attackline;
	
	table picker_fourwin(Position *);
};


void print_picker_regular(Position *, int);












