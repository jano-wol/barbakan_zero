#include <stdio.h> 
#include "surewin_search.h"
#include "tt.h"
#include "evaluate.h"
#include "picker.h"

#define SUREWIN_CHECK_DEPTH 21

StateInfo st_search[BOARDS*BOARDS];
StateInfo st_surewin[BOARDS*BOARDS];
int surewin_winner_move;
int root_best_move;
int root_value;
int state[TSIZE];

int search_root (Position * p, int alpha, int beta, uint8_t depth_left)
{
	int sq, i;
	int16_t val;
	uint16_t tt_move, best_move;
	int16_t tt_val;
	uint8_t tt_flag, tt_depth;
	table loop;
	
	best_move = MOVE_NONE;
	
	if (print)
	{
		p->print_board();
		printf("ply:%d depth_left:%d\n", 0, depth_left);
		getchar();
	}
	Picker pick(p);
	// is there legal move
	if (!(!pick.legal))
	{
		return MOVE_NONE;
	}

	//2. surewin attack may help	
	if (true)
	{
		loop.null();
		loop = (~loop);
		for (i = 1 * PLY_SUREWIN; i <= SUREWIN_CHECK_DEPTH + 7; i += (2 * PLY_SUREWIN))
		{
			if (surewin_attack(p, i, 0, loop, false) == 1)
			{
				printf("MOVE:%d value:%d nodes:%ld depth:%d\n", surewin_winner_move, WINNING_VALUE, p -> nodes, depth_left);
				root_best_move = surewin_winner_move;
				root_value =  WINNING_VALUE;
				return surewin_winner_move;
			}
		}	
	}	
	//TT
	tt_probe(p, &tt_move, &tt_val, &tt_flag, &tt_depth);
	if (tt_val != INVALID)
	{
		pick.tt_move = tt_move;
		if (tt_depth >= depth_left)
		{
			if (tt_flag == TT_VALUE_EXACT)
			{
				if (tt_val < alpha)
				{
					root_value =  alpha;					
					return tt_move;
				}
				if (tt_val > beta)
				{
					root_value = beta;
					return tt_move;
				}
				root_value = tt_val;
				printf("MOVE:%d value:%d nodes:TT depth:%d\n", tt_move, tt_val, tt_depth);
				return tt_move;
			}
			if (tt_flag == TT_VALUE_MORE)
			{
				if (tt_val <= alpha)
				{
					root_value =  alpha;					
					return tt_move;
				}
			}
			if (tt_flag == TT_VALUE_LESS)
			{
				if (tt_val >= beta)
				{
					root_value =  beta;					
					return tt_move;
				}
			}
		}
	}
	
	for (i = 0; i < TSIZE; i++)
	{
		state[i] = 0;
	}
	
	while (!(loop = pick.picker(p)))
	{
		if (pick.stage_curr == END_REGULAR)
		{
			break;
		}
		while (!loop)
		{
			if (pick.stage_curr >= DIST1)
			{
				break;
			}			
			sq = bitScanForward(loop);
			if ((p -> do_move(sq, st_search[0])) == WINNING_MOVE)
			{
				printf("MOVE:%d value:%d nodes:%ld depth:%d\n", sq, beta, p -> nodes, depth_left);
				p -> undo_move();
				root_best_move = sq;
				root_value =  beta;	
				tt_save(p, WINNING_VALUE, TT_VALUE_EXACT, sq, 100);			
				return sq;		
			}
			val = - search (p, - beta, - alpha, MAX(depth_left - PLY + (PLY - 1) * (pick.is_extreme_pos == true), 0), 1);
			
			p -> undo_move();
			REDUCE(loop, sq);
			
			if (val > alpha)
			{
				best_move = sq;
				printf("m:%d value:%d\n", sq, val);
				if (val >= beta)
				{
					//assert(false);
					alpha=beta;
					tt_save(p, alpha, TT_VALUE_LESS, best_move, depth_left);
					goto out;
				}
				alpha = val;
			}
		}
	}
	if (best_move == MOVE_NONE)
	{
		tt_save(p, alpha, TT_VALUE_MORE, best_move, depth_left);
	}
	else
	{
		tt_save(p, alpha, TT_VALUE_EXACT, best_move, depth_left);
	}	
out:
	printf("MOVE:%d value:%d nodes:%ld depth:%d s_tt_index:%d\n", best_move, alpha, p -> nodes, depth_left / PLY, shallow_tt_index);
	i = 1;
	while (state[i] != 0)
	{
		printf("ply:%d %d, ", i, state[i]);
		i++;
	}
	printf("\n");
	tt_info(p);
	root_best_move = best_move;
	root_value =  alpha;		
	return best_move;	
}

