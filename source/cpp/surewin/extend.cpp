#include <stdio.h> 
#include "extend.h"
#include "surewin_search.h"
#include "tt.h"
#include "picker_heuristic.h"

StateInfo st_extend[BOARDS*BOARDS];
StateInfo st_extend_defense[BOARDS*BOARDS];

Picker_extend::Picker_extend (Position * p)
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
		ply = 0;
	}	
}

table Picker_extend::picker_surewin_extend_attack(Position * p)
{
	int dir1, dir2, sq;
	table ret;
	table loop;
	table t;
	StateInfo st;	

up:	
	if (stage == START_EXTEND)
	{
		assert((!legal));
		if (p -> five_threat != END_OBJECT) // we need to defend five threat
		{
			if (!(p -> st -> threat[p -> turn_glob][0][0]) || !(p -> st -> threat[p -> turn_glob][0][1]) || !(p -> st -> threat[p -> turn_glob][0][2]) || !(p -> st -> threat[p -> turn_glob][0][3])) //the opponent made a ft interruption		
			{
				stage = END_EXTEND;
				stage_curr = START_EXTEND;
				sent |= legal;
				return legal;
			}
			goto card;		
		}
		stage++;
	}
	if (stage == TT_EXTEND)
	{
		if (tt_move != MOVE_NONE)
		{
			ret.null();
			EXPAND(ret, tt_move);
			sent |= ret;
			stage++;
			stage_curr = TT_EXTEND;
			return ret;
		}
		else
		{
			stage++;
		}		
	}	
	// we can not have ff, we use it	
	assert(stage == END_EXTEND || !(!(p -> st -> threat[p -> turn_glob][0][0] | p -> st -> threat[p -> turn_glob][0][1] | p -> st -> threat[p -> turn_glob][0][2] | p -> st -> threat[p -> turn_glob][0][3])));
	if (stage == DOUBLE_FOUR_EXTEND) // doble four threat
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
			stage_curr = DOUBLE_FOUR_EXTEND;
			return ret;
		}
		else
		{
			stage++;
		}
	}
	if (stage == FOUR_THREE_EXTEND)
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
				stage_curr = FOUR_THREE_EXTEND;
				return ret;
			}
			else
			{
				stage++;
			}
		}						
	}
	if (stage == INIT_EXTEND)
	{
		if (is_extreme_pos) //extreme pos
		{
			stage = COUNTER_ATTACK_REGULAR_DEFENSE_EXTEND;
		}
		else
		{
			stage++;
		}	
	}
	if (stage == DOUBLE_THREE_EXTEND)
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
			stage_curr = DOUBLE_THREE_EXTEND;
			return ret;
		}
		else
		{
			stage++;
		}			
	}
	if (stage == INTERESTING_EXTEND) // based on interesting heuristic
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
		stage = INTERESTING_ATTACKLINE_OK_FT_EXTEND;						
	}
	if (stage == INTERESTING_ATTACKLINE_OK_FT_EXTEND)
	{
		ret = (interesting[3] & (p -> st) -> threat_type_unio[p -> turn_glob][0]);
		if (!ret)
		{
			stage_curr = INTERESTING_ATTACKLINE_OK_FT_EXTEND;
			stage++;
			sent |= ret;
			return ret;
		}
		stage++;
	}
	if (stage == INTERESTING_ATTACKLINE_OK_TT_EXTEND)
	{
		ret = (interesting[3] & (p -> st) -> threat_type_unio[p -> turn_glob][1]);
		if (!ret)
		{
			stage_curr = INTERESTING_ATTACKLINE_OK_TT_EXTEND;
			stage++;
			sent |= ret;
			return ret;
		}
		stage++;
	}
	if (stage == NOT_INTERESTING_ATTACKLINE_OK_EXTEND)
	{
		if (depth_left - INTERESTING_PENALITY <= 1)
		{
			goto skip_not_interesting;
		}
		if (!interesting[1])
		{
			stage_curr = NOT_INTERESTING_ATTACKLINE_OK_EXTEND;
			stage++;
			sent |= interesting[1];
			return interesting[1];
		}
skip_not_interesting:
		stage++;
	}
	if (stage == INTERESTING_NOT_ATTACKLINE_OK_EXTEND)
	{
		if (depth_left - ATTACKLINE_PENALITY <= 1)
		{
			goto skip_not_attackline;
		}
		if (!interesting[2])
		{
			stage_curr = INTERESTING_NOT_ATTACKLINE_OK_EXTEND;
			stage++;
			sent |= interesting[2];
			return interesting[2];
		}
skip_not_attackline:
		stage++;
	}
	if (stage == NOT_INTERESTING_NOT_ATTACKLINE_OK_EXTEND)
	{
		if (depth_left - ATTACKLINE_PENALITY - INTERESTING_PENALITY <= 1)
		{
			goto skip_not_interesting_not_attackline;
		}
		if (!interesting[0])
		{
			stage_curr = NOT_INTERESTING_NOT_ATTACKLINE_OK_EXTEND;
			stage = EXTEND;
			sent |= interesting[0];
			return interesting[0];
		}
skip_not_interesting_not_attackline:
		stage = EXTEND;
	}	
	if (stage == COUNTER_ATTACK_REGULAR_DEFENSE_EXTEND)
	{
card:			
		ret = regular & ((p -> st) -> threat_unio[p -> turn_glob]) & (~sent);
		if (!ret)
		{
			stage = INTERESTING_EXTEND;
			stage_curr = COUNTER_ATTACK_REGULAR_DEFENSE_EXTEND;
			sent |= ret;
			return ret;
		}
		stage = INTERESTING_EXTEND;
	}
	if (stage == EXTEND)
	{
		// no normal winning move, we try to extend
		ret.null();	
		if (depth_left > 13) //5 maybe better
		{	
			t.null();
			t = (~t);
			loop = (legal & (~sent));			
			while (!loop)
			{
				sq = bitScanForward(loop);	
				if (p -> do_move(sq, st) == WINNING_MOVE)
				{
					p -> undo_move();
					EXPAND(ret, sq);
					stage_curr = EXTEND;
					stage = END_EXTEND;	
					sent |= ret;										
					return ret;
				}
				if (p -> is_extreme_pos())
				{
					p -> print_board();
					assert(false);
					EXPAND(ret, sq);
				}
				else
				{
					p -> turn_glob ^= 1;
					p -> st -> key = p -> null_move_key();
					if (is_fourwin(p, 11 * PLY_SUREWIN, 0, t) == 1) //5 maybe better
					{
						EXPAND(ret, sq);
					}
					p -> turn_glob ^= 1;
					p -> st -> key = p -> null_move_key();
					p -> undo_move();						
					REDUCE(loop, sq);				
				}
			}		
		}
		if (!ret)
		{
			stage_curr = EXTEND;
			stage = END_EXTEND;
			sent |= ret;
			return ret;
		}		
		stage = END_EXTEND;		
	}
	if (stage == END_EXTEND)
	{
		legal.null();
		stage_curr = END_EXTEND;
		return legal;
	}
	goto up;
}

