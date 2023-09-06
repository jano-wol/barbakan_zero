#include <stdio.h>
#include "picker.h"
#include "picker_heuristic.h"
#include "tt.h"

Picker::Picker (Position * p)
{
	sent.null();
	stage = 0;		
	tt_move = MOVE_NONE;
	is_extreme_pos = p -> is_extreme_pos();
	
	if (is_extreme_pos)
	{
		regular.null();
		ft.null();
		p -> generate_regular(& regular);
		if (p ->five_threat == END_OBJECT)
		{
			p -> generate_ft(& ft);
			ft &= (~ regular);	
		}
		legal = (regular | ft);
	}
	else
	{	
		counter = 0;
		help = (p -> st);		
		p -> generate_legal(& legal);
	}	
}

table Picker::picker(Position * p)
{
	int dir1, dir2, sq;
	table ret;
	table loop;
	
	if (is_extreme_pos)
	{
		return picker_defense(p);
	}	
	if (stage == TT_REGULAR)
	{
		if (tt_move != MOVE_NONE)
		{
			ret.null();
			EXPAND(ret, tt_move);
			sent |= ret;
			stage++;
			stage_curr = TT_REGULAR;
			return ret;
		}
		else
		{
			stage++;
		}
	}	
	if (stage == DOUBLE_FOUR_REGULAR) // double four threat
	{
		ret.null();
		for (dir1 = 0; dir1 < 4; dir1++)
		{
			for (dir2 = dir1 + 1; dir2 < 4; dir2++)
			{
				ret |= (p -> st -> threat[p -> turn_glob][1][dir1] & p -> st -> threat[p -> turn_glob][1][dir2]);
			}
		}
		ret &= (~sent);
		if (!ret)
		{
			sent |= ret;
			stage++;
			stage_curr = DOUBLE_FOUR_REGULAR;
			return ret;
		}
		else
		{
			stage++;
		}
	}
	if (stage == FOUR_THREE_REGULAR)
	{
		if (!(!((p -> st) -> threat_type_unio[p -> turn_glob][0] & (p -> st) -> threat_type_unio[p -> turn_glob][1])))
		{
			stage++;
		}
		else
		{
			ret.null();
			for (dir1 = 0; dir1 < 4; dir1++)
			{
				for (dir2 = 0; dir2 < 4; dir2++)
				{
					ret |= (p -> st -> threat[p -> turn_glob][1][dir1] & p -> st -> threat[p -> turn_glob][2][dir2]);
				}
			}
			ret &= (~sent);
			if (!ret)
			{
				sent |= ret;
				stage++;
				stage_curr = FOUR_THREE_REGULAR;
				return ret;
			}
			else
			{
				stage++;
			}
		}			
	}
	if (stage == DOUBLE_THREE_REGULAR)
	{
		ret.null();
		for (dir1 = 0; dir1 < 4; dir1++)
		{
			for (dir2 = dir1 + 1; dir2 < 4; dir2++)
			{
				ret |= (p -> st -> threat[p -> turn_glob][2][dir1] & p -> st -> threat[p -> turn_glob][2][dir2]); 
			}
		}
		ret &= (~sent);
		stage++;
		if (!ret)
		{
			sent |= ret;
			stage_curr = DOUBLE_THREE_REGULAR;
			return ret;
		}		
	}
	if (stage == DOUBLE_FOUR_REGULAR_OPP) // double four threat
	{
		ret.null();
		for (dir1 = 0; dir1 < 4; dir1++)
		{
			for (dir2 = dir1 + 1; dir2 < 4; dir2++)
			{
				ret |= (p -> st -> threat[p -> turn_glob ^ 1][1][dir1] & p -> st -> threat[p -> turn_glob ^ 1][1][dir2]);
			}
		}
		ret &= (~sent);
		if (!ret)
		{
			sent |= ret;
			stage++;
			stage_curr = DOUBLE_FOUR_REGULAR_OPP;
			return ret;
		}
		else
		{
			stage++;
		}
	}
	if (stage == FOUR_THREE_REGULAR_OPP)
	{
		if (!(!((p -> st) -> threat_type_unio[p -> turn_glob ^ 1][0] & (p -> st) -> threat_type_unio[p -> turn_glob ^ 1][1])))
		{
			stage++;
		}
		else
		{
			ret.null();
			for (dir1 = 0; dir1 < 4; dir1++)
			{
				for (dir2 = 0; dir2 < 4; dir2++)
				{
					ret |= (p -> st -> threat[p -> turn_glob ^ 1][1][dir1] & p -> st -> threat[p -> turn_glob ^ 1][2][dir2]);
				}
			}
			ret &= (~sent);
			if (!ret)
			{
				sent |= ret;
				stage++;
				stage_curr = FOUR_THREE_REGULAR_OPP;
				return ret;
			}
			else
			{
				stage++;
			}
		}			
	}
	if (stage == INTERESTING_ATTACK_REGULAR) // based on interesting heuristic
	{
		while (1)
		{
			if (help -> move == END_OBJECT || YOUNG_LIMIT < counter)
			{
				stage = DOUBLE_THREE_REGULAR_OPP;
				help = (p -> st);
				counter = 0;
				break;
			}
			counter++;			
			ret.null();
			loop = ((p -> st) -> threat_unio[p -> turn_glob] & (~(help -> threat_unio[p -> turn_glob])));
			loop &= (~sent);
			while (!loop)
			{
				sq = bitScanForward(loop);
				if (is_interesting_move_threat(p, sq))
				{
					EXPAND(ret, sq);
				}
				REDUCE(loop, sq);
			}
			info = counter;
			help = help -> previous;
			if (!ret)
			{
				sent |= ret;
				stage_curr = INTERESTING_ATTACK_REGULAR;
				return ret;	
			}	
		}				
	}
	if (stage == DOUBLE_THREE_REGULAR_OPP)
	{
		ret.null();
		for (dir1 = 0; dir1 < 4; dir1++)
		{
			for (dir2 = dir1 + 1; dir2 < 4; dir2++)
			{
				ret |= (p -> st -> threat[p -> turn_glob ^ 1][2][dir1] & p -> st -> threat[p -> turn_glob ^ 1][2][dir2]); 
			}
		}
		ret &= (~sent);
		stage++;
		if (!ret)
		{
			sent |= ret;
			stage_curr = DOUBLE_THREE_REGULAR_OPP;
			return ret;
		}		
	}
	if (stage == INTERESTING_ATTACK_REGULAR_OPP)
	{
		while (1)
		{
			if (help -> move == END_OBJECT || YOUNG_LIMIT < counter)
			{
				stage = NOT_INTERESTING_ATTACK;
				help = (p -> st);
				counter = 0;
				break;
			}
			counter++;			
			ret.null();
			loop = ((p -> st) -> threat_unio[p -> turn_glob ^ 1] & (~(help -> threat_unio[p -> turn_glob ^ 1])));
			loop &= (~sent);
			p -> turn_glob ^= 1; // WE HACK
			while (!loop)
			{
				sq = bitScanForward(loop);
				if (is_interesting_move_threat(p, sq))
				{
					EXPAND(ret, sq);
				}
				REDUCE(loop, sq);
			}
			p -> turn_glob ^= 1; // WE RE HACK
			info = counter;
			help = help -> previous;
			if (!ret)
			{
				sent |= ret;
				stage_curr = INTERESTING_ATTACK_REGULAR_OPP;
				return ret;	
			}	
		}		
	}
	if (stage == NOT_INTERESTING_ATTACK)
	{
		while (1)
		{
			if (help -> move == END_OBJECT || YOUNG_LIMIT < counter)
			{
				stage = NOT_INTERESTING_ATTACK_OPP;
				help = (p -> st);
				counter = 0;
				break;
			}
			counter++;
			ret = ((p -> st) -> threat_unio[p -> turn_glob] & (~(help -> threat_unio[p -> turn_glob])));
			ret &= (~sent);
			info = counter;
			help = help -> previous;
			if (!ret)
			{
				sent |= ret;
				stage_curr = NOT_INTERESTING_ATTACK;
				return ret;
			}
		}
	}
	if (stage == NOT_INTERESTING_ATTACK_OPP)
	{
		while (1)
		{
			if (help -> move == END_OBJECT || YOUNG_LIMIT < counter)
			{
				stage = LOCAL1;
				help = (p -> st);
				counter = 0;
				break;
			}
			counter++;
			ret = ((p -> st) -> threat_unio[p -> turn_glob ^ 1] & (~(help -> threat_unio[p -> turn_glob ^ 1])));
			ret &= (~sent);
			info = counter;
			help = help -> previous;
			if (!ret)
			{
				sent |= ret;
				stage_curr = NOT_INTERESTING_ATTACK_OPP;
				return ret;
			}
		}
	}
	while (stage == LOCAL1 || stage == LOCAL2 || stage == LOCAL3)
	{
		if (help -> move == END_OBJECT)
		{
			stage = DIST1;
			help = (p -> st);
			passive[0].null();
			passive[1].null();
			break;
		}
		if (stage == LOCAL1)
		{
			ret = distance[0][help -> move];
			ret &= (~sent);
			ret &= legal;
			ret = interesting_move_homogen(p, ret);
			info = help -> move;
			stage++;
			if (!ret)
			{
				sent |= ret;
				stage_curr = LOCAL1;
				return ret;
			}						
		}
		if (stage == LOCAL2)
		{
			ret = distance[1][help -> move];
			ret &= (~sent);
			ret &= legal;
			ret = interesting_move_homogen(p, ret);
			info = help -> move;
			stage++;
			if (!ret)
			{
				sent |= ret;
				stage_curr = LOCAL2;
				return ret;
			}						
		}
		if (stage == LOCAL3)
		{
			ret = distance[2][help -> move];
			ret &= (~sent);
			ret &= legal;
			ret = interesting_move_homogen(p, ret);
			stage = LOCAL1;
			info = help -> move;
			help = help -> previous;
			if (!ret)
			{
				sent |= ret;
				stage_curr = LOCAL3;
				return ret;
			}						
		}			
	}
	if (stage == DIST1)
	{
		while (1)
		{
			if (help -> move == END_OBJECT)
			{
				stage++;
				help = (p -> st);
				break;
			}				
			ret = distance[0][help -> move];
			passive[0] |= (ret & side_distance[0]);
			passive[1] |= (ret & side_distance[1]);
			ret &= (~passive[0]);
			ret &= (~passive[1]);
			ret &= (~sent);
			ret &= legal;
			info = help -> move;
			help = help -> previous;
			if (!ret)
			{
				sent |= ret;
				stage_curr = DIST1;
				return ret;
			}								
		}
	} 	
	if (stage == DIST2)
	{
		while (1)
		{
			if (help -> move == END_OBJECT)
			{
				stage++;
				help = (p -> st);
				break;
			}				
			ret = distance[1][help -> move];
			passive[0] |= (ret & side_distance[0]);
			passive[1] |= (ret & side_distance[1]);
			ret &= (~passive[0]);
			ret &= (~passive[1]);
			ret &= (~sent);
			ret &= legal;
			info = help -> move;
			help = help -> previous;
			if (!ret)
			{
				sent |= ret;
				stage_curr = DIST2;
				return ret;
			}								
		}
	}
	if (stage == DIST3)
	{
		while (1)
		{
			if (help -> move == END_OBJECT)
			{
				stage++;
				help = (p -> st);
				break;
			}				
			ret = distance[2][help -> move];
			passive[0] |= (ret & side_distance[0]);
			passive[1] |= (ret & side_distance[1]);
			ret &= (~passive[0]);
			ret &= (~passive[1]);
			ret &= (~sent);
			ret &= legal;
			info = help -> move;
			help = help -> previous;
			if (!ret)
			{
				sent |= ret;
				stage_curr = DIST3;
				return ret;
			}								
		}
	}
	if (stage == PASSIVE1)
	{
		stage++;
		passive[1] &= (~sent);
		passive[1] &= legal;
		if (!passive[1])
		{
			sent |= passive[1];
			stage_curr = PASSIVE1;
			return passive[1];
		}		
	}	
	if (stage == PASSIVE0)
	{
		stage++;
		passive[0] &= (~sent);
		passive[0] &= legal;
		if (!passive[0])
		{
			sent |= passive[0];
			stage_curr = PASSIVE0;
			return passive[0];
		}			
	}		
	if (stage == END_REGULAR)
	{
		ret.null();
		ret = (~ret);
		ret &= (~(p -> square[0]));
		ret &= (~(p -> square[1]));
		ret &= (~(sent));
		stage_curr = END_REGULAR;
		return ret;
	}
	ret.null();
	return ret;
}

