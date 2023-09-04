#include "../neuralnet/nninputs.h"

using namespace std;

int NNPos::xyToPos(int x, int y, int nnXLen) {
  return y * nnXLen + x;
}
int NNPos::locToPos(Loc loc, int boardXSize, int nnXLen, int nnYLen) {

  if(loc == Board::NULL_LOC)
    return nnXLen * (nnYLen + 1);
  return Location::getY(loc,boardXSize) * nnXLen + Location::getX(loc,boardXSize);
}
Loc NNPos::posToLoc(int pos, int boardXSize, int boardYSize, int nnXLen, int nnYLen) {
  int x = pos % nnXLen;
  int y = pos / nnXLen;
  if(x < 0 || x >= boardXSize || y < 0 || y >= boardYSize)
    return Board::NULL_LOC;
  return Location::getLoc(x,y,boardXSize);
}

int NNPos::getPolicySize(int nnXLen, int nnYLen) {
  return nnXLen * nnYLen + 1;
}

//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------

double ScoreValue::whiteWinsOfWinner(Player winner) {
  if(winner == P_WHITE)
    return 1.0;
  else if(winner == P_BLACK)
    return 0.0;
  return 0.5; // FILL be careful with this
}

//-----------------------------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------------------------


NNOutput::NNOutput()
  :noisedPolicyProbs(NULL)
{}
NNOutput::NNOutput(const NNOutput& other) {
  nnHash = other.nnHash;
  whiteWinProb = other.whiteWinProb;
  whiteLossProb = other.whiteLossProb;
  whiteNoResultProb = other.whiteNoResultProb;
  whiteScoreMean = other.whiteScoreMean;
  whiteScoreMeanSq = other.whiteScoreMeanSq;
  whiteLead = other.whiteLead;
  varTimeLeft = other.varTimeLeft;
  shorttermWinlossError = other.shorttermWinlossError;
  shorttermScoreError = other.shorttermScoreError;

  nnXLen = other.nnXLen;
  nnYLen = other.nnYLen;

  if(other.noisedPolicyProbs != NULL) {
    noisedPolicyProbs = new float[NNPos::MAX_NN_POLICY_SIZE];
    std::copy(other.noisedPolicyProbs, other.noisedPolicyProbs + NNPos::MAX_NN_POLICY_SIZE, noisedPolicyProbs);
  }
  else
    noisedPolicyProbs = NULL;

  std::copy(other.policyProbs, other.policyProbs+NNPos::MAX_NN_POLICY_SIZE, policyProbs);
}

NNOutput::NNOutput(const vector<shared_ptr<NNOutput>>& others) {
  assert(others.size() < 1000000);
  int len = (int)others.size();
  float floatLen = (float)len;
  assert(len > 0);
  for(int i = 1; i<len; i++) {
    assert(others[i]->nnHash == others[0]->nnHash);
  }
  nnHash = others[0]->nnHash;

  whiteWinProb = 0.0f;
  whiteLossProb = 0.0f;
  whiteNoResultProb = 0.0f;
  whiteScoreMean = 0.0f;
  whiteScoreMeanSq = 0.0f;
  whiteLead = 0.0f;
  varTimeLeft = 0.0f;
  shorttermWinlossError = 0.0f;
  shorttermScoreError = 0.0f;
  for(int i = 0; i<len; i++) {
    const NNOutput& other = *(others[i]);
    whiteWinProb += other.whiteWinProb;
    whiteLossProb += other.whiteLossProb;
    whiteNoResultProb += other.whiteNoResultProb;
    whiteScoreMean += other.whiteScoreMean;
    whiteScoreMeanSq += other.whiteScoreMeanSq;
    whiteLead += other.whiteLead;
    varTimeLeft += other.varTimeLeft;
    shorttermWinlossError += other.shorttermWinlossError;
    shorttermScoreError += other.shorttermScoreError;
  }
  whiteWinProb /= floatLen;
  whiteLossProb /= floatLen;
  whiteNoResultProb /= floatLen;
  whiteScoreMean /= floatLen;
  whiteScoreMeanSq /= floatLen;
  whiteLead /= floatLen;
  varTimeLeft /= floatLen;
  shorttermWinlossError /= floatLen;
  shorttermScoreError /= floatLen;

  nnXLen = others[0]->nnXLen;
  nnYLen = others[0]->nnYLen;

  noisedPolicyProbs = NULL;

  //For technical correctness in case of impossibly rare hash collisions:
  //Just give up if they don't all match in move legality
  {
    bool mismatch = false;
    std::fill(policyProbs, policyProbs + NNPos::MAX_NN_POLICY_SIZE, 0.0f);
    for(int i = 0; i<len; i++) {
      const NNOutput& other = *(others[i]);
      for(int pos = 0; pos<NNPos::MAX_NN_POLICY_SIZE; pos++) {
        if(i > 0 && (policyProbs[pos] < 0) != (other.policyProbs[pos] < 0))
          mismatch = true;
        policyProbs[pos] += other.policyProbs[pos];
      }
    }
    //In case of mismatch, just take the first one
    //This should basically never happen, only on true hash collisions
    if(mismatch) {
      const NNOutput& other = *(others[0]);
      std::copy(other.policyProbs, other.policyProbs + NNPos::MAX_NN_POLICY_SIZE, policyProbs);
    }
    else {
      for(int pos = 0; pos<NNPos::MAX_NN_POLICY_SIZE; pos++)
        policyProbs[pos] /= floatLen;
    }
  }

}