int16_t search (Position * p, int alpha, int beta, uint8_t depth_left, uint8_t ply)
{
	int sq, i, s_t_p;
	int16_t val;
	uint16_t tt_move, best_move;
	int16_t tt_val;
	uint8_t tt_flag, tt_depth;
	table loop;
	
	best_move = MOVE_NONE;
	state[ply]++;
	
	if (print)
	{
		p -> print_board();
		printf("%ld\n", p -> st -> key);
		getchar();
	}
			
	//1. step is there legal move
	Picker pick(p);
	if (!(!pick.legal))
	{
		if (!(~(p->square[0] | p->square[1]))) // somebody win
		{
			return - (WINNING_VALUE);
		}
		else //draw
		{
			return 0;
		}
	}

	//2. TT
	tt_probe(p, &tt_move, &tt_val, &tt_flag, &tt_depth);
	if (tt_val != INVALID)
	{
		pick.tt_move = tt_move;

		if (tt_depth >= depth_left)
		{
			if (tt_flag == TT_VALUE_EXACT)
			{
				if (tt_val < alpha)
				{		
					return alpha;
				}
				if (tt_val > beta)
				{
					return beta;
				}
				return tt_val;
			}
			if (tt_flag == TT_VALUE_MORE)
			{
				if (tt_val <= alpha)
				{			
					return alpha;
				}
			}
			if (tt_flag == TT_VALUE_LESS)
			{
				if (tt_val >= beta)
				{		
					return beta;
				}
			}
		}
	}	
	//2. Is stable position
	/*if (ply <= 3)
	{
		s_t_p = shallow_tt_probe(p);
		if (s_t_p == 0)
		{
			return WINNING_VALUE;
		}
		if (s_t_p == 1)
		{
			goto stable_end;
		}
		if (s_t_p == INVALID)
		{
			loop.null();
			loop = (~loop);
			for (i = 1 * PLY_SUREWIN; i <= SUREWIN_CHECK_DEPTH; i += (2 * PLY_SUREWIN))
			{
				if (surewin_attack(p, i, 0, loop, false) == 1)
				{
					shallow_tt_save(p, 0);
					return WINNING_VALUE;
				}
			}
			shallow_tt_save(p, 1);
		}	
	}
stable_end:
* */
	
	//3. step end of search
	if (depth_left <= 0)
	{
		return evaluate(p);
	}			
	
	//4. step loop through the moves
	while (!(loop = pick.picker(p)))
	{
		if (pick.stage_curr >= DIST1)
		{
			break;
		}
		while (!loop)
		{
			sq = bitScanForward(loop);
			if ((p -> do_move(sq, st_search[ply])) == WINNING_MOVE)
			{
				p -> undo_move();
				tt_save(p, WINNING_VALUE, TT_VALUE_EXACT, sq, 100);
				return (WINNING_VALUE);		
			}
			val = - search (p, - beta, - alpha, MAX(depth_left - PLY + (PLY - 1) * (pick.is_extreme_pos == true), 0), ply + 1);
			
			p -> undo_move();
			REDUCE(loop, sq);
			
			if (val > alpha)
			{
				best_move = sq;
				if (val >= beta)
				{
					alpha=beta;
					tt_save(p, alpha, TT_VALUE_LESS, best_move, depth_left);
					goto out;
				}
				alpha = val;
			}
		}
	}
	if (best_move == MOVE_NONE)
	{
		tt_save(p, alpha, TT_VALUE_MORE, best_move, depth_left);
	}
	else
	{
		tt_save(p, alpha, TT_VALUE_EXACT, best_move, depth_left);
	}			
out:	
	return alpha;
}