Picker_surewin::Picker_surewin (Position * p)
{	
	sent.null();
	stage = 0;	
	tt_move = MOVE_NONE;
	is_extreme_pos = p -> is_extreme_pos(); 

	if (is_extreme_pos)
	{
		regular.null();
		ft.null();
		p -> generate_regular(& regular);
		if (p ->five_threat == END_OBJECT)
		{
			p -> generate_ft(& ft);
			ft &= (~ regular);	
		}
		legal = (regular | ft);
	}
	else
	{
		p -> generate_legal(& legal);
	}	
}

table Picker_surewin::picker_surewin_attack(Position * p)
{
	int dir1, dir2, sq;
	table ret;
	table loop;

up:	
	if (stage == START_ATT)
	{
		assert((!legal));
		if (p -> five_threat != END_OBJECT) // we need to defend five threat
		{
			if (!(p -> st -> threat[p -> turn_glob][0][0]) || !(p -> st -> threat[p -> turn_glob][0][1]) || !(p -> st -> threat[p -> turn_glob][0][2]) || !(p -> st -> threat[p -> turn_glob][0][3])) //the opponent made a ft interruption		
			{
				stage = END_ATT;
				stage_curr = START_ATT;
				return legal;
			}
			else
			{
				stage = END_ATT;
				stage_curr = START_ATT;
				return (legal & ((p -> st) -> threat_unio[p -> turn_glob]));
			}				
		}
		stage++;
	}
	if (stage == TT_ATT)
	{
		if (tt_move != MOVE_NONE)
		{
			ret.null();
			EXPAND(ret, tt_move);
			sent |= ret;
			stage++;
			stage_curr = TT_DEF;
			return ret;
		}
		else
		{
			stage++;
		}		
	}	
	// we can not have ff, we use it	
	assert(stage == END_ATT || !(!(p -> st -> threat[p -> turn_glob][0][0] | p -> st -> threat[p -> turn_glob][0][1] | p -> st -> threat[p -> turn_glob][0][2] | p -> st -> threat[p -> turn_glob][0][3])));
	if (stage == DOUBLE_FOUR_ATT) // doble four threat
	{
		ret.null();
		for (dir1 = 0; dir1 < 4; dir1++)
		{
			for (dir2 = dir1 + 1; dir2 < 4; dir2++)
			{
				ret |= (p -> st -> threat[p -> turn_glob][1][dir1] & p -> st -> threat[p -> turn_glob][1][dir2]);
			}
		}
		ret &= (~sent);
		ret &= (legal);
		if (!ret)
		{
			sent |= ret;
			stage++;
			stage_curr = DOUBLE_FOUR_ATT;
			return ret;
		}
		else
		{
			stage++;
		}
	}
	if (stage == FOUR_THREE_ATT)
	{
		if (!(!((p -> st) -> threat_type_unio[p -> turn_glob][0] & (p -> st) -> threat_type_unio[p -> turn_glob][1])))
		{
			stage++;
		}
		else
		{
			ret.null();
			for (dir1 = 0; dir1 < 4; dir1++)
			{
				for (dir2 = 0; dir2 < 4; dir2++)
				{
					ret |= (p -> st -> threat[p -> turn_glob][1][dir1] & p -> st -> threat[p -> turn_glob][2][dir2]);
				}
			}
			ret &= (~sent);
			if (!ret)
			{
				sent |= ret;
				stage++;
				stage_curr = FOUR_THREE_ATT;
				return ret;
			}
			else
			{
				stage++;
			}
		}						
	}
	if (stage == INIT_ATT)
	{
		if (is_extreme_pos) //extreme pos
		{
			stage = COUNTER_ATTACK_REGULAR_DEFENSE_ATT;
		}
		else
		{
			stage++;
		}	
	}
	if (stage == DOUBLE_THREE_ATT)
	{
		ret.null();
		for (dir1 = 0; dir1 < 4; dir1++)
		{
			for (dir2 = dir1 + 1; dir2 < 4; dir2++)
			{
				ret |= (p -> st -> threat[p -> turn_glob][2][dir1] & p -> st -> threat[p -> turn_glob][2][dir2]); 
			}
		}
		ret &= (~sent);
		if (!ret)
		{
			sent |= ret;
			stage++;
			stage_curr = DOUBLE_THREE_ATT;
			return ret;
		}
		else
		{
			stage++;
		}			
	}
	if (stage == INTERESTING_ATT) // based on interesting heuristic
	{
		ret.null();
		if (depth_left <= 1)
		{
			return ret;
		}
		loop = ((p -> st) -> threat_unio[p -> turn_glob]) & legal; // we need this because we are here in the counter_attack case
		loop &= (~sent);
		interesting[0].null();
		interesting[1].null();
		interesting[2].null();
		interesting[3].null();
		while (!loop)
		{
			sq = bitScanForward(loop);
			interesting[how_interesting_move_surewin_attack(p, sq, attackline)].t[sq >> 6] |= (1ULL << (sq - ((sq >> 6) << 6)));
			loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
		}
		stage = INTERESTING_ATTACKLINE_OK_FT_ATT;						
	}
	if (stage == INTERESTING_ATTACKLINE_OK_FT_ATT)
	{
		ret = (interesting[3] & (p -> st) -> threat_type_unio[p -> turn_glob][0]);
		if (!ret)
		{
			stage_curr = INTERESTING_ATTACKLINE_OK_FT_ATT;
			stage++;
			return ret;
		}
		stage++;
	}
	if (stage == INTERESTING_ATTACKLINE_OK_TT_ATT)
	{
		ret = (interesting[3] & (p -> st) -> threat_type_unio[p -> turn_glob][1]);
		if (!ret)
		{
			stage_curr = INTERESTING_ATTACKLINE_OK_TT_ATT;
			stage++;
			return ret;
		}
		stage++;
	}
	if (stage == NOT_INTERESTING_ATTACKLINE_OK_ATT)
	{
		if (depth_left - INTERESTING_PENALITY <= 1)
		{
			goto skip_not_interesting;
		}
		if (!interesting[1])
		{
			stage_curr = NOT_INTERESTING_ATTACKLINE_OK_ATT;
			stage++;
			return interesting[1];
		}
skip_not_interesting:
		stage++;
	}
	if (stage == INTERESTING_NOT_ATTACKLINE_OK_ATT)
	{
		if (depth_left - ATTACKLINE_PENALITY <= 1)
		{
			goto skip_not_attackline;
		}
		if (!interesting[2])
		{
			stage_curr = INTERESTING_NOT_ATTACKLINE_OK_ATT;
			stage++;
			return interesting[2];
		}
skip_not_attackline:
		stage++;
	}
	if (stage == NOT_INTERESTING_NOT_ATTACKLINE_OK_ATT)
	{
		if (depth_left - ATTACKLINE_PENALITY - INTERESTING_PENALITY <= 1)
		{
			goto skip_not_interesting_not_attackline;
		}
		if (!interesting[0])
		{
			stage_curr = NOT_INTERESTING_NOT_ATTACKLINE_OK_ATT;
			stage = END_ATT;
			return interesting[0];
		}
skip_not_interesting_not_attackline:
		stage = END_ATT;
	}	
	if (stage == COUNTER_ATTACK_REGULAR_DEFENSE_ATT)
	{
		ret = regular & ((p -> st) -> threat_unio[p -> turn_glob]) & (~sent);
		if (!ret)
		{
			stage = INTERESTING_ATT;
			stage_curr = COUNTER_ATTACK_REGULAR_DEFENSE_ATT;
			sent |= ret;
			return ret;
		}
		stage = INTERESTING_ATT;
	}
	if (stage == END_ATT)
	{
		legal.null();
		stage_curr = END_ATT;
		return legal;
	}
	goto up;
}