NNOutput& NNOutput::operator=(const NNOutput& other) {
  if(&other == this)
    return *this;
  nnHash = other.nnHash;
  whiteWinProb = other.whiteWinProb;
  whiteLossProb = other.whiteLossProb;
  whiteNoResultProb = other.whiteNoResultProb;
  whiteScoreMean = other.whiteScoreMean;
  whiteScoreMeanSq = other.whiteScoreMeanSq;
  whiteLead = other.whiteLead;
  varTimeLeft = other.varTimeLeft;
  shorttermWinlossError = other.shorttermWinlossError;
  shorttermScoreError = other.shorttermScoreError;

  nnXLen = other.nnXLen;
  nnYLen = other.nnYLen;

  if(noisedPolicyProbs != NULL)
    delete[] noisedPolicyProbs;
  if(other.noisedPolicyProbs != NULL) {
    noisedPolicyProbs = new float[NNPos::MAX_NN_POLICY_SIZE];
    std::copy(other.noisedPolicyProbs, other.noisedPolicyProbs + NNPos::MAX_NN_POLICY_SIZE, noisedPolicyProbs);
  }
  else
    noisedPolicyProbs = NULL;

  std::copy(other.policyProbs, other.policyProbs+NNPos::MAX_NN_POLICY_SIZE, policyProbs);

  return *this;
}


NNOutput::~NNOutput() {
  if(noisedPolicyProbs != NULL) {
    delete[] noisedPolicyProbs;
    noisedPolicyProbs = NULL;
  }
}


void NNOutput::debugPrint(ostream& out, const Board& board) {
  out << "Win " << Global::strprintf("%.2fc",whiteWinProb*100) << endl;
  out << "Loss " << Global::strprintf("%.2fc",whiteLossProb*100) << endl;
  out << "NoResult " << Global::strprintf("%.2fc",whiteNoResultProb*100) << endl;
  out << "ScoreMean " << Global::strprintf("%.1f",whiteScoreMean) << endl;
  out << "ScoreMeanSq " << Global::strprintf("%.1f",whiteScoreMeanSq) << endl;
  out << "Lead " << Global::strprintf("%.1f",whiteLead) << endl;
  out << "VarTimeLeft " << Global::strprintf("%.1f",varTimeLeft) << endl;
  out << "STWinlossError " << Global::strprintf("%.1f",shorttermWinlossError) << endl;
  out << "STScoreError " << Global::strprintf("%.1f",shorttermScoreError) << endl;

  out << "Policy" << endl;
  for(int y = 0; y<board.y_size; y++) {
    for(int x = 0; x<board.x_size; x++) {
      int pos = NNPos::xyToPos(x,y,nnXLen);
      float prob = policyProbs[pos];
      if(prob < 0)
        out << "   - ";
      else
        out << Global::strprintf("%4d ", (int)round(prob * 1000));
    }
    out << endl;
  }
}

//-------------------------------------------------------------------------------------------------------------

static void copyWithSymmetry(const float* src, float* dst, int nSize, int hSize, int wSize, int cSize, bool useNHWC, int symmetry, bool reverse) {
  bool transpose = (symmetry & 0x4) != 0 && hSize == wSize;
  bool swapX = (symmetry & 0x2) != 0;
  bool swapY = (symmetry & 0x1) != 0;
  if(transpose && !reverse)
    std::swap(swapX,swapY);
  if(useNHWC) {
    int nStride = hSize * wSize * cSize;
    int hStride = wSize * cSize;
    int wStride = cSize;
    int hBaseNew = 0; int hStrideNew = hStride;
    int wBaseNew = 0; int wStrideNew = wStride;

    if(swapY) { hBaseNew = (hSize-1) * hStrideNew; hStrideNew = -hStrideNew; }
    if(swapX) { wBaseNew = (wSize-1) * wStrideNew; wStrideNew = -wStrideNew; }

    if(transpose)
      std::swap(hStrideNew,wStrideNew);

    for(int n = 0; n<nSize; n++) {
      for(int h = 0; h<hSize; h++) {
        int nhOld = n * nStride + h*hStride;
        int nhNew = n * nStride + hBaseNew + h*hStrideNew;
        for(int w = 0; w<wSize; w++) {
          int nhwOld = nhOld + w*wStride;
          int nhwNew = nhNew + wBaseNew + w*wStrideNew;
          for(int c = 0; c<cSize; c++) {
            dst[nhwNew + c] = src[nhwOld + c];
          }
        }
      }
    }
  }
  else {
    int ncSize = nSize * cSize;
    int ncStride = hSize * wSize;
    int hStride = wSize;
    int wStride = 1;
    int hBaseNew = 0; int hStrideNew = hStride;
    int wBaseNew = 0; int wStrideNew = wStride;

    if(swapY) { hBaseNew = (hSize-1) * hStrideNew; hStrideNew = -hStrideNew; }
    if(swapX) { wBaseNew = (wSize-1) * wStrideNew; wStrideNew = -wStrideNew; }

    if(transpose)
      std::swap(hStrideNew,wStrideNew);

    for(int nc = 0; nc<ncSize; nc++) {
      for(int h = 0; h<hSize; h++) {
        int nchOld = nc * ncStride + h*hStride;
        int nchNew = nc * ncStride + hBaseNew + h*hStrideNew;
        for(int w = 0; w<wSize; w++) {
          int nchwOld = nchOld + w*wStride;
          int nchwNew = nchNew + wBaseNew + w*wStrideNew;
          dst[nchwNew] = src[nchwOld];
        }
      }
    }
  }
}


void SymmetryHelpers::copyInputsWithSymmetry(const float* src, float* dst, int nSize, int hSize, int wSize, int cSize, bool useNHWC, int symmetry) {
  copyWithSymmetry(src, dst, nSize, hSize, wSize, cSize, useNHWC, symmetry, false);
}

void SymmetryHelpers::copyOutputsWithSymmetry(const float* src, float* dst, int nSize, int hSize, int wSize, int symmetry) {
  copyWithSymmetry(src, dst, nSize, hSize, wSize, 1, false, symmetry, true);
}

