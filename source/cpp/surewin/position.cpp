#include <stdio.h>
#include "position.h"
#include "rkiss.h" 

uint32_t border_bit[DIRECTION_NB][BOARDS + BOARDS - 1];
int three_base_convert[1024];
uint32_t what_to_do_global[59049]; //3^10 [enviroment] an element means [4][4][4][4][2][2][2][2] first threat rel pos, ..., fourth threat rel pos, first threat type, ... fourth threat type;
uint32_t is_interesting_global[59049]; // almost the same as what_to_do_global if IS_SIX_WINS == 1
uint8_t threat_global[59049]; //[enviroment]
table neighbourhood_3[TSIZE][4]; //[field][direction] field is not in the table
table neighbourhood_4[TSIZE][4]; //[field][direction] field is not in the table
table neighbourhood_5[TSIZE][4]; //[field][direction] field is not in the table
table distance[3][TSIZE]; //[distance][field] 0 == neighbouring to field etc. field is in the table.

table table_indicator;
table side_distance[2]; //the set of passivemoves per dcef if index = 0 OS if 1 O.S

namespace Zobrist 
{
	Key table_code[3][BOARDS*BOARDS];
	Key exclusion;
	Key null_move;
}

Key Position::exclusion_key() const 
{ 
	return st->key ^ Zobrist::exclusion;
}

Key Position::null_move_key() const 
{ 
	return st->key ^ Zobrist::null_move;
}