table Picker::picker_defense(Position * p) // same as the surewin defense
{
	int dir1, dir2;
	table ret;
		
	if (stage == START_DEF)
	{
		if (!(!legal)) // no legal move
		{
			stage_curr = START_DEF;
			return legal;
		}
		if (p -> five_threat != END_OBJECT) // we need to defend five threat
		{
			stage = END_DEF;
			stage_curr = START_DEF;
			return legal;			
		}
		stage++;
	}
	if (stage == TT_DEF)
	{
		if (tt_move != MOVE_NONE)
		{
			ret.null();
			EXPAND(ret, tt_move);
			sent |= ret;
			stage++;
			stage_curr = TT_DEF;
			return ret;
		}
		else
		{
			stage++;
		}		
	}
	if (stage == DOUBLE_FOUR_DEF) // doble four threat
	{
		ret.null();
		for (dir1 = 0; dir1 < 4; dir1++)
		{
			for (dir2 = dir1 + 1; dir2 < 4; dir2++)
			{
				ret |= (p -> st -> threat[p -> turn_glob][1][dir1] & p -> st -> threat[p -> turn_glob][1][dir2]);
			}
		}
		if (!ret)
		{
			sent |= ret;
			stage++;
			stage_curr = DOUBLE_FOUR_DEF;
			return ret;
		}
		else
		{
			stage++;
		}
	}
	if (stage == FOUR_THREE_DEF)
	{
		ret.null();
		for (dir1 = 0; dir1 < 4; dir1++)
		{
			for (dir2 = 0; dir2 < 4; dir2++)
			{
				ret |= (p -> st -> threat[p -> turn_glob][1][dir1] & p -> st -> threat[p -> turn_glob][2][dir2]);
			}
		}
		ret &= (~sent);
		if (!ret)
		{
			sent |= ret;
			stage++;
			stage_curr = FOUR_THREE_DEF;
			return ret;
		}
		else
		{
			stage++;
		}			
	}
	if (stage == FIGHT_BACK_REGULAR_DEF)
	{
		if (!regular)
		{
			defender_threat = (p -> st -> threat[p -> turn_glob][0][0] | p -> st -> threat[p -> turn_glob][0][1] | p -> st -> threat[p -> turn_glob][0][2] | p -> st -> threat[p -> turn_glob][0][3] | p -> st -> threat[p -> turn_glob][1][0] | p -> st -> threat[p -> turn_glob][1][1] | p -> st -> threat[p -> turn_glob][1][2] | p -> st -> threat[p -> turn_glob][1][3] | p -> st -> threat[p -> turn_glob][2][0] | p -> st -> threat[p -> turn_glob][2][1] | p -> st -> threat[p -> turn_glob][2][2] | p -> st -> threat[p -> turn_glob][2][3]);						
			ret = ((defender_threat & regular) & (~sent));
			if (!ret)
			{
				sent |= ret;
				stage++;
				stage_curr = FIGHT_BACK_REGULAR_DEF;
				return ret;
			}
			stage++;
		}
		else
		{
			stage++;
		}
	}
	if (stage == FREE_FOUR_DEF)
	{
		if (!regular)
		{
			ret.null();
			for (dir1 = 0; dir1 < 4; dir1++)
			{
				ret |= ((p -> st) -> threat[(p -> turn_glob) ^ 1][0][dir1]);
			} 
			ret &= regular;
			ret &= (~sent);
			if (!ret)
			{
				sent |= ret;
				stage++;
				stage_curr = FREE_FOUR_DEF;
				return ret;
			}
			else
			{
				stage++;
			}
		}
		else
		{
			stage++;
		}
	}
	if (stage == REST_REGULAR_DEF)
	{
		if (!regular)
		{
			ret = regular;
			ret &= (~sent);
			if (!ret)
			{
				sent |= ret;
				stage++;
				stage_curr = REST_REGULAR_DEF;
				return ret;					
			}
			else
			{
				stage++;
			}
		}
		else
		{
			stage++;
		}			
	}
	if (stage == REST_FT_DEF)
	{
		ret = ft;
		ret &= (~sent);
		if (!ret)
		{
			sent |= ret;
			stage++;
			stage_curr = REST_FT_DEF;
			return ret;
		}
		else
		{
			stage++;
		}
	}	
	if (stage == END_DEF)
	{
		legal.null();
		stage_curr = END_DEF;
		return legal;
	}
	legal.null();
	return legal;
}