//-------------------------------------------------------------------------------------------------------------

static void setRowBin(float* rowBin, int pos, int feature, float value, int posStride, int featureStride) {
  rowBin[pos * posStride + feature * featureStride] = value;
}

//Currently does NOT depend on history (except for marking ko-illegal spots)
Hash128 NNInputs::getHash(
  const Board& board, const BoardHistory& hist, Player nextPlayer,
  const MiscNNInputParams& nnInputParams
) {
  int xSize = board.x_size;
  int ySize = board.y_size;

  //Note that board.pos_hash also incorporates the size of the board.
  Hash128 hash = board.pos_hash;
  hash ^= Board::ZOBRIST_PLAYER_HASH[nextPlayer];

  //Fold in the ko, scoring, and suicide rules
  hash ^= Rules::ZOBRIST_KO_RULE_HASH[hist.rules.koRule];
  hash ^= Rules::ZOBRIST_SCORING_RULE_HASH[hist.rules.scoringRule];
  hash ^= Rules::ZOBRIST_TAX_RULE_HASH[hist.rules.taxRule];
  if(hist.rules.multiStoneSuicideLegal)
    hash ^= Rules::ZOBRIST_MULTI_STONE_SUICIDE_HASH;

  //Fold in whether the game is over or not, since this affects how we compute input features
  //but is not a function necessarily of previous hashed values.
  //If the history is in a weird prolonged state, also treat it similarly.
  if(hist.isGameFinished)
    hash ^= Board::ZOBRIST_GAME_IS_OVER;

  //Fold in asymmetric playout indicator
  if(nnInputParams.playoutDoublingAdvantage != 0) {
    ASSERT_UNREACHABLE;
  }

  //Fold in policy temperature
  if(nnInputParams.nnPolicyTemperature != 1.0f) {
    ASSERT_UNREACHABLE;
  }

  if(nnInputParams.avoidMYTDaggerHack){
    ASSERT_UNREACHABLE;
  }

  return hash;
}

//===========================================================================================
//INPUTSVERSION 3
//===========================================================================================

void NNInputs::fillRowV3(
  const Board& board, const BoardHistory& hist, Player nextPlayer,
  const MiscNNInputParams& nnInputParams,
  int nnXLen, int nnYLen, bool useNHWC, float* rowBin, float* rowGlobal
) {
  assert(nnXLen <= NNPos::MAX_BOARD_LEN);
  assert(nnYLen <= NNPos::MAX_BOARD_LEN);
  assert(board.x_size <= nnXLen);
  assert(board.y_size <= nnYLen);
  std::fill(rowBin,rowBin+NUM_FEATURES_SPATIAL_V3*nnXLen*nnYLen,false);
  std::fill(rowGlobal,rowGlobal+NUM_FEATURES_GLOBAL_V3,0.0f);

  Player pla = nextPlayer;
  Player opp = getOpp(pla);
  int xSize = board.x_size;
  int ySize = board.y_size;

  int featureStride;
  int posStride;
  if(useNHWC) {
    featureStride = 1;
    posStride = NNInputs::NUM_FEATURES_SPATIAL_V3;
  }
  else {
    featureStride = nnXLen * nnYLen;
    posStride = 1;
  }

  for(int y = 0; y<ySize; y++) {
    for(int x = 0; x<xSize; x++) {
      int pos = NNPos::xyToPos(x,y,nnXLen);
      Loc loc = Location::getLoc(x,y,xSize);

      //Feature 0 - on board
      setRowBin(rowBin,pos,0, 1.0f, posStride, featureStride);

      Color stone = board.colors[loc];

      //Features 1,2 - pla,opp stone
      //Features 3,4,5 - 1,2,3 libs
      if(stone == pla)
        setRowBin(rowBin,pos,1, 1.0f, posStride, featureStride);
      else if(stone == opp)
        setRowBin(rowBin,pos,2, 1.0f, posStride, featureStride);
    }
  }

  //Feature 6, 7, 8 - ko-ban locations, including possibly superko.

  //Hide history from the net if a pass would end things and we're behaving as if a pass won't.
  //Or if the game is in fact over right now!
  bool hideHistory =
    hist.isGameFinished;

  //Features 9,10,11,12,13
  if(!hideHistory) {
    const vector<Move>& moveHistory = hist.moveHistory;
    size_t moveHistoryLen = moveHistory.size();
    if(moveHistoryLen >= 1 && moveHistory[moveHistoryLen-1].pla == opp) {
      Loc prev1Loc = moveHistory[moveHistoryLen-1].loc;
      if(prev1Loc != Board::NULL_LOC) {
        int pos = NNPos::locToPos(prev1Loc,xSize,nnXLen,nnYLen);
        setRowBin(rowBin,pos,9, 1.0f, posStride, featureStride);
      }
      if(moveHistoryLen >= 2 && moveHistory[moveHistoryLen-2].pla == pla) {
        Loc prev2Loc = moveHistory[moveHistoryLen-2].loc;
        if(prev2Loc != Board::NULL_LOC) {
          int pos = NNPos::locToPos(prev2Loc,xSize,nnXLen,nnYLen);
          setRowBin(rowBin,pos,10, 1.0f, posStride, featureStride);
        }
        if(moveHistoryLen >= 3 && moveHistory[moveHistoryLen-3].pla == opp) {
          Loc prev3Loc = moveHistory[moveHistoryLen-3].loc;
          if(prev3Loc != Board::NULL_LOC) {
            int pos = NNPos::locToPos(prev3Loc,xSize,nnXLen,nnYLen);
            setRowBin(rowBin,pos,11, 1.0f, posStride, featureStride);
          }
          if(moveHistoryLen >= 4 && moveHistory[moveHistoryLen-4].pla == pla) {
            Loc prev4Loc = moveHistory[moveHistoryLen-4].loc;
            if(prev4Loc != Board::NULL_LOC) {
              int pos = NNPos::locToPos(prev4Loc,xSize,nnXLen,nnYLen);
              setRowBin(rowBin,pos,12, 1.0f, posStride, featureStride);
            }
            if(moveHistoryLen >= 5 && moveHistory[moveHistoryLen-5].pla == opp) {
              Loc prev5Loc = moveHistory[moveHistoryLen-5].loc;
              if(prev5Loc != Board::NULL_LOC) {
                int pos = NNPos::locToPos(prev5Loc,xSize,nnXLen,nnYLen);
                setRowBin(rowBin,pos,13, 1.0f, posStride, featureStride);
              }
            }
          }
        }
      }
    }
  }

  //Ladder features 14,15,16,17

  //Features 18,19 - current territory
  Color area[Board::MAX_ARR_SIZE];
  bool nonPassAliveStones;
  bool safeBigTerritories;
  bool unsafeBigTerritories;
  if(hist.rules.scoringRule == Rules::SCORING_AREA) {
    nonPassAliveStones = true;
    safeBigTerritories = true;
    unsafeBigTerritories = true;
  }
  else if(hist.rules.scoringRule == Rules::SCORING_TERRITORY) {
    nonPassAliveStones = false;
    safeBigTerritories = true;
    unsafeBigTerritories = false;
  }
  else {
    ASSERT_UNREACHABLE;
  }

  for(int y = 0; y<ySize; y++) {
    for(int x = 0; x<xSize; x++) {
      Loc loc = Location::getLoc(x,y,xSize);
      int pos = NNPos::locToPos(loc,xSize,nnXLen,nnYLen);
      if(area[loc] == pla)
        setRowBin(rowBin,pos,18, 1.0f, posStride, featureStride);
      else if(area[loc] == opp)
        setRowBin(rowBin,pos,19, 1.0f, posStride, featureStride);
    }
  }

  //Features 20, 21 - second encore starting stones

  //Global features.
  //The first 5 of them were set already above to flag which of the past 5 moves were passes.

  rowGlobal[5] = 0.0f;

  //Ko rule
  if(hist.rules.koRule == Rules::KO_SIMPLE) {}
  else if(hist.rules.koRule == Rules::KO_POSITIONAL || hist.rules.koRule == Rules::KO_SPIGHT) {
    rowGlobal[6] = 1.0f;
    rowGlobal[7] = 0.5f;
  }
  else if(hist.rules.koRule == Rules::KO_SITUATIONAL) {
    rowGlobal[6] = 1.0f;
    rowGlobal[7] = -0.5f;
  }
  else
    ASSERT_UNREACHABLE;

  //Suicide
  if(hist.rules.multiStoneSuicideLegal)
    rowGlobal[8] = 1.0f;

  //Scoring
  if(hist.rules.scoringRule == Rules::SCORING_AREA) {}
  else if(hist.rules.scoringRule == Rules::SCORING_TERRITORY)
    rowGlobal[9] = 1.0f;
  else
    ASSERT_UNREACHABLE;

  //Encore phase
  //if(hist.encorePhase > 0)
  rowGlobal[10] = 0.0f;
  //if(hist.encorePhase > 1)
  rowGlobal[11] = 0.0f;

  //Does a pass end the current phase given the ruleset and history?
  bool passWouldEndPhase = false;
  rowGlobal[12] = passWouldEndPhase ? 1.0f : 0.0f;
  rowGlobal[13] = 0.0f;

}


