/*
 * board.h
 * Originally from an unreleased project back in 2010, modified since.
 * Authors: brettharrison (original), David Wu (original and later modifications).
 */

#ifndef GAME_BOARD_H_
#define GAME_BOARD_H_

#include "../core/global.h"
#include "../core/hash.h"

#ifndef COMPILE_MAX_BOARD_LEN
#define COMPILE_MAX_BOARD_LEN 20
#endif

#define MAX_LEN_THREATHANDLER COMPILE_MAX_BOARD_LEN
#define NO_FIVE (-1) 
#define FIVE_WIN (10000)

struct Table
{
    Table();
    Table(const Table& other);
    Table& operator=(const Table&) = default;

	static const int index64[64];
	uint64_t t[8];
	
	inline void null()
	{
		this -> t[0] = this -> t[1] = this -> t[2] = this -> t[3] = this -> t[4] = this -> t[5] = this -> t[6] = this -> t[7] = 0; 
	}	
	inline bool operator!()
	{
		if (this -> t[0] != 0 || this -> t[1] != 0 || this -> t[2] != 0 || this -> t[3] != 0 || this -> t[4] != 0 || this -> t[5] != 0 || this -> t[6] != 0 || this -> t[7] != 0)
		{
			return true;
		}
		return false;
	}
	inline bool operator==(const Table& rhs)
	{
		if (this -> t[0] == rhs.t[0] && this -> t[1] == rhs.t[1] && this -> t[2] == rhs.t[2] && this -> t[3] == rhs.t[3] && this -> t[4] == rhs.t[4] && this -> t[5] == rhs.t[5] && this -> t[6] == rhs.t[6] && this -> t[7] == rhs.t[7])
		{
			return true;
		}
		return false;
	}
	inline bool operator!=(const Table& rhs)
	{
		if (this -> t[0] == rhs.t[0] && this -> t[1] == rhs.t[1] && this -> t[2] == rhs.t[2] && this -> t[3] == rhs.t[3] && this -> t[4] == rhs.t[4] && this -> t[5] == rhs.t[5] && this -> t[6] == rhs.t[6] && this -> t[7] == rhs.t[7])
		{
			return false;
		}
		return true;
	}				
	inline Table operator|=(const Table& rhs)
	{
		this -> t[0] |= rhs.t[0];
		this -> t[1] |= rhs.t[1];
		this -> t[2] |= rhs.t[2];
		this -> t[3] |= rhs.t[3];
		this -> t[4] |= rhs.t[4];
		this -> t[5] |= rhs.t[5];
		this -> t[6] |= rhs.t[6];
		this -> t[7] |= rhs.t[7];
		return * this;
	}
	inline Table operator~()
	{
		Table ret;
		ret.t[0] = (~(this->t[0]));
		ret.t[1] = (~(this->t[1]));
		ret.t[2] = (~(this->t[2]));
		ret.t[3] = (~(this->t[3]));
		ret.t[4] = (~(this->t[4]));
		ret.t[5] = (~(this->t[5]));
		ret.t[6] = (~(this->t[6]));
		ret.t[7] = (~(this->t[7]));
		return ret;
	}
	inline Table operator&(const Table& rhs)
	{
		Table ret;
		ret.t[0] = (this -> t[0] & rhs.t[0]);
		ret.t[1] = (this -> t[1] & rhs.t[1]);
		ret.t[2] = (this -> t[2] & rhs.t[2]);
		ret.t[3] = (this -> t[3] & rhs.t[3]);
		ret.t[4] = (this -> t[4] & rhs.t[4]);
		ret.t[5] = (this -> t[5] & rhs.t[5]);
		ret.t[6] = (this -> t[6] & rhs.t[6]);
		ret.t[7] = (this -> t[7] & rhs.t[7]);
		return ret;
	}
	inline Table operator|(const Table& rhs)
	{
		Table ret;
		ret.t[0] = (this -> t[0] | rhs.t[0]);
		ret.t[1] = (this -> t[1] | rhs.t[1]);
		ret.t[2] = (this -> t[2] | rhs.t[2]);
		ret.t[3] = (this -> t[3] | rhs.t[3]);
		ret.t[4] = (this -> t[4] | rhs.t[4]);
		ret.t[5] = (this -> t[5] | rhs.t[5]);
		ret.t[6] = (this -> t[6] | rhs.t[6]);
		ret.t[7] = (this -> t[7] | rhs.t[7]);
		return ret;
	}
	inline int bitScanForward() 
	{
		assert (operator!());
		if (t[0] != 0)
		{
			return index64[((t[0] ^ (t[0] - 1)) * (0x03f79d71b4cb0a89)) >> 58];
		}
		if (t[1] != 0)
		{
			return index64[((t[1] ^ (t[1] - 1)) * (0x03f79d71b4cb0a89)) >> 58] + 64;
		}
		if (t[2] != 0)
		{
			return index64[((t[2] ^ (t[2] - 1)) * (0x03f79d71b4cb0a89)) >> 58] + 128;
		}
		if (t[3] != 0)
		{
			return index64[((t[3] ^ (t[3] - 1)) * (0x03f79d71b4cb0a89)) >> 58] + 192;
		}
		if (t[4] != 0)
		{
			return index64[((t[4] ^ (t[4] - 1)) * (0x03f79d71b4cb0a89)) >> 58] + 256;
		}
		if (t[5] != 0)
		{
			return index64[((t[5] ^ (t[5] - 1)) * (0x03f79d71b4cb0a89)) >> 58] + 320;
		}
		if (t[6] != 0)
		{
			return index64[((t[6] ^ (t[6] - 1)) * (0x03f79d71b4cb0a89)) >> 58] + 384;
		}
		if (t[7] != 0)
		{
			return index64[((t[7] ^ (t[7] - 1)) * (0x03f79d71b4cb0a89)) >> 58] + 448;
		}
		return -1;
	}