table Picker_surewin::picker_surewin_defense(Position * p)
{
	int dir1, dir2;
	table ret;
		
	if (stage == START_DEF)
	{
		if (!(!legal)) // no legal move
		{
			stage_curr = START_DEF;
			return legal;
		}
		if (p -> five_threat != END_OBJECT) // we need to defend five threat
		{
			stage = END_DEF;
			stage_curr = START_DEF;
			return legal;			
		}
		stage++;
	}
	if (stage == TT_DEF)
	{
		/*if (surewin_tt_probe(p, &tt_move, &depth, &easy) != INVALID)
		{
			ret.null();
			ret.t[tt_move >> 6] |= (1ULL << (tt_move - ((tt_move >> 6) << 6)));
			sent |= ret;
			stage++;
			stage_curr = TT_DEF;
			return ret;
		}*/
		stage++;		
	}
	if (stage == DOUBLE_FOUR_DEF) // doble four threat
	{
		ret.null();
		for (dir1 = 0; dir1 < 4; dir1++)
		{
			for (dir2 = dir1 + 1; dir2 < 4; dir2++)
			{
				ret |= (p -> st -> threat[p -> turn_glob][1][dir1] & p -> st -> threat[p -> turn_glob][1][dir2]);
			}
		}
		if (!ret)
		{
			sent |= ret;
			stage++;
			stage_curr = DOUBLE_FOUR_DEF;
			return ret;
		}
		else
		{
			stage++;
		}
	}
	if (stage == FOUR_THREE_DEF)
	{
		ret.null();
		for (dir1 = 0; dir1 < 4; dir1++)
		{
			for (dir2 = 0; dir2 < 4; dir2++)
			{
				ret |= (p -> st -> threat[p -> turn_glob][1][dir1] & p -> st -> threat[p -> turn_glob][2][dir2]);
			}
		}
		ret &= (~sent);
		if (!ret)
		{
			sent |= ret;
			stage++;
			stage_curr = FOUR_THREE_DEF;
			return ret;
		}
		else
		{
			stage++;
		}			
	}
	if (stage == FIGHT_BACK_REGULAR_DEF)
	{
		if (!regular)
		{
			defender_threat = (p -> st -> threat[p -> turn_glob][0][0] | p -> st -> threat[p -> turn_glob][0][1] | p -> st -> threat[p -> turn_glob][0][2] | p -> st -> threat[p -> turn_glob][0][3] | p -> st -> threat[p -> turn_glob][1][0] | p -> st -> threat[p -> turn_glob][1][1] | p -> st -> threat[p -> turn_glob][1][2] | p -> st -> threat[p -> turn_glob][1][3] | p -> st -> threat[p -> turn_glob][2][0] | p -> st -> threat[p -> turn_glob][2][1] | p -> st -> threat[p -> turn_glob][2][2] | p -> st -> threat[p -> turn_glob][2][3]);						
			ret = ((defender_threat & regular) & (~sent));
			if (!ret)
			{
				sent |= ret;
				stage++;
				stage_curr = FIGHT_BACK_REGULAR_DEF;
				return ret;
			}
			stage++;
		}
		else
		{
			stage++;
		}
	}
	if (stage == FREE_FOUR_DEF)
	{
		if (!regular)
		{
			ret.null();
			for (dir1 = 0; dir1 < 4; dir1++)
			{
				ret |= ((p -> st) -> threat[(p -> turn_glob) ^ 1][0][dir1]);
			} 
			ret &= regular;
			ret &= (~sent);
			if (!ret)
			{
				sent |= ret;
				stage++;
				stage_curr = FREE_FOUR_DEF;
				return ret;
			}
			else
			{
				stage++;
			}
		}
		else
		{
			stage++;
		}
	}
	if (stage == REST_REGULAR_DEF)
	{
		if (!regular)
		{
			ret = regular;
			ret &= (~sent);
			if (!ret)
			{
				sent |= ret;
				stage++;
				stage_curr = REST_REGULAR_DEF;
				return ret;					
			}
			else
			{
				stage++;
			}
		}
		else
		{
			stage++;
		}			
	}
	if (stage == REST_FT_DEF)
	{
		ret = ft;
		ret &= (~sent);
		if (!ret)
		{
			sent |= ret;
			stage++;
			stage_curr = REST_FT_DEF;
			return ret;
		}
		else
		{
			stage++;
		}
	}	
	if (stage == END_DEF)
	{
		legal.null();
		stage_curr = END_DEF;
		return legal;
	}
	legal.null();
	return legal;
}

