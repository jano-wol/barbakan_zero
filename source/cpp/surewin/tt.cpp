#include <stdio.h>
#include <string.h>
#include "tt.h"

StateInfo st_tt[BOARDS*BOARDS];
struct surewin_tt_entry * surewin_tt;
struct shallow_tt_entry * shallow_tt;
struct tt_entry * tt;
int shallow_tt_index;
int surewin_tt_size;
int shallow_tt_size;
int tt_size;
int best_move_safe;

int surewin_tt_setsize (int size) 
{ 
	int i;
	
	free(surewin_tt); 
	if (size & (size - 1)) 
	{ 
		size--;
		for (i=1; i<32; i=i*2)
		{
			size |= size >> i;
		}
		size++;
		size>>=1;
	}
	
	if (size < 16) 
	{
		surewin_tt_size = 0;
		return 0;
	}
	
	surewin_tt_size = 524287;
	surewin_tt = (surewin_tt_entry *) malloc((surewin_tt_size + 1) * sizeof(surewin_tt_entry));
	//printf("tt_size:%d\n", tt_size);
	//printf("size stt_entry:%ld\n", sizeof(stt_entry));
	return 0;
}

int16_t surewin_tt_probe(Position * p, uint16_t * best, uint8_t * depth, bool * easy) 
{
	struct surewin_tt_entry * phashe;
	phashe = &surewin_tt[((p->st)->key) & surewin_tt_size];
 
	if (phashe->hash == (p->st)->key) 
	{ 
		if ((phashe->square[0] != p-> square[0]) || (phashe->square[1] != p-> square[1]) || (p -> turn_glob != phashe -> turn))
		{
			printf("TT keey coll\n");
			getchar();
		}
		*best = phashe->bestmove;
		*easy = phashe->easy;
		*depth = phashe->depth;
		return phashe->val;
	}
	return INVALID;
}

void surewin_tt_save(Position * p, int16_t val, uint16_t best, uint8_t depth, bool easy)
{
	struct surewin_tt_entry * phashe;
	phashe = &surewin_tt[((p->st)->key) & surewin_tt_size];

	phashe->hash = (p->st)->key;
	phashe->val = val;
	phashe->bestmove = best;
	phashe->square[0] = p-> square[0];
	phashe->square[1] = p-> square[1];
	phashe->depth = depth;
	phashe->easy = easy;
	phashe-> turn = p->turn_glob;
}

void surewin_tt_kill()
{
	memset(surewin_tt, 0, (surewin_tt_size+1)*sizeof(struct surewin_tt_entry));
}

int shallow_tt_setsize (int size) 
{ 
	shallow_tt_index = 0;
	free(shallow_tt); 
	shallow_tt_size = size;
	shallow_tt = (shallow_tt_entry *) malloc(shallow_tt_size * sizeof(shallow_tt_entry));
	return 0;
}

int16_t shallow_tt_probe(Position * p) 
{
	int i;
	
	for (i = 0; i < shallow_tt_index; i++)
	{
		if (shallow_tt[i].hash == (p -> st) -> key)
		{
			return shallow_tt[i].is_stable;
		}
	}
	return INVALID;
}

void shallow_tt_save(Position * p, int16_t is_stable)
{
	shallow_tt[shallow_tt_index].hash = p -> st -> key;
	shallow_tt[shallow_tt_index].is_stable = is_stable;
	shallow_tt_index++;
	assert (shallow_tt_index != shallow_tt_size);
}

void shallow_tt_kill()
{
	shallow_tt_index = 0;
	memset(shallow_tt, 0, (shallow_tt_size * sizeof(struct shallow_tt_entry)));
}

int tt_setsize (int size) 
{ 
	int i;
	
	free(tt); 
	if (size & (size - 1)) 
	{ 
		size--;
		for (i=1; i<32; i=i*2)
		{
			size |= size >> i;
		}
		size++;
		size>>=1;
	}
	
	if (size < 16) 
	{
		surewin_tt_size = 0;
		return 0;
	}
	
	tt_size = 524287;
	tt = (tt_entry *) malloc((tt_size + 1) * sizeof(tt_entry));
	//printf("tt_size:%d\n", tt_size);
	//printf("size stt_entry:%ld\n", sizeof(stt_entry));
	return 0;
}

void tt_probe(Position * p, uint16_t * bestmove, int16_t * val, uint8_t * flag, uint8_t * depth) 
{
	struct tt_entry * phashe;
	phashe = &tt[((p->st)->key) & tt_size];
 
	if (phashe->hash == (p->st)->key) 
	{
		if ((phashe->square[0] != p-> square[0]) || (phashe->square[1] != p-> square[1]) || (p -> turn_glob != phashe -> turn))
		{
			printf("TT keey coll\n");
			getchar();
		}
		*bestmove = phashe -> bestmove;
		*val = phashe -> val;
		*flag = phashe -> flag;	
		*depth = phashe -> depth;
		return;
	}
	*val = INVALID;
	return;
}

void tt_save(Position * p, int16_t val, uint8_t flag, uint16_t best, uint8_t depth)
{
	struct tt_entry * phashe;
	phashe = &tt[((p->st)->key) & tt_size];

	phashe->hash = (p->st)->key;
	phashe->val = val;
	phashe->bestmove = best;
	phashe->flag = flag;
	phashe->depth = depth;
	phashe->square[0] = p-> square[0];
	phashe->square[1] = p-> square[1];
	phashe-> turn = p->turn_glob;
}

void tt_kill()
{
	memset(tt, 0, (tt_size+1)*sizeof(struct tt_entry));
}

void tt_info(Position * p)
{
	int ply;
	int i;
	int all_nb;
	int cut_nb;
	int pv_nb;
	
	uint16_t tt_move;
	int16_t tt_val;
	uint8_t tt_flag, tt_depth;
	all_nb=cut_nb=pv_nb=0;
	
	ply=0;
	//print pv
	while (1)
	{
		tt_probe(p, &tt_move, &tt_val, &tt_flag, &tt_depth);
		if (tt_move == MOVE_NONE || tt_val == INVALID)
		{
			break;
		}
		printf ("%d ", tt_move);
		p->do_move(tt_move, st_tt[ply++]);
	}
	while (ply>0)
	{
		p->undo_move();
		ply--;
	}
	printf("\n");
	
	printf("tt_size:%d\n", tt_size+1);
	for(i=0; i<tt_size+1; i++)
	{
		if (tt[i].hash!=0)
		{
			if (tt[i].flag==TT_VALUE_MORE)
			{
				all_nb++;
			}
			if (tt[i].flag==TT_VALUE_LESS)
			{
				cut_nb++;
			}
			if (tt[i].flag==TT_VALUE_EXACT)
			{
				pv_nb++;
			}
		}
	}
	printf("pv:%d cut:%d all:%d\n", pv_nb, cut_nb, all_nb);
}