	void print(int boardSize)
	{
		int i;
		
		for (i = 0; i < boardSize * boardSize; i++)
		{
			if ((t[i >> 6] & (1ULL << (i - ((i >> 6) << 6)))) != 0)
			{
				printf("1 ");
				if (i % boardSize == boardSize - 1)
				{
					printf("\n");
				}
				continue;
			}
			printf("0 ");
			if (i % boardSize == boardSize - 1)
			{
				printf("\n");
			}
		}
		printf("\n");
	}
};

struct ThreatHandler 
{
    ThreatHandler();
	ThreatHandler(int boardS, int sixW);
    ThreatHandler(const ThreatHandler& other);
    ThreatHandler& operator=(const ThreatHandler&) = default;
	
	void init(int boardS, int sixW);
	void print(uint32_t line);
	void print(uint32_t line, uint32_t border);
	void print(const Table&);
	void print_board_extended(std::ostream& out) const;
  
	// Doing and undoing moves
	void do_move(int m, int side);
	void undo_move(int m, int side);
  
	// Updateing
	int is_there_a_three (uint32_t line, uint32_t border, int index);
	int is_there_a_four (uint32_t line, uint32_t border, int index);
	int is_there_a_free_four (uint32_t line, uint32_t border, int index);
	int is_there_a_five (uint32_t line, int index);
	void local_update_of_threats(int m);

	// Generate legal moves for the next_player 
	void generate_legal_moves(int next_player);

	// Data

	// Data input
	int boardSize;
	int six_wins;
	Table square[2]; // [0 == O, 1 == X]
	Table legal;
	uint32_t linear_bit[2][4][MAX_LEN_THREATHANDLER + MAX_LEN_THREATHANDLER - 1]; //[0 == O, 1 == X][0 == E, 1 == N, 2 == NE, 3 == NW][NUMBER_OF_SET]
	uint32_t border_bit[4][MAX_LEN_THREATHANDLER + MAX_LEN_THREATHANDLER - 1];

	// Data output
	int five_threat;
	Table threat[2][3][4]; // [0 == O, 1 == X][0 == ff, 1 == ft, 2 == tt][direction]
};


//TYPES AND CONSTANTS-----------------------------------------------------------------

struct Board;

//Player
typedef int8_t Player;
static constexpr Player P_BLACK = 1;
static constexpr Player P_WHITE = 2;

//Color of a point on the board
typedef int8_t Color;
static constexpr Color C_EMPTY = 0;
static constexpr Color C_BLACK = 1;
static constexpr Color C_WHITE = 2;
static constexpr Color C_WALL = 3;
static constexpr int NUM_BOARD_COLORS = 4;

static inline Color getOpp(Color c)
{return c ^ 3;}

//Conversions for players and colors
namespace PlayerIO {
  char colorToChar(Color c);
  std::string playerToStringShort(Player p);
  std::string playerToString(Player p);
  bool tryParsePlayer(const std::string& s, Player& pla);
  Player parsePlayer(const std::string& s);
}

//Location of a point on the board
//(x,y) is represented as (x+1) + (y+1)*(x_size+1)
typedef short Loc;
namespace Location
{
  Loc getLoc(int x, int y, int x_size);
  int getX(Loc loc, int x_size);
  int getY(Loc loc, int x_size);

  void getAdjacentOffsets(short adj_offsets[8], int x_size);
  bool isAdjacent(Loc loc0, Loc loc1, int x_size);
  Loc getMirrorLoc(Loc loc, int x_size, int y_size);
  Loc getCenterLoc(int x_size, int y_size);
  bool isCentral(Loc loc, int x_size, int y_size);
  int distance(Loc loc0, Loc loc1, int x_size);
  int euclideanDistanceSquared(Loc loc0, Loc loc1, int x_size);

