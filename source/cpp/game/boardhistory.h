#ifndef GAME_BOARDHISTORY_H_
#define GAME_BOARDHISTORY_H_

#include "../core/global.h"
#include "../core/hash.h"
#include "../game/board.h"
#include "../game/rules.h"

//A data structure enabling checking of move legality, support for various rulesets (see rules.h)
struct BoardHistory {
  Rules rules;

  //Chronological history of moves
  std::vector<Move> moveHistory;

  //The board and player to move as of the very start, before moveHistory.
  Board initialBoard;
  Player initialPla;

  //The "turn number" as of the initial board. Does not affect any rules, but possibly uses may
  //care about this number, for cases where we set up a position from midgame.
  int initialTurnNumber;
  bool whiteHasMoved;

  static const int NUM_RECENT_BOARDS = 6;
  Board recentBoards[NUM_RECENT_BOARDS];
  int currentRecentBoardIdx;
  Player presumedNextMovePla;

  //Did this board location ever have a stone there before, or was it ever played?
  bool wasEverOccupiedOrPlayed[Board::MAX_ARR_SIZE];

  //How many turns of history do we have?
  int numTurns;

  //Is the game supposed to be ended now?
  bool isGameFinished;
  //Winner of the game if the game is supposed to have ended now, C_EMPTY if it is a draw or isNoResult.
  Player winner;
  //True if this game is supposed to be ended but there is no result
  bool isNoResult;
  //True if this game is supposed to be ended but it was by resignation rather than an actual end position
  bool isResignation;

  BoardHistory();
  ~BoardHistory();

  BoardHistory(const Board& board, Player pla, const Rules& rules);

  BoardHistory(const BoardHistory& other);
  BoardHistory& operator=(const BoardHistory& other);

  BoardHistory(BoardHistory&& other) noexcept;
  BoardHistory& operator=(BoardHistory&& other) noexcept;

  //Clears all history and status
  void clear(const Board& board, Player pla, const Rules& rules);

  //Set the initial turn number. Affects nothing else.
  void setInitialTurnNumber(int n);

  //Returns a reference a recent board state, where 0 is the current board, 1 is 1 move ago, etc.
  //Requires that numMovesAgo < NUM_RECENT_BOARDS
  const Board& getRecentBoard(int numMovesAgo) const;

  //Check if a move on the board is legal, taking into account the full game state
  bool isLegal(const Board& board, Loc moveLoc, Player movePla) const;

  //This function should behave gracefully so long as it is pseudolegal (board.isLegal)
  void makeBoardMoveAssumeLegal(Board& board, Loc moveLoc, Player movePla);
  //Make a move with legality checking, but be mostly tolerant and allow moves that can still be handled but that may not technically
  //be legal. This is intended for reading moves from SGFs and such where maybe we're getting moves that were played in a different
  //ruleset than ours. Returns true if successful, false if was illegal even unter tolerant rules.
  bool makeBoardMoveTolerant(Board& board, Loc moveLoc, Player movePla);
  bool isLegalTolerant(const Board& board, Loc moveLoc, Player movePla) const;

  //Finish the board.
  void endGameNow(Player winnerPlayer);

  void setWinnerByResignation(Player pla);

  void printBasicInfo(std::ostream& out, const Board& board) const;
  void printDebugInfo(std::ostream& out, const Board& board) const;

private:
};

#endif  // GAME_BOARDHISTORY_H_
