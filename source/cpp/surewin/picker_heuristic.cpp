#include "picker_heuristic.h"

int how_interesting_move_surewin_attack(Position * p, int sq, table attackline)
{
	int dir;
	int index;
	int counter;
	int sq_x, sq_y;
	int sq_threat;
	
	bool is_attackline_ok;
	
	uint32_t line, line_threat, border;
	table loop;
		
	counter = 0;
	is_attackline_ok = false;
	sq_x = sq / BOARDS;
	sq_y = sq % BOARDS;	
		
	for (dir = 0; dir < 4; dir++)
	{				
		if (dir == 1)
		{
			index = sq_x + 5;
		}
		else
		{
			index = sq_y + 5;
		}		
		loop =  ((p -> st) -> threat_unio[p -> turn_glob]) & neighbourhood_5[sq][dir];
		line_threat = 0;
		while (!loop)
		{
			sq_threat = bitScanForward(loop);
			line_threat |= (1 << (index + ((sq_threat - sq) / DIRECTION_SUREWIN(dir)))); 
			loop.t[sq_threat >> 6] ^= (1ULL << (sq_threat - ((sq_threat >> 6) << 6)));
		}		
		
		if (dir == 0)
		{
			line = p -> linear_bit[p -> turn_glob][0][sq_x];
			border = (border_bit[0][sq_x] | p -> linear_bit[p -> turn_glob ^ 1][0][sq_x]);
		}
		if (dir == 1)
		{
			line = p -> linear_bit[p -> turn_glob][1][sq_y];
			border = (border_bit[1][sq_y] | p -> linear_bit[p -> turn_glob ^ 1][1][sq_y]);
		}
		if (dir == 2)
		{
			line = p -> linear_bit[p -> turn_glob][2][sq_x + sq_y];		
			border = (border_bit[2][sq_x + sq_y] | p -> linear_bit[p -> turn_glob ^ 1][2][sq_x + sq_y]);	
		}
		if (dir == 3)
		{
			line = p -> linear_bit[p -> turn_glob][3][sq_y - sq_x + BOARDS - 1];
			border = (border_bit[3][sq_y - sq_x + BOARDS - 1] | p -> linear_bit[p -> turn_glob ^ 1][3][sq_y - sq_x + BOARDS - 1]);
		}
		line |= line_threat;				
		if (is_interesting_global[p -> code_enviroment(line, border, index)] != 0)
		{
			if (!(attackline & neighbourhood_4[sq][dir]))
			{
				is_attackline_ok = true;
			}
			counter++;
		}
	}
	if (counter >= 2 && is_attackline_ok == true)
	{
		return 3;
	}
	if (counter >= 2 && is_attackline_ok == false)
	{
		return 2;
	}
	if (counter < 2 && is_attackline_ok == true)
	{
		return 1;
	}
	if (counter < 2 && is_attackline_ok == false)
	{
		return 0;
	}	
	return 0;
}

bool is_attackline_ok(table t, int sq)
{
	int dir;
	
	for (dir = 0; dir < 4; dir++)
	{		
		if (!(t & neighbourhood_3[sq][dir]))
		{
			return true;
		}
	}	
	return false;
}

bool is_interesting_move_threat(Position * p, int sq)
{
	int dir;
	int index;
	int counter;
	int sq_x, sq_y;
	int sq_threat;
	
	uint32_t line, line_threat, border;
	table loop;
		
	counter = 0;
	sq_x = sq / BOARDS;
	sq_y = sq % BOARDS;	
		
	for (dir = 0; dir < 4; dir++)
	{				
		if (dir == 1)
		{
			index = sq_x + 5;
		}
		else
		{
			index = sq_y + 5;
		}		
		loop =  ((p -> st) -> threat_unio[p -> turn_glob]) & neighbourhood_5[sq][dir];		
		line_threat = 0;
		while (!loop)
		{
			sq_threat = bitScanForward(loop);
			line_threat |= (1 << (index + ((sq_threat - sq) / DIRECTION_SUREWIN(dir)))); 
			loop.t[sq_threat >> 6] ^= (1ULL << (sq_threat - ((sq_threat >> 6) << 6)));
		}		
		if (dir == 0)
		{
			line = p -> linear_bit[p -> turn_glob][0][sq_x];
			border = (border_bit[0][sq_x] | p -> linear_bit[p -> turn_glob ^ 1][0][sq_x]);
		}
		if (dir == 1)
		{
			line = p -> linear_bit[p -> turn_glob][1][sq_y];
			border = (border_bit[1][sq_y] | p -> linear_bit[p -> turn_glob ^ 1][1][sq_y]);
		}
		if (dir == 2)
		{
			line = p -> linear_bit[p -> turn_glob][2][sq_x + sq_y];		
			border = (border_bit[2][sq_x + sq_y] | p -> linear_bit[p -> turn_glob ^ 1][2][sq_x + sq_y]);	
		}
		if (dir == 3)
		{
			line = p -> linear_bit[p -> turn_glob][3][sq_y - sq_x + BOARDS - 1];
			border = (border_bit[3][sq_y - sq_x + BOARDS - 1] | p -> linear_bit[p -> turn_glob ^ 1][3][sq_y - sq_x + BOARDS - 1]);
		}
		line |= line_threat;				
		if (is_interesting_global[p -> code_enviroment(line, border, index)] != 0)
		{
			counter++;
			if (counter >= 2)
			{
				return true;
			}
		}
	}
	return false;
}

