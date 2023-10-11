#include "../game/board.h"

/*
 * board.cpp
 * Originally from an unreleased project back in 2010, modified since.
 * Authors: brettharrison (original), David Wu (original and later modificationss).
 */

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#include "../core/rand.h"

using namespace std;

const int Table::index64[64] = 
{
	0, 47,  1, 56, 48, 27,  2, 60,
	57, 49, 41, 37, 28, 16,  3, 61,
	54, 58, 35, 52, 50, 42, 21, 44,
	38, 32, 29, 23, 17, 11,  4, 62,
	46, 55, 26, 59, 40, 36, 15, 53,
	34, 51, 20, 43, 31, 22, 10, 45,
	25, 39, 14, 33, 19, 30,  9, 24,
	13, 18,  8, 12,  7,  6,  5, 63
};

Table::Table()
{
  null();
}

Table::Table(const Table& other)
{
	memcpy(t, other.t, sizeof(uint64_t)*8);
}

ThreatHandler::ThreatHandler()
{
	init(19, 1);
}

ThreatHandler::ThreatHandler(int boardS, int sixW)
{
	init(boardS, sixW);
}

ThreatHandler::ThreatHandler(const ThreatHandler& other)
{
	boardSize = other.boardSize;
	six_wins = other.six_wins;
	square[0] = other.square[0];
	square[1] = other.square[1];
	legal = other.legal;
	five_threat = other.five_threat;

	for (int i = 0; i < 2; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			for (int k = 0; k < MAX_LEN_THREATHANDLER + MAX_LEN_THREATHANDLER - 1; k++)
			{
				linear_bit[i][j][k] = other.linear_bit[i][j][k]; 
			}
		}
	}
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < MAX_LEN_THREATHANDLER + MAX_LEN_THREATHANDLER - 1; j++)
		{
			border_bit[i][j] = other.border_bit[i][j]; 
		}
	}
	for (int i = 0; i < 2; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			for (int k = 0; k < 4; k++)
			{
				threat[i][j][k] = other.threat[i][j][k]; 
			}
		}
	}
}