Picker_fourwin::Picker_fourwin (Position * p)
{	
	sent.null();
	legal.null();	
	stage = 0;	
	p -> generate_legal(& legal);	
}

table Picker_fourwin::picker_fourwin(Position * p)
{
	int dir1, dir2, sq;
	table ret;
	table loop;

	if (stage == START_FOURWIN)
	{
		assert((!legal));
		if (p -> five_threat != END_OBJECT) // we need to defend five threat
		{
			stage = END_FOURWIN;
			stage_curr = START_FOURWIN;
			loop.null();
			p -> generate_ft(& loop);			
			return (legal & loop);			
		}
		stage++;
	}
	// we can not have ff, we use it	
	assert(stage == END_FOURWIN || !(!(p -> st -> threat[p -> turn_glob][0][0] | p -> st -> threat[p -> turn_glob][0][1] | p -> st -> threat[p -> turn_glob][0][2] | p -> st -> threat[p -> turn_glob][0][3])));
	if (stage == DOUBLE_FOUR_FOURWIN) // doble four threat
	{
		ret.null();
		for (dir1 = 0; dir1 < 4; dir1++)
		{
			for (dir2 = dir1 + 1; dir2 < 4; dir2++)
			{
				ret |= (p -> st -> threat[p -> turn_glob][1][dir1] & p -> st -> threat[p -> turn_glob][1][dir2]);
			}
		}
		ret &= (~sent);
		ret &= (legal);
		if (!ret)
		{
			sent |= ret;
			stage++;
			stage_curr = DOUBLE_FOUR_FOURWIN;
			return ret;
		}
		else
		{
			stage++;
		}
	}
	if (stage == FOUR_THREE_FOURWIN)
	{
		if (!(!((p -> st) -> threat_type_unio[p -> turn_glob][0] & (p -> st) -> threat_type_unio[p -> turn_glob][1])))
		{
			stage++;
		}
		else
		{
			ret.null();
			for (dir1 = 0; dir1 < 4; dir1++)
			{
				for (dir2 = 0; dir2 < 4; dir2++)
				{
					ret |= (p -> st -> threat[p -> turn_glob][1][dir1] & p -> st -> threat[p -> turn_glob][2][dir2]);
				}
			}
			ret &= (~sent);
			if (!ret)
			{
				sent |= ret;
				stage++;
				stage_curr = FOUR_THREE_FOURWIN;
				return ret;
			}
			else
			{
				stage++;
			}
		}						
	}
	if (stage == INTERESTING_FOURWIN) // based on interesting heuristic
	{
		ret.null();
		if (depth_left <= 1)
		{
			return ret;
		}
		loop = ((p -> st) -> threat_type_unio[p -> turn_glob][0]) & legal; // we need this because we are here in the counter_attack case
		loop &= (~sent);
		interesting[0].null();
		interesting[1].null();
		interesting[2].null();
		interesting[3].null();
		while (!loop)
		{
			sq = bitScanForward(loop);
			interesting[how_interesting_move_surewin_attack(p, sq, attackline)].t[sq >> 6] |= (1ULL << (sq - ((sq >> 6) << 6)));
			loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
		}
		stage = INTERESTING_ATTACKLINE_OK_FOURWIN;						
	}
	if (stage == INTERESTING_ATTACKLINE_OK_FOURWIN)
	{
		ret = interesting[3];
		if (!ret)
		{
			stage_curr = INTERESTING_ATTACKLINE_OK_FOURWIN;
			stage++;
			return ret;
		}
		stage++;
	}
	if (stage == NOT_INTERESTING_ATTACKLINE_OK_FOURWIN)
	{
		if (depth_left - INTERESTING_PENALITY <= 1)
		{
			goto skip_not_interesting;
		}
		if (!interesting[1])
		{
			stage_curr = NOT_INTERESTING_ATTACKLINE_OK_FOURWIN;
			stage++;
			return interesting[1];
		}
skip_not_interesting:
		stage++;
	}
	if (stage == INTERESTING_NOT_ATTACKLINE_OK_FOURWIN)
	{
		if (depth_left - ATTACKLINE_PENALITY <= 1)
		{
			goto skip_not_attackline;
		}
		if (!interesting[2])
		{
			stage_curr = INTERESTING_NOT_ATTACKLINE_OK_FOURWIN;
			stage++;
			return interesting[2];
		}
skip_not_attackline:
		stage++;
	}
	if (stage == NOT_INTERESTING_NOT_ATTACKLINE_OK_FOURWIN)
	{
		if (depth_left - ATTACKLINE_PENALITY - INTERESTING_PENALITY <= 1)
		{
			goto skip_not_interesting_not_attackline;
		}
		if (!interesting[0])
		{
			stage_curr = NOT_INTERESTING_NOT_ATTACKLINE_OK_FOURWIN;
			stage = END_FOURWIN;
			return interesting[0];
		}
skip_not_interesting_not_attackline:
		stage = END_FOURWIN;
	}	
	if (stage == END_FOURWIN)
	{
		legal.null();
		stage_curr = END_FOURWIN;
		return legal;
	}
	legal.null();
	return legal;
}


void print_picker_regular(Position * p, int limit)
{
	table ret, loop;
	Picker pick(p);
	
	ret.null();
	while (!(loop = pick.picker(p)))
	{
		ret |= loop;
		if (pick.stage_curr == limit)
		{
			break;
		}
	}	
	print_general(p, ret);
	return;
}