//===========================================================================================
//INPUTSVERSION 4
//===========================================================================================

void NNInputs::fillRowV4(
  const Board& board, const BoardHistory& hist, Player nextPlayer,
  const MiscNNInputParams& nnInputParams,
  int nnXLen, int nnYLen, bool useNHWC, float* rowBin, float* rowGlobal
) {
  assert(nnXLen <= NNPos::MAX_BOARD_LEN);
  assert(nnYLen <= NNPos::MAX_BOARD_LEN);
  assert(board.x_size <= nnXLen);
  assert(board.y_size <= nnYLen);
  std::fill(rowBin,rowBin+NUM_FEATURES_SPATIAL_V4*nnXLen*nnYLen,false);
  std::fill(rowGlobal,rowGlobal+NUM_FEATURES_GLOBAL_V4,0.0f);

  Player pla = nextPlayer;
  Player opp = getOpp(pla);
  int xSize = board.x_size;
  int ySize = board.y_size;

  int featureStride;
  int posStride;
  if(useNHWC) {
    featureStride = 1;
    posStride = NNInputs::NUM_FEATURES_SPATIAL_V4;
  }
  else {
    featureStride = nnXLen * nnYLen;
    posStride = 1;
  }

  for(int y = 0; y<ySize; y++) {
    for(int x = 0; x<xSize; x++) {
      int pos = NNPos::xyToPos(x,y,nnXLen);
      Loc loc = Location::getLoc(x,y,xSize);

      //Feature 0 - on board
      setRowBin(rowBin,pos,0, 1.0f, posStride, featureStride);

      Color stone = board.colors[loc];

      //Features 1,2 - pla,opp stone
      //Features 3,4,5 - 1,2,3 libs
      if(stone == pla)
        setRowBin(rowBin,pos,1, 1.0f, posStride, featureStride);
      else if(stone == opp)
        setRowBin(rowBin,pos,2, 1.0f, posStride, featureStride);

    }
  }

  //Feature 6, 7, 8 - ko-ban locations, including possibly superko.

  //Hide history from the net if a pass would end things and we're behaving as if a pass won't.
  //Or if the game is in fact over right now!
  bool hideHistory =
    hist.isGameFinished;

  //Features 9,10,11,12,13
  if(!hideHistory) {
    const vector<Move>& moveHistory = hist.moveHistory;
    size_t moveHistoryLen = moveHistory.size();
    if(moveHistoryLen >= 1 && moveHistory[moveHistoryLen-1].pla == opp) {
      Loc prev1Loc = moveHistory[moveHistoryLen-1].loc;
      if(prev1Loc != Board::NULL_LOC) {
        int pos = NNPos::locToPos(prev1Loc,xSize,nnXLen,nnYLen);
        setRowBin(rowBin,pos,9, 1.0f, posStride, featureStride);
      }
      if(moveHistoryLen >= 2 && moveHistory[moveHistoryLen-2].pla == pla) {
        Loc prev2Loc = moveHistory[moveHistoryLen-2].loc;
        if(prev2Loc != Board::NULL_LOC) {
          int pos = NNPos::locToPos(prev2Loc,xSize,nnXLen,nnYLen);
          setRowBin(rowBin,pos,10, 1.0f, posStride, featureStride);
        }
        if(moveHistoryLen >= 3 && moveHistory[moveHistoryLen-3].pla == opp) {
          Loc prev3Loc = moveHistory[moveHistoryLen-3].loc;
          if(prev3Loc != Board::NULL_LOC) {
            int pos = NNPos::locToPos(prev3Loc,xSize,nnXLen,nnYLen);
            setRowBin(rowBin,pos,11, 1.0f, posStride, featureStride);
          }
          if(moveHistoryLen >= 4 && moveHistory[moveHistoryLen-4].pla == pla) {
            Loc prev4Loc = moveHistory[moveHistoryLen-4].loc;
            if(prev4Loc != Board::NULL_LOC) {
              int pos = NNPos::locToPos(prev4Loc,xSize,nnXLen,nnYLen);
              setRowBin(rowBin,pos,12, 1.0f, posStride, featureStride);
            }
            if(moveHistoryLen >= 5 && moveHistory[moveHistoryLen-5].pla == opp) {
              Loc prev5Loc = moveHistory[moveHistoryLen-5].loc;
              if(prev5Loc != Board::NULL_LOC) {
                int pos = NNPos::locToPos(prev5Loc,xSize,nnXLen,nnYLen);
                setRowBin(rowBin,pos,13, 1.0f, posStride, featureStride);
              }
            }
          }
        }
      }
    }
  }

  //Ladder features 14,15,16,17

  //Features 18,19 - pass alive territory and stones
  Color area[Board::MAX_ARR_SIZE];
  {
  }

  for(int y = 0; y<ySize; y++) {
    for(int x = 0; x<xSize; x++) {
      Loc loc = Location::getLoc(x,y,xSize);
      int pos = NNPos::locToPos(loc,xSize,nnXLen,nnYLen);
      if(area[loc] == pla)
        setRowBin(rowBin,pos,18, 1.0f, posStride, featureStride);
      else if(area[loc] == opp)
        setRowBin(rowBin,pos,19, 1.0f, posStride, featureStride);
    }
  }

  //Features 20, 21 - second encore starting stones


  //Global features.
  //The first 5 of them were set already above to flag which of the past 5 moves were passes.

  //Komi and any score adjustments
  rowGlobal[5] = 0.0f;

  //Ko rule
  if(hist.rules.koRule == Rules::KO_SIMPLE) {}
  else if(hist.rules.koRule == Rules::KO_POSITIONAL || hist.rules.koRule == Rules::KO_SPIGHT) {
    rowGlobal[6] = 1.0f;
    rowGlobal[7] = 0.5f;
  }
  else if(hist.rules.koRule == Rules::KO_SITUATIONAL) {
    rowGlobal[6] = 1.0f;
    rowGlobal[7] = -0.5f;
  }
  else
    ASSERT_UNREACHABLE;

  //Suicide
  if(hist.rules.multiStoneSuicideLegal)
    rowGlobal[8] = 1.0f;

  //Scoring
  if(hist.rules.scoringRule == Rules::SCORING_AREA) {}
  else if(hist.rules.scoringRule == Rules::SCORING_TERRITORY)
    rowGlobal[9] = 1.0f;
  else
    ASSERT_UNREACHABLE;

  //Encore phase
  //if(hist.encorePhase > 0)
  rowGlobal[10] = 0.0f;
  //if(hist.encorePhase > 1)
  rowGlobal[11] = 0.0f;

  //Does a pass end the current phase given the ruleset and history?
  bool passWouldEndPhase = hideHistory ? false : false;
  rowGlobal[12] = passWouldEndPhase ? 1.0f : 0.0f;
  rowGlobal[13] = 0.0f;

}