table Picker_extend::picker_surewin_extend_defense(Position * p)
{
	int dir1, dir2, sq, i, winning_move;
	table ret, t;
		
	if (stage == START_DEF_EXTEND)
	{
		if (!(!legal)) // no legal move
		{
			stage_curr = START_DEF_EXTEND;
			return legal;
		}
		if (p -> five_threat != END_OBJECT) // we need to defend five threat
		{
			stage = END_DEF_EXTEND;
			stage_curr = START_DEF_EXTEND;
			return legal;			
		}
		stage++;
	}			
	if (stage == TT_DEF_EXTEND)
	{
		/*if (surewin_tt_probe(p, &tt_move, &easy) != INVALID)
		{
			ret.null();
			ret.t[tt_move >> 6] |= (1ULL << (tt_move - ((tt_move >> 6) << 6)));
			sent |= ret;
			stage++;
			stage_curr = TT_DEF_EXTEND;
			return ret;
		}*/
		stage++;		
	}
	if (stage == DOUBLE_FOUR_DEF_EXTEND) // doble four threat
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
			stage_curr = DOUBLE_FOUR_DEF_EXTEND;
			return ret;
		}
		else
		{
			stage++;
		}
	}
	if (stage == FOUR_THREE_DEF_EXTEND)
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
			if ((!is_extreme_pos))
			{
				stage = EXTEND_DEF_REGULAR;
			}
			else
			{
				stage = FIGHT_BACK_REGULAR_DEF_EXTEND;
			}
			stage_curr = FOUR_THREE_DEF_EXTEND;
			return ret;
		}
		else
		{
			if ((!is_extreme_pos))
			{
				stage = EXTEND_DEF_REGULAR;
			}
			else
			{
				stage = FIGHT_BACK_REGULAR_DEF_EXTEND;
			}				
		}			
	}
	if ((stage == EXTEND_DEF_REGULAR) && (!is_extreme_pos)) //IN EXTEND CASE IT MAY HAPPEN
	{
again:
		stage = EXTEND_DEF_REGULAR;
		stage_curr = EXTEND_DEF_REGULAR;
		if (ply == 0)
		{
			t.null();
			t = ~t;
			p -> turn_glob ^= 1;
			if (is_fourwin(p, 11, 0, t, winning_sequence))
			{
				;
			}	
			else
			{
				p -> print_board();
				assert(false);
			}
			p -> turn_glob ^= 1;			
		}
		ret.null();
		if (winning_sequence[ply] != TSIZE)
		{
			EXPAND(ret, winning_sequence[ply]);
			ret &= ~sent;
			sent |= ret;
			ply++;
			if (!ret)
			{
				return ret;
			}
			else
			{
				goto again;
			}
		}
		//assert (p -> five_threat != END_OBJECT);
		//EXPAND(ret, p -> five_threat);
		ret &= ~sent;
		sent |= ret;
		ply++;
		stage = EXTEND_DEF_IRREGULAR;
		if (!ret)
		{
			return ret;
		}
	}
	if ((stage == EXTEND_DEF_IRREGULAR) && (!is_extreme_pos))
	{
		stage_curr = EXTEND_DEF_IRREGULAR;
		p -> turn_glob ^= 1;
		i = 0;
		ret.null();	
		while (winning_sequence[i] != TSIZE)
		{
			winning_move = p -> do_move(winning_sequence[i], st_extend_defense[i]);
			if ((i % 2) == 1)
			{
				ret |= (p -> st -> threat[p -> turn_glob ^ 1][0][0] & ~(p -> st -> previous -> threat[p -> turn_glob ^ 1][0][0]));
				ret |= (p -> st -> threat[p -> turn_glob ^ 1][0][1] & ~(p -> st -> previous -> threat[p -> turn_glob ^ 1][0][1]));
				ret |= (p -> st -> threat[p -> turn_glob ^ 1][0][2] & ~(p -> st -> previous -> threat[p -> turn_glob ^ 1][0][2]));
				ret |= (p -> st -> threat[p -> turn_glob ^ 1][0][3] & ~(p -> st -> previous -> threat[p -> turn_glob ^ 1][0][3]));
				ret |= (p -> st -> threat[p -> turn_glob ^ 1][1][0] & ~(p -> st -> previous -> threat[p -> turn_glob ^ 1][1][0]));
				ret |= (p -> st -> threat[p -> turn_glob ^ 1][1][1] & ~(p -> st -> previous -> threat[p -> turn_glob ^ 1][1][1]));
				ret |= (p -> st -> threat[p -> turn_glob ^ 1][1][2] & ~(p -> st -> previous -> threat[p -> turn_glob ^ 1][1][2]));
				ret |= (p -> st -> threat[p -> turn_glob ^ 1][1][3] & ~(p -> st -> previous -> threat[p -> turn_glob ^ 1][1][3]));			
			}
			i++;
		}
		if (winning_move == WINNING_MOVE)
		{
			p -> undo_move();
			ret |= p -> why_win(winning_sequence[i-1]);
		}
		else
		{
			EXPAND(ret, p -> five_threat);
			dir2 = p -> five_threat_dir(winning_sequence[i-1]);
			for (dir1 = 0; dir1 < 4; dir1++)
			{
				if (dir1 == dir2)
				{
					continue;
				}
				if (!(p -> st -> threat[p -> turn_glob ^ 1][0][dir1])) 	
				{
					sq = bitScanForward(p -> st -> threat[p -> turn_glob ^ 1][0][dir1]);
					break;
				}
			}
			ret |= p -> generate_single_regular_fast(p -> turn_glob, sq, dir1, 0);		
		}
		while (i > 0)
		{
			if (winning_move == WINNING_MOVE && i == 1)
			{
				break;
			}
			p -> undo_move();
			i--;
		}
		p -> turn_glob ^= 1;
		ret &= ~sent;
		sent |= ret;
		stage = EXTEND_DEF_REST;
		if (!ret)
		{
			return ret;
		}
	}
	if ((stage == EXTEND_DEF_REST) && (!is_extreme_pos))	
	{
		ret.null();
		p -> generate_ft(& ret);	
		ret &= ~sent;
		stage = END_DEF_EXTEND;
		if (!ret)
		{
			stage_curr = EXTEND_DEF_REST;
			return ret;
		}
	}		
	if (stage == FIGHT_BACK_REGULAR_DEF_EXTEND)
	{
		if (!regular)
		{
			defender_threat = (p -> st -> threat[p -> turn_glob][0][0] | p -> st -> threat[p -> turn_glob][0][1] | p -> st -> threat[p -> turn_glob][0][2] | p -> st -> threat[p -> turn_glob][0][3] | p -> st -> threat[p -> turn_glob][1][0] | p -> st -> threat[p -> turn_glob][1][1] | p -> st -> threat[p -> turn_glob][1][2] | p -> st -> threat[p -> turn_glob][1][3] | p -> st -> threat[p -> turn_glob][2][0] | p -> st -> threat[p -> turn_glob][2][1] | p -> st -> threat[p -> turn_glob][2][2] | p -> st -> threat[p -> turn_glob][2][3]);						
			ret = ((defender_threat & regular) & (~sent));
			if (!ret)
			{
				sent |= ret;
				stage++;
				stage_curr = FIGHT_BACK_REGULAR_DEF_EXTEND;
				return ret;
			}
			stage++;
		}
		else
		{
			stage++;
		}
	}
	if (stage == FREE_FOUR_DEF_EXTEND)
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
				stage_curr = FREE_FOUR_DEF_EXTEND;
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
	if (stage == REST_REGULAR_DEF_EXTEND)
	{
		if (!regular)
		{
			ret = regular;
			ret &= (~sent);
			if (!ret)
			{
				sent |= ret;
				stage++;
				stage_curr = REST_REGULAR_DEF_EXTEND;
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
	if (stage == REST_FT_DEF_EXTEND)
	{
		ret = ft;
		ret &= (~sent);
		if (!ret)
		{
			sent |= ret;
			stage++;
			stage_curr = REST_FT_DEF_EXTEND;
			return ret;
		}
		else
		{
			stage++;
		}
	}	
	if (stage == END_DEF_EXTEND)
	{
		legal.null();
		stage_curr = END_DEF_EXTEND;
		return legal;
	}
	legal.null();
	return legal;
}

int surewin_extend_attack (Position * p, int8_t depth_left, uint8_t ply, table attackline, bool easy)
{
	int sq;
	table new_attackline;
	table loop, t;	
	
	if (print)
	{
		p -> print_board();
		printf("%ld\n", p -> st -> key);
		getchar();
	}
	if (depth_left < 0)
	{
		return 0;
	}
		
	Picker_extend pick(p);
	pick.depth_left = depth_left;
	pick.attackline = attackline;
	// is there legal move
	if (!(!pick.legal))
	{
//		if (!(~(p->square[0] | p->square[1]))) // somebody win
//		{
//			return 0;
//		}
//		else //draw
//		{
//			return 0;
//		}
        return 0;
	}
	
	//check tt
	uint16_t tt_move;
	uint8_t tt_depth;
	bool easy_tt;
	if (surewin_tt_probe(p, &tt_move, &tt_depth, &easy_tt) == SUREWIN_WIN)
	{
		if (easy_tt == easy || (easy_tt == false))
		{
			if (ply == 0)
			{
				surewin_winner_move = tt_move;
			}
			return 1;
		}
		else
		{
			pick.tt_move = tt_move;
		}
	}
	
	while (!(loop = pick.picker_surewin_extend_attack(p)))
	{
		while (!loop)
		{
			sq = bitScanForward(loop);
			if (p -> five_threat != END_OBJECT || pick.stage_curr == COUNTER_ATTACK_REGULAR_DEFENSE_EXTEND)
			{
				new_attackline = attackline;
				new_attackline.t[sq >> 6] |= (1ULL << (sq - ((sq >> 6) << 6)));
			}
			else
			{
				new_attackline.null();
				new_attackline.t[sq >> 6] |= (1ULL << (sq - ((sq >> 6) << 6)));				
			}
			if ((p -> do_move(sq, st_extend[ply])) == WINNING_MOVE)
			{
				if (ply == 0)
				{
					surewin_winner_move = sq;
				}
				p -> undo_move();	
				surewin_tt_save(p, SUREWIN_WIN, sq, 0, easy);
				return 1;		
			}
			if (surewin_extend_defense(p, depth_left - PLY_SUREWIN - (pick.stage_curr == INTERESTING_NOT_ATTACKLINE_OK_EXTEND || pick.stage_curr == NOT_INTERESTING_NOT_ATTACKLINE_OK_EXTEND) * ATTACKLINE_PENALITY - (pick.stage_curr == NOT_INTERESTING_ATTACKLINE_OK_EXTEND || pick.stage_curr == NOT_INTERESTING_NOT_ATTACKLINE_OK_EXTEND) * INTERESTING_PENALITY, ply + 1, new_attackline, easy) != 1)
			{
				if (ply == 0)
				{
					surewin_winner_move = sq;
				}
				p -> undo_move();
				surewin_tt_save(p, SUREWIN_WIN, sq, 0, easy);
				return 1;
			}
			p -> undo_move();		
			loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
		}
		if (depth_left == 1)
		{
			if (pick.stage > DOUBLE_THREE_EXTEND)
			{
				break;
			}			
		}
		if (depth_left == 3)
		{
			if (pick.stage_curr >= INTERESTING_ATTACKLINE_OK_TT_EXTEND) //if (pick.stage_curr >= INTERESTING_ATTACKLINE_OK_FT) maybe better
			{
				break;
			}			
		}
		if (pick.stage == EXTEND)
		{
			t.null();
			t = (~t);
			if (is_fourwin(p, 11 * PLY_SUREWIN, 0, t))
			{
				return 1;
			}
		}		
	}
	return 0;
}

int surewin_extend_defense (Position * p, int8_t depth_left, uint8_t ply, table attackline, bool easy)
{
	int sq;
	table loop;	
	
	if (print)
	{
		p -> print_board();
		printf("%ld\n", p -> st -> key);
		getchar();
	}
	if (depth_left < 0)
	{
		return 1;
	}
	if (IS_SIX_WINS == 0) // in this case it may happen that after our move we dont have ff
	{
		if ((p -> five_threat != END_OBJECT) || !(p-> st -> threat[(p -> turn_glob) ^ 1][0][0]) || !(p -> st -> threat[(p -> turn_glob) ^ 1][0][1]) || !(p -> st -> threat[(p -> turn_glob) ^ 1][0][2]) || !(p -> st -> threat[(p -> turn_glob) ^ 1][0][3])) //extrem position
		{
			;
		}
		else
		{
			return 1;
		}
	}
		
	Picker_extend pick(p);
	pick.depth_left = depth_left;
	
	if (depth_left == 0 && !(pick.regular))
	{
		return 1;
	}
	// is there legal move
	if (!(!pick.legal))
	{
		if (!(~(p->square[0] | p->square[1]))) // somebody win
		{
			return 0;
		}
		else //draw
		{
			return 1;
		}
	}
	// check tt
	uint16_t tt_move;
	uint8_t tt_depth;
	bool easy_tt;
	if (surewin_tt_probe(p, &tt_move, &tt_depth, &easy_tt) == SUREWIN_NO_WIN)
	{
		if (easy_tt == easy || (easy_tt == false))
		{
			if (tt_depth >= depth_left)
			{
				return 1;
			}
		}
	}		
		
	while (!(loop = pick.picker_surewin_extend_defense(p)))
	{
		if (easy)
		{
			if (pick.stage_curr == REST_FT_DEF_EXTEND)
			{
				break;
			}
		}
		while (!loop)
		{
			sq = bitScanForward(loop);
			if ((p -> do_move(sq, st_extend[ply])) == WINNING_MOVE)
			{
				p -> undo_move();
				surewin_tt_save(p, SUREWIN_NO_WIN, sq, 100, easy);	
				if (ply == 0)
				{
					surewin_winner_move = sq;
				}				
				return 1;		
			}
			if (surewin_extend_attack(p, depth_left - PLY_SUREWIN  + ((pick.stage_curr == REST_FT_DEF_EXTEND || pick.stage_curr == EXTEND_DEF_REST) * 2), ply + 1, attackline, easy) != 1)
			{
				p -> undo_move();
				surewin_tt_save(p, SUREWIN_NO_WIN, sq, depth_left, easy);
				if (ply == 0)
				{
					surewin_winner_move = sq;
				}				
				return 1;
			}
			p -> undo_move();		
			loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
		}
	}
	return 0;		
}

