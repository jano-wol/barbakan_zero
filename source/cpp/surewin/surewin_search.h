#include "position.h"

int search_root (Position *, int, int, uint8_t);
int16_t search (Position *, int, int, uint8_t, uint8_t);
int16_t quiet (Position *, int, int, uint8_t);
int surewin_attack (Position *, int8_t, uint8_t, table, bool);
int surewin_defense (Position *, int8_t, uint8_t, table, bool);
bool is_fourwin (Position *, int8_t, uint8_t, table);
bool is_fourwin (Position *, int8_t, uint8_t, table, int *);

extern int surewin_winner_move;
extern int root_best_move;
extern int root_value;