//===========================================================================================
//INPUTSVERSION 5
//===========================================================================================

void NNInputs::fillRowV5(
  const Board& board, const BoardHistory& hist, Player nextPlayer,
  const MiscNNInputParams& nnInputParams,
  int nnXLen, int nnYLen, bool useNHWC, float* rowBin, float* rowGlobal
) {
  assert(nnXLen <= NNPos::MAX_BOARD_LEN);
  assert(nnYLen <= NNPos::MAX_BOARD_LEN);
  assert(board.x_size <= nnXLen);
  assert(board.y_size <= nnYLen);
  std::fill(rowBin,rowBin+NUM_FEATURES_SPATIAL_V5*nnXLen*nnYLen,false);
  std::fill(rowGlobal,rowGlobal+NUM_FEATURES_GLOBAL_V5,0.0f);

  Player pla = nextPlayer;
  Player opp = getOpp(pla);
  int xSize = board.x_size;
  int ySize = board.y_size;

  int featureStride;
  int posStride;
  if(useNHWC) {
    featureStride = 1;
    posStride = NNInputs::NUM_FEATURES_SPATIAL_V5;
  }
  else {
    featureStride = nnXLen * nnYLen;
    posStride = 1;
  }

  for(int y = 0; y<ySize; y++) {
    for(int x = 0; x<xSize; x++) {
      int pos = NNPos::xyToPos(x,y,nnXLen);
      Loc loc = Location::getLoc(x,y,xSize);

      //Feature 0 - on board
      setRowBin(rowBin,pos,0, 1.0f, posStride, featureStride);

      Color stone = board.colors[loc];

      //Features 1,2 - pla,opp stone
      if(stone == pla)
        setRowBin(rowBin,pos,1, 1.0f, posStride, featureStride);
      else if(stone == opp)
        setRowBin(rowBin,pos,2, 1.0f, posStride, featureStride);
    }
  }

  //Feature 3, 4, 5 - ko-ban locations, including possibly superko.


  //Hide history from the net if a pass would end things and we're behaving as if a pass won't.
  //Or if the game is in fact over right now!
  bool hideHistory =
    hist.isGameFinished;

  //Features 6,7,8,9,10
  if(!hideHistory) {
    const vector<Move>& moveHistory = hist.moveHistory;
    size_t moveHistoryLen = moveHistory.size();
    if(moveHistoryLen >= 1 && moveHistory[moveHistoryLen-1].pla == opp) {
      Loc prev1Loc = moveHistory[moveHistoryLen-1].loc;
      if(prev1Loc != Board::NULL_LOC) {
        int pos = NNPos::locToPos(prev1Loc,xSize,nnXLen,nnYLen);
        setRowBin(rowBin,pos,6, 1.0f, posStride, featureStride);
      }
      if(moveHistoryLen >= 2 && moveHistory[moveHistoryLen-2].pla == pla) {
        Loc prev2Loc = moveHistory[moveHistoryLen-2].loc;
        if(prev2Loc != Board::NULL_LOC) {
          int pos = NNPos::locToPos(prev2Loc,xSize,nnXLen,nnYLen);
          setRowBin(rowBin,pos,7, 1.0f, posStride, featureStride);
        }
        if(moveHistoryLen >= 3 && moveHistory[moveHistoryLen-3].pla == opp) {
          Loc prev3Loc = moveHistory[moveHistoryLen-3].loc;
          if(prev3Loc != Board::NULL_LOC) {
            int pos = NNPos::locToPos(prev3Loc,xSize,nnXLen,nnYLen);
            setRowBin(rowBin,pos,8, 1.0f, posStride, featureStride);
          }
          if(moveHistoryLen >= 4 && moveHistory[moveHistoryLen-4].pla == pla) {
            Loc prev4Loc = moveHistory[moveHistoryLen-4].loc;
            if(prev4Loc != Board::NULL_LOC) {
              int pos = NNPos::locToPos(prev4Loc,xSize,nnXLen,nnYLen);
              setRowBin(rowBin,pos,9, 1.0f, posStride, featureStride);
            }
            if(moveHistoryLen >= 5 && moveHistory[moveHistoryLen-5].pla == opp) {
              Loc prev5Loc = moveHistory[moveHistoryLen-5].loc;
              if(prev5Loc != Board::NULL_LOC) {
                int pos = NNPos::locToPos(prev5Loc,xSize,nnXLen,nnYLen);
                setRowBin(rowBin,pos,10, 1.0f, posStride, featureStride);
              }
            }
          }
        }
      }
    }
  }

  //Features 11, 12 - second encore starting stones


  //Global features. NOT USED
  //The first 5 of them were set already above to flag which of the past 5 moves were passes.

}