Key Position::compute_key() const 
{
	int i, j;
	Key k = 0;

	for (i = 0; i < TSIZE; i++)
	{
		if ((square[0].t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			j = 1;
		}
		else
		{
			if ((square[1].t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
			{
				j = 2;
			}
			else
			{
				j = 0;
			}
		}
		k ^= Zobrist::table_code[j][i];
	}	
	//k^= Zobrist::exclusion;
	return k;
}

/*void print_general(uint32_t line, uint32_t border)
{
	int i;
	
	for (i = 0; i < 32; i++)
	{
		if (line & (1ULL << i))
		{
			printf("O");
			continue;
		}
		if (border & (1ULL << i))
		{
			printf("X");
			continue;
		}
		printf(".");
	}
	printf("\n");
}

void print_general(uint32_t line)
{
	int i;
	
	for (i = 0; i < 32; i++)
	{
		if (line & (1ULL << i))
		{
			printf("1");
			continue;
		}
		printf("0");
	}
	printf("\n");
}

void print_general(table s)
{
	int i;
	
	for (i = 0; i < TSIZE; i++)
	{
		if ((s.t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			printf("1 ");
			if (i % BOARDS == BOARDS - 1)
			{
				printf("\n");
			}
			continue;
		}
		printf("0 ");
		if (i % BOARDS == BOARDS - 1)
		{
			printf("\n");
		}
	}
	printf("\n");
}

void print_general(Position * p, table s)
{	
	int i;
	
	for (i = 0; i < TSIZE; i++)
	{
		if ((p -> square[0].t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			printf("O ");
			if (i % BOARDS == BOARDS - 1)
			{
				printf("\n");
			}
			continue;
		}
		if ((p -> square[1].t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			printf("X ");
			if (i % BOARDS == BOARDS - 1)
			{
				printf("\n");
			}
			continue;
		}
		if ((s.t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			printf("* ");
			if (i % BOARDS == BOARDS - 1)
			{
				printf("\n");
			}
			continue;
		}
		printf(". ");
		if (i % BOARDS == BOARDS - 1)
		{
			printf("\n");
		}
	}
	if (p -> turn_glob == 0)
	{
		printf("O\n");
	}
	if (p -> turn_glob == 1)
	{
		printf("X\n");
	}	
}*/

void Position::init() //it initialize the position globals, and make a clear board 
{
	int k;
	uint32_t i, j;
	RKISS rk;

	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < TSIZE; j++)
		{
			Zobrist::table_code[i][j] = rk.rand<Key>();
		}
	}
	Zobrist::exclusion = rk.rand<Key>();
	Zobrist::null_move = rk.rand<Key>();

	square[0].null();
	square[1].null();
	turn_glob = O_TURN;
	five_threat = END_OBJECT;

	for (i = 0; i < 2; i++)
	{
		for (j = 0; j < DIRECTION_NB; j++)
		{
			for (k = 0; k < BOARDS + BOARDS - 1; k++)
			{
				linear_bit[i][j][k] = 0;
			}
		}
	}
	//init st
	st = (StateInfo*)malloc(sizeof(StateInfo));
	st->key = 0;
	for (i = 0; i < TSIZE; i++)
	{
		if ((square[0].t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			j = 1;
		}
		else
		{
			if ((square[1].t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
			{
				j = 2;
			}
			else
			{
				j = 0;
			}
		}
		st->key ^= Zobrist::table_code[j][i];
	}
	for (i = 0; i < 2; i++)
	{
		for (j = 0; j < 3; j++)
		{
			for (k = 0; k < 4; k++)
			{
				st->threat[i][j][k].null();
			}
		}
	}
	st->five_threat = END_OBJECT;
	st->move = END_OBJECT;
}

void Position::initGlobals()
{
	int res, i_help, j_help, k, l;
	uint32_t i, j, dir;
	uint32_t line, empty, border;

	for (i = 0; i < DIRECTION_NB; i++)
	{
		for (j = 0; j < BOARDS + BOARDS - 1; j++)
		{
			border_bit[i][j] = 0;			
			if (i == 0 || i == 1)
			{				
				for (k = 0; k < 32; k++)
				{
					if ((k < 5) || (5 + BOARDS <= k))
					{
						border_bit[i][j] |= (1ULL << k);
					}
				}
			}
			if (i == 2 || i == 3)
			{
				for (k = 0; k < 32; k++)
				{
					if (j <= BOARDS - 1)
					{
						if ((k < 5) || (5 + j + 1 <= k))
						{
							border_bit[i][j] |= (1ULL << k);
						}
					}
					else
					{
						if ((k < 5 + j - (BOARDS - 1)) || ((BOARDS + 5) <= k))
						{
							border_bit[i][j] |= (1ULL << k);
						}					
					}
				}				
			}
		}
	}
	for (i = 0; i < 1024; i++)
	{
		res = 0;
		k = 1;
		for (j = 0; j < 10; j++)
		{
			if (i & (1 << j))
			{
				res += k;
			}
			k *= 3;
		}
		three_base_convert[i] = res;
	}
	for (i = 0; i < 59049; i++)
	{
		i_help = i;
		line = border = 0;
		for (j = 0; j < 11; j++)
		{
			if (j == 5)
			{
				continue;
			}
			if ((i_help % 3) == 0)
			{
				empty |= (1 << j);
			}
			if ((i_help % 3) == 1)
			{
				line |= (1 << j);
			}
			if ((i_help % 3) == 2)
			{
				border |= (1 << j);
			}
			i_help /= 3;			
		}
		if (is_there_a_five(line, border, 5, IS_SIX_WINS))
		{
			threat_global[i] = 3;
			continue;
		}
		if (is_there_a_free_four(line, border, 5, IS_SIX_WINS))
		{
			threat_global[i] = 0;
			continue;
		}
		if (is_there_a_four(line, border, 5, IS_SIX_WINS))
		{
			threat_global[i] = 1;
			continue;
		}
		if (is_there_a_three(line, border, 5, IS_SIX_WINS))
		{
			threat_global[i] = 2;
			continue;
		}
		threat_global[i] = 4;
	}
	for (i = 0; i < 59049; i++)
	{
		i_help = i;
		line = empty = border = 0;
		for (j = 0; j < 11; j++)
		{
			if (j == 5)
			{
				continue;
			}
			if ((i_help % 3) == 0)
			{
				empty |= (1 << j);
			}
			if ((i_help % 3) == 1)
			{
				line |= (1 << j);
			}
			if ((i_help % 3) == 2)
			{
				border |= (1 << j);
			}
			i_help /= 3;			
		}
		line |= (1 << 5);
		line = (line << 5);
		border = (border << 5);
		border |= (1 << 4);
		border |= (1 << 16);
		empty = (~(line  | border));
		
		what_to_do_global[i] = 0;
		k = 0;
		for (j = 11; j < 15; j++)
		{
			if ((border & (1 << j)) || k == 2)
			{
				break;
			}
			if (empty & (1 << j))
			{
				k++;
				if (is_there_a_five(line, border, j, IS_SIX_WINS))
				{
					what_to_do_global[i] |= ((j - 10) << ((k - 1) * 8));
					what_to_do_global[i] |= (3 << (16 + (k - 1) * 4));
					continue;
				}
				if (is_there_a_free_four(line, border, j, IS_SIX_WINS))
				{
					what_to_do_global[i] |= ((j - 10) << ((k - 1) * 8));
					what_to_do_global[i] |= (0 << (16 + (k - 1) * 4));
					continue;
				}
				if (is_there_a_four(line, border, j, IS_SIX_WINS))
				{
					what_to_do_global[i] |= ((j - 10) << ((k - 1) * 8));
					what_to_do_global[i] |= (1 << (16 + (k - 1) * 4));
					continue;
				}
				if (is_there_a_three(line, border, j, IS_SIX_WINS))
				{
					what_to_do_global[i] |= ((j - 10) << ((k - 1) * 8));
					what_to_do_global[i] |= (2 << (16 + (k - 1) * 4));
					continue;
				}				
			}
		}
		k = 0;
		for (j = 9; j > 5; j--)
		{
			if ((border & (1 << j)) || k == 2)
			{
				break;
			}
			if (empty & (1 << j))
			{
				k++;
				if (is_there_a_five(line, border, j, IS_SIX_WINS))
				{
					what_to_do_global[i] |= ((18 - j) << (((k - 1) * 8) + 4));
					what_to_do_global[i] |= (3 << (18 + (k - 1) * 4));
					continue;
				}
				if (is_there_a_free_four(line, border, j, IS_SIX_WINS))
				{
					what_to_do_global[i] |= ((18 - j) << (((k - 1) * 8) + 4));
					what_to_do_global[i] |= (0 << (18 + (k - 1) * 4));
					continue;
				}
				if (is_there_a_four(line, border, j, IS_SIX_WINS))
				{
					what_to_do_global[i] |= ((18 - j) << (((k - 1) * 8) + 4));
					what_to_do_global[i] |= (1 << (18 + (k - 1) * 4));
					continue;
				}
				if (is_there_a_three(line, border, j, IS_SIX_WINS))
				{				
					what_to_do_global[i] |= ((18 - j) << (((k - 1) * 8) + 4));
					what_to_do_global[i] |= (2 << (18 + (k - 1) * 4));
					continue;
				}				
			}
		}
	}
	for (i = 0; i < 59049; i++)
	{
		i_help = i;
		line = empty = border = 0;
		for (j = 0; j < 11; j++)
		{
			if (j == 5)
			{
				continue;
			}
			if ((i_help % 3) == 0)
			{
				empty |= (1 << j);
			}
			if ((i_help % 3) == 1)
			{
				line |= (1 << j);
			}
			if ((i_help % 3) == 2)
			{
				border |= (1 << j);
			}
			i_help /= 3;			
		}
		line |= (1 << 5);
		line = (line << 5);
		border = (border << 5);
		border |= (1 << 4);
		border |= (1 << 16);
		empty = (~(line  | border));
		
		is_interesting_global[i] = 0;
		
		i_help = 0;
		for (j = 11; j < 15; j++)
		{
			if (line & (1 << j))
			{
				i_help++;
			}
			if (border & (1 << j))
			{
				break;
			}		
		}
		for (j = 9; j > 5; j--)
		{
			if (line & (1 << j))
			{
				i_help++;
			}
			if (border & (1 << j))
			{
				break;
			}		
		}
		if (i_help >= 4)
		{
			is_interesting_global[i] = 1;
			continue;
		}
		
		k = 0;
		for (j = 11; j < 15; j++)
		{
			if ((border & (1 << j)) || k == 2)
			{
				break;
			}
			if (empty & (1 << j))
			{
				k++;
				if (is_there_a_five(line, border, j, 1))
				{
					is_interesting_global[i] |= ((j - 10) << ((k - 1) * 8));
					is_interesting_global[i] |= (3 << (16 + (k - 1) * 4));
					continue;
				}
				if (is_there_a_free_four(line, border, j, 1))
				{
					is_interesting_global[i] |= ((j - 10) << ((k - 1) * 8));
					is_interesting_global[i] |= (0 << (16 + (k - 1) * 4));
					continue;
				}
				if (is_there_a_four(line, border, j, 1))
				{
					is_interesting_global[i] |= ((j - 10) << ((k - 1) * 8));
					is_interesting_global[i] |= (1 << (16 + (k - 1) * 4));
					continue;
				}
				if (is_there_a_three(line, border, j, 1))
				{
					is_interesting_global[i] |= ((j - 10) << ((k - 1) * 8));
					is_interesting_global[i] |= (2 << (16 + (k - 1) * 4));
					continue;
				}				
			}
		}
		k = 0;
		for (j = 9; j > 5; j--)
		{
			if ((border & (1 << j)) || k == 2)
			{
				break;
			}
			if (empty & (1 << j))
			{
				k++;
				if (is_there_a_five(line, border, j, 1))
				{
					is_interesting_global[i] |= ((18 - j) << (((k - 1) * 8) + 4));
					is_interesting_global[i] |= (3 << (18 + (k - 1) * 4));
					continue;
				}
				if (is_there_a_free_four(line, border, j, 1))
				{
					is_interesting_global[i] |= ((18 - j) << (((k - 1) * 8) + 4));
					is_interesting_global[i] |= (0 << (18 + (k - 1) * 4));
					continue;
				}
				if (is_there_a_four(line, border, j, 1))
				{
					is_interesting_global[i] |= ((18 - j) << (((k - 1) * 8) + 4));
					is_interesting_global[i] |= (1 << (18 + (k - 1) * 4));
					continue;
				}
				if (is_there_a_three(line, border, j, 1))
				{				
					is_interesting_global[i] |= ((18 - j) << (((k - 1) * 8) + 4));
					is_interesting_global[i] |= (2 << (18 + (k - 1) * 4));
					continue;
				}				
			}
		}
	}
	//neighbour init
	for (i = 0; i < TSIZE; i++)
	{	
		for (dir = 0; dir < 8; dir++)
		{
			if (dir < 4)
			{
				neighbourhood_3[i][dir].null();
				neighbourhood_4[i][dir].null();
				neighbourhood_5[i][dir].null();
			}
			j = i;
			for (k = 0; k < 3; k++)
			{
				if ((j + DIRECTION_SIGNED(dir) < TSIZE) && (int(DIRECTION_SIGNED(dir) + j) >= 0) && !((j % BOARDS == 0 && ((j + DIRECTION_SIGNED(dir)) % BOARDS) == BOARDS - 1) || (j % BOARDS == BOARDS - 1 && ((j + DIRECTION_SIGNED(dir)) % BOARDS) == 0)))
				{
					neighbourhood_3[i][dir % 4].t[(j + DIRECTION_SIGNED(dir)) >> 6] |= (1ULL << ((j + DIRECTION_SIGNED(dir)) - (((j + DIRECTION_SIGNED(dir)) >> 6) << 6)));
					j += DIRECTION_SIGNED(dir);
				}
				else
				{
					break;
				}
			}
			j = i;
			for (k = 0; k < 4; k++)
			{
				if ((j + DIRECTION_SIGNED(dir) < TSIZE) && (int(j + DIRECTION_SIGNED(dir)) >= 0) && !((j % BOARDS == 0 && ((j + DIRECTION_SIGNED(dir)) % BOARDS) == BOARDS - 1) || (j % BOARDS == BOARDS - 1 && ((j + DIRECTION_SIGNED(dir)) % BOARDS) == 0)))
				{
					neighbourhood_4[i][dir % 4].t[(j + DIRECTION_SIGNED(dir)) >> 6] |= (1ULL << ((j + DIRECTION_SIGNED(dir)) - (((j + DIRECTION_SIGNED(dir)) >> 6) << 6)));
					j += DIRECTION_SIGNED(dir);
				}
				else
				{
					break;
				}
			}
			j = i;
			for (k = 0; k < 5; k++)
			{
				if ((j + DIRECTION_SIGNED(dir) < TSIZE) && (int(j + DIRECTION_SIGNED(dir)) >= 0) && !((j % BOARDS == 0 && ((j + DIRECTION_SIGNED(dir)) % BOARDS) == BOARDS - 1) || ((j % BOARDS) == BOARDS - 1 && ((j + DIRECTION_SIGNED(dir)) % BOARDS) == 0)))
				{
					neighbourhood_5[i][dir % 4].t[(j + DIRECTION_SIGNED(dir)) >> 6] |= (1ULL << ((j + DIRECTION_SIGNED(dir)) - (((j + DIRECTION_SIGNED(dir)) >> 6) << 6)));
					j += DIRECTION_SIGNED(dir);
				}
				else
				{
					break;
				}
			}			
		}				
	}
	// distance init
	for (i_help = 0; i_help < 3; i_help++)
	{	
		for (j_help = 0; j_help < TSIZE; j_help++)
		{
			distance[i_help][j_help].null();
			for (k = MAX(-(j_help / BOARDS), - (i_help + 1)); k <= MIN((TSIZE - 1 - j_help)/BOARDS , i_help + 1); k++)
			{
				for (l = MAX(-(j_help % BOARDS), - (i_help + 1)); l <= MIN(((BOARDS - 1) - (j_help % BOARDS)), i_help + 1) ; l++)
				{
					//printf("%d", (j_help + k * BOARDS + l));
					//getchar();
					distance[i_help][j_help].t[(j_help + k * BOARDS + l) >> 6] |= (1ULL << ((((j_help + k * BOARDS + l) - (((j_help + k * BOARDS + l) >> 6) << 6)))));
				}
			} 
		}		
	}	
	//side distance init
	side_distance[0].null();
	side_distance[1].null();
	for (i = 0; i < 2; i++)
	{
		for (j = 0; j < TSIZE; j++)
		{
			if (((j / BOARDS) == i) || ((j % BOARDS) == i) || ((j % BOARDS) == (BOARDS - 1 - i)) || ((j / BOARDS) == BOARDS - 1 - i))
			{ 
				EXPAND(side_distance[i], j);
			}
		}
	}
	side_distance[1] = (side_distance[1] & (~side_distance[0]));
		
	// table indicator.null();
	table_indicator.null();
	for (i = 0; i < TSIZE; i++)
	{
		table_indicator.t[i >> 6] |= (1ULL << (i - ((i >> 6) << 6)));
	}		
}

void Position::print_board()
{
	int i;
	for (i = 0; i < TSIZE; i++)
	{
		if ((square[0].t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			printf("O ");
			if (i % BOARDS == BOARDS - 1)
			{
				printf("\n");
			}
			continue;
		}
		if ((square[1].t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			printf("X ");
			if (i % BOARDS == BOARDS - 1)
			{
				printf("\n");
			}
			continue;
		}
		printf(". ");
		if (i % BOARDS == BOARDS - 1)
		{
			printf("\n");
		}
	}
	if (turn_glob == 0)
	{
		printf("O\n");
	}
	if (turn_glob == 1)
	{
		printf("X\n");
	}
}

void Position::print_board_extended()
{
	int i, j, dir, sq;
	table loop;
	for (i = 0; i < TSIZE; i++)
	{
		if ((square[0].t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			printf("O ");
			if (i % BOARDS == BOARDS - 1)
			{
				printf("\n");
			}
			continue;
		}
		if ((square[1].t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			printf("X ");
			if (i % BOARDS == BOARDS - 1)
			{
				printf("\n");
			}
			continue;
		}
		printf(". ");
		if (i % BOARDS == BOARDS - 1)
		{
			printf("\n");
		}
	}
	for (i = 0; i < 2; i++)
	{
		for (j = 0; j < 3; j++)
		{
			if (i == 0 && j == 0)
			{
				printf("O ff: ");
			}
			if (i == 0 && j == 1)
			{
				printf("O ft: ");
			}
			if (i == 0 && j == 2)
			{
				printf("O tt: ");
			}
			if (i == 1 && j == 0)
			{
				printf("X ff: ");
			}
			if (i == 1 && j == 1)
			{
				printf("X ft: ");
			}
			if (i == 1 && j == 2)
			{
				printf("X tt: ");
			}
			for (dir = 0; dir < 4; dir++)
			{
				loop = st -> threat[i][j][dir];
				while (!loop)
				{
					sq = bitScanForward(loop);
					printf("%d ", sq);
					loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
				}
			}																
			printf("\n");
		}
	}
	
	printf("Five: %d\n", five_threat);
	if (turn_glob==0)
	{
		printf("O\n");
	}
	if (turn_glob==1)
	{
		printf("X\n");
	}
}

int Position::do_move(int move, StateInfo& newSt)
{
	nodes++;
	if (update_attack(move, newSt) == WINNING_MOVE)
	{
		return WINNING_MOVE;
	}
	old_attack(move);
	update_defense(move);
	clearing(move);
	generate_threat_unio();
	generate_threat_unio(turn_glob ^ 1); 	
	return 0;
}

int Position::update_attack(int move, StateInfo& newSt)
{
	int move_x, move_y;
	int dir;
	uint32_t border;
	uint32_t line[DIRECTION_NB];
	uint32_t what_to_do;
	int index, pre_index;

	assert (move >= 0 && move < TSIZE);
	assert (((square[turn_glob].t[move >> 6] & (1ULL << (move - ((move >> 6) << 6)))) == 0) && ((square[turn_glob ^ 1].t[move >> 6] & (1ULL << (move - ((move >> 6) << 6)))) == 0));

	newSt.previous = st;
	st = &newSt;
	
	square[turn_glob].t[move >> 6] |= (1ULL << (move - ((move >> 6) << 6)));
	move_x = move / BOARDS;
	move_y = move % BOARDS;
	line[0] = (linear_bit[turn_glob][0][move_x] |= (1 << (move_y + 5)));
	line[1] = (linear_bit[turn_glob][1][move_y] |= (1 << (move_x + 5)));
	line[2] = (linear_bit[turn_glob][2][move_x + move_y] |= (1 << (move_y + 5)));
	line[3] = (linear_bit[turn_glob][3][move_y - move_x + BOARDS - 1] |= (1 << (move_y + 5)));
	
	
	memset(st -> threat, 0, sizeof(table) * 24);						
	st->key = ((newSt.previous->key)^Zobrist::table_code[0][move])^Zobrist::table_code[turn_glob+1][move];
	five_threat = END_OBJECT;
	//attack
	//local things
	for (dir = 0; dir < 4; dir++)
	{
		if (dir != 1)
		{
			index = move_y + 5;
		}
		else
		{
			index = move_x + 5;
		}
		if ((line[dir] & (119 << (index - 3)))) // three_radius = 119 = 1 + 2 + 4 + 16 + 32 + 64 = 1110111
		{
			if (dir == 0)
			{
				border = (border_bit[0][move_x] | linear_bit[turn_glob ^ 1][0][move_x]);	
			}
			if (dir == 1)
			{
				border = (border_bit[1][move_y] | linear_bit[turn_glob ^ 1][1][move_y]);	
			}
			if (dir == 2)
			{
				border = (border_bit[2][move_x + move_y] | linear_bit[turn_glob ^ 1][2][move_x + move_y]);
			}
			if (dir == 3)
			{
				border = (border_bit[3][move_y - move_x + BOARDS - 1] | linear_bit[turn_glob ^ 1][3][move_y - move_x + BOARDS - 1]);		
			}
			what_to_do = what_to_do_global[code_enviroment(line[dir], border, index)];
		
			if ((pre_index = (what_to_do & 15)))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				if (((what_to_do & (3 << 16)) >> 16) == 3) // five_threat
				{
					if (five_threat != END_OBJECT)
					{
						turn_glob ^= 1;	
						st->move=move;	
						return WINNING_MOVE;							
					}
					five_threat = move + (pre_index * DIRECTION_SUREWIN(dir));
				}
				else
				{
					st -> threat[turn_glob][((what_to_do & (3 << 16)) >> 16)][dir].t[((move + (pre_index * DIRECTION_SUREWIN(dir))) >> 6)] |= (1ULL << ((move + (pre_index * DIRECTION_SUREWIN(dir))) - (((move + (pre_index * DIRECTION_SUREWIN(dir))) >> 6) << 6)));
				}
			}
			if ((pre_index = ((what_to_do & (15 << 4)) >> 4)))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				if (((what_to_do & (3 << 18)) >> 18) == 3) // five_threat
				{
					if (five_threat != END_OBJECT)
					{
						turn_glob ^= 1;	
						st->move=move;	
						return WINNING_MOVE;							
					}
					five_threat = move + (pre_index * DIRECTION_SUREWIN(dir));
				}
				else
				{
					st -> threat[turn_glob][((what_to_do & (3 << 18)) >> 18)][dir].t[((move + (pre_index * DIRECTION_SUREWIN(dir))) >> 6)] |= (1ULL << ((move + (pre_index * DIRECTION_SUREWIN(dir))) - (((move + (pre_index * DIRECTION_SUREWIN(dir))) >> 6) << 6)));
				}					
			}
			if ((pre_index = ((what_to_do & (15 << 8)) >> 8)))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				if (((what_to_do & (3 << 20)) >> 20) == 3) // five_threat
				{
					if (five_threat != END_OBJECT)
					{
						turn_glob ^= 1;	
						st->move=move;	
						return WINNING_MOVE;							
					}
					five_threat = move + (pre_index * DIRECTION_SUREWIN(dir));
				}
				else
				{
					st -> threat[turn_glob][((what_to_do & (3 << 20)) >> 20)][dir].t[((move + (pre_index * DIRECTION_SUREWIN(dir))) >> 6)] |= (1ULL << ((move + (pre_index * DIRECTION_SUREWIN(dir))) - (((move + (pre_index * DIRECTION_SUREWIN(dir))) >> 6) << 6)));
				}					
			}
			if ((pre_index = ((what_to_do & (15 << 12)) >> 12)))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				if (((what_to_do & (3 << 22)) >> 22) == 3) // five_threat
				{
					if (five_threat != END_OBJECT)
					{
						turn_glob ^= 1;	
						st->move=move;	
						return WINNING_MOVE;							
					}
					five_threat = move + (pre_index * DIRECTION_SUREWIN(dir));
				}
				else
				{
					st -> threat[turn_glob][((what_to_do & (3 << 22)) >> 22)][dir].t[((move + (pre_index * DIRECTION_SUREWIN(dir))) >> 6)] |= (1ULL << ((move + (pre_index * DIRECTION_SUREWIN(dir))) - (((move + (pre_index * DIRECTION_SUREWIN(dir))) >> 6) << 6)));
				}					
			}						
		}
	}
	return 0;			
}

void Position::old_attack(int move)
{
	int i, dir, index, move_x, move_y, sq, k;
	table loop;
	uint32_t line[4];
	uint32_t border;

	move_x = move / BOARDS;
	move_y = move % BOARDS;	
	
	line[0] = (linear_bit[turn_glob][0][move_x] |= (1 << (move_y + 5)));
	line[1] = (linear_bit[turn_glob][1][move_y] |= (1 << (move_x + 5)));
	line[2] = (linear_bit[turn_glob][2][move_x + move_y] |= (1 << (move_y + 5)));
	line[3] = (linear_bit[turn_glob][3][move_y - move_x + BOARDS - 1] |= (1 << (move_y + 5)));
		
	//old attack moves
	if (IS_SIX_WINS)
	{
		for (i = 0; i < 3; i++)
		{
			for (dir = 0; dir < 4; dir++)
			{
				st -> threat[turn_glob][i][dir] |=  (st -> previous) -> threat[turn_glob][i][dir];
				//move should not in threat, five_threat is optional
				st -> threat[turn_glob][i][dir].t[move >> 6] &= (~(1ULL << (move - ((move >> 6) << 6))));
				if (five_threat != END_OBJECT)
				{
					st -> threat[turn_glob][i][dir].t[five_threat >> 6] &= (~(1ULL << (five_threat - ((five_threat >> 6) << 6))));
				}
			}
		}	
	}
	else
	{
		for (i = 0; i < 3; i++)
		{
			for (dir = 0; dir < 4; dir++)
			{
				st -> threat[turn_glob][i][dir] |= (((st -> previous) -> threat[turn_glob][i][dir]) & (~neighbourhood_5[move][dir]));
				//move should be eliminated from threat
				(st -> threat[turn_glob][i][dir]).t[move >> 6] &= (~(1ULL << (move - ((move >> 6) << 6))));		
							
				loop = ((st -> previous) -> threat[turn_glob][i][dir] & neighbourhood_5[move][dir]);				
				//five threat should be eliminated from loop and threat
				if (five_threat != END_OBJECT)
				{
					loop.t[five_threat >> 6] &= (~(1ULL << (five_threat - ((five_threat >> 6) << 6))));
					(st -> threat[turn_glob][i][dir]).t[five_threat >> 6] &= (~(1ULL << (five_threat - ((five_threat >> 6) << 6))));
				}				
				while (!loop)
				{
					sq = bitScanForward(loop);
					if (dir == 0)
					{
						border = (border_bit[0][move_x] | linear_bit[turn_glob ^ 1][0][move_x]);
						index = move_y + 5;	
					}
					if (dir == 1)
					{
						border = (border_bit[1][move_y] | linear_bit[turn_glob ^ 1][1][move_y]);
						index = move_x + 5;	
					}
					if (dir == 2)
					{
						border = (border_bit[2][move_x + move_y] | linear_bit[turn_glob ^ 1][2][move_x + move_y]);
						index = move_y + 5;
					}
					if (dir == 3)
					{
						border = (border_bit[3][move_y - move_x + BOARDS - 1] | linear_bit[turn_glob ^ 1][3][move_y - move_x + BOARDS - 1]);
						index = move_y + 5;		
					}
					index += ((sq - move) / DIRECTION_SUREWIN(dir));
					k = threat_global[code_enviroment(line[dir], border, index)];
					if (k != 3 && k != 4)
					{
						st -> threat[turn_glob][k][dir].t[sq >> 6] |= (1ULL << (sq - ((sq >> 6) << 6)));					
					}				
					loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));	
				}	
			}
		}
	}							
}

void Position::update_defense(int move)
{
	int i, dir, index, move_x, move_y, sq, k;
	table loop;
	uint32_t line[4];
	uint32_t border;

	move_x = move / BOARDS;
	move_y = move % BOARDS;	
	//defense
	for (i = 0; i < 3; i++)
	{
		for (dir = 0; dir < 4; dir++)
		{	
			st -> threat[turn_glob ^ 1][i][dir] |= (((st -> previous) -> threat[turn_glob ^ 1][i][dir]) & (~neighbourhood_4[move][dir]));
			//move should be eliminated from threat
			(st -> threat[turn_glob ^ 1][i][dir]).t[move >> 6] &= (~(1ULL << (move - ((move >> 6) << 6))));								
			
			loop = ((st -> previous) -> threat[turn_glob ^ 1][i][dir] & neighbourhood_4[move][dir]);							
			while (!loop)
			{
				sq = bitScanForward(loop);
				if (dir == 0)
				{
					line[dir] = linear_bit[turn_glob ^ 1][0][move_x];
					border = (border_bit[0][move_x] | linear_bit[turn_glob][0][move_x]);
					index = move_y + 5;	
				}
				if (dir == 1)
				{
					line[dir] = linear_bit[turn_glob ^ 1][1][move_y];
					border = (border_bit[1][move_y] | linear_bit[turn_glob][1][move_y]);
					index = move_x + 5;	
				}
				if (dir == 2)
				{
					line[dir] = linear_bit[turn_glob ^ 1][2][move_x + move_y];
					border = (border_bit[2][move_x + move_y] | linear_bit[turn_glob][2][move_x + move_y]);
					index = move_y + 5;
				}
				if (dir == 3)
				{
					line[dir] = linear_bit[turn_glob ^ 1][3][move_y - move_x + BOARDS - 1];
					border = (border_bit[3][move_y - move_x + BOARDS - 1] | linear_bit[turn_glob][3][move_y - move_x + BOARDS - 1]);
					index = move_y + 5;		
				}
				index += ((sq - move) / DIRECTION_SUREWIN(dir));
				k = threat_global[code_enviroment(line[dir], border, index)];
				if (k != 4)
				{
					assert (k != 3);
					st -> threat[turn_glob ^ 1][k][dir].t[sq >> 6] |= (1ULL << (sq - ((sq >> 6) << 6)));				
				}
				loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
			}
		}
	}	
}

void Position::clearing(int move)
{
	//attacker tt moves may be in ff or ft, we solve this problem
	st -> threat[turn_glob][2][0] &= (~(st -> threat[turn_glob][0][0]));
	st -> threat[turn_glob][2][1] &= (~(st -> threat[turn_glob][0][1])); 
	st -> threat[turn_glob][2][2] &= (~(st -> threat[turn_glob][0][2])); 
	st -> threat[turn_glob][2][3] &= (~(st -> threat[turn_glob][0][3]));  
	st -> threat[turn_glob][2][0] &= (~(st -> threat[turn_glob][1][0]));
	st -> threat[turn_glob][2][1] &= (~(st -> threat[turn_glob][1][1])); 
	st -> threat[turn_glob][2][2] &= (~(st -> threat[turn_glob][1][2])); 
	st -> threat[turn_glob][2][3] &= (~(st -> threat[turn_glob][1][3]));	

	//attacker ft moves may be in ff, we solve this problem
	st -> threat[turn_glob][1][0] &= (~(st -> threat[turn_glob][0][0]));
	st -> threat[turn_glob][1][1] &= (~(st -> threat[turn_glob][0][1])); 
	st -> threat[turn_glob][1][2] &= (~(st -> threat[turn_glob][0][2])); 
	st -> threat[turn_glob][1][3] &= (~(st -> threat[turn_glob][0][3]));  
			
	turn_glob ^= 1;	
	st->move = move;	
	st->five_threat = five_threat;	
}

void Position::undo_move() 
{
	square[turn_glob ^ 1].t[(st->move) >> 6] &= (~(1ULL << ((st->move) - (((st->move) >> 6) << 6))));
	linear_bit[turn_glob ^ 1][0][((st->move) / BOARDS)] ^= (1 << (((st->move) % BOARDS) + 5));
	linear_bit[turn_glob ^ 1][1][((st->move) % BOARDS)] ^= (1 << (((st->move) / BOARDS) + 5));
	linear_bit[turn_glob ^ 1][2][((st->move) / BOARDS) + ((st->move) % BOARDS)] ^= (1 << (((st->move) % BOARDS) + 5));
	linear_bit[turn_glob ^ 1][3][((st->move) % BOARDS) - ((st->move) / BOARDS) + BOARDS - 1] ^= (1 << (((st->move) % BOARDS) + 5)); 
	turn_glob ^= 1;			
	five_threat = (st->previous)->five_threat;

	st = st->previous;
}

int Position::is_there_a_three(uint32_t line, uint32_t border, int index, int rule)
{
	uint32_t empty;
	empty = (~(line  | border));
	
	if (rule)
	{
		//.0OO.
		if (((line & (6 << index)) == (6 << index)) && ((empty & (17 << (index - 1))) == (17 << (index - 1))))
		{
			if ((border & (65 << (index - 2))) != (65 << (index - 2)))
			{
				return 1;
			}
			return 0;
		}			
		//.O0O.		
		if (((line & (5 << (index - 1))) == (5 << (index - 1))) && ((empty & (17 << (index - 2))) == (17 << (index - 2))))
		{
			if ((border & (65 << (index - 3))) != (65 << (index - 3)))
			{
				return 1;
			}
			return 0;
		}			
		//.OO0.
		if (((line & (3 << (index - 2))) == (3 << (index - 2))) && ((empty & (17 << (index - 3))) == (17 << (index - 3))))
		{
			if ((border & (65 << (index - 4))) != (65 << (index - 4)))
			{
				return 1;
			}
			return 0;
		}			
		//.0.OO.
		if (((line & (12 << index)) == (12 << index)) && ((empty & (37 << (index - 1))) == (37 << (index - 1))))	
		{
			return 1;
		}
		//.O.0O.
		if (((line & (9 << (index - 2))) == (9 << (index - 2))) && ((empty & (37 << (index - 3))) == (37 << (index - 3))))	
		{
			return 1;
		}
		//.O.O0.
		if (((line & (5 << (index - 3))) == (5 << (index - 3))) && ((empty & (37 << (index - 4))) == (37 << (index - 4))))	
		{
			return 1;
		}		
		//.0O.O.
		if (((line & (10 << index)) == (10 << index)) && ((empty & (41 << (index - 1))) == (41 << (index - 1))))	
		{
			return 1;
		}
		//.O0.O.
		if (((line & (9 << (index - 1))) == (9 << (index - 1))) && ((empty & (41 << (index - 2))) == (41 << (index - 2))))	
		{
			return 1;
		}
		//.OO.0.
		if (((line & (3 << (index - 3))) == (3 << (index - 3))) && ((empty & (41 << (index - 4))) == (41 << (index - 4))))	
		{
			return 1;
		}
	}
	else
	{
		//.0OO.
		if (((line & (6 << index)) == (6 << index)) && ((empty & (17 << (index - 1))) == (17 << (index - 1))))
		{
			if (!(line & (65 << (index - 2))) && (border & (65 << (index - 2))) != (65 << (index - 2))) 
			{
				if ((!(border & (1 << (index - 2))) && (!(line & (1 << (index - 3))))) || ((!(border & (1 << (index + 4)))) && (!(line & (1 << (index + 5)))))) 
				{
					return 1;
				}
			}
			return 0;
		}			
		//.O0O.		
		if (((line & (5 << (index - 1))) == (5 << (index - 1))) && ((empty & (17 << (index - 2))) == (17 << (index - 2))))
		{
			if (!(line & (65 << (index - 3))) && (border & (65 << (index - 3))) != (65 << (index - 3))) 
			{
				if ((!(border & (1 << (index - 3))) && (!(line & (1 << (index - 4))))) || ((!(border & (1 << (index + 3)))) && (!(line & (1 << (index + 4)))))) 
				{
					return 1;
				}
			}
			return 0;
		}			
		//.OO0.
		if (((line & (3 << (index - 2))) == (3 << (index - 2))) && ((empty & (17 << (index - 3))) == (17 << (index - 3))))
		{
			if (!(line & (65 << (index - 4))) && (border & (65 << (index - 4))) != (65 << (index - 4))) 
			{
				if ((!(border & (1 << (index - 4))) && (!(line & (1 << (index - 5))))) || ((!(border & (1 << (index + 2)))) && (!(line & (1 << (index + 3)))))) 
				{
					return 1;
				}
			}
			return 0;
		}	
		//.0.OO.
		if (((line & (12 << index)) == (12 << index)) && ((empty & (37 << (index - 1))) == (37 << (index - 1))))	
		{
			if (!(line & (129 << (index - 2))))
			{
				return 1;
			}
			return 0;
		}
		//.O.0O.
		if (((line & (9 << (index - 2))) == (9 << (index - 2))) && ((empty & (37 << (index - 3))) == (37 << (index - 3))))	
		{
			if (!(line & (129 << (index - 4))))
			{
				return 1;
			}
			return 0;
		}
		//.O.O0.
		if (((line & (5 << (index - 3))) == (5 << (index - 3))) && ((empty & (37 << (index - 4))) == (37 << (index - 4))))	
		{
			if (!(line & (129 << (index - 5))))
			{
				return 1;
			}
			return 0;
		}		
		//.0O.O.
		if (((line & (10 << index)) == (10 << index)) && ((empty & (41 << (index - 1))) == (41 << (index - 1))))	
		{
			if (!(line & (129 << (index - 2))))
			{
				return 1;
			}
			return 0;
		}
		//.O0.O.
		if (((line & (9 << (index - 1))) == (9 << (index - 1))) && ((empty & (41 << (index - 2))) == (41 << (index - 2))))	
		{
			if (!(line & (129 << (index - 3))))
			{
				return 1;
			}
			return 0;
		}
		//.OO.0.
		if (((line & (3 << (index - 3))) == (3 << (index - 3))) && ((empty & (41 << (index - 4))) == (41 << (index - 4))))	
		{
			if (!(line & (129 << (index - 5))))
			{
				return 1;
			}
			return 0;
		}		
	}	
	return 0;
}

int Position::is_there_a_four (uint32_t line, uint32_t border, int index, int rule)
{
	uint32_t empty;
	empty = (~(line  | border));
	
	if (rule)
	{
		//.0OOO.
		if ((line & (14 << index)) == (14 << index))
		{
			if ((border & (33 << (index - 1))) != (33 << (index - 1)))
			{
				return 1;
			}
			return 0;
		}			
		//.O0OO.		
		if ((line & (13 << (index - 1))) == (13 << (index - 1)))
		{
			if ((border & (33 << (index - 2))) != (33 << (index - 2)))
			{
				return 1;
			}
			return 0;
		}			
		//.OO0O.
		if ((line & (11 << (index - 2))) == (11 << (index - 2)))
		{
			if ((border & (33 << (index - 3))) != (33 << (index - 3)))
			{
				return 1;
			}
			return 0;
		}
		//.OOO0.
		if ((line & (7 << (index - 3))) == (7 << (index - 3)))
		{
			if ((border & (33 << (index - 4))) != (33 << (index - 4)))
			{
				return 1;
			}
			return 0;
		}
		//0.OOO
		if ((line & (28 << index)) == (28 << index))
		{
			if (!(border & (1 << (index + 1))))
			{
				return 1;
			}
		}			
		//O.0OO		
		if ((line & (25 << (index - 2))) == (25 << (index - 2)))
		{
			if (!(border & (1 << (index - 1))))
			{
				return 1;
			}
		}			
		//O.O0O
		if ((line & (21 << (index - 3))) == (21 << (index - 3)))
		{
			if (!(border & (1 << (index - 2))))
			{
				return 1;
			}
		}
		//O.OO0
		if ((line & (13 << (index - 4))) == (13 << (index - 4)))
		{
			if (!(border & (1 << (index - 3))))
			{
				return 1;
			}
		}
		//0OO.O
		if ((line & (22 << index)) == (22 << index))
		{
			if (!(border & (1 << (index + 3))))
			{
				return 1;
			}
		}			
		//O0O.O	
		if ((line & (21 << (index - 1))) == (21 << (index - 1)))
		{
			if (!(border & (1 << (index + 2))))
			{
				return 1;
			}
		}			
		//OO0.O
		if ((line & (19 << (index - 2))) == (19 << (index - 2)))
		{
			if (!(border & (1 << (index + 1))))
			{
				return 1;
			}
		}
		//OOO.0
		if ((line & (7 << (index - 4))) == (7 << (index - 4)))
		{
			if (!(border & (1 << (index - 1))))
			{
				return 1;
			}
		}	
		//0O.OO
		if ((line & (26 << index)) == (26 << index))
		{
			if (!(border & (1 << (index + 2))))
			{
				return 1;
			}
		}			
		//O0.OO	
		if ((line & (25 << (index - 1))) == (25 << (index - 1)))
		{
			if (!(border & (1 << (index + 1))))
			{
				return 1;
			}
		}			
		//OO.0O
		if ((line & (19 << (index - 3))) == (19 << (index - 3)))
		{
			if (!(border & (1 << (index - 1))))
			{
				return 1;
			}
		}
		//OO.O0
		if ((line & (11 << (index - 4))) == (11 << (index - 4)))
		{
			if (!(border & (1 << (index - 2))))
			{
				return 1;
			}
		}					
	}
	else
	{
		//.0OOO.
		if (((line & (14 << index)) == (14 << index)) && !(line & (33 << (index - 1))))
		{
			if (((empty & (1 << (index - 1))) && (!(line & (1 << (index - 2))))) || ((empty & (1 << (index + 4))) && (!(line & (1 << (index + 5))))))
			{
				return 1;
			}
			return 0;
		}			
		//.O0OO.		
		if (((line & (13 << (index - 1))) == (13 << (index - 1))) && !(line & (33 << (index - 2))))
		{
			if (((empty & (1 << (index - 2))) && (!(line & (1 << (index - 3))))) || ((empty & (1 << (index + 3))) && (!(line & (1 << (index + 4))))))
			{
				return 1;
			}
			return 0;
		}			
		//.OO0O.
		if (((line & (11 << (index - 2))) == (11 << (index - 2))) && !(line & (33 << (index - 3))))
		{
			if (((empty & (1 << (index - 3))) && (!(line & (1 << (index - 4))))) || ((empty & (1 << (index + 2))) && (!(line & (1 << (index + 3))))))
			{
				return 1;
			}
			return 0;
		}
		//.OOO0.
		if (((line & (7 << (index - 3))) == (7 << (index - 3))) && !(line & (33 << (index - 4))))
		{
			if (((empty & (1 << (index - 4))) && (!(line & (1 << (index - 5))))) || ((empty & (1 << (index + 1))) && (!(line & (1 << (index + 2))))))
			{
				return 1;
			}
			return 0;
		}
		//0.OOO
		if ((line & (28 << index)) == (28 << index))
		{
			if ((!(border & (1 << (index + 1)))) && (!(line & (65 << (index - 1)))))
			{
				return 1;
			}
		}			
		//O.0OO		
		if ((line & (25 << (index - 2))) == (25 << (index - 2)))
		{
			if ((!(border & (1 << (index - 1)))) && (!(line & (65 << (index - 3)))))
			{
				return 1;
			}
		}			
		//O.O0O
		if ((line & (21 << (index - 3))) == (21 << (index - 3)))
		{
			if ((!(border & (1 << (index - 2)))) && (!(line & (65 << (index - 4)))))
			{
				return 1;
			}
		}
		//O.OO0
		if ((line & (13 << (index - 4))) == (13 << (index - 4)))
		{
			if ((!(border & (1 << (index - 3)))) && (!(line & (65 << (index - 5)))))
			{
				return 1;
			}
		}
		//0OO.O
		if ((line & (22 << index)) == (22 << index))
		{
			if ((!(border & (1 << (index + 3)))) && (!(line & (65 << (index - 1)))))
			{
				return 1;
			}
		}			
		//O0O.O	
		if ((line & (21 << (index - 1))) == (21 << (index - 1)))
		{
			if ((!(border & (1 << (index + 2)))) && (!(line & (65 << (index - 2)))))
			{
				return 1;
			}
		}			
		//OO0.O
		if ((line & (19 << (index - 2))) == (19 << (index - 2)))
		{
			if ((!(border & (1 << (index + 1)))) && (!(line & (65 << (index - 3)))))
			{
				return 1;
			}
		}
		//OOO.0
		if ((line & (7 << (index - 4))) == (7 << (index - 4)))
		{
			if ((!(border & (1 << (index - 1)))) && (!(line & (65 << (index - 5)))))
			{
				return 1;
			}
		}	
		//0O.OO
		if ((line & (26 << index)) == (26 << index))
		{
			if ((!(border & (1 << (index + 2)))) && (!(line & (65 << (index - 1)))))
			{
				return 1;
			}
		}			
		//O0.OO	
		if ((line & (25 << (index - 1))) == (25 << (index - 1)))
		{
			if ((!(border & (1 << (index + 1)))) && (!(line & (65 << (index - 2)))))
			{
				return 1;
			}
		}			
		//OO.0O
		if ((line & (19 << (index - 3))) == (19 << (index - 3)))
		{
			if ((!(border & (1 << (index - 1)))) && (!(line & (65 << (index - 4)))))
			{
				return 1;
			}
		}
		//OO.O0
		if ((line & (11 << (index - 4))) == (11 << (index - 4)))
		{
			if ((!(border & (1 << (index - 2)))) && (!(line & (65 << (index - 5)))))
			{
				return 1;
			}
		}
	}
	return 0;					
}

int Position::is_there_a_free_four (uint32_t line, uint32_t border, int index, int rule)
{
	uint32_t empty;	
	empty = (~(line  | border));
	
	if (rule)
	{
		//.0OOO.
		if ((line & (14 << index)) == (14 << index))
		{
			if ((empty & (33 << (index - 1))) == (33 << (index - 1)))
			{
				return 1;
			}
			return 0;
		}			
		//.O0OO.		
		if ((line & (13 << (index - 1))) == (13 << (index - 1)))
		{
			if ((empty & (33 << (index - 2))) == (33 << (index - 2)))
			{
				return 1;
			}
			return 0;
		}			
		//.OO0O.
		if ((line & (11 << (index - 2))) == (11 << (index - 2)))
		{
			if ((empty & (33 << (index - 3))) == (33 << (index - 3)))
			{
				return 1;
			}
			return 0;
		}
		//.OOO0.
		if ((line & (7 << (index - 3))) == (7 << (index - 3)))
		{
			if ((empty & (33 << (index - 4))) == (33 << (index - 4)))
			{
				return 1;
			}
			return 0;
		}
	}
	else
	{
		//.0OOO.
		if ((line & (14 << index)) == (14 << index))
		{
			if (((empty & (33 << (index - 1))) == (33 << (index - 1))) && (!(line & (129 << (index - 2)))))
			{
				return 1;
			}
			return 0;
		}			
		//.O0OO.		
		if ((line & (13 << (index - 1))) == (13 << (index - 1)))
		{
			if (((empty & (33 << (index - 2))) == (33 << (index - 2))) && (!(line & (129 << (index - 3)))))
			{
				return 1;
			}
			return 0;
		}			
		//.OO0O.
		if ((line & (11 << (index - 2))) == (11 << (index - 2)))
		{
			if (((empty & (33 << (index - 3))) == (33 << (index - 3))) && (!(line & (129 << (index - 4)))))
			{
				return 1;
			}
			return 0;
		}
		//.OOO0.
		if ((line & (7 << (index - 3))) == (7 << (index - 3)))
		{
			if (((empty & (33 << (index - 4))) == (33 << (index - 4))) && (!(line & (129 << (index - 5)))))
			{
				return 1;
			}
			return 0;
		}
	}
	return 0;
}

int Position::is_there_a_five (uint32_t line, uint32_t /*border*/, int index, int rule)
{	
	if (rule)
	{
		//0OOOO
		if ((line & (30 << index)) == (30 << index))
		{
			return 1;
		}			
		//O0OOO		
		if ((line & (29 << (index - 1))) == (29 << (index - 1)))
		{
			return 1;
		}			
		//OO0OO
		if ((line & (27 << (index - 2))) == (27 << (index - 2)))
		{
			return 1;
		}
		//OOO0O
		if ((line & (23 << (index - 3))) == (23 << (index - 3)))
		{
			return 1;
		}
		//OOOO0
		if ((line & (15 << (index - 4))) == (15 << (index - 4)))
		{
			return 1;
		}
	}
	else
	{
		//0OOOO
		if ((line & (30 << index)) == (30 << index))
		{
			if (!(line & (65 << (index - 1))))
			{
				return 1;
			}
		}			
		//O0OOO		
		if ((line & (29 << (index - 1))) == (29 << (index - 1)))
		{
			if (!(line & (65 << (index - 2))))
			{
				return 1;
			}
		}			
		//OO0OO
		if ((line & (27 << (index - 2))) == (27 << (index - 2)))
		{
			if (!(line & (65 << (index - 3))))
			{
				return 1;
			}
		}
		//OOO0O
		if ((line & (23 << (index - 3))) == (23 << (index - 3)))
		{
			if (!(line & (65 << (index - 4))))
			{
				return 1;
			}
		}
		//OOOO0
		if ((line & (15 << (index - 4))) == (15 << (index - 4)))
		{
			if (!(line & (65 << (index - 5))))
			{
				return 1;
			}
		}
	}
	return 0;
}

bool Position::is_extreme_pos()
{
	if ((five_threat != END_OBJECT) || !(st -> threat[(turn_glob) ^ 1][0][0]) || !(st -> threat[(turn_glob) ^ 1][0][1]) || !(st -> threat[(turn_glob) ^ 1][0][2]) || !(st -> threat[(turn_glob) ^ 1][0][3])) //extrem position
	{
		return true;
	}
	return false;
}

void Position::generate_legal(table * legal)
{
	int i, dir, move_x, move_y, index, sq;
	int d[3];
	table loop;	
	uint32_t line, border;
	
	if ((five_threat != END_OBJECT) || !(st -> threat[(turn_glob) ^ 1][0][0]) || !(st -> threat[(turn_glob) ^ 1][0][1]) || !(st -> threat[(turn_glob) ^ 1][0][2]) || !(st -> threat[(turn_glob) ^ 1][0][3])) //extrem position
	{
		if (five_threat != END_OBJECT)
		{
			//ft and ff is legal on five threat
			for (i = 0; i < 2; i++)
			{
				for (dir = 0; dir < 4; dir++)
				{
					if (st -> threat[turn_glob][i][dir].t[(five_threat) >> 6] & (1ULL << ((five_threat) - (((five_threat) >> 6) << 6))))
					{
						legal -> t[(five_threat) >> 6] ^= (1ULL << ((five_threat) - (((five_threat) >> 6) << 6)));
						return;
					}
				}
			}
			//if no more ff than five threat legal
			if (!(!(st -> threat[(turn_glob) ^ 1][0][0])) && !(!(st -> threat[(turn_glob) ^ 1][0][1])) && !(!(st -> threat[(turn_glob) ^ 1][0][2])) && !(!(st -> threat[(turn_glob) ^ 1][0][3])))
			{
				legal -> t[(five_threat) >> 6] ^= (1ULL << ((five_threat) - (((five_threat) >> 6) << 6)));
				return;				
			}
			// almost surly there is no legal move, we try to prove this 
			for (dir = 0; dir < 4; dir++)
			{
				if (!(st -> threat[turn_glob ^ 1][0][dir] & (~neighbourhood_4[five_threat][dir]))) //we use that on five threat there is no ff	
				{
					return;
				}	
			}
			//very rearly we need this
			move_x = five_threat / BOARDS;
			move_y = five_threat % BOARDS;
			for (dir = 0; dir < 4; dir++)
			{											
				loop = st -> threat[turn_glob ^ 1][0][dir];							
				while (!loop)
				{
					sq = bitScanForward(loop);
					if (dir == 0)
					{
						line = linear_bit[turn_glob ^ 1][0][move_x];
						border = (border_bit[0][move_x] | linear_bit[turn_glob][0][move_x]);
						index = move_y + 5;
						border |= (1 << index);	
					}
					if (dir == 1)
					{
						line = linear_bit[turn_glob ^ 1][1][move_y];
						border = (border_bit[1][move_y] | linear_bit[turn_glob][1][move_y]);
						index = move_x + 5;	
						border |= (1 << index);	
					}
					if (dir == 2)
					{
						line = linear_bit[turn_glob ^ 1][2][move_x + move_y];
						border = (border_bit[2][move_x + move_y] | linear_bit[turn_glob][2][move_x + move_y]);
						index = move_y + 5;
						border |= (1 << index);	
					}
					if (dir == 3)
					{
						line = linear_bit[turn_glob ^ 1][3][move_y - move_x + BOARDS - 1];
						border = (border_bit[3][move_y - move_x + BOARDS - 1] | linear_bit[turn_glob][3][move_y - move_x + BOARDS - 1]);
						index = move_y + 5;	
						border |= (1 << index);		
					}
					index += ((sq - five_threat) / DIRECTION_SUREWIN(dir));
					if (threat_global[code_enviroment(line, border, index)] == 0)
					{
						return;
					}
					loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
				}
			}
			legal -> t[(five_threat) >> 6] ^= (1ULL << ((five_threat) - (((five_threat) >> 6) << 6)));
			return;							
		}
		else
		{
			for (dir = 0; dir < 4; dir++)
			{
				if (!(st -> threat[turn_glob ^ 1][0][dir])) 	
				{
					sq = bitScanForward(st -> threat[turn_glob ^ 1][0][dir]);
					break;
				}
			}
			d[0] = sq;
			while (square[turn_glob ^ 1].t[(sq + DIRECTION_SUREWIN(dir)) >> 6] & (1ULL << ((sq + DIRECTION_SUREWIN(dir)) - (((sq + DIRECTION_SUREWIN(dir)) >> 6) << 6))))
			{
				sq += DIRECTION_SUREWIN(dir);
			}
			d[1] = sq + DIRECTION_SUREWIN(dir);
			sq = d[0];
			while (square[turn_glob ^ 1].t[(sq - DIRECTION_SUREWIN(dir)) >> 6] & (1ULL << ((sq - DIRECTION_SUREWIN(dir)) - (((sq - DIRECTION_SUREWIN(dir)) >> 6) << 6))))
			{
				sq -= DIRECTION_SUREWIN(dir);
			}
			d[2] = sq - DIRECTION_SUREWIN(dir);
			for (i = 0; i < 3; i++)
			{					
				move_x = d[i] / BOARDS;
				move_y = d[i] % BOARDS;
				linear_bit[turn_glob][0][move_x] ^= (1 << (move_y + 5));
				linear_bit[turn_glob][1][move_y] ^= (1 << (move_x + 5));
				linear_bit[turn_glob][2][move_x + move_y] ^= (1 << (move_y + 5));
				linear_bit[turn_glob][3][move_y - move_x + BOARDS - 1] ^= (1 << (move_y + 5));
				for (dir = 0; dir < 4; dir++)
				{
					loop = (st -> threat[turn_glob ^ 1][0][dir] & (~neighbourhood_4[d[i]][dir]));
					loop.t[d[i] >> 6] &= (~(1ULL << (d[i] - ((d[i] >> 6) << 6))));
					if (!loop) // only ft possibility
					{
						goto new_i;
					}													
					loop = (st -> threat[turn_glob ^ 1][0][dir] & neighbourhood_4[d[i]][dir]);							
					while (!loop)
					{
						sq = bitScanForward(loop);
						if (dir == 0)
						{
							line = linear_bit[turn_glob ^ 1][0][move_x];
							border = (border_bit[0][move_x] | linear_bit[turn_glob][0][move_x]);
							index = move_y + 5;	
						}
						if (dir == 1)
						{
							line = linear_bit[turn_glob ^ 1][1][move_y];
							border = (border_bit[1][move_y] | linear_bit[turn_glob][1][move_y]);
							index = move_x + 5;	
						}
						if (dir == 2)
						{
							line = linear_bit[turn_glob ^ 1][2][move_x + move_y];
							border = (border_bit[2][move_x + move_y] | linear_bit[turn_glob][2][move_x + move_y]);
							index = move_y + 5;
						}
						if (dir == 3)
						{
							line = linear_bit[turn_glob ^ 1][3][move_y - move_x + BOARDS - 1];
							border = (border_bit[3][move_y - move_x + BOARDS - 1] | linear_bit[turn_glob][3][move_y - move_x + BOARDS - 1]);
							index = move_y + 5;		
						}
						index += ((sq - d[i]) / DIRECTION_SUREWIN(dir));
						if (threat_global[code_enviroment(line, border, index)] == 0)
						{
							goto new_i;
						}
						loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
					}
				}
				legal -> t[d[i] >> 6] |= (1ULL << (d[i] - ((d[i] >> 6) << 6)));
new_i:
				linear_bit[turn_glob][0][move_x] ^= (1 << (move_y + 5));
				linear_bit[turn_glob][1][move_y] ^= (1 << (move_x + 5));
				linear_bit[turn_glob][2][move_x + move_y] ^= (1 << (move_y + 5));
				linear_bit[turn_glob][3][move_y - move_x + BOARDS - 1] ^= (1 << (move_y + 5));
			}
			*legal |= st -> threat[turn_glob][1][0];
			*legal |= st -> threat[turn_glob][1][1];
			*legal |= st -> threat[turn_glob][1][2];
			*legal |= st -> threat[turn_glob][1][3];
			return;
		}				
	}
	else //quiet position
	{
		(*legal) = ((~(square[0] | square[1])) & table_indicator); 
		return;
	}
	return;
}

table Position::generate_single_regular_fast(int side, int sq, int dir, int five) // if we know the type (and usually we know) we can be much faster, we generate regular defense moves for the defense side
{
	int d;
	table ret;
	
	ret.null();	
	if (five == 0) // free four case
	{
		d = sq;
		ret.t[d >> 6] ^= (1ULL << (d - ((d >> 6) << 6)));
		
		while (square[side ^ 1].t[(d + DIRECTION_SUREWIN(dir)) >> 6] & (1ULL << ((d + DIRECTION_SUREWIN(dir)) - (((d + DIRECTION_SUREWIN(dir)) >> 6) << 6))))
		{
			d += DIRECTION_SUREWIN(dir);
		}
		d += DIRECTION_SUREWIN(dir);
		ret.t[d >> 6] ^= (1ULL << (d - ((d >> 6) << 6)));
		
		d = sq;
		while (square[side ^ 1].t[(d - DIRECTION_SUREWIN(dir)) >> 6] & (1ULL << ((d - DIRECTION_SUREWIN(dir)) - (((d - DIRECTION_SUREWIN(dir)) >> 6) << 6))))
		{
			d -= DIRECTION_SUREWIN(dir);
		}
		d -= DIRECTION_SUREWIN(dir);
		ret.t[d >> 6] ^= (1ULL << (d - ((d >> 6) << 6)));		
	}
	if (five == 1) // five case
	{
		d = sq;
		ret.t[d >> 6] ^= (1ULL << (d - ((d >> 6) << 6)));
	}	
	return ret;	
}

void Position::generate_regular(table * regular) // regular should be nulled
{
	int i, dir;
	int sq;
	table loop;
	
	assert(is_extreme_pos());
	*regular = (~(*regular));
	
	if (five_threat != END_OBJECT)
	{
		(*regular) &= generate_single_regular_fast(turn_glob, five_threat, 0, 1);
		for (i = 0; i < 2; i++)
		{
			for (dir = 0; dir < 4; dir++)
			{
				if (st -> threat[turn_glob][i][dir].t[(five_threat) >> 6] & (1ULL << ((five_threat) - (((five_threat) >> 6) << 6))))
				{
					return;
				}
			}
		}
	}
	for (dir = 0; dir < 4; dir++)
	{
		loop = st -> threat[turn_glob ^ 1][0][dir];
		while (!loop)
		{
			sq = bitScanForward(loop);
			(*regular) &= generate_single_regular_fast(turn_glob, sq, dir, 0); 
			loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
		}			
	}
	return;
}

void Position::generate_ft(table * ft)
{
	*ft |= st -> threat[turn_glob][1][0];
	*ft |= st -> threat[turn_glob][1][1];
	*ft |= st -> threat[turn_glob][1][2];
	*ft |= st -> threat[turn_glob][1][3];
}

int Position::code_enviroment(uint32_t line, uint32_t border, int index)
{
	return three_base_convert[((line & (31 << (index - 5))) >> (index - 5)) + ((line & (31 << (index + 1))) >> (index - 4))] + (three_base_convert[((border & (31 << (index - 5))) >> (index - 5)) + ((border & (31 << (index + 1))) >> (index - 4))] << 1);
}

void Position::generate_threat_unio()
{
	st -> threat_type_unio[turn_glob][0] = (st -> threat[turn_glob][1][0] | st -> threat[turn_glob][1][1] | st -> threat[turn_glob][1][2] | st -> threat[turn_glob][1][3]);
	st -> threat_type_unio[turn_glob][1] = (st -> threat[turn_glob][2][0] | st -> threat[turn_glob][2][1] | st -> threat[turn_glob][2][2] | st -> threat[turn_glob][2][3]);			
	st -> threat_unio[turn_glob] = (st -> threat_type_unio[turn_glob][0] | st -> threat_type_unio[turn_glob][1]);
}

void Position::generate_threat_unio(int side)
{
	st -> threat_type_unio[side][0] = (st -> threat[side][1][0] | st -> threat[side][1][1] | st -> threat[side][1][2] | st -> threat[side][1][3]);
	st -> threat_type_unio[side][1] = (st -> threat[side][2][0] | st -> threat[side][2][1] | st -> threat[side][2][2] | st -> threat[side][2][3]);			
	st -> threat_unio[side] = (st -> threat_type_unio[side][0] | st -> threat_type_unio[side][1]);
}

void Position::generate_local_bit(int side, int sq, int dir, uint32_t * line, uint32_t * border, int * index) //works only if linear bits are OK
{
	int sq_x, sq_y;
	
	sq_x = sq / BOARDS;
	sq_y = sq % BOARDS;
	if (dir == 1)
	{
		*index = sq_x + 5;
	}
	else
	{
		*index = sq_y + 5;
	}
	if (dir == 0)
	{
		*line = linear_bit[side][0][sq_x];
		*border = (border_bit[0][sq_x] | linear_bit[side ^ 1][0][sq_x]);
	}
	if (dir == 1)
	{
		*line = linear_bit[side][1][sq_y];
		*border = (border_bit[1][sq_y] | linear_bit[side ^ 1][1][sq_y]);
	}
	if (dir == 2)
	{
		*line = linear_bit[side][2][sq_x + sq_y];		
		*border = (border_bit[2][sq_x + sq_y] | linear_bit[side ^ 1][2][sq_x + sq_y]);	
	}
	if (dir == 3)
	{
		*line = linear_bit[side][3][sq_y - sq_x + BOARDS - 1];
		*border = (border_bit[3][sq_y - sq_x + BOARDS - 1] | linear_bit[side ^ 1][3][sq_y - sq_x + BOARDS - 1]);
	}
}

table Position::why_win(int move)
{
	int move_x, move_y;
	int dir;
	uint32_t border;
	uint32_t line[DIRECTION_NB];
	uint32_t what_to_do;
	int index, pre_index;
	table ret;

	ret.null();
	move_x = move / BOARDS;
	move_y = move % BOARDS;
	line[0] = (linear_bit[turn_glob][0][move_x] | (1 << (move_y + 5)));
	line[1] = (linear_bit[turn_glob][1][move_y] | (1 << (move_x + 5)));
	line[2] = (linear_bit[turn_glob][2][move_x + move_y] | (1 << (move_y + 5)));
	line[3] = (linear_bit[turn_glob][3][move_y - move_x + BOARDS - 1] | (1 << (move_y + 5)));
	
	//attack
	//local things
	for (dir = 0; dir < 4; dir++)
	{
		if (dir != 1)
		{
			index = move_y + 5;
		}
		else
		{
			index = move_x + 5;
		}
		if ((line[dir] & (119 << (index - 3)))) // three_radius = 119 = 1 + 2 + 4 + 16 + 32 + 64 = 1110111
		{
			if (dir == 0)
			{
				border = (border_bit[0][move_x] | linear_bit[turn_glob ^ 1][0][move_x]);	
			}
			if (dir == 1)
			{
				border = (border_bit[1][move_y] | linear_bit[turn_glob ^ 1][1][move_y]);	
			}
			if (dir == 2)
			{
				border = (border_bit[2][move_x + move_y] | linear_bit[turn_glob ^ 1][2][move_x + move_y]);
			}
			if (dir == 3)
			{
				border = (border_bit[3][move_y - move_x + BOARDS - 1] | linear_bit[turn_glob ^ 1][3][move_y - move_x + BOARDS - 1]);		
			}
			what_to_do = what_to_do_global[code_enviroment(line[dir], border, index)];
		
			if ((pre_index = (what_to_do & 15)))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				if (((what_to_do & (3 << 16)) >> 16) == 3) // five_threat
				{
					if ((!ret))
					{
						EXPAND(ret,  (move + (pre_index * DIRECTION_SUREWIN(dir))));
						return ret;
					}
					EXPAND(ret,  (move + (pre_index * DIRECTION_SUREWIN(dir))));
				}
			}
			if ((pre_index = ((what_to_do & (15 << 4)) >> 4)))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				if (((what_to_do & (3 << 18)) >> 18) == 3) // five_threat
				{
					if ((!ret))
					{
						EXPAND(ret, (move + (pre_index * DIRECTION_SUREWIN(dir))));
						return ret;
					}
					EXPAND(ret,  (move + (pre_index * DIRECTION_SUREWIN(dir))));
				}					
			}
			if ((pre_index = ((what_to_do & (15 << 8)) >> 8)))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				if (((what_to_do & (3 << 20)) >> 20) == 3) // five_threat
				{
					if ((!ret))
					{
						EXPAND(ret, (move + (pre_index * DIRECTION_SUREWIN(dir))));
						return ret;
					}
					EXPAND(ret, (move + (pre_index * DIRECTION_SUREWIN(dir))));
				}				
			}
			if ((pre_index = ((what_to_do & (15 << 12)) >> 12)))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				if (((what_to_do & (3 << 22)) >> 22) == 3) // five_threat
				{
					if ((!ret))
					{
						EXPAND(ret, (move + (pre_index * DIRECTION_SUREWIN(dir))));
						return ret;
					}
					EXPAND(ret, (move + (pre_index * DIRECTION_SUREWIN(dir))));
				}				
			}						
		}
	}
	return ret;			
}