int surewin_attack (Position * p, int8_t depth_left, uint8_t ply, table attackline, bool easy)
{
	int sq;
	table new_attackline;
	table loop;	
	

	if (p->nodes > 5000)
	{
		return 0;
	}
	if (depth_left < 0)
	{
		return 0;
	}
		
	Picker_surewin pick(p);
	pick.depth_left = depth_left;
	pick.attackline = attackline;
	// is there legal move
	if (!(!pick.legal))
	{
		if (!(~(p->square[0] | p->square[1]))) // somebody win
		{
			return 0;
		}
		else //draw
		{
			return 0;
		}
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
	
	while (!(loop = pick.picker_surewin_attack(p)))
	{
		while (!loop)
		{
			sq = bitScanForward(loop);
			if (p -> five_threat != END_OBJECT || pick.stage_curr == COUNTER_ATTACK_REGULAR_DEFENSE_ATT)
			{
				new_attackline = attackline;
				new_attackline.t[sq >> 6] |= (1ULL << (sq - ((sq >> 6) << 6)));
			}
			else
			{
				new_attackline.null();
				new_attackline.t[sq >> 6] |= (1ULL << (sq - ((sq >> 6) << 6)));				
			}
			if ((p -> do_move(sq, st_surewin[ply])) == WINNING_MOVE)
			{
				if (ply == 0)
				{
					surewin_winner_move = sq;
				}
				p -> undo_move();	
				surewin_tt_save(p, SUREWIN_WIN, sq, 0, easy);
				return 1;		
			}
			if (surewin_defense(p, depth_left - PLY_SUREWIN - (pick.stage_curr == INTERESTING_NOT_ATTACKLINE_OK_ATT || pick.stage_curr == NOT_INTERESTING_NOT_ATTACKLINE_OK_ATT) * ATTACKLINE_PENALITY - (pick.stage_curr == NOT_INTERESTING_ATTACKLINE_OK_ATT || pick.stage_curr == NOT_INTERESTING_NOT_ATTACKLINE_OK_ATT) * INTERESTING_PENALITY, ply + 1, new_attackline, easy) != 1)
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
			if (pick.stage > DOUBLE_THREE_ATT)
			{
				break;
			}			
		}
		if (depth_left == 3)
		{
			if (pick.stage_curr >= INTERESTING_ATTACKLINE_OK_TT_ATT) //if (pick.stage_curr >= INTERESTING_ATTACKLINE_OK_FT) maybe better
			{
				break;
			}			
		}
	}
	return 0;
}

int surewin_defense (Position * p, int8_t depth_left, uint8_t ply, table attackline, bool easy)
{
	int sq;
	int new_depth;
	table loop;	
	
	if (p->nodes > 5000)
	{
		return 1;
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
		
	Picker_surewin pick(p);
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
		
	while (!(loop = pick.picker_surewin_defense(p)))
	{
		if (easy)
		{
			if (pick.stage_curr == REST_FT_DEF)
			{
				break;
			}
		}
		while (!loop)
		{
			sq = bitScanForward(loop);
			if ((p -> do_move(sq, st_surewin[ply])) == WINNING_MOVE)
			{
				p -> undo_move();
				surewin_tt_save(p, SUREWIN_NO_WIN, sq, 100, easy);	
				return 1;		
			}
			if (surewin_attack(p, depth_left - PLY_SUREWIN  + ((pick.stage_curr == REST_FT_DEF) * 2), ply + 1, attackline, easy) != 1)
			{
				p -> undo_move();
				surewin_tt_save(p, SUREWIN_NO_WIN, sq, depth_left, easy);
				return 1;
			}
			p -> undo_move();		
			loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
		}
	}
	return 0;		
}

bool is_fourwin (Position * p, int8_t depth_left, uint8_t ply, table attackline)
{
	int sq;
	table new_attackline;
	table loop;
	table help;	
	
	if (print)
	{
		/*p->print_board();
		printf("ply:%d depth_left:%d\n", 0, depth_left);
		getchar();*/
	}	
	
	if (depth_left < 0)
	{
		return false;
	}
		
	Picker_fourwin pick(p);
	pick.depth_left = depth_left;
	pick.attackline = attackline;
	// is there legal move
	if (!(!pick.legal))
	{
		if (!(~(p->square[0] | p->square[1]))) // somebody win
		{
			return false;
		}
		else //draw
		{
			return false;
		}
	}
	
	while (!(loop = pick.picker_fourwin(p)))
	{
		while (!loop)
		{
			sq = bitScanForward(loop);
			if (ply == 0)
			{
				new_attackline.null();
				EXPAND(new_attackline, sq);
			}
			else
			{
				new_attackline = attackline;
				EXPAND(new_attackline, sq);				
			}
			if ((p -> do_move(sq, st_surewin[ply])) == WINNING_MOVE)
			{
				p -> undo_move();	
				return true;		
			}
			help.null();
			p -> generate_legal(&help);
			if (!(!help))
			{
				p -> undo_move();	
				return true;
			}
			if (((p -> do_move(p -> five_threat, st_surewin[ply + 1])) == WINNING_MOVE) || is_fourwin(p, depth_left - 2 * PLY_SUREWIN - (pick.stage_curr == INTERESTING_NOT_ATTACKLINE_OK_FOURWIN || pick.stage_curr == NOT_INTERESTING_NOT_ATTACKLINE_OK_FOURWIN) * ATTACKLINE_PENALITY - (pick.stage_curr == NOT_INTERESTING_ATTACKLINE_OK_FOURWIN || pick.stage_curr == NOT_INTERESTING_NOT_ATTACKLINE_OK_FOURWIN) * INTERESTING_PENALITY, ply + 2, new_attackline) == false)
			{
				p -> undo_move();
				p -> undo_move();		
				REDUCE(loop, sq);
			}
			else
			{
				p -> undo_move();
				p -> undo_move();		
				return true;			
			}
		}
		if (depth_left == 1)
		{
			if (pick.stage > FOUR_THREE_FOURWIN)
			{
				break;
			}			
		}
		if (depth_left == 3)
		{
			if (pick.stage_curr >= INTERESTING_ATTACKLINE_OK_FOURWIN) //if (pick.stage_curr >= INTERESTING_ATTACKLINE_OK_FT) maybe better
			{
				break;
			}			
		}
	}
	return 0;
}

bool is_fourwin (Position * p, int8_t depth_left, uint8_t ply, table attackline, int * winning_sequence)
{
	int sq, i;
	table new_attackline;
	table loop;
	table help;	
	
	if (print)
	{
		/*p->print_board();
		for (i = 0; i < ply; i++)
		{
			printf("%d\n", winning_sequence[i]);
		}
		printf("ply:%d depth_left:%d\n", 0, depth_left);
		getchar();*/
	}	
	
	if (depth_left < 0)
	{
		return false;
	}
		
	Picker_fourwin pick(p);
	pick.depth_left = depth_left;
	pick.attackline = attackline;
	// is there legal move
	if (!(!pick.legal))
	{
		if (!(~(p->square[0] | p->square[1]))) // somebody win
		{
			return false;
		}
		else //draw
		{
			return false;
		}
	}
	
	while (!(loop = pick.picker_fourwin(p)))
	{
		while (!loop)
		{
			sq = bitScanForward(loop);
			if (ply == 0)
			{
				new_attackline.null();
				EXPAND(new_attackline, sq);
			}
			else
			{
				new_attackline = attackline;
				EXPAND(new_attackline, sq);				
			}
			winning_sequence[ply] = sq;
			if ((p -> do_move(sq, st_surewin[ply])) == WINNING_MOVE)
			{
				winning_sequence[ply + 1] = TSIZE;
				p -> undo_move();	
				return true;		
			}
			help.null();
			p -> generate_legal(&help);
			if (!(!help))
			{
				winning_sequence[ply + 1] = TSIZE;
				p -> undo_move();	
				return true;
			}
			winning_sequence[ply + 1] = p -> five_threat;
			if (((p -> do_move(p -> five_threat, st_surewin[ply + 1])) == WINNING_MOVE) || is_fourwin(p, depth_left - 2 * PLY_SUREWIN - (pick.stage_curr == INTERESTING_NOT_ATTACKLINE_OK_FOURWIN || pick.stage_curr == NOT_INTERESTING_NOT_ATTACKLINE_OK_FOURWIN) * ATTACKLINE_PENALITY - (pick.stage_curr == NOT_INTERESTING_ATTACKLINE_OK_FOURWIN || pick.stage_curr == NOT_INTERESTING_NOT_ATTACKLINE_OK_FOURWIN) * INTERESTING_PENALITY, ply + 2, new_attackline, winning_sequence) == false)
			{
				p -> undo_move();
				p -> undo_move();		
				REDUCE(loop, sq);
			}
			else
			{
				p -> undo_move();
				p -> undo_move();		
				return true;			
			}
		}
		if (depth_left == 1)
		{
			if (pick.stage > FOUR_THREE_FOURWIN)
			{
				break;
			}			
		}
		if (depth_left == 3)
		{
			if (pick.stage_curr >= INTERESTING_ATTACKLINE_OK_FOURWIN) //if (pick.stage_curr >= INTERESTING_ATTACKLINE_OK_FT) maybe better
			{
				break;
			}			
		}
	}
	return 0;
}