  std::string toString(Loc loc, int x_size, int y_size);
  std::string toString(Loc loc, const Board& b);
  std::string toStringMach(Loc loc, int x_size);
  std::string toStringMach(Loc loc, const Board& b);

  bool tryOfString(const std::string& str, int x_size, int y_size, Loc& result);
  bool tryOfString(const std::string& str, const Board& b, Loc& result);
  Loc ofString(const std::string& str, int x_size, int y_size);
  Loc ofString(const std::string& str, const Board& b);

  std::vector<Loc> parseSequence(const std::string& str, const Board& b);
}

//Simple structure for storing moves. Not used below, but this is a convenient place to define it.
STRUCT_NAMED_PAIR(Loc,loc,Player,pla,Move);

//Fast lightweight board designed for playouts and simulations, where speed is essential.
//Does not enforce player turn order.

struct Board
{
  //Initialization------------------------------
  //Initialize the zobrist hash.
  //MUST BE CALLED AT PROGRAM START!
  static void initBoardStruct();

  //Board parameters and Constants----------------------------------------

  static const int MAX_LEN = COMPILE_MAX_BOARD_LEN;  //Maximum edge length allowed for the board
  static const int MAX_PLAY_SIZE = MAX_LEN * MAX_LEN;  //Maximum number of playable spaces
  static const int MAX_ARR_SIZE = (MAX_LEN+1)*(MAX_LEN+2)+1; //Maximum size of arrays needed

  //Location used to indicate an invalid spot on the board.
  static const Loc NULL_LOC = 0;

  //Zobrist Hashing------------------------------
  static bool IS_INITALIZED;
  static Hash128 ZOBRIST_SIZE_X_HASH[MAX_LEN+1];
  static Hash128 ZOBRIST_SIZE_Y_HASH[MAX_LEN+1];
  static Hash128 ZOBRIST_BOARD_HASH[MAX_ARR_SIZE][4];
  static Hash128 ZOBRIST_PLAYER_HASH[4];
  static const Hash128 ZOBRIST_GAME_IS_OVER;

  //Structs---------------------------------------

  //Move data passed back when moves are made to allow for undos
  struct MoveRecord {
    Player pla;
    Loc loc;
  };

  //Constructors---------------------------------
  Board();  //Create Board of size (19,19)
  Board(int x, int y); //Create Board of size (x,y)
  Board(const Board& other);

  Board& operator=(const Board&) = default;

  //Functions------------------------------------
  //Check if moving here is legal.
  bool isLegal(Loc loc, Player pla) const;
  //Check if this location is on the board
  bool isOnBoard(Loc loc) const;
  //Check if this location is adjacent to stones of the specified color
  bool isAdjacentToPla(Loc loc, Player pla) const;
  bool isAdjacentOrDiagonalToPla(Loc loc, Player pla) const;
  //Is this board empty?
  bool isEmpty() const;
  //Is this board full?
  bool isFull() const;
  //Count the number of stones on the board
  int numStonesOnBoard() const;

  //Sets the specified stone if possible. Returns true usually, returns false location or color were out of range.
  bool setStone(Loc loc, Color color);

  //Attempts to play the specified move. Returns true if successful, returns false if the move was illegal.
  bool playMove(Loc loc, Player pla);

  //Plays the specified move, assuming it is legal.
  void playMoveAssumeLegal(Loc loc, Player pla);

  //Plays the specified move, assuming it is legal, and returns a MoveRecord for the move.
  MoveRecord playMoveRecorded(Loc loc, Player pla);

  //Undo the move given by record. Moves MUST be undone in the order they were made.
  void undo(MoveRecord record);

  //Get what the position hash would be if we were to play this move.
  //Assumes the move is on an empty location.
  Hash128 getPosHashAfterMove(Loc loc, Player pla) const;

  static Board parseBoard(int xSize, int ySize, const std::string& s);
  static Board parseBoard(int xSize, int ySize, const std::string& s, char lineDelimiter);
  static void printBoard(std::ostream& out, const Board& board, Loc markLoc, const std::vector<Move>* hist);
  static std::string toStringSimple(const Board& board, char lineDelimiter);

  //Data--------------------------------------------

  int x_size;                  //Horizontal size of board
  int y_size;                  //Vertical size of board
  Color colors[MAX_ARR_SIZE];  //Color of each location on the board
  int num_stones;              //Number of stones on the board
  ThreatHandler threatHandler;

  /* PointList empty_list; //List of all empty locations on board */

  Hash128 pos_hash; //A zobrist hash of the current board position (does not include player to move)
  short adj_offsets[8]; //Indices 0-3: Offsets to add for adjacent points. Indices 4-7: Offsets for diagonal points. 2 and 3 are +x and +y.

  private:
  void init(int xS, int yS);
  void removeSingleStone(Loc loc);

  friend std::ostream& operator<<(std::ostream& out, const Board& board);
};

#endif // GAME_BOARD_H_