//===========================================================================================
//INPUTSVERSION 6
//===========================================================================================


void NNInputs::fillRowV6(
  const Board& board, const BoardHistory& hist, Player nextPlayer,
  const MiscNNInputParams& nnInputParams,
  int nnXLen, int nnYLen, bool useNHWC, float* rowBin, float* rowGlobal
) {
  assert(nnXLen <= NNPos::MAX_BOARD_LEN);
  assert(nnYLen <= NNPos::MAX_BOARD_LEN);
  assert(board.x_size <= nnXLen);
  assert(board.y_size <= nnYLen);
  std::fill(rowBin,rowBin+NUM_FEATURES_SPATIAL_V6*nnXLen*nnYLen,false);
  std::fill(rowGlobal,rowGlobal+NUM_FEATURES_GLOBAL_V6,0.0f);

  Player pla = nextPlayer;
  Player opp = getOpp(pla);
  int xSize = board.x_size;
  int ySize = board.y_size;

  int featureStride;
  int posStride;
  if(useNHWC) {
    featureStride = 1;
    posStride = NNInputs::NUM_FEATURES_SPATIAL_V6;
  }
  else {
    featureStride = nnXLen * nnYLen;
    posStride = 1;
  }

  for(int y = 0; y<ySize; y++) {
    for(int x = 0; x<xSize; x++) {
      int pos = NNPos::xyToPos(x,y,nnXLen);
      Loc loc = Location::getLoc(x,y,xSize);

      //Feature 0 - on board
      setRowBin(rowBin,pos,0, 1.0f, posStride, featureStride);

      Color stone = board.colors[loc];

      //Features 1,2 - pla,opp stone
      //Features 3,4,5 - 1,2,3 libs
      if(stone == pla)
        setRowBin(rowBin,pos,1, 1.0f, posStride, featureStride);
      else if(stone == opp)
        setRowBin(rowBin,pos,2, 1.0f, posStride, featureStride);
    }
  }

  //Feature 6, 7, 8 - ko-ban locations, including possibly superko.


  //Hide history from the net if a pass would end things and we're behaving as if a pass won't.
  //Or if the game is in fact over right now!
  bool hideHistory =
    hist.isGameFinished;

  //Features 9,10,11,12,13
  if(!hideHistory) {
    const vector<Move>& moveHistory = hist.moveHistory;
    size_t moveHistoryLen = moveHistory.size();
    //Also effectively wipe history as we change phase
    assert(moveHistoryLen >= hist.numTurns);
    int numTurnsThisPhase = hist.numTurns;

    if(numTurnsThisPhase >= 1 && moveHistory[moveHistoryLen-1].pla == opp) {
      Loc prev1Loc = moveHistory[moveHistoryLen-1].loc;
      if(prev1Loc != Board::NULL_LOC) {
        int pos = NNPos::locToPos(prev1Loc,xSize,nnXLen,nnYLen);
        setRowBin(rowBin,pos,9, 1.0f, posStride, featureStride);
      }
      if(numTurnsThisPhase >= 2 && moveHistory[moveHistoryLen-2].pla == pla) {
        Loc prev2Loc = moveHistory[moveHistoryLen-2].loc;
        if(prev2Loc != Board::NULL_LOC) {
          int pos = NNPos::locToPos(prev2Loc,xSize,nnXLen,nnYLen);
          setRowBin(rowBin,pos,10, 1.0f, posStride, featureStride);
        }
        if(numTurnsThisPhase >= 3 && moveHistory[moveHistoryLen-3].pla == opp) {
          Loc prev3Loc = moveHistory[moveHistoryLen-3].loc;
          if(prev3Loc != Board::NULL_LOC) {
            int pos = NNPos::locToPos(prev3Loc,xSize,nnXLen,nnYLen);
            setRowBin(rowBin,pos,11, 1.0f, posStride, featureStride);
          }
          if(numTurnsThisPhase >= 4 && moveHistory[moveHistoryLen-4].pla == pla) {
            Loc prev4Loc = moveHistory[moveHistoryLen-4].loc;
            if(prev4Loc != Board::NULL_LOC) {
              int pos = NNPos::locToPos(prev4Loc,xSize,nnXLen,nnYLen);
              setRowBin(rowBin,pos,12, 1.0f, posStride, featureStride);
            }
            if(numTurnsThisPhase >= 5 && moveHistory[moveHistoryLen-5].pla == opp) {
              Loc prev5Loc = moveHistory[moveHistoryLen-5].loc;
              if(prev5Loc != Board::NULL_LOC) {
                int pos = NNPos::locToPos(prev5Loc,xSize,nnXLen,nnYLen);
                setRowBin(rowBin,pos,13, 1.0f, posStride, featureStride);
              }
            }
          }
        }
      }
    }
  }

  //Ladder features 14,15,16,17

  //Features 18,19 - current territory, not counting group tax
  Color area[Board::MAX_ARR_SIZE];
  bool hasAreaFeature = false;
  if(hist.rules.scoringRule == Rules::SCORING_AREA && hist.rules.taxRule == Rules::TAX_NONE) {
    hasAreaFeature = true;
    bool nonPassAliveStones = true;
    bool safeBigTerritories = true;
    bool unsafeBigTerritories = true;
  }
  else {
    bool keepTerritories = false;
    bool keepStones = false;
    int whiteMinusBlackIndependentLifeRegionCount = 0;
    if(hist.rules.scoringRule == Rules::SCORING_AREA && (hist.rules.taxRule == Rules::TAX_SEKI || hist.rules.taxRule == Rules::TAX_ALL)) {
      hasAreaFeature = true;
      keepTerritories = false;
      keepStones = true;
    }
    else {
      ASSERT_UNREACHABLE;
    }

    if(hasAreaFeature) {
    }
  }

  if(hasAreaFeature) {
  }

  //Features 20, 21 - second encore starting stones
  //Global features. NOT USED
}

