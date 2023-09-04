#include <stdio.h> 
#include "evaluate.h"
#include "search.h"
#include "picker_heuristic.h"

#define INTERESTING_ATTACK_VALUE_PLAYER 8
#define INTERESTING_ATTACK_VALUE_NPLAYER 5

int16_t evaluate(Position * p)
{
	int i, initiative, res, sq;
	uint64_t h;
	table loop;

	res = 0;
	loop = p -> square[p -> turn_glob];
	while (!loop)
	{
		sq = bitScanForward(loop);
		res += evaluate_piece(p, p -> turn_glob, sq);
		REDUCE(loop, sq); 
	}
	loop = p -> square[(p -> turn_glob) ^ 1];
	while (!loop)
	{
		sq = bitScanForward(loop);
		res -= evaluate_piece(p, (p -> turn_glob) ^ 1, sq);
		REDUCE(loop, sq); 
	}
	
	loop = (p -> st) -> threat_unio[p -> turn_glob];
	while (!loop)
	{
		sq = bitScanForward(loop);
		if (is_interesting_move_threat(p, sq))
		{
			res += INTERESTING_ATTACK_VALUE_PLAYER;
		}
		else
		{
			res += 6;
		}
		REDUCE(loop, sq);
	}
	p -> turn_glob ^= 1; //HACK
	loop = (p -> st) -> threat_unio[p -> turn_glob];
	while (!loop)
	{
		sq = bitScanForward(loop);
		if (is_interesting_move_threat(p, sq))
		{
			res -= INTERESTING_ATTACK_VALUE_NPLAYER;
		}
		else
		{
			res -= 2;
		}
		REDUCE(loop, sq);
	}
	p -> turn_glob ^= 1;			
	return res;			
}

int evaluate_piece(Position * p, int side, int sq)
{
	int dir, index, r_b, l_b, res, i;
	uint32_t line, border;
	
	res = 0;
	for (dir = 0; dir < 4; dir++)
	{
		p -> generate_local_bit(side, sq, dir, &line, &border, &index);
		r_b = 5;
		for (i = 1; i <= 4; i++)
		{
			if ((border & (1 << (index + i))) != 0)
			{
				r_b = i;
				break;
			}
		}
		l_b = 5;
		for (i = 1; i <= 4; i++)
		{
			if ((border & (1 << (index - i))) != 0)
			{
				l_b = i;
				break;
			}
		}
		res += MAX ((r_b + l_b - 5), 0);		
	}
	return res;
}
