#ifndef POSITION_H_INCLUDED
#define POSITION_H_INCLUDED

#include "types.h"
#include <assert.h>
#include <string.h>

#define WINNING_MOVE 1
#define NO_LEGAL_MOVE -1
#define END_OBJECT -1
#define MOVE_NONE (TSIZE)

#define BOARDS 19 // MAX 22 lehet
#define EMPTY 0
#define O_TURN 0
#define X_TURN 1
#define DIRECTION_SUREWIN(dir) (direction[dir])
#define DIRECTION_SIGNED(dir) (direction_signed[dir])
#define DIRECTION_NB 4

#define E_SUREWIN 1
#define S_SUREWIN (BOARDS)
#define NE_SUREWIN (- BOARDS + 1)
#define SE_SUREWIN (BOARDS + 1)

#define TSIZE (BOARDS * BOARDS)

#define EXPAND(a, b) (a.t[b >> 6] |= (1ULL << (b - ((b >> 6) << 6))))
#define REDUCE(a, b) (a.t[b >> 6] ^= (1ULL << (b - ((b >> 6) << 6))))

static const int direction[4] = {
   E_SUREWIN, S_SUREWIN, NE_SUREWIN, SE_SUREWIN
};

static const int direction_signed[8] = {
   E_SUREWIN, S_SUREWIN, NE_SUREWIN, SE_SUREWIN, -E_SUREWIN, -S_SUREWIN, -NE_SUREWIN, -SE_SUREWIN 
};

struct table
{
	uint64_t t[8];
	
	inline void null()
	{
		this -> t[0] = this -> t[1] = this -> t[2] = this -> t[3] = this -> t[4] = this -> t[5] = this -> t[6] = this -> t[7] = 0; 
	}	
	inline bool operator!()
	{
		if (this -> t[0] != 0 || this -> t[1] != 0 || this -> t[2] != 0 || this -> t[3] != 0 || this -> t[4] != 0 || this -> t[5] != 0 || this -> t[6] != 0 || this -> t[7] != 0)
		{
			return true;
		}
		return false;
	}
	inline bool operator==(const table& rhs)
	{
		if (this -> t[0] == rhs.t[0] && this -> t[1] == rhs.t[1] && this -> t[2] == rhs.t[2] && this -> t[3] == rhs.t[3] && this -> t[4] == rhs.t[4] && this -> t[5] == rhs.t[5] && this -> t[6] == rhs.t[6] && this -> t[7] == rhs.t[7])
		{
			return true;
		}
		return false;
	}
	inline bool operator!=(const table& rhs)
	{
		if (this -> t[0] == rhs.t[0] && this -> t[1] == rhs.t[1] && this -> t[2] == rhs.t[2] && this -> t[3] == rhs.t[3] && this -> t[4] == rhs.t[4] && this -> t[5] == rhs.t[5] && this -> t[6] == rhs.t[6] && this -> t[7] == rhs.t[7])
		{
			return false;
		}
		return true;
	}				
	inline table operator|=(const table& rhs)
	{
		this -> t[0] |= rhs.t[0];
		this -> t[1] |= rhs.t[1];
		this -> t[2] |= rhs.t[2];
		this -> t[3] |= rhs.t[3];
		this -> t[4] |= rhs.t[4];
		this -> t[5] |= rhs.t[5];
		this -> t[6] |= rhs.t[6];
		this -> t[7] |= rhs.t[7];
		return * this;
	}
	inline table operator&=(const table& rhs)
	{
		this -> t[0] &= rhs.t[0];
		this -> t[1] &= rhs.t[1];
		this -> t[2] &= rhs.t[2];
		this -> t[3] &= rhs.t[3];
		this -> t[4] &= rhs.t[4];
		this -> t[5] &= rhs.t[5];
		this -> t[6] &= rhs.t[6];
		this -> t[7] &= rhs.t[7];
		return * this;
	}
	inline table operator~()
	{
		table ret;
		ret.t[0] = (~(this->t[0]));
		ret.t[1] = (~(this->t[1]));
		ret.t[2] = (~(this->t[2]));
		ret.t[3] = (~(this->t[3]));
		ret.t[4] = (~(this->t[4]));
		ret.t[5] = (~(this->t[5]));
		ret.t[6] = (~(this->t[6]));
		ret.t[7] = (~(this->t[7]));
		return ret;
	}
	inline table operator&(const table& rhs)
	{
		table ret;
		ret.t[0] = (this -> t[0] & rhs.t[0]);
		ret.t[1] = (this -> t[1] & rhs.t[1]);
		ret.t[2] = (this -> t[2] & rhs.t[2]);
		ret.t[3] = (this -> t[3] & rhs.t[3]);
		ret.t[4] = (this -> t[4] & rhs.t[4]);
		ret.t[5] = (this -> t[5] & rhs.t[5]);
		ret.t[6] = (this -> t[6] & rhs.t[6]);
		ret.t[7] = (this -> t[7] & rhs.t[7]);
		return ret;
	}
	inline table operator|(const table& rhs)
	{
		table ret;
		ret.t[0] = (this -> t[0] | rhs.t[0]);
		ret.t[1] = (this -> t[1] | rhs.t[1]);
		ret.t[2] = (this -> t[2] | rhs.t[2]);
		ret.t[3] = (this -> t[3] | rhs.t[3]);
		ret.t[4] = (this -> t[4] | rhs.t[4]);
		ret.t[5] = (this -> t[5] | rhs.t[5]);
		ret.t[6] = (this -> t[6] | rhs.t[6]);
		ret.t[7] = (this -> t[7] | rhs.t[7]);
		return ret;
	}
};