//===========================================================================================
//INPUTSVERSION 7
//===========================================================================================


void NNInputs::fillRowV7(
  const Board& board, const BoardHistory& hist, Player nextPlayer,
  const MiscNNInputParams& nnInputParams,
  int nnXLen, int nnYLen, bool useNHWC, float* rowBin, float* rowGlobal
) {
  assert(nnXLen <= NNPos::MAX_BOARD_LEN);
  assert(nnYLen <= NNPos::MAX_BOARD_LEN);
  assert(board.x_size <= nnXLen);
  assert(board.y_size <= nnYLen);
  std::fill(rowBin,rowBin+NUM_FEATURES_SPATIAL_V7*nnXLen*nnYLen,false);
  std::fill(rowGlobal,rowGlobal+NUM_FEATURES_GLOBAL_V7,0.0f);

  Player pla = nextPlayer;
  Player opp = getOpp(pla);
  int xSize = board.x_size;
  int ySize = board.y_size;

  int featureStride;
  int posStride;
  if(useNHWC) {
    ASSERT_UNREACHABLE;
    featureStride = 1;
    posStride = NNInputs::NUM_FEATURES_SPATIAL_V7;
  }
  else {
    featureStride = nnXLen * nnYLen;
    posStride = 1;
  }

  for(int y = 0; y<ySize; y++) {
    for(int x = 0; x<xSize; x++) {
      int pos = NNPos::xyToPos(x,y,nnXLen);
      Loc loc = Location::getLoc(x,y,xSize);

      //Feature 0 - on board
      setRowBin(rowBin,pos,0, 1.0f, posStride, featureStride);

      Color stone = board.colors[loc];

      //Features 1,2 - pla,opp stone
      if(stone == pla)
        setRowBin(rowBin,pos,1, 1.0f, posStride, featureStride);
      else if(stone == opp)
        setRowBin(rowBin,pos,2, 1.0f, posStride, featureStride);
    }
  }
  //Feature 3 player all ff
  {
    Table loop = board.threatHandler.threat[(pla - 1)][0][0];

    for (int dir = 1; dir < 4; dir++)
    {
      loop |= board.threatHandler.threat[(pla - 1)][0][dir];
    }
    while (!loop)
		{
			int sq = loop.bitScanForward();
      setRowBin(rowBin, sq, 3, 1.0f, posStride, featureStride);
			loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
    }
  }
  //Feature 4 opp all ff
  {
    Table loop = board.threatHandler.threat[(pla - 1)^1][0][0];

    for (int dir = 1; dir < 4; dir++)
    {
      loop |= board.threatHandler.threat[(pla - 1)^1][0][dir];
    }
    while (!loop)
		{
			int sq = loop.bitScanForward();
      setRowBin(rowBin, sq, 4, 1.0f, posStride, featureStride);
			loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
    }
  }  
  //Feature 5, 6, 7, 8 play player ft-s
  {
    for (int dir = 0; dir < 4; dir++)
    {
      Table loop = board.threatHandler.threat[pla - 1][1][dir];
      while (!loop)
			{
				int sq = loop.bitScanForward();
        setRowBin(rowBin, sq, 5 + dir, 1.0f, posStride, featureStride);
				loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
			}
    }
  }
  //Feature 9, 10, 11, 12 opp player ft-s
  {
    for (int dir = 0; dir < 4; dir++)
    {
      Table loop = board.threatHandler.threat[(pla - 1)^1][1][dir];
      while (!loop)
			{
				int sq = loop.bitScanForward();
        setRowBin(rowBin, sq, 9 + dir, 1.0f, posStride, featureStride);
				loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
			}
    }
  }  

  //Feature 13, 14, 15, 16 play player tt-s
  {
    for (int dir = 0; dir < 4; dir++)
    {
      Table loop = board.threatHandler.threat[pla - 1][2][dir];
      while (!loop)
			{
				int sq = loop.bitScanForward();
        setRowBin(rowBin, sq, 13 + dir, 1.0f, posStride, featureStride);
				loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
			}
    }
  }
  //Feature 17, 18, 19, 20 opp player tt-s
  {
    for (int dir = 0; dir < 4; dir++)
    {
      Table loop = board.threatHandler.threat[(pla - 1)^1][2][dir];
      while (!loop)
			{
				int sq = loop.bitScanForward();
        setRowBin(rowBin, sq, 17 + dir, 1.0f, posStride, featureStride);
				loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
			}
    }
  }  

  //Hide history if the game is in fact over right now!
  bool hideHistory =
    hist.isGameFinished;

  //Features 21 last move
  if(!hideHistory) {
    const vector<Move>& moveHistory = hist.moveHistory;
    size_t moveHistoryLen = moveHistory.size();
    //Also effectively wipe history as we change phase
    assert(moveHistoryLen >= hist.numTurns);
    int numTurnsThisPhase = hist.numTurns;

    if(numTurnsThisPhase >= 1 && moveHistory[moveHistoryLen-1].pla == opp) {
      Loc prev1Loc = moveHistory[moveHistoryLen-1].loc;
      if(prev1Loc != Board::NULL_LOC) {
        int pos = NNPos::locToPos(prev1Loc,xSize,nnXLen,nnYLen);
        setRowBin(rowBin,pos, 21, 1.0f, posStride, featureStride);
      }
    }
  }

  //Ladder features 14,15,16,17 NOT USED

  //Features 18,19 - current territory, not counting group tax NOT USED


  //Features 20, 21 - second encore starting stones NOT USED


  //Global features. NOT USED
  //The first 5 of them were set already above to flag which of the past 5 moves were passes. NOT USED

  rowGlobal[5] = 0.0f;

  //Ko rule NOT USED
  rowGlobal[6] = 0.0f;
  rowGlobal[7] = 0.0f;

  //Suicide NOT USED
  //if(hist.rules.multiStoneSuicideLegal)
  rowGlobal[8] = 0.0f;

  //Scoring NOT USED
  //if(hist.rules.scoringRule == Rules::SCORING_AREA) {}
  //else if(hist.rules.scoringRule == Rules::SCORING_TERRITORY)
  //  rowGlobal[9] = 1.0f;
  //else
  //  ASSERT_UNREACHABLE;
  //Tax
  //if(hist.rules.taxRule == Rules::TAX_NONE) {}
  //else if(hist.rules.taxRule == Rules::TAX_SEKI)
  //  rowGlobal[10] = 1.0f;
  //else if(hist.rules.taxRule == Rules::TAX_ALL) {
  //  rowGlobal[10] = 1.0f;
  //  rowGlobal[11] = 1.0f;
  //}
  //else
    //ASSERT_UNREACHABLE;
  rowGlobal[9] = 0.0f;
  rowGlobal[10] = 0.0f;
  rowGlobal[11] = 0.0f;
  rowGlobal[12] = 0.0f;

  //Encore phase NOT USED
  //if(hist.encorePhase > 0)
  rowGlobal[12] = 0.0f;
  //if(hist.encorePhase > 1)
  rowGlobal[13] = 0.0f;

  //Does a pass end the current phase given the ruleset and history?
  //bool passWouldEndPhase = hideHistory ? false : false;
  //rowGlobal[14] = passWouldEndPhase ? 1.0f : 0.0f; NOT USED

  //Used for handicap play NOT USED
  //Parameter 15 is used because there's actually a discontinuity in how training behavior works when this is
  //nonzero, no matter how slightly.
  //if(nnInputParams.playoutDoublingAdvantage != 0) {
  //  rowGlobal[15] = 1.0;
  //  rowGlobal[16] = (float)(0.5 * nnInputParams.playoutDoublingAdvantage);
  //}
  rowGlobal[15] = 0.0f;
  rowGlobal[16] = 0.0f;


  rowGlobal[17] = 0.0f;
  rowGlobal[18] = 0.0f;
}
