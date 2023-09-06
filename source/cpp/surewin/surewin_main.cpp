#include <stdio.h>
#include <stdlib.h>
#include "position.h"
#include "surewin_search.h"
#include "tt.h"
#include "evaluate.h"
#include "picker.h"
#include "picker_heuristic.h"
#include "extend.h"
#include "types.h"

//void print_board_root(Position *);
//StateInfo st_main[1000];

/*int surewin_main(int argc, char *argv[])
{
	int ply;
	int move;
	int i, j, sq;
	Position root;
	FILE * f;
	table t, s, loop;
	char st_mainr[256];
	char line[256];
		
	ply=0;
	surewin_tt_setsize (10250000); 
	shallow_tt_setsize (1000000);
	tt_setsize (10250000);
	surewin_tt_kill();
	tt_kill();	
	root.init();
	t.null();
	t = (~t);	
	
	sprintf(st_mainr, "games_path_here", argv[1]);
	if ((f=fopen(st_mainr, "r"))!=0)
	{
		if (fgets(line, 256, f) == NULL) // no need for the first_main line
		{
			printf("error\n");
			return 0;
		}
		j=0;
		while (1)
		{
			if (fgets(line, 256, f) == NULL)
			{
				printf("error\n");
				return 0;
			}
			if (strlen(line)>4)
			{
				if ('0' <= line[0] && line[0] <= '9')
				{
					sscanf(line, "%d,%d", &i, &j);
					root.do_move(i - 1 + (j - 1) * BOARDS, st_main[ply++]);
				}
				else
				{
					break;
				}
			}
			else
			{
				break;
			}
		}						
		fclose(f);
	}
	print = (argc == 3);

	for(;;)
	{
		root.print_board();
		printf("Key:%ld\n", root.st -> key);
		printf("Five:%d\n", root.five_threat);
		if (scanf("%d", &move) == 1)
		{
			if (move < 0)
			{
				if (move == -1)
				{
					root.undo_move();
					ply--;
				}
				if (move == -2)
				{
					root.nodes = 0;
					i = 1;
					for (; true; i++)
					{	
						search_root (&root, -WINNING_VALUE, WINNING_VALUE, i * PLY);		
						printf ("Push -1 to continue!\n");			
						if (scanf("%d", &move) == 1)
						{
							if (move != -1)
							{
								shallow_tt_kill();
								break;
							}
						}							
					}						
				}
				if (move == -6)
				{
					printf("Extend surewin defense\n");
					root.nodes = 0;
					for (i = 2 * PLY_SUREWIN; true; i += (2 * PLY_SUREWIN))
					{
						if (i >= 16)
						{
							printf("no win in depth %d. Conitune?", i - 2);
							if (scanf("%d", &move) == 1)
							{
								if (move == 0)
								{
									break;
								}
							}							
						}
						if (surewin_extend_defense(&root, i, 0, t, false) == 1)
						{
							printf("depth:%d DEFENSE MOVE:%d %ld\n", i, surewin_winner_move, root.nodes);
						}
						else
						{
							printf("RESIGN\n");
							break;
						}
					}		
				}								
				if (move == -7)
				{
					printf("Extend surewin\n");
					root.nodes = 0;
					for (i = 1 * PLY_SUREWIN; true; i += (2 * PLY_SUREWIN))
					{
						if (i >= 17)
						{
							printf("no win in depth %d. Conitune?", i - 2);
							if (scanf("%d", &move) == 1)
							{
								if (move == 0)
								{
									break;
								}
							}							
						}
						if (surewin_extend_attack(&root, i, 0, t, false) == 1)
						{
							printf(":-) depth:%d MOVE:%d %ld\n", i, surewin_winner_move, root.nodes);
							break;
						}
						printf("%d depth ready %ld\n", i, root.nodes);
					}		
				}				
				if (move == -8)
				{
					printf("Easy surewin\n");
					root.nodes = 0;
					for (i = 1 * PLY_SUREWIN; true; i += (2 * PLY_SUREWIN))
					{
						if (i >= 27)
						{
							printf("no win in depth %d. Conitune?", i - 2);
							if (scanf("%d", &move) == 1)
							{
								if (move == 0)
								{
									break;
								}
							}							
						}
						if (surewin_attack(&root, i, 0, t, true) == 1)
						{
							printf(":-) depth:%d %ld\n", i, root.nodes);
							break;
						}
						printf("%d depth ready %ld\n", i, root.nodes);
					}		
				}				
				if (move == -9)
				{
					root.nodes = 0;
					for (i = 1 * PLY_SUREWIN; true; i += (2 * PLY_SUREWIN))
					{
						if (i >= 27)
						{
							printf("no win in depth %d. Conitune?", i - 2);
							if (scanf("%d", &move) == 1)
							{
								if (move == 0)
								{
									break;
								}
							}							
						}
						if (surewin_attack(&root, i, 0, t, false) == 1)
						{
							printf(":-) depth:%d %ld\n", i, root.nodes);
							break;
						}
						printf("%d depth ready %ld\n", i, root.nodes);
					}		
				}				
				if (move == -10)
				{
					root.nodes = 0;
					loop.null();
					s.null();
					root.generate_legal(&loop);				
					while (!loop)
					{
						sq = bitScanForward(loop);	
						if (root.do_move(sq, st_main[ply]) == WINNING_MOVE)
						{
							root.undo_move();						
							REDUCE(loop, sq);							
							EXPAND(s, sq);
							continue;
						}
						if (root.is_extreme_pos())
						{
							EXPAND(s, sq);
						}
						else
						{
							root.turn_glob ^= 1;
							root.st -> key = root.null_move_key();
							if (is_fourwin(&root, 11 * PLY_SUREWIN, 0, t) == 1)
							{
								EXPAND(s, sq);
							}
							root.turn_glob ^= 1;
							root.st -> key = root.null_move_key();
						}
						root.undo_move();						
						REDUCE(loop, sq);				
					}
					print_general(&root, s);
					printf("Is_four moves %ld\n", root.nodes);
				}				
				if (move == -11)
				{
					root.nodes = 0;
					loop.null();
					s.null();
					root.generate_legal(&loop);				
					while (!loop)
					{
						sq = bitScanForward(loop);	
						if (root.do_move(sq, st_main[ply]) == WINNING_MOVE)
						{
							root.undo_move();						
							REDUCE(loop, sq);							
							EXPAND(s, sq);
							continue;
						}
						if (root.is_extreme_pos())
						{
							EXPAND(s, sq);
						}
						else
						{
							root.turn_glob ^= 1;
							root.st -> key = root.null_move_key();
							if (surewin_attack(&root, 13 * PLY_SUREWIN, 0, t, true))
							{
								EXPAND(s, sq);
							}
							root.turn_glob ^= 1;
							root.st -> key = root.null_move_key();
						}
						root.undo_move();						
						REDUCE(loop, sq);				
					}
					print_general(&root, s);
					printf("Attacking moves %ld\n", root.nodes);
				}
				if (move == -12)
				{
					root.nodes = 0;
					loop.null();
					s.null();
					root.generate_legal(&loop);				
					while (!loop)
					{
						sq = bitScanForward(loop);	
						if (root.do_move(sq, st_main[ply]) == WINNING_MOVE)
						{
							root.undo_move();						
							REDUCE(loop, sq);							
							EXPAND(s, sq);
							continue;
						}
						if (root.is_extreme_pos())
						{
							EXPAND(s, sq);
						}
						else
						{
							if (surewin_attack(&root, 13 * PLY_SUREWIN, 0, t, true) != 1)
							{
								EXPAND(s, sq);
							}
						}
						root.undo_move();						
						REDUCE(loop, sq);				
					}
					print_general(&root, s);
					printf("Defending moves %ld\n", root.nodes);
				}
				if (move == -13)
				{
					Picker_extend pick(&root);
					while (!(loop = pick.picker_surewin_extend_defense(&root)))
					{
						print_general(&root, loop);
						getchar();
					}
					continue;				
				}																			
				if (move == -3)
				{
					return 0;
				}
				if (move == -4)
				{
					print_picker_regular(&root, INTERESTING_ATTACK_REGULAR + 2);
					getchar();
					getchar();
					continue;
				}
				if (move == -5)
				{
					print_general(&root, interesting_linear(&root));
					getchar();
					getchar();
				}
			}
			else
			{
				if ((root.square[0].t[move >> 6] & (1LL << (move - ((move >> 6) << 6)))) || (root.square[1].t[move >> 6] & (1LL << (move - ((move >> 6) << 6)))))
				{
					printf("Invalid move!");
					continue;
				}
				root.do_move(move, st_main[ply++]);
			}
		}
	}	
}*/