table local_defense(Position * p, int sq)
{
	table ret;
	uint32_t line, border, what_to_do;
	int dir;
	int index, pre_index;
	
	ret.null();
	
	for (dir = 0; dir < 4; dir++)
	{
		p -> generate_local_bit((p -> turn_glob) ^ 1, sq, dir, &line, &border, &index); //works only if linear bits are OK
		what_to_do = what_to_do_global[p -> code_enviroment(line, border, index)];
		if (what_to_do != 0)
		{
			if (pre_index = (what_to_do & 15))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				EXPAND(ret, (sq + pre_index * DIRECTION_SUREWIN(dir)));
			}
			if (pre_index = ((what_to_do & (15 << 4)) >> 4))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				EXPAND(ret, (sq + pre_index * DIRECTION_SUREWIN(dir)));
			}
			if (pre_index = ((what_to_do & (15 << 8)) >> 8))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				EXPAND(ret, (sq + pre_index * DIRECTION_SUREWIN(dir)));
			}
			if (pre_index = ((what_to_do & (15 << 12)) >> 12))
			{
				if (pre_index > 8)
				{
					pre_index = (8 - pre_index);
				}
				EXPAND(ret, (sq + pre_index * DIRECTION_SUREWIN(dir)));
			}	
		}
	}
	return ret;
}

bool is_interesting_move_homogen(Position * p, int sq)
{
	int counter, dir, index;
	uint32_t line, border, what_to_do;
	
	counter = 0;
	for (dir = 0; dir < 4; dir++)
	{
		p -> generate_local_bit((p -> turn_glob), sq, dir, &line, &border, &index); //works only if linear bits are OK
		what_to_do = what_to_do_global[p -> code_enviroment(line, border, index)];
		if (what_to_do != 0)
		{
			counter++;
			if (counter == 2)
			{
				return true;
			}
			continue;
		}
		p -> generate_local_bit((p -> turn_glob) ^ 1, sq, dir, &line, &border, &index); //works only if linear bits are OK
		what_to_do = what_to_do_global[p -> code_enviroment(line, border, index)];		
		if (what_to_do != 0)
		{
			counter++;
			if (counter == 2)
			{
				return true;
			}
		}
	}
	return false;		
}

table interesting_move_threat(Position * p, table t)
{
	int sq;
	table ret;
	
	ret.null();
	
	while(!t)
	{
		sq = bitScanForward(t);
		if (is_interesting_move_threat(p, sq))
		{
			EXPAND(ret, sq);
		}
		REDUCE(t, sq);
	}	
	return ret;
}

table interesting_move_homogen(Position * p, table t)
{
	int sq;
	table ret;
	
	ret.null();
	
	while(!t)
	{
		sq = bitScanForward(t);
		if (is_interesting_move_homogen(p, sq))
		{
			EXPAND(ret, sq);
		}
		REDUCE(t, sq);
	}	
	return ret;
}

table interesting_linear(Position * p)
{
	int sq;
	table ret, loop;
	
	ret.null();
	loop.null();
	
	loop = (~loop);
	loop &= ~(p -> square[0]);
	loop &= ~(p -> square[1]);
	
	while(!loop)
	{
		sq = bitScanForward(loop);
		if (is_interesting_move_homogen(p, sq))
		{
			EXPAND(ret, sq);
		}
		REDUCE(loop, sq);
	}	
	return ret;
}


