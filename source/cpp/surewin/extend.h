#include "position.h"

#define ATTACKLINE_PENALITY 4 //should be an even 2k number, if it happens than this line will be searchd with reduced by k depth
#define INTERESTING_PENALITY 6 //same as above

class Picker_extend
{
public:
	Picker_extend(Position *);
	
	table regular;
	table ft;
	table legal;
	table sent;
	table defender_threat;
	table interesting[4];
	int winning_sequence[TSIZE];
	int ply;	
	uint16_t tt_move;
	
	int stage;
	int stage_curr;
	bool is_extreme_pos;
	uint8_t depth_left;
	table attackline;
	
	table picker_surewin_extend_attack(Position * p);
	table picker_surewin_extend_defense(Position * p);
};

enum
{
	START_EXTEND,
	TT_EXTEND,
	DOUBLE_FOUR_EXTEND,
	FOUR_THREE_EXTEND,
	INIT_EXTEND,
	DOUBLE_THREE_EXTEND,
	INTERESTING_EXTEND,
	INTERESTING_ATTACKLINE_OK_FT_EXTEND,
	INTERESTING_ATTACKLINE_OK_TT_EXTEND,
	NOT_INTERESTING_ATTACKLINE_OK_EXTEND,
	INTERESTING_NOT_ATTACKLINE_OK_EXTEND,
	NOT_INTERESTING_NOT_ATTACKLINE_OK_EXTEND,	
	COUNTER_ATTACK_REGULAR_DEFENSE_EXTEND,
	EXTEND,
	END_EXTEND
};

enum
{
	START_DEF_EXTEND,
	TT_DEF_EXTEND,
	DOUBLE_FOUR_DEF_EXTEND,
	FOUR_THREE_DEF_EXTEND,
	EXTEND_DEF_REGULAR,
	EXTEND_DEF_IRREGULAR,
	EXTEND_DEF_REST,
	FIGHT_BACK_REGULAR_DEF_EXTEND,
	FREE_FOUR_DEF_EXTEND,
	REST_REGULAR_DEF_EXTEND,
	REST_FT_DEF_EXTEND,
	END_DEF_EXTEND
};

int surewin_extend_attack(Position *, int8_t, uint8_t, table, bool);
int surewin_extend_defense(Position *, int8_t, uint8_t, table, bool);