void ThreatHandler::print(uint32_t line, uint32_t border)
{
	for (int i = 0; i < 32; i++)
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

void ThreatHandler::print(uint32_t line)
{
	for (int i = 0; i < 32; i++)
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

void ThreatHandler::print(const Table& s)
{	
	for (int i = 0; i < boardSize * boardSize; i++)
	{
		if ((square[0].t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			printf("O ");
			if (i % boardSize == boardSize - 1)
			{
				printf("\n");
			}
			continue;
		}
		if ((square[1].t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			printf("X ");
			if (i % boardSize == boardSize - 1)
			{
				printf("\n");
			}
			continue;
		}
		if ((s.t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			printf("* ");
			if (i % boardSize == boardSize - 1)
			{
				printf("\n");
			}
			continue;
		}
		printf(". ");
		if (i % boardSize == boardSize - 1)
		{
			printf("\n");
		}
	}	
}

void ThreatHandler::init(int boardS, int sixW) //it initialize the position globals, and make a clear board 
{
	boardSize = boardS;
	six_wins = sixW;
	square[0].null();
	square[1].null();
	legal.null();
	legal = (~legal);
	five_threat = NO_FIVE;
	
	for (int i = 0; i < 2; i++)
	{
		for (int j = 0; j < 4; j++)
		{
			for (int k = 0; k < boardSize + boardSize - 1; k++)
			{
				linear_bit[i][j][k] = 0;
			}
		}
	}
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < boardSize + boardSize - 1; j++)
		{
			border_bit[i][j] = 0;			
			if (i == 0 || i == 1)
			{				
				for (int k = 0; k < 32; k++)
				{
					if ((k < 5) || (5 + boardSize <= k))
					{
						border_bit[i][j] |= (1ULL << k);
					}
				}
			}
			if (i == 2 || i == 3)
			{
				for (int k = 0; k < 32; k++)
				{
					if (j <= boardSize - 1)
					{
						if ((k < 5) || (5 + j + 1 <= k))
						{
							border_bit[i][j] |= (1ULL << k);
						}
					}
					else
					{
						if ((k < 5 + j - (boardSize - 1)) || ((boardSize + 5) <= k))
						{
							border_bit[i][j] |= (1ULL << k);
						}					
					}
				}				
			}
		}
	}
	for (int i = 0; i < 2; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			for (int k = 0; k < 4; k++)
			{
				threat[i][j][k].null();
			}
		}
	}
}

bool ThreatHandler::setStartPosition(const std::vector<int>& blackStones, const std::vector<int>& whiteStones, int posLen)
{
  int sixW = 1;	
  int num_stones = blackStones.size() + whiteStones.size();
  int next_player = ((num_stones % 2) == 0) ? 0 : 1;

  init(posLen, sixW);
  for (auto b : blackStones)
  {
	int move_x = b / boardSize;
	int move_y = b % boardSize;
	int side = 0;
	linear_bit[side][0][move_x] |= (1 << (move_y + 5));
	linear_bit[side][1][move_y] |= (1 << (move_x + 5));
	linear_bit[side][2][move_x + move_y] |= (1 << (move_y + 5));
	linear_bit[side][3][move_y - move_x + boardSize - 1] |= (1 << (move_y + 5));
	square[0].t[b >> 6] |= ((1ULL << (b - ((b >> 6) << 6))));
  }
  for (auto w : whiteStones)
  {
	int move_x = w / boardSize;
	int move_y = w % boardSize;
	int side = 1;
	linear_bit[side][0][move_x] |= (1 << (move_y + 5));
	linear_bit[side][1][move_y] |= (1 << (move_x + 5));
	linear_bit[side][2][move_x + move_y] |= (1 << (move_y + 5));
	linear_bit[side][3][move_y - move_x + boardSize - 1] |= (1 << (move_y + 5));
	square[1].t[w >> 6] |= ((1ULL << (w - ((w >> 6) << 6))));
  }
  for (int m = 0; m < posLen * posLen; ++m)
  {
	for (int side = 0; side < 2; ++side)
	{
		for (int dir = 0; dir < 4; ++ dir)
		{
			update_move_side_dir(m, side, dir);
		}
	}
  }
  generate_legal_moves(next_player);
  bool isSane = (five_threat == FIVE_WIN) ? false : true;
  return isSane;
}

void ThreatHandler::print_board_extended(ostream& out) const
{
	int i, j, dir, sq;
	Table loop;
	for (i = 0; i < boardSize * boardSize; i++)
	{
		if ((square[0].t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			out << "O ";
			if (i % boardSize == boardSize - 1)
			{
				out << "\n";
			}
			continue;
		}
		if ((square[1].t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
		{
			out << "X ";
			if (i % boardSize == boardSize - 1)
			{
				out << "\n";
			}
			continue;
		}
		out << ". ";
		if (i % boardSize == boardSize - 1)
		{
			out << "\n";
		}
	}
	for (i = 0; i < 2; i++)
	{
		for (j = 0; j < 3; j++)
		{
			if (i == 0 && j == 0)
			{
				out << "O ff: ";
			}
			if (i == 0 && j == 1)
			{
				out << "O ft: ";
			}
			if (i == 0 && j == 2)
			{
				out << "O tt: ";
			}
			if (i == 1 && j == 0)
			{
				out << "X ff: ";
			}
			if (i == 1 && j == 1)
			{
				out << "X ft: ";
			}
			if (i == 1 && j == 2)
			{
				out << "X tt: ";
			}
			for (dir = 0; dir < 4; dir++)
			{
				loop = threat[i][j][dir];
				while (!loop)
				{
					sq = loop.bitScanForward();
					out << sq << " ";
					loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
				}
			}																
			out << "\n";
		}
	}
	out << "Five: " << five_threat << "\n";
}

void ThreatHandler::do_move(int m, int side)
{
	assert (m >= 0 && m < boardSize * boardSize);
	if ((((square[side].t[m >> 6] & (1ULL << (m - ((m >> 6) << 6)))) == 0) && ((square[side ^ 1].t[m >> 6] & (1ULL << (m - ((m >> 6) << 6)))) == 0)) == false)
	{
		std::cerr<< "move: " << m << " side: " << side << "\n";
		print_board_extended(std::cerr);

        // If there are at most two stones on the board we consider that this is a problem in the trainer we just return and hope everything will be OK
		int numberOfStones = 0;
		Table loop = square[side];
		while (!loop)
		{
			int sq = loop.bitScanForward();
			std::cerr<< "side player sq: " << sq << "\n";
			numberOfStones++;
			loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
		}
		loop = square[side ^ 1];
		while (!loop)
		{
			int sq = loop.bitScanForward();
			std::cerr<< "side opp player sq: " << sq << "\n";
			numberOfStones++;
			loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
		}		
        if (numberOfStones <= 2)
		{
			return;
		}

		assert(false);
	}

	// square update
	square[side].t[m >> 6] |= (1ULL << (m - ((m >> 6) << 6)));

	int move_x = m / boardSize;
	int move_y = m % boardSize;

	// linear bit update
	linear_bit[side][0][move_x] |= (1 << (move_y + 5));
	linear_bit[side][1][move_y] |= (1 << (move_x + 5));
	linear_bit[side][2][move_x + move_y] |= (1 << (move_y + 5));
	linear_bit[side][3][move_y - move_x + boardSize - 1] |= (1 << (move_y + 5));

    // we assume that we have no existing five threat outside the update area
    five_threat = NO_FIVE;

	// threat update
	local_update_of_threats(m);
}

void ThreatHandler::undo_move(int m, int side) 
{
	assert (m >= 0 && m < boardSize * boardSize);
	assert (((square[side].t[m >> 6] & (1ULL << (m - ((m >> 6) << 6)))) != 0) && ((square[side ^ 1].t[m >> 6] & (1ULL << (m - ((m >> 6) << 6)))) == 0));

	// square update
	square[side].t[m >> 6] &= (~(1ULL << (m - ((m >> 6) << 6))));

	// linear bit update
	linear_bit[side][0][(m / boardSize)] ^= (1 << ((m % boardSize) + 5));
	linear_bit[side][1][(m % boardSize)] ^= (1 << ((m / boardSize) + 5));
	linear_bit[side][2][(m / boardSize) + (m % boardSize)] ^= (1 << ((m % boardSize) + 5));
	linear_bit[side][3][(m % boardSize) - (m / boardSize) + boardSize - 1] ^= (1 << ((m % boardSize) + 5)); 

	// we assume that we have no existing five threat outside the update area
    five_threat = NO_FIVE;

	// threat update
    local_update_of_threats(m);
}

void ThreatHandler::local_update_of_threats(int m)
{
	int move_x = m / boardSize;
	int move_y = m % boardSize;
	for (int s = 0; s < 2; s++)
	{
		for (int dir = 0; dir < 4; dir++)
		{
			int index;
			uint32_t line;
			uint32_t border;
			int mult;

			if (dir == 0)
			{
				mult = 1;
				index = move_y + 5;
				line = linear_bit[s][0][move_x];
				border = (border_bit[0][move_x] | linear_bit[s ^ 1][0][move_x]);	
			}
			if (dir == 1)
			{
				mult = boardSize;
				index = move_x + 5;
				line = linear_bit[s][1][move_y];
				border = (border_bit[1][move_y] | linear_bit[s ^ 1][1][move_y]);	
			}
			if (dir == 2)
			{
				mult = (- boardSize + 1);
				index = move_y + 5;
				line = linear_bit[s][2][move_x + move_y];
				border = (border_bit[2][move_x + move_y] | linear_bit[s ^ 1][2][move_x + move_y]);
			}
			if (dir == 3)
			{
				mult = (boardSize + 1);
				index = move_y + 5;
				line = linear_bit[s][3][move_y - move_x + boardSize - 1];
				border = (border_bit[3][move_y - move_x + boardSize - 1] | linear_bit[s ^ 1][3][move_y - move_x + boardSize - 1]);		
			}

			for (int curr = index - 5; curr <= index + 5; curr++)
			{
				int curr_m = m + (curr - index) * mult;
				if ((line & (1 << curr)) || (border & (1 << curr)))
				{
					if (curr == index)
					{
						threat[s][0][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
						threat[s][1][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
						threat[s][2][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
					}
					continue;
				}
				if (is_there_a_five(line, curr))
				{
					if (five_threat == NO_FIVE)
					{
						five_threat = curr_m;
					}
					else
					{
						five_threat = FIVE_WIN;
					}
					threat[s][0][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
					threat[s][1][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
					threat[s][2][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
					continue;
				}
				if (is_there_a_free_four(line, border, curr))
				{
					threat[s][0][dir].t[curr_m >> 6] |= ((1ULL << (curr_m - ((curr_m >> 6) << 6))));
					threat[s][1][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
					threat[s][2][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
					continue;
				}
				if (is_there_a_four(line, border, curr))
				{
					threat[s][0][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
					threat[s][1][dir].t[curr_m >> 6] |= ((1ULL << (curr_m - ((curr_m >> 6) << 6))));
					threat[s][2][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
					continue;
				}
				if (is_there_a_three(line, border, curr))
				{
					threat[s][0][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
					threat[s][1][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
					threat[s][2][dir].t[curr_m >> 6] |= ((1ULL << (curr_m - ((curr_m >> 6) << 6))));
					continue;
				}
				threat[s][0][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
				threat[s][1][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
				threat[s][2][dir].t[curr_m >> 6] &= (~(1ULL << (curr_m - ((curr_m >> 6) << 6))));
			}
		}
	}
}

void ThreatHandler::update_move_side_dir(int m, int side, int dir)
{
	int move_x = m / boardSize;
	int move_y = m % boardSize;
	int index;
	uint32_t line;
	uint32_t border;

	if (dir == 0)
	{
		index = move_y + 5;
		line = linear_bit[side][0][move_x];
		border = (border_bit[0][move_x] | linear_bit[side ^ 1][0][move_x]);	
	}
	if (dir == 1)
	{
		index = move_x + 5;
		line = linear_bit[side][1][move_y];
		border = (border_bit[1][move_y] | linear_bit[side ^ 1][1][move_y]);	
	}
	if (dir == 2)
	{
		index = move_y + 5;
		line = linear_bit[side][2][move_x + move_y];
		border = (border_bit[2][move_x + move_y] | linear_bit[side ^ 1][2][move_x + move_y]);
	}
	if (dir == 3)
	{
		index = move_y + 5;
		line = linear_bit[side][3][move_y - move_x + boardSize - 1];
		border = (border_bit[3][move_y - move_x + boardSize - 1] | linear_bit[side ^ 1][3][move_y - move_x + boardSize - 1]);		
	}

	if ((border & (1 << index)) || (line & (1 << index)))
	{
		return;
	}
	if (is_there_a_five(line, index))
	{
		if (five_threat == NO_FIVE)
		{
			five_threat = m;
		}
		else
		{
			five_threat = FIVE_WIN;
		}
        return;
	}
	if (is_there_a_free_four(line, border, index))
	{
		threat[side][0][dir].t[m >> 6] |= ((1ULL << (m - ((m >> 6) << 6))));
		return;
	}
	if (is_there_a_four(line, border, index))
	{
		threat[side][1][dir].t[m >> 6] |= ((1ULL << (m - ((m >> 6) << 6))));
		return;
	}
	if (is_there_a_three(line, border, index))
	{
		threat[side][2][dir].t[m >> 6] |= ((1ULL << (m - ((m >> 6) << 6))));
		return;
	}
	return;
}

int ThreatHandler::is_there_a_three(uint32_t line, uint32_t border, int index)
{
	uint32_t empty;
	empty = (~(line  | border));
	
	if (six_wins)
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

int ThreatHandler::is_there_a_four (uint32_t line, uint32_t border, int index)
{
	uint32_t empty;
	empty = (~(line  | border));
	
	if (six_wins)
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

int ThreatHandler::is_there_a_free_four (uint32_t line, uint32_t border, int index)
{
	uint32_t empty;	
	empty = (~(line  | border));
	
	if (six_wins)
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

int ThreatHandler::is_there_a_five (uint32_t line, int index)
{	
	if (six_wins)
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

void ThreatHandler::generate_legal_moves(int next_player)
{
	if (five_threat == FIVE_WIN)
	{
		legal.null();
		return;
  	}

	bool five_threat_presented = false;
	bool ff_threat_presented = false;
	int possible_regular_defence_moves[3] = {-1, -1, -1};
	int old_player = next_player ^ 1;

	if (five_threat != NO_FIVE)
	{
		five_threat_presented = true;
    	possible_regular_defence_moves[0] = five_threat;
		legal.null();
		for (int type = 0; type < 2; type++)
		{
			for (int dir = 0; dir < 4; dir++)
			{ 
				if (threat[next_player][type][dir].t[five_threat >> 6] & (1ULL << (five_threat - ((five_threat >> 6) << 6))))
				{
					legal.t[five_threat >> 6] |= (1ULL << (five_threat - ((five_threat >> 6) << 6)));
				}
			} 
		}
  	}
	if (!(threat[old_player][0][0]) || !(threat[old_player][0][1]) || !(threat[old_player][0][2]) || !(threat[old_player][0][3]))
	{
		ff_threat_presented = true;
		if (five_threat_presented == false)
		{
			int sq = -1;
			int mult = -1;
			for (int dir = 0; dir < 4; dir++)
			{
				if (!(threat[old_player][0][dir]))
				{
					if (dir == 0)
					{
						mult = 1;
					}
					if (dir == 1)
					{
						mult = boardSize;
					}
					if (dir == 2)
					{
						mult = (-boardSize + 1);
					}
					if (dir == 3)
					{
						mult = (boardSize + 1);
					}															
					sq = threat[old_player][0][dir].bitScanForward();
					break;
				}
			}

			possible_regular_defence_moves[0] = sq;
			while (square[old_player].t[(sq + mult) >> 6] & (1ULL << ((sq + mult) - (((sq + mult) >> 6) << 6))))
			{
				sq += mult;
			}
			possible_regular_defence_moves[1] = sq + mult;
			sq = possible_regular_defence_moves[0];
			while (square[old_player].t[(sq - mult) >> 6] & (1ULL << ((sq - mult) - (((sq - mult) >> 6) << 6))))
			{
				sq -= mult;
			}
			possible_regular_defence_moves[2] = sq - mult;			
			// All ff-s and ft-s are legal
			legal.null();
		  	legal |= threat[next_player][0][0];
		  	legal |= threat[next_player][0][1];
		  	legal |= threat[next_player][0][2];
		  	legal |= threat[next_player][0][3];
		  	legal |= threat[next_player][1][0];
		  	legal |= threat[next_player][1][1];
		  	legal |= threat[next_player][1][2];
		  	legal |= threat[next_player][1][3];
	  }
  	}

  	if ((five_threat_presented == false) && (ff_threat_presented == false))
  	{
	  	legal = (~(square[0] | square[1])); // OLD version legal = (~(square[0] | square[1]) & table_indicator) but now we assure that only on board moves can be made  } 
      	return;
  	}

  	// At this point legal is inited, the only  question is to add the possible regular defence moves or not
  	for (int i = 0; i < 3; i++)
  	{
		if (possible_regular_defence_moves[i] == -1)
		{
			break;
		}

		int move_x = possible_regular_defence_moves[i] / boardSize;
		int move_y = possible_regular_defence_moves[i] % boardSize;

		// HACK
		linear_bit[next_player][0][move_x] |= (1 << (move_y + 5));
		linear_bit[next_player][1][move_y] |= (1 << (move_x + 5));
		linear_bit[next_player][2][move_x + move_y] |= (1 << (move_y + 5));
		linear_bit[next_player][3][move_y - move_x + boardSize - 1] |= (1 << (move_y + 5));

    	// iterate on all the ff-s of old_player, if any of them is still a ff then the regular defence move is not need to be added (either not legal or already added to legal)      
		for (int dir = 0; dir < 4; dir++)
		{
			Table loop = threat[old_player][0][dir];
			while (!loop)
			{
				int sq = loop.bitScanForward();
				if (sq != possible_regular_defence_moves[i])
				{
					int ff_x = sq / boardSize;
					int ff_y = sq % boardSize;
			    	int index;
			    	uint32_t line;
			    	uint32_t border;
					if (dir == 0)
					{
						index = ff_y + 5;
						line = linear_bit[old_player][0][ff_x];
						border = (border_bit[0][ff_x] | linear_bit[old_player ^ 1][0][ff_x]);	
					}
					if (dir == 1)
					{
						index = ff_x + 5;
						line = linear_bit[old_player][1][ff_y];
						border = (border_bit[1][ff_y] | linear_bit[old_player ^ 1][1][ff_y]);	
					}
					if (dir == 2)
					{
						index = ff_y + 5;
						line = linear_bit[old_player][2][ff_x + ff_y];
						border = (border_bit[2][ff_x + ff_y] | linear_bit[old_player ^ 1][2][ff_x + ff_y]);
					}
					if (dir == 3)
					{
						index = ff_y + 5;
						line = linear_bit[old_player][3][ff_y - ff_x + boardSize - 1];
						border = (border_bit[3][ff_y - ff_x + boardSize - 1] | linear_bit[old_player ^ 1][3][ff_y - ff_x + boardSize - 1]);		
					}
					if (is_there_a_free_four(line, border, index))
					{
						goto out;
					}
				} 
				loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
			}
		}
    	legal.t[possible_regular_defence_moves[i] >> 6] |= (1ULL << (possible_regular_defence_moves[i] - ((possible_regular_defence_moves[i] >> 6) << 6)));
out:;
		// UNDO HACK
		linear_bit[next_player][0][move_x] ^= (1 << (move_y + 5));
		linear_bit[next_player][1][move_y] ^= (1 << (move_x + 5));
		linear_bit[next_player][2][move_x + move_y] ^= (1 << (move_y + 5));
		linear_bit[next_player][3][move_y - move_x + boardSize - 1] ^= (1 << (move_y + 5));
  	}
	return;
}




//STATIC VARS-----------------------------------------------------------------------------
bool Board::IS_INITALIZED = false;
Hash128 Board::ZOBRIST_SIZE_X_HASH[MAX_LEN+1];
Hash128 Board::ZOBRIST_SIZE_Y_HASH[MAX_LEN+1];
Hash128 Board::ZOBRIST_BOARD_HASH[MAX_ARR_SIZE][4];
Hash128 Board::ZOBRIST_PLAYER_HASH[4];
const Hash128 Board::ZOBRIST_GAME_IS_OVER = //Based on sha256 hash of Board::ZOBRIST_GAME_IS_OVER
  Hash128(0xb6f9e465597a77eeULL, 0xf1d583d960a4ce7fULL);

//LOCATION--------------------------------------------------------------------------------
Loc Location::getLoc(int x, int y, int x_size)
{
  return (x+1) + (y+1)*(x_size+1);
}
int Location::getX(Loc loc, int x_size)
{
  return (loc % (x_size+1)) - 1;
}
int Location::getY(Loc loc, int x_size)
{
  return (loc / (x_size+1)) - 1;
}
void Location::getAdjacentOffsets(short adj_offsets[8], int x_size)
{
  adj_offsets[0] = -(x_size+1);
  adj_offsets[1] = -1;
  adj_offsets[2] = 1;
  adj_offsets[3] = (x_size+1);
  adj_offsets[4] = -(x_size+1)-1;
  adj_offsets[5] = -(x_size+1)+1;
  adj_offsets[6] = (x_size+1)-1;
  adj_offsets[7] = (x_size+1)+1;
}

bool Location::isAdjacent(Loc loc0, Loc loc1, int x_size)
{
  return loc0 == loc1 - (x_size+1) || loc0 == loc1 - 1 || loc0 == loc1 + 1 || loc0 == loc1 + (x_size+1);
}

Loc Location::getMirrorLoc(Loc loc, int x_size, int y_size) {
  if(loc == Board::NULL_LOC)
    return loc;
  return getLoc(x_size-1-getX(loc,x_size),y_size-1-getY(loc,x_size),x_size);
}

Loc Location::getCenterLoc(int x_size, int y_size) {
  if(x_size % 2 == 0 || y_size % 2 == 0)
    return Board::NULL_LOC;
  return getLoc(x_size / 2, y_size / 2, x_size);
}

bool Location::isCentral(Loc loc, int x_size, int y_size) {
  int x = getX(loc,x_size);
  int y = getY(loc,x_size);
  return x >= (x_size-1)/2 && x <= x_size/2 && y >= (y_size-1)/2 && y <= y_size/2;
}


#define FOREACHADJ(BLOCK) {int ADJOFFSET = -(x_size+1); {BLOCK}; ADJOFFSET = -1; {BLOCK}; ADJOFFSET = 1; {BLOCK}; ADJOFFSET = x_size+1; {BLOCK}};
#define ADJ0 (-(x_size+1))
#define ADJ1 (-1)
#define ADJ2 (1)
#define ADJ3 (x_size+1)

//CONSTRUCTORS AND INITIALIZATION----------------------------------------------------------

Board::Board()
{
  init(20,20);
}

Board::Board(int x, int y)
{
  init(x,y);
}

Board::Board(const Board& other)
{
  x_size = other.x_size;
  y_size = other.y_size;

  memcpy(colors, other.colors, sizeof(Color)*MAX_ARR_SIZE);
  num_stones = other.num_stones;
  pos_hash = other.pos_hash;
  threatHandler = other.threatHandler;

  memcpy(adj_offsets, other.adj_offsets, sizeof(short)*8);
}

void Board::init(int xS, int yS)
{
  assert(IS_INITALIZED);
  if(xS < 0 || yS < 0 || xS > MAX_LEN || yS > MAX_LEN)
    throw StringError("Board::init - invalid board size");

  int sixW = 1; // 1 if six wins 0 otherwise
  x_size = xS;
  y_size = yS;

  for(int i = 0; i < MAX_ARR_SIZE; i++)
    colors[i] = C_WALL;

  for(int y = 0; y < y_size; y++)
  {
    for(int x = 0; x < x_size; x++)
    {
      Loc loc = (x+1) + (y+1)*(x_size+1);
      colors[loc] = C_EMPTY;
      // empty_list.add(loc);
    }
  }
  threatHandler.init(x_size, sixW);

  num_stones = 0;
  pos_hash = ZOBRIST_SIZE_X_HASH[x_size] ^ ZOBRIST_SIZE_Y_HASH[y_size];

  Location::getAdjacentOffsets(adj_offsets,x_size);
}

bool Board::setStartPosition(const std::vector<int>& blackStones, const std::vector<int>& whiteStones, int posLen)
{
  init(posLen, posLen);
  num_stones = blackStones.size() + whiteStones.size();
  for (auto b : blackStones)
  {
	int y = b / x_size;
	int x = b % x_size;
	Loc loc = (x+1) + (y+1)*(x_size+1);
	pos_hash ^= ZOBRIST_BOARD_HASH[loc][P_BLACK];
	colors[loc] = C_BLACK;
  }
  for (auto w : whiteStones)
  {
	int y = w / x_size;
	int x = w % x_size;
	Loc loc = (x+1) + (y+1)*(x_size+1);
	pos_hash ^= ZOBRIST_BOARD_HASH[loc][P_WHITE];
	colors[loc] = C_WHITE;
  }  
  bool isSanePosition = threatHandler.setStartPosition(blackStones, whiteStones, posLen);
  return isSanePosition;
}

void Board::initBoardStruct()
{
  if(IS_INITALIZED)
    return;
  Rand rand("Board::initHash()");

  auto nextHash = [&rand]() {
    uint64_t h0 = rand.nextUInt64();
    uint64_t h1 = rand.nextUInt64();
    return Hash128(h0,h1);
  };

  for(int i = 0; i<4; i++)
    ZOBRIST_PLAYER_HASH[i] = nextHash();

  //Do this second so that the player hashes are not
  //afffected by the size of the board we compile with.
  for(int i = 0; i<MAX_ARR_SIZE; i++)
  {
    for(Color j = 0; j<4; j++)
    {
      if(j == C_EMPTY || j == C_WALL)
        ZOBRIST_BOARD_HASH[i][j] = Hash128();
      else
        ZOBRIST_BOARD_HASH[i][j] = nextHash();
    }
  }

  //Reseed the random number generator so that these size hashes are also
  //not affected by the size of the board we compile with
  rand.init("Board::initHash() for ZOBRIST_SIZE hashes");
  for(int i = 0; i<MAX_LEN+1; i++) {
    ZOBRIST_SIZE_X_HASH[i] = nextHash();
    ZOBRIST_SIZE_Y_HASH[i] = nextHash();
  }

  IS_INITALIZED = true;
}

bool Board::isOnBoard(Loc loc) const {
  return loc >= 0 && loc < MAX_ARR_SIZE && colors[loc] != C_WALL;
}

//Check if moving here is legal.
bool Board::isLegal(Loc loc, Player /*pla*/) const
{
	if (isOnBoard(loc) == false)
	{
		return false;
	}
    int threat_handler_index = int(loc) - x_size - 2 - ((int(loc) - x_size - 2) / (x_size + 1));
	return (threatHandler.legal.t[threat_handler_index >> 6] & (1ULL << (threat_handler_index - ((threat_handler_index >> 6) << 6))));
}

bool Board::isAdjacentToPla(Loc loc, Player pla) const {
  FOREACHADJ(
    Loc adj = loc + ADJOFFSET;
    if(colors[adj] == pla)
      return true;
  );
  return false;
}

bool Board::isAdjacentOrDiagonalToPla(Loc loc, Player pla) const {
  for(int i = 0; i<8; i++) {
    Loc adj = loc + adj_offsets[i];
    if(colors[adj] == pla)
      return true;
  }
  return false;
}

bool Board::isEmpty() const {
  for(int y = 0; y < y_size; y++) {
    for(int x = 0; x < x_size; x++) {
      Loc loc = Location::getLoc(x,y,x_size);
      if(colors[loc] != C_EMPTY)
        return false;
    }
  }
  return true;
}

bool Board::isFull() const {
  return (num_stones == (x_size * y_size));
}

int Board::numStonesOnBoard() const {
  int num = 0;
  for(int y = 0; y < y_size; y++) {
    for(int x = 0; x < x_size; x++) {
      Loc loc = Location::getLoc(x,y,x_size);
      if(colors[loc] == C_BLACK || colors[loc] == C_WHITE)
        num += 1;
    }
  }
  return num;
}

bool Board::setStone(Loc loc, Color color)
{
  if(loc < 0 || loc >= MAX_ARR_SIZE || colors[loc] == C_WALL)
    return false;
  if(color != C_BLACK && color != C_WHITE && color != C_EMPTY)
    return false;

  if(colors[loc] == color)
  {}
  else if(colors[loc] == C_EMPTY)
    playMoveAssumeLegal(loc,color);
  else if(color == C_EMPTY)
    removeSingleStone(loc);
  else {
    removeSingleStone(loc);
    playMoveAssumeLegal(loc,color);
  }

  return true;
}

//Remove a single stone
void Board::removeSingleStone(Loc loc)
{
  pos_hash ^= ZOBRIST_BOARD_HASH[loc][colors[loc]];
  colors[loc] = C_EMPTY;
  num_stones -= 1;
}


//Attempts to play the specified move. Returns true if successful, returns false if the move was illegal.
bool Board::playMove(Loc loc, Player pla)
{
  if(isLegal(loc,pla))
  {
    playMoveAssumeLegal(loc,pla);
    return true;
  }
  return false;
}

//Plays the specified move, assuming it is legal, and returns a MoveRecord for the move
Board::MoveRecord Board::playMoveRecorded(Loc loc, Player pla)
{
  MoveRecord record;
  record.loc = loc;
  record.pla = pla;

  playMoveAssumeLegal(loc, pla);
  return record;
}

//Undo the move given by record. Moves MUST be undone in the order they were made.
void Board::undo(Board::MoveRecord record)
{
  Loc loc = record.loc;
  int l = int(loc);
  int p = int(colors[loc]);
  threatHandler.undo_move(l - x_size - 2 - (l - x_size - 2) / (x_size + 1), p - 1);

  //Delete the stone played here.
  pos_hash ^= ZOBRIST_BOARD_HASH[loc][colors[loc]];
  colors[loc] = C_EMPTY;
  num_stones -= 1;
}

Hash128 Board::getPosHashAfterMove(Loc loc, Player pla) const {
  assert(loc != NULL_LOC);

  Hash128 hash = pos_hash;
  hash ^= ZOBRIST_BOARD_HASH[loc][pla];

  return hash;
}

//Plays the specified move, assuming it is legal.
void Board::playMoveAssumeLegal(Loc loc, Player pla)
{
  //Add the new stone 
  colors[loc] = pla;
  pos_hash ^= ZOBRIST_BOARD_HASH[loc][pla];
  int l = int(loc);
  int p = int(pla);
  threatHandler.do_move(l - x_size - 2 - (l - x_size - 2) / (x_size + 1), p - 1);
  num_stones += 1;
}

int Location::distance(Loc loc0, Loc loc1, int x_size) {
  int dx = getX(loc1,x_size) - getX(loc0,x_size);
  int dy = (loc1-loc0-dx) / (x_size+1);
  return (dx >= 0 ? dx : -dx) + (dy >= 0 ? dy : -dy);
}

int Location::euclideanDistanceSquared(Loc loc0, Loc loc1, int x_size) {
  int dx = getX(loc1,x_size) - getX(loc0,x_size);
  int dy = (loc1-loc0-dx) / (x_size+1);
  return dx*dx + dy*dy;
}

//TACTICAL STUFF--------------------------------------------------------------------


//IO FUNCS------------------------------------------------------------------------------------------

char PlayerIO::colorToChar(Color c)
{
  switch(c) {
  case C_BLACK: return 'X';
  case C_WHITE: return 'O';
  case C_EMPTY: return '.';
  default:  return '#';
  }
}

string PlayerIO::playerToString(Color c)
{
  switch(c) {
  case C_BLACK: return "Black";
  case C_WHITE: return "White";
  case C_EMPTY: return "Empty";
  default:  return "Wall";
  }
}

string PlayerIO::playerToStringShort(Color c)
{
  switch(c) {
  case C_BLACK: return "B";
  case C_WHITE: return "W";
  case C_EMPTY: return "E";
  default:  return "";
  }
}

bool PlayerIO::tryParsePlayer(const string& s, Player& pla) {
  string str = Global::toLower(s);
  if(str == "black" || str == "b") {
    pla = P_BLACK;
    return true;
  }
  else if(str == "white" || str == "w") {
    pla = P_WHITE;
    return true;
  }
  return false;
}

Player PlayerIO::parsePlayer(const string& s) {
  Player pla = C_EMPTY;
  bool suc = tryParsePlayer(s,pla);
  if(!suc)
    throw StringError("Could not parse player: " + s);
  return pla;
}

string Location::toStringMach(Loc loc, int x_size)
{
  if(loc == Board::NULL_LOC)
    return string("null");
  char buf[128];
  sprintf(buf,"(%d,%d)",getX(loc,x_size),getY(loc,x_size));
  return string(buf);
}

string Location::toString(Loc loc, int x_size, int y_size)
{
  if(x_size > 25*25)
    return toStringMach(loc,x_size);
  if(loc == Board::NULL_LOC)
    return string("null");
  const char* xChar = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
  int x = getX(loc,x_size);
  int y = getY(loc,x_size);
  if(x >= x_size || x < 0 || y < 0 || y >= y_size)
    return toStringMach(loc,x_size);

  char buf[128];
  if(x <= 24)
    sprintf(buf,"%c%d",xChar[x],y_size-y);
  else
    sprintf(buf,"%c%c%d",xChar[x/25-1],xChar[x%25],y_size-y);
  return string(buf);
}

string Location::toString(Loc loc, const Board& b) {
  return toString(loc,b.x_size,b.y_size);
}

string Location::toStringMach(Loc loc, const Board& b) {
  return toStringMach(loc,b.x_size);
}

static bool tryParseLetterCoordinate(char c, int& x) {
  if(c >= 'A' && c <= 'H')
    x = c-'A';
  else if(c >= 'a' && c <= 'h')
    x = c-'a';
  else if(c >= 'J' && c <= 'Z')
    x = c-'A'-1;
  else if(c >= 'j' && c <= 'z')
    x = c-'a'-1;
  else
    return false;
  return true;
}

bool Location::tryOfString(const string& str, int x_size, int y_size, Loc& result) {
  string s = Global::trim(str);
  if(s.length() < 2)
    return false;
  if(Global::isEqualCaseInsensitive(s,string("pass")) || Global::isEqualCaseInsensitive(s,string("pss"))) {
    result = Board::NULL_LOC;
    return true;
  }
  if(s[0] == '(') {
    if(s[s.length()-1] != ')')
      return false;
    s = s.substr(1,s.length()-2);
    vector<string> pieces = Global::split(s,',');
    if(pieces.size() != 2)
      return false;
    int x;
    int y;
    bool sucX = Global::tryStringToInt(pieces[0],x);
    bool sucY = Global::tryStringToInt(pieces[1],y);
    if(!sucX || !sucY)
      return false;
    result = Location::getLoc(x,y,x_size);
    return true;
  }
  else {
    int x;
    if(!tryParseLetterCoordinate(s[0],x))
      return false;

    //Extended format
    if((s[1] >= 'A' && s[1] <= 'Z') || (s[1] >= 'a' && s[1] <= 'z')) {
      int x1;
      if(!tryParseLetterCoordinate(s[1],x1))
        return false;
      x = (x+1) * 25 + x1;
      s = s.substr(2,s.length()-2);
    }
    else {
      s = s.substr(1,s.length()-1);
    }

    int y;
    bool sucY = Global::tryStringToInt(s,y);
    if(!sucY)
      return false;
    y = y_size - y;
    if(x < 0 || y < 0 || x >= x_size || y >= y_size)
      return false;
    result = Location::getLoc(x,y,x_size);
    return true;
  }
}

bool Location::tryOfString(const string& str, const Board& b, Loc& result) {
  return tryOfString(str,b.x_size,b.y_size,result);
}

Loc Location::ofString(const string& str, int x_size, int y_size) {
  Loc result;
  if(tryOfString(str,x_size,y_size,result))
    return result;
  throw StringError("Could not parse board location: " + str);
}

Loc Location::ofString(const string& str, const Board& b) {
  return ofString(str,b.x_size,b.y_size);
}

vector<Loc> Location::parseSequence(const string& str, const Board& board) {
  vector<string> pieces = Global::split(Global::trim(str),' ');
  vector<Loc> locs;
  for(size_t i = 0; i<pieces.size(); i++) {
    string piece = Global::trim(pieces[i]);
    if(piece.length() <= 0)
      continue;
    locs.push_back(Location::ofString(piece,board));
  }
  return locs;
}

void Board::printBoard(ostream& out, const Board& board, Loc markLoc, const vector<Move>* hist) {
  if(hist != NULL)
    out << "MoveNum: " << hist->size() << " ";
  out << "HASH: " << board.pos_hash << "\n";
  bool showCoords = board.x_size <= 50 && board.y_size <= 50;
  if(showCoords) {
    const char* xChar = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
    out << "  ";
    for(int x = 0; x < board.x_size; x++) {
      if(x <= 24) {
        out << " ";
        out << xChar[x];
      }
      else {
        out << "A" << xChar[x-25];
      }
    }
    out << "\n";
  }

  for(int y = 0; y < board.y_size; y++)
  {
    if(showCoords) {
      char buf[16];
      sprintf(buf,"%2d",board.y_size-y);
      out << buf << ' ';
    }
    for(int x = 0; x < board.x_size; x++)
    {
      Loc loc = Location::getLoc(x,y,board.x_size);
      char s = PlayerIO::colorToChar(board.colors[loc]);
      if(board.colors[loc] == C_EMPTY && markLoc == loc)
        out << '@';
      else
        out << s;

      bool histMarked = false;
      if(hist != NULL) {
        for(int i = (int)hist->size()-3; i<hist->size(); i++) {
          if(i >= 0 && (*hist)[i].loc == loc) {
            out << i - (hist->size()-3) + 1;
            histMarked = true;
            break;
          }
        }
      }

      if(x < board.x_size-1 && !histMarked)
        out << ' ';
    }
    out << "\n";
  }
  out << "\nThreatHandler\n";
  board.threatHandler.print_board_extended(out);

}

ostream& operator<<(ostream& out, const Board& board) {
  Board::printBoard(out,board,Board::NULL_LOC,NULL);
  return out;
}


string Board::toStringSimple(const Board& board, char lineDelimiter) {
  string s;
  for(int y = 0; y < board.y_size; y++) {
    for(int x = 0; x < board.x_size; x++) {
      Loc loc = Location::getLoc(x,y,board.x_size);
      s += PlayerIO::colorToChar(board.colors[loc]);
    }
    s += lineDelimiter;
  }
  return s;
}

Board Board::parseBoard(int xSize, int ySize, const string& s) {
  return parseBoard(xSize,ySize,s,'\n');
}

Board Board::parseBoard(int xSize, int ySize, const string& s, char lineDelimiter) {
  Board board(xSize,ySize);
  vector<string> lines = Global::split(Global::trim(s),lineDelimiter);

  //Throw away coordinate labels line if it exists
  if(lines.size() == ySize+1 && Global::isPrefix(lines[0],"A"))
    lines.erase(lines.begin());

  if(lines.size() != ySize)
    throw StringError("Board::parseBoard - string has different number of board rows than ySize");

  for(int y = 0; y<ySize; y++) {
    string line = Global::trim(lines[y]);
    //Throw away coordinates if they exist
    size_t firstNonDigitIdx = 0;
    while(firstNonDigitIdx < line.length() && Global::isDigit(line[firstNonDigitIdx]))
      firstNonDigitIdx++;
    line.erase(0,firstNonDigitIdx);
    line = Global::trim(line);

    if(line.length() != xSize && line.length() != 2*xSize-1)
      throw StringError("Board::parseBoard - line length not compatible with xSize");

    for(int x = 0; x<xSize; x++) {
      char c;
      if(line.length() == xSize)
        c = line[x];
      else
        c = line[x*2];

      Loc loc = Location::getLoc(x,y,board.x_size);
      if(c == '.' || c == ' ' || c == '*' || c == ',' || c == '`')
        continue;
      else if(c == 'o' || c == 'O')
        board.setStone(loc,P_WHITE);
      else if(c == 'x' || c == 'X')
        board.setStone(loc,P_BLACK);
      else
        throw StringError(string("Board::parseBoard - could not parse board character: ") + c);
    }
  }
  return board;
}
