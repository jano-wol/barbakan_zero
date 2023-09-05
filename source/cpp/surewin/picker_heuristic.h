#include "position.h"

int how_interesting_move_surewin_attack(Position *, int, table); //for surewin
bool is_attackline_ok(table, int); //for surewin

bool is_interesting_move_threat(Position *, int); //interesting we consider the threats also
bool is_interesting_move_homogen(Position *, int); //interesting we consider only already played pieces, its homogen
table interesting_move_threat(Position * p, table);
table interesting_move_homogen(Position * p, table);
table local_defense(Position * p, int);
table interesting_linear(Position * p);