struct StateInfo {
  Key key;
  StateInfo* previous;
  int move;
  table threat[2][3][4]; // [0 == O, 1 == X][0 == ff, 1 == ft, 2 == tt][direction]
  table threat_type_unio[2][2]; //ff is not here [0 == O, 1 == X][0 == ft, 1 == tt]
  table threat_unio[2]; // ff is not here!!! [0 == O, 1 == X]
  int five_threat;
  int eval;
};

class Position 
{
public:
	Position() {}
	
	void init();
	static void initGlobals();
	void print_board_extended();
	void print_board();
  
	// Doing and undoing moves
	int do_move(int m, StateInfo& st);
	void undo_move();
  
	// Updateing
	static int is_there_a_three (uint32_t line, uint32_t border, int index, int rule);
	static int is_there_a_four (uint32_t line, uint32_t border, int index, int rule);
	static int is_there_a_free_four (uint32_t line, uint32_t border, int index, int rule);
	static int is_there_a_five (uint32_t line, uint32_t border, int index, int rule);
	static int code_enviroment(uint32_t line, uint32_t border, int index);
	void generate_threat_unio();
	void generate_threat_unio(int);

	// Generate legal move 
	void generate_legal(table *);
	void generate_regular(table *);
	void generate_ft(table *);
	table generate_single_regular_fast(int, int, int, int);
	void generate_local_bit(int, int, int, uint32_t *, uint32_t *, int *); //works only if linear bits are OK
	

	// Accessing hash keys
	Key key() const;
	Key exclusion_key() const;
	Key null_move_key() const;	
	Key compute_key() const;

	table square[2]; // [0 == O, 1 == X]
	uint32_t linear_bit[2][DIRECTION_NB][BOARDS + BOARDS - 1]; //[0 == O, 1 == X][0 == E, 1 == N, 2 == NE, 3 == NW][NUMBER_OF_SET]
	int turn_glob;
	int five_threat;
	int surewin_search;
	int surewin_win;
	int ft_net_call;

	int update_attack(int, StateInfo&);
	table why_win(int);
	int five_threat_dir(int);
	void old_attack(int);
	void update_defense(int);
	void clearing(int);
	
	// Other info
	bool is_extreme_pos();
	long int nodes;
	StateInfo* st;
};

inline Key Position::key() const {
  return st->key;
}

const int index64[64] = 
{
	0, 47,  1, 56, 48, 27,  2, 60,
	57, 49, 41, 37, 28, 16,  3, 61,
	54, 58, 35, 52, 50, 42, 21, 44,
	38, 32, 29, 23, 17, 11,  4, 62,
	46, 55, 26, 59, 40, 36, 15, 53,
	34, 51, 20, 43, 31, 22, 10, 45,
	25, 39, 14, 33, 19, 30,  9, 24,
	13, 18,  8, 12,  7,  6,  5, 63
};

void print_general(table);
void print_general(Position *, table);
void history(Position *, int *);

inline int bitScanForward(table bb) 
{
	assert (!bb);
	if (bb.t[0] != 0)
	{
		return index64[((bb.t[0] ^ (bb.t[0] - 1)) * (0x03f79d71b4cb0a89)) >> 58];
	}
	if (bb.t[1] != 0)
	{
		return index64[((bb.t[1] ^ (bb.t[1] - 1)) * (0x03f79d71b4cb0a89)) >> 58] + 64;
	}
	if (bb.t[2] != 0)
	{
		return index64[((bb.t[2] ^ (bb.t[2] - 1)) * (0x03f79d71b4cb0a89)) >> 58] + 128;
	}
	if (bb.t[3] != 0)
	{
		return index64[((bb.t[3] ^ (bb.t[3] - 1)) * (0x03f79d71b4cb0a89)) >> 58] + 192;
	}
	if (bb.t[4] != 0)
	{
		return index64[((bb.t[4] ^ (bb.t[4] - 1)) * (0x03f79d71b4cb0a89)) >> 58] + 256;
	}
	if (bb.t[5] != 0)
	{
		return index64[((bb.t[5] ^ (bb.t[5] - 1)) * (0x03f79d71b4cb0a89)) >> 58] + 320;
	}
	if (bb.t[6] != 0)
	{
		return index64[((bb.t[6] ^ (bb.t[6] - 1)) * (0x03f79d71b4cb0a89)) >> 58] + 384;
	}
	if (bb.t[7] != 0)
	{
		return index64[((bb.t[7] ^ (bb.t[7] - 1)) * (0x03f79d71b4cb0a89)) >> 58] + 448;
	}
	return -1;
}

extern uint32_t border_bit[DIRECTION_NB][BOARDS + BOARDS - 1];
extern uint32_t what_to_do_global[59049]; //3^10 [enviroment] an element means [4][4][4][4][2][2][2][2] first threat rel pos, ..., fourth threat rel pos, first threat type, ... fourth threat type;
extern uint32_t is_interesting_global[59049]; // almost the same as what_to_do_global if IS_SIX_WINS == 1
extern uint8_t threat_global[59049]; //[enviroment]
extern table neighbourhood_3[TSIZE][4]; //[field][direction] field is not in the table
extern table neighbourhood_4[TSIZE][4]; //[field][direction] field is not in the table
extern table neighbourhood_5[TSIZE][4]; //[field][direction] field is not in the table
extern table distance[3][TSIZE]; //[distance][field] 0 == neighbouring to field etc. field is in the table.
extern table side_distance[2]; //the set of passivemoves per dcef if index = 0 OS if 1 O.S


#endif // #ifndef POSITION_H_INCLUDED