int Position::five_threat_dir(int move)
{
	int move_x, move_y;
	int dir;
	uint32_t border;
	uint32_t line[DIRECTION_NB];
	uint32_t what_to_do;
	int index, pre_index;

	move_x = move / BOARDS;
	move_y = move % BOARDS;
	turn_glob ^= 1;
	line[0] = linear_bit[turn_glob][0][move_x];
	line[1] = linear_bit[turn_glob][1][move_y];
	line[2] = linear_bit[turn_glob][2][move_x + move_y];
	line[3] = linear_bit[turn_glob][3][move_y - move_x + BOARDS - 1];
	
	//attack
	//local things
	for (dir = 0; dir < 4; dir++)
	{
		if (dir != 1)
		{
			index = move_y + 5;
		}
		else
		{
			index = move_x + 5;
		}
		if ((line[dir] & (119 << (index - 3)))) // three_radius = 119 = 1 + 2 + 4 + 16 + 32 + 64 = 1110111
		{
			if (dir == 0)
			{
				border = (border_bit[0][move_x] | linear_bit[turn_glob ^ 1][0][move_x]);	
			}
			if (dir == 1)
			{
				border = (border_bit[1][move_y] | linear_bit[turn_glob ^ 1][1][move_y]);	
			}
			if (dir == 2)
			{
				border = (border_bit[2][move_x + move_y] | linear_bit[turn_glob ^ 1][2][move_x + move_y]);
			}
			if (dir == 3)
			{
				border = (border_bit[3][move_y - move_x + BOARDS - 1] | linear_bit[turn_glob ^ 1][3][move_y - move_x + BOARDS - 1]);		
			}
			what_to_do = what_to_do_global[code_enviroment(line[dir], border, index)];
		
			if ((pre_index = (what_to_do & 15)))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				if (((what_to_do & (3 << 16)) >> 16) == 3) // five_threat
				{
					turn_glob ^= 1;
					return dir;
				}
			}
			if ((pre_index = ((what_to_do & (15 << 4)) >> 4)))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				if (((what_to_do & (3 << 18)) >> 18) == 3) // five_threat
				{
					turn_glob ^= 1;
					return dir;
				}					
			}
			if ((pre_index = ((what_to_do & (15 << 8)) >> 8)))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				if (((what_to_do & (3 << 20)) >> 20) == 3) // five_threat
				{
					turn_glob ^= 1;
					return dir;
				}				
			}
			if ((pre_index = ((what_to_do & (15 << 12)) >> 12)))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				if (((what_to_do & (3 << 22)) >> 22) == 3) // five_threat
				{
					turn_glob ^= 1;
					return dir;
				}				
			}						
		}
	}
	turn_glob ^= 1;
	assert(false);
	return 0;	
}

void history(Position * p, int * history)
{
	StateInfo * help;
	int history_index;
	
	help = p -> st;
	history_index = 0;
	
	while (help -> move != END_OBJECT)
	{
		history[history_index] = help -> move; 		
		help = help -> previous;
		history_index++;
	}
	history[history_index] = END_OBJECT;
}
