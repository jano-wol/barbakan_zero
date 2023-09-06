
//-------------------------------------------------------------------------------------
//This file contains the main core logic of the search.
//-------------------------------------------------------------------------------------

#include "../search/search.h"

#include <algorithm>
#include <iostream>
#include <numeric>

#include "../core/fancymath.h"
#include "../core/timer.h"
#include "../search/distributiontable.h"

using namespace std;

ReportedSearchValues::ReportedSearchValues()
{}
ReportedSearchValues::~ReportedSearchValues()
{}

NodeStats::NodeStats()
  :visits(0),winValueSum(0.0),sureWinStatus(-1),noResultValueSum(0.0),scoreMeanSum(0.0),scoreMeanSqSum(0.0),leadSum(0.0),utilitySum(0.0),utilitySqSum(0.0),weightSum(0.0),weightSqSum(0.0)
{}
NodeStats::~NodeStats()
{}

double NodeStats::getResultUtilitySum(const SearchParams& searchParams) const {
  return (
    (2.0*winValueSum - weightSum + noResultValueSum) * searchParams.winLossUtilityFactor +
    noResultValueSum * searchParams.noResultUtilityForWhite
  );
}

double Search::getResultUtility(double winValue, double noResultValue) const {
  return (
    (2.0*winValue - 1.0 + noResultValue) * searchParams.winLossUtilityFactor +
    noResultValue * searchParams.noResultUtilityForWhite
  );
}

double Search::getResultUtilityFromNN(const NNOutput& nnOutput) const {
  return (
    (nnOutput.whiteWinProb - nnOutput.whiteLossProb) * searchParams.winLossUtilityFactor +
    nnOutput.whiteNoResultProb * searchParams.noResultUtilityForWhite
  );
}

double Search::getScoreStdev(double scoreMean, double scoreMeanSq) {
  double variance = scoreMeanSq - scoreMean * scoreMean;
  if(variance <= 0.0)
    return 0.0;
  return sqrt(variance);
}

//-----------------------------------------------------------------------------------------

SearchNode::SearchNode(Search& search, Player prevPla, Rand& rand, Loc prevLoc, SearchNode* p)
  :lockIdx(),
   nextPla(getOpp(prevPla)),prevMoveLoc(prevLoc),
   nnOutput(),
   nnOutputAge(0),
   parent(p),
   children(NULL),numChildren(0),childrenCapacity(0),
   stats(),virtualLosses(0)
{
  lockIdx = rand.nextUInt(search.mutexPool->getNumMutexes());
}
SearchNode::~SearchNode() {
  if(children != NULL) {
    for(int i = 0; i<numChildren; i++)
      delete children[i];
  }
  delete[] children;
}

//-----------------------------------------------------------------------------------------

static string makeSeed(const Search& search, int threadIdx) {
  stringstream ss;
  ss << search.randSeed;
  ss << "$searchThread$";
  ss << threadIdx;
  ss << "$";
  ss << search.rootBoard.pos_hash;
  ss << "$";
  ss << search.rootHistory.moveHistory.size();
  ss << "$";
  ss << search.numSearchesBegun;
  return ss.str();
}

SearchThread::SearchThread(int tIdx, const Search& search, Logger* lg)
  :threadIdx(tIdx),
   pla(search.rootPla),board(search.rootBoard),
   history(search.rootHistory),
   rand(makeSeed(search,tIdx)),
   nnResultBuf(),
   logStream(NULL),
   logger(lg),
   weightFactorBuf(),
   weightBuf(),
   weightSqBuf(),
   winValuesBuf(),
   noResultValuesBuf(),
   scoreMeansBuf(),
   scoreMeanSqsBuf(),
   leadsBuf(),
   utilityBuf(),
   utilitySqBuf(),
   selfUtilityBuf(),
   visitsBuf(),
   upperBoundVisitsLeft(1e30)
{
  if(logger != NULL)
    logStream = logger->createOStream();

  weightFactorBuf.reserve(NNPos::MAX_NN_POLICY_SIZE);
    
  surewin_tt_setsize (10000); 
  surewin_tt_kill();	
  root.init();

  weightBuf.resize(NNPos::MAX_NN_POLICY_SIZE);
  weightSqBuf.resize(NNPos::MAX_NN_POLICY_SIZE);
  winValuesBuf.resize(NNPos::MAX_NN_POLICY_SIZE);
  noResultValuesBuf.resize(NNPos::MAX_NN_POLICY_SIZE);
  scoreMeansBuf.resize(NNPos::MAX_NN_POLICY_SIZE);
  scoreMeanSqsBuf.resize(NNPos::MAX_NN_POLICY_SIZE);
  leadsBuf.resize(NNPos::MAX_NN_POLICY_SIZE);
  utilityBuf.resize(NNPos::MAX_NN_POLICY_SIZE);
  utilitySqBuf.resize(NNPos::MAX_NN_POLICY_SIZE);
  selfUtilityBuf.resize(NNPos::MAX_NN_POLICY_SIZE);
  visitsBuf.resize(NNPos::MAX_NN_POLICY_SIZE);

}
SearchThread::~SearchThread() {
  if(logStream != NULL)
    delete logStream;
  logStream = NULL;
  logger = NULL;
  free(surewin_tt);
  free(root.st);
}

//-----------------------------------------------------------------------------------------

static const double VALUE_WEIGHT_DEGREES_OF_FREEDOM = 3.0;

Search::Search(SearchParams params, NNEvaluator* nnEval, const string& rSeed)
  :rootPla(P_BLACK),
   rootBoard(),rootHistory(),rootHintLoc(Board::NULL_LOC),
   avoidMoveUntilByLocBlack(),avoidMoveUntilByLocWhite(),
   rootSafeArea(NULL),
   recentScoreCenter(0.0),
   alwaysIncludeOwnerMap(false),
   searchParams(params),numSearchesBegun(0),searchNodeAge(0),
   plaThatSearchIsFor(C_EMPTY),plaThatSearchIsForLastSearch(C_EMPTY),
   lastSearchNumPlayouts(0),
   effectiveSearchTimeCarriedOver(0.0),
   randSeed(rSeed),
   normToTApproxZ(0.0),
   nnEvaluator(nnEval),
   nonSearchRand(rSeed + string("$nonSearchRand"))
{
  nnXLen = nnEval->getNNXLen();
  nnYLen = nnEval->getNNYLen();
  assert(nnXLen > 0 && nnXLen <= NNPos::MAX_BOARD_LEN);
  assert(nnYLen > 0 && nnYLen <= NNPos::MAX_BOARD_LEN);
  policySize = NNPos::getPolicySize(nnXLen,nnYLen);

  rootSafeArea = new Color[Board::MAX_ARR_SIZE];

  valueWeightDistribution = new DistributionTable(
    [](double z) { return FancyMath::tdistpdf(z,VALUE_WEIGHT_DEGREES_OF_FREEDOM); },
    [](double z) { return FancyMath::tdistcdf(z,VALUE_WEIGHT_DEGREES_OF_FREEDOM); },
    -50.0,
    50.0,
    2000
  );
  rootNode = NULL;
  mutexPool = new MutexPool(params.mutexPoolSize);

  rootHistory.clear(rootBoard,rootPla,Rules());
}

Search::~Search() {
  delete[] rootSafeArea;
  delete valueWeightDistribution;
  delete rootNode;
  delete mutexPool;
}

const Board& Search::getRootBoard() const {
  return rootBoard;
}
const BoardHistory& Search::getRootHist() const {
  return rootHistory;
}
Player Search::getRootPla() const {
  return rootPla;
}

void Search::setPosition(Player pla, const Board& board, const BoardHistory& history) {
  clearSearch();
  rootPla = pla;
  plaThatSearchIsFor = C_EMPTY;
  rootBoard = board;
  rootHistory = history;
  avoidMoveUntilByLocBlack.clear();
  avoidMoveUntilByLocWhite.clear();
}

void Search::setPlayerAndClearHistory(Player pla) {
  clearSearch();
  rootPla = pla;
  plaThatSearchIsFor = C_EMPTY;
  Rules rules = rootHistory.rules;
  //Preserve this value even when we get multiple moves in a row by some player
  rootHistory.clear(rootBoard,rootPla,rules);

  avoidMoveUntilByLocBlack.clear();
  avoidMoveUntilByLocWhite.clear();
}

void Search::setAvoidMoveUntilByLoc(const std::vector<int>& bVec, const std::vector<int>& wVec) {
  if(avoidMoveUntilByLocBlack == bVec && avoidMoveUntilByLocWhite == wVec)
    return;
  clearSearch();
  avoidMoveUntilByLocBlack = bVec;
  avoidMoveUntilByLocWhite = wVec;
}

void Search::setRootHintLoc(Loc loc) {
  //When we positively change the hint loc, we clear the search to make absolutely sure
  //that the hintloc takes effect, and that all nnevals (including the root noise that adds the hintloc) has a chance to happen
  if(loc != Board::NULL_LOC && rootHintLoc != loc)
    clearSearch();
  rootHintLoc = loc;
}

void Search::setAlwaysIncludeOwnerMap(bool b) {
  if(!alwaysIncludeOwnerMap && b)
    clearSearch();
  alwaysIncludeOwnerMap = b;
}

void Search::setParams(SearchParams params) {
  clearSearch();
  searchParams = params;
}

void Search::setParamsNoClearing(SearchParams params) {
  searchParams = params;
}

void Search::setNNEval(NNEvaluator* nnEval) {
  clearSearch();
  nnEvaluator = nnEval;
  nnXLen = nnEval->getNNXLen();
  nnYLen = nnEval->getNNYLen();
  assert(nnXLen > 0 && nnXLen <= NNPos::MAX_BOARD_LEN);
  assert(nnYLen > 0 && nnYLen <= NNPos::MAX_BOARD_LEN);
  policySize = NNPos::getPolicySize(nnXLen,nnYLen);
}

void Search::clearSearch() {
  effectiveSearchTimeCarriedOver = 0.0;
  delete rootNode;
  rootNode = NULL;
}

bool Search::isLegalTolerant(Loc moveLoc, Player movePla) const {
  //Tolerate sgf files or GTP reporting suicide moves, even if somehow the rules are set to disallow them.
  bool multiStoneSuicideLegal = true;

  //If we somehow have the same player making multiple moves in a row (possible in GTP or an sgf file),
  //clear the ko loc - the simple ko loc of a player should not prohibit the opponent playing there!
  if(movePla != rootPla) {
    Board copy = rootBoard;
    return copy.isLegal(moveLoc,movePla);
  }
  else {
    return rootHistory.isLegalTolerant(rootBoard,moveLoc,movePla);
  }
}

bool Search::isLegalStrict(Loc moveLoc, Player movePla) const {
  return movePla == rootPla && rootHistory.isLegal(rootBoard,moveLoc,movePla);
}

bool Search::makeMove(Loc moveLoc, Player movePla) {
  return makeMove(moveLoc,movePla,false);
}

bool Search::makeMove(Loc moveLoc, Player movePla, bool preventEncore) {
  if(!isLegalTolerant(moveLoc,movePla))
    return false;

  if(movePla != rootPla)
    setPlayerAndClearHistory(movePla);

  if(rootNode != NULL) {
    bool foundChild = false;
    int foundChildIdx = -1;
    for(int i = 0; i<rootNode->numChildren; i++) {
      SearchNode* child = rootNode->children[i];
      if(!foundChild && child->prevMoveLoc == moveLoc) {
        foundChild = true;
        foundChildIdx = i;
        break;
      }
    }

    //Just in case, make sure the child has an nnOutput, otherwise no point keeping it.
    //This is a safeguard against any oddity involving node preservation into states that
    //were considered terminal.
    if(foundChild) {
      SearchNode* child = rootNode->children[foundChildIdx];
      std::mutex& mutex = mutexPool->getMutex(child->lockIdx);
      lock_guard<std::mutex> lock(mutex);
      if(child->nnOutput == nullptr)
        foundChild = false;
    }

    if(foundChild) {
      //Grab out the node to prevent its deletion along with the root
      //Delete the root and replace it with the child
      SearchNode* child = rootNode->children[foundChildIdx];

      {
        while(rootNode->statsLock.test_and_set(std::memory_order_acquire));
        int64_t rootVisits = rootNode->stats.visits;
        rootNode->statsLock.clear(std::memory_order_release);
        while(child->statsLock.test_and_set(std::memory_order_acquire));
        int64_t childVisits = child->stats.visits;
        child->statsLock.clear(std::memory_order_release);
        effectiveSearchTimeCarriedOver = effectiveSearchTimeCarriedOver * (double)childVisits / (double)rootVisits * searchParams.treeReuseCarryOverTimeFactor;
      }

      child->parent = NULL;
      rootNode->children[foundChildIdx] = NULL;
      recursivelyRemoveSubtreeValueBiasBeforeDeleteSynchronous(rootNode);
      delete rootNode;
      rootNode = child;
    }
    else {
      clearSearch();
    }
  }

  rootHistory.makeBoardMoveAssumeLegal(rootBoard,moveLoc,rootPla);
  rootPla = getOpp(rootPla);  

  return true;
}


double Search::getScoreUtility(double scoreMeanSum, double scoreMeanSqSum, double weightSum) const {
  double scoreMean = scoreMeanSum / weightSum;
  double scoreMeanSq = scoreMeanSqSum / weightSum;
  double scoreStdev = getScoreStdev(scoreMean, scoreMeanSq);
  double staticScoreValue = 0.0;
  double dynamicScoreValue = 0.0;
  return staticScoreValue * searchParams.staticScoreUtilityFactor + dynamicScoreValue * searchParams.dynamicScoreUtilityFactor;
}

double Search::getScoreUtilityDiff(double scoreMeanSum, double scoreMeanSqSum, double weightSum, double delta) const {
  double scoreMean = scoreMeanSum / weightSum;
  double scoreMeanSq = scoreMeanSqSum / weightSum;
  double scoreStdev = getScoreStdev(scoreMean, scoreMeanSq);
  double staticScoreValueDiff = 0.0;
  double dynamicScoreValueDiff = 0.0;
  return staticScoreValueDiff * searchParams.staticScoreUtilityFactor + dynamicScoreValueDiff * searchParams.dynamicScoreUtilityFactor;
}

double Search::getUtilityFromNN(const NNOutput& nnOutput) const {
  double resultUtility = getResultUtilityFromNN(nnOutput);
  return resultUtility + getScoreUtility(nnOutput.whiteScoreMean, nnOutput.whiteScoreMeanSq, 1.0);
}

uint32_t Search::chooseIndexWithTemperature(Rand& rand, const double* relativeProbs, int numRelativeProbs, double temperature) {
  assert(numRelativeProbs > 0);
  assert(numRelativeProbs <= Board::MAX_ARR_SIZE); //We're just doing this on the stack
  double processedRelProbs[Board::MAX_ARR_SIZE];

  double maxValue = 0.0;
  for(int i = 0; i<numRelativeProbs; i++) {
    if(relativeProbs[i] > maxValue)
      maxValue = relativeProbs[i];
  }
  assert(maxValue > 0.0);

  //Temperature so close to 0 that we just calculate the max directly
  if(temperature <= 1.0e-4) {
    double bestProb = relativeProbs[0];
    int bestIdx = 0;
    for(int i = 1; i<numRelativeProbs; i++) {
      if(relativeProbs[i] > bestProb) {
        bestProb = relativeProbs[i];
        bestIdx = i;
      }
    }
    return bestIdx;
  }
  //Actual temperature
  else {
    double logMaxValue = log(maxValue);
    double sum = 0.0;
    for(int i = 0; i<numRelativeProbs; i++) {
      //Numerically stable way to raise to power and normalize
      processedRelProbs[i] = relativeProbs[i] <= 0.0 ? 0.0 : exp((log(relativeProbs[i]) - logMaxValue) / temperature);
      sum += processedRelProbs[i];
    }
    assert(sum > 0.0);
    uint32_t idxChosen = rand.nextUInt(processedRelProbs,numRelativeProbs);
    return idxChosen;
  }
}

double Search::interpolateEarly(double halflife, double earlyValue, double value) const {
  double rawHalflives = (rootHistory.initialTurnNumber + rootHistory.moveHistory.size()) / halflife;
  double halflives = rawHalflives * 19.0 / sqrt(rootBoard.x_size*rootBoard.y_size);
  return value + (earlyValue - value) * pow(0.5, halflives);
}

Loc Search::runWholeSearchAndGetMove(Player movePla, Logger& logger) {
  return runWholeSearchAndGetMove(movePla,logger,false);
}

Loc Search::runWholeSearchAndGetMove(Player movePla, Logger& logger, bool pondering) {
  runWholeSearch(movePla,logger,pondering);
  return getChosenMoveLoc();
}

void Search::runWholeSearch(Player movePla, Logger& logger) {
  runWholeSearch(movePla,logger,false);
}

void Search::runWholeSearch(Player movePla, Logger& logger, bool pondering) {
  if(movePla != rootPla)
    setPlayerAndClearHistory(movePla);
  std::atomic<bool> shouldStopNow(false);
  runWholeSearch(logger,shouldStopNow,pondering);
}

void Search::runWholeSearch(Logger& logger, std::atomic<bool>& shouldStopNow) {
  runWholeSearch(logger,shouldStopNow, false);
}

void Search::runWholeSearch(Logger& logger, std::atomic<bool>& shouldStopNow, bool pondering) {
  std::function<void()>* searchBegun = NULL;
  runWholeSearch(logger,shouldStopNow,searchBegun,pondering,TimeControls(),1.0);
}

double Search::numVisitsNeededToBeNonFutile(double maxVisitsMoveVisits) {
  double requiredVisits = searchParams.futileVisitsThreshold * maxVisitsMoveVisits;
  //In the case where we're playing high temperature, also require that we can't get to more than a 1:100 odds of playing the move.
  double chosenMoveTemperature = interpolateEarly(
    searchParams.chosenMoveTemperatureHalflife, searchParams.chosenMoveTemperatureEarly, searchParams.chosenMoveTemperature
  );
  if(chosenMoveTemperature < 1e-3)
    return requiredVisits;
  double requiredVisitsDueToTemp = maxVisitsMoveVisits * pow(0.01, chosenMoveTemperature);
  return std::min(requiredVisits, requiredVisitsDueToTemp);
}

double Search::computeUpperBoundVisitsLeftDueToTime(
  int64_t rootVisits, double timeUsed, double plannedTimeLimit
) {
  if(rootVisits <= 1)
    return 1e30;
  double timeThoughtSoFar = effectiveSearchTimeCarriedOver + timeUsed;
  double timeLeftPlanned = plannedTimeLimit - timeUsed;
  //Require at least a tenth of a second of search to begin to trust an estimate of visits/time.
  if(timeThoughtSoFar < 0.1)
    return 1e30;

  double proportionOfTimeThoughtLeft = timeLeftPlanned / timeThoughtSoFar;
  return ceil(proportionOfTimeThoughtLeft * rootVisits + searchParams.numThreads-1);
}

double Search::recomputeSearchTimeLimit(
  const TimeControls& tc, double timeUsed, double searchFactor, int64_t rootVisits
) {
  double tcMin;
  double tcRec;
  double tcMax;
  tc.getTime(rootBoard,rootHistory,searchParams.lagBuffer,tcMin,tcRec,tcMax);

  tcRec *= searchParams.overallocateTimeFactor;

  if(searchParams.midgameTimeFactor != 1.0) {
    double boardAreaScale = rootBoard.x_size * rootBoard.y_size / 361.0;
    int64_t presumedTurnNumber = rootHistory.initialTurnNumber + rootHistory.moveHistory.size();
    if(presumedTurnNumber < 0) presumedTurnNumber = 0;

    double midGameWeight;
    if(presumedTurnNumber < searchParams.midgameTurnPeakTime * boardAreaScale)
      midGameWeight = (double)presumedTurnNumber / (searchParams.midgameTurnPeakTime * boardAreaScale);
    else
      midGameWeight = exp(
        -(presumedTurnNumber - searchParams.midgameTurnPeakTime * boardAreaScale) /
        (searchParams.endgameTurnTimeDecay * boardAreaScale)
      );
    if(midGameWeight < 0)
      midGameWeight = 0;
    if(midGameWeight > 1)
      midGameWeight = 1;

    tcRec *= 1.0 + midGameWeight * (searchParams.midgameTimeFactor - 1.0);
  }

  if(searchParams.obviousMovesTimeFactor < 1.0) {
    double surprise = 0.0;
    double searchEntropy = 0.0;
    double policyEntropy = 0.0;
    bool suc = getPolicySurpriseAndEntropy(surprise, searchEntropy, policyEntropy);
    if(suc) {
      //If the original policy was confident and the surprise is low, then this is probably an "obvious" move.
      double obviousnessByEntropy = exp(-policyEntropy/searchParams.obviousMovesPolicyEntropyTolerance);
      double obviousnessBySurprise = exp(-surprise/searchParams.obviousMovesPolicySurpriseTolerance);
      double obviousnessWeight = std::min(obviousnessByEntropy, obviousnessBySurprise);
      tcRec *= 1.0 + obviousnessWeight * (searchParams.obviousMovesTimeFactor - 1.0);
    }
  }

  if(tcRec > 1e-20) {
    double remainingTimeNeeded = tcRec - effectiveSearchTimeCarriedOver;
    double remainingTimeNeededFactor = remainingTimeNeeded/tcRec;
    //TODO this is a bit conservative relative to old behavior, it might be of slightly detrimental value, needs testing.
    //Apply softplus so that we still do a tiny bit of search even in the presence of variable search time instead of instamoving,
    //there are some benefits from root-level search due to broader root exploration and the cost is small, also we may be over
    //counting the ponder benefit if search is faster on this node than on the previous turn.
    tcRec = tcRec * std::min(1.0, log(1.0+exp(remainingTimeNeededFactor * 6.0)) / 6.0);
  }

  //Make sure we're not wasting time
  tcRec = tc.roundUpTimeLimitIfNeeded(searchParams.lagBuffer,timeUsed,tcRec);
  if(tcRec > tcMax) tcRec = tcMax;

  //After rounding up time, check if with our planned rounded time, anything is futile to search
  if(searchParams.futileVisitsThreshold > 0) {
    double upperBoundVisitsLeftDueToTime = computeUpperBoundVisitsLeftDueToTime(rootVisits, timeUsed, tcRec);
    if(upperBoundVisitsLeftDueToTime < searchParams.futileVisitsThreshold * rootVisits) {
      vector<Loc> locs;
      vector<double> playSelectionValues;
      vector<double> visitCounts;
      bool suc = getPlaySelectionValues(locs, playSelectionValues, &visitCounts, 1.0);
      if(suc && playSelectionValues.size() > 0) {
        //This may fail to hold if we have no actual visits and play selections are being pulled from stuff like raw policy
        if(playSelectionValues.size() == visitCounts.size()) {
          int numMoves = (int)playSelectionValues.size();
          int maxVisitsIdx = 0;
          int bestMoveIdx = 0;
          for(int i = 1; i<numMoves; i++) {
            if(playSelectionValues[i] > playSelectionValues[bestMoveIdx])
              bestMoveIdx = i;
            if(visitCounts[i] > visitCounts[maxVisitsIdx])
              maxVisitsIdx = i;
          }
          if(maxVisitsIdx == bestMoveIdx) {
            double requiredVisits = numVisitsNeededToBeNonFutile(visitCounts[maxVisitsIdx]);
            bool foundPossibleAlternativeMove = false;
            for(int i = 0; i<numMoves; i++) {
              if(i == bestMoveIdx)
                continue;
              if(visitCounts[i] + upperBoundVisitsLeftDueToTime >= requiredVisits) {
                foundPossibleAlternativeMove = true;
                break;
              }
            }
            if(!foundPossibleAlternativeMove) {
              //We should stop search now - set our desired thinking to very slightly smaller than what we used.
              tcRec = timeUsed * (1.0 - (1e-10));
            }
          }
        }
      }
    }
  }

  //Make sure we're not wasting time, even after considering that we might want to stop early
  tcRec = tc.roundUpTimeLimitIfNeeded(searchParams.lagBuffer,timeUsed,tcRec);
  if(tcRec > tcMax) tcRec = tcMax;

  //Apply caps and search factor
  //Since searchFactor is mainly used for friendliness (like, play faster after many passes)
  //we allow it to violate the min time.
  if(tcRec < tcMin) tcRec = tcMin;
  tcRec *= searchFactor;
  if(tcRec > tcMax) tcRec = tcMax;

  return tcRec;
}

void Search::runWholeSearch(
  Logger& logger,
  std::atomic<bool>& shouldStopNow,
  std::function<void()>* searchBegun,
  bool pondering,
  const TimeControls& tc,
  double searchFactor
) {

  ClockTimer timer;
  atomic<int64_t> numPlayoutsShared(0);

  if(!std::atomic_is_lock_free(&numPlayoutsShared))
    logger.write("Warning: int64_t atomic numPlayoutsShared is not lock free");
  if(!std::atomic_is_lock_free(&shouldStopNow))
    logger.write("Warning: bool atomic shouldStopNow is not lock free");

  //Do this first, just in case this causes us to clear things and have 0 effective time carried over
  beginSearch(pondering);
  if(searchBegun != NULL)
    (*searchBegun)();
  const int64_t numNonPlayoutVisits = getRootVisits();

  //Compute caps on search
  int64_t maxVisits = pondering ? searchParams.maxVisitsPondering : searchParams.maxVisits;
  int64_t maxPlayouts = pondering ? searchParams.maxPlayoutsPondering : searchParams.maxPlayouts;
  double_t maxTime = pondering ? searchParams.maxTimePondering : searchParams.maxTime;

  {
    if(searchFactor != 1.0) {
      double cap = (double)((int64_t)1L << 62);
      maxVisits = (int64_t)ceil(std::min(cap, maxVisits * searchFactor));
      maxPlayouts = (int64_t)ceil(std::min(cap, maxPlayouts * searchFactor));
      maxTime = maxTime * searchFactor;
    }
  }

  //Apply time controls. These two don't particularly need to be synchronized with each other so its fine to have two separate atomics.
  std::atomic<double> tcMaxTime(1e30);
  std::atomic<double> upperBoundVisitsLeftDueToTime(1e30);
  const bool hasMaxTime = maxTime < 1.0e12;
  const bool hasTc = !pondering && !tc.isEffectivelyUnlimitedTime();
  if(!pondering && (hasTc || hasMaxTime)) {
    int64_t rootVisits = numPlayoutsShared.load(std::memory_order_relaxed) + numNonPlayoutVisits;
    double timeUsed = timer.getSeconds();
    double tcLimit = 1e30;
    if(hasTc) {
      tcLimit = recomputeSearchTimeLimit(tc, timeUsed, searchFactor, rootVisits);
      tcMaxTime.store(tcLimit, std::memory_order_release);
    }
    double upperBoundVisits = computeUpperBoundVisitsLeftDueToTime(rootVisits, timeUsed, std::min(tcLimit,maxTime));
    upperBoundVisitsLeftDueToTime.store(upperBoundVisits, std::memory_order_release);
  }

  auto searchLoop = [
    this,&timer,&numPlayoutsShared,numNonPlayoutVisits,&tcMaxTime,&upperBoundVisitsLeftDueToTime,&tc,
    &hasMaxTime,&hasTc,
    &logger,&shouldStopNow,maxVisits,maxPlayouts,maxTime,pondering,searchFactor
  ](int threadIdx) {
    SearchThread* stbuf = new SearchThread(threadIdx,*this,&logger);

    int64_t numPlayouts = numPlayoutsShared.load(std::memory_order_relaxed);
    try {
      double lastTimeUsedRecomputingTcLimit = 0.0;
      while(true) {
        double timeUsed = 0.0;
        if(hasTc || hasMaxTime)
          timeUsed = timer.getSeconds();

        double tcMaxTimeLimit = 0.0;
        if(hasTc)
          tcMaxTimeLimit = tcMaxTime.load(std::memory_order_acquire);

        bool shouldStop =
          (numPlayouts >= maxPlayouts) ||
          (numPlayouts + numNonPlayoutVisits >= maxVisits);

        if(hasMaxTime && numPlayouts >= 2 && timeUsed >= maxTime)
          shouldStop = true;
        if(hasTc && numPlayouts >= 2 && timeUsed >= tcMaxTimeLimit)
          shouldStop = true;

        if(shouldStop || shouldStopNow.load(std::memory_order_relaxed)) {
          shouldStopNow.store(true,std::memory_order_relaxed);
          break;
        }

        //Thread 0 alone is responsible for recomputing time limits every once in a while
        //Cap of 10 times per second.
        if(!pondering && (hasTc || hasMaxTime) && threadIdx == 0 && timeUsed >= lastTimeUsedRecomputingTcLimit + 0.1) {
          int64_t rootVisits = numPlayouts + numNonPlayoutVisits;
          double tcLimit = 1e30;
          if(hasTc) {
            tcLimit = recomputeSearchTimeLimit(tc, timeUsed, searchFactor, rootVisits);
            tcMaxTime.store(tcLimit, std::memory_order_release);
          }
          double upperBoundVisits = computeUpperBoundVisitsLeftDueToTime(rootVisits, timeUsed, std::min(tcLimit,maxTime));
          upperBoundVisitsLeftDueToTime.store(upperBoundVisits, std::memory_order_release);
        }

        double upperBoundVisitsLeft = 1e30;
        if(hasTc)
          upperBoundVisitsLeft = upperBoundVisitsLeftDueToTime.load(std::memory_order_acquire);
        upperBoundVisitsLeft = std::min(upperBoundVisitsLeft, (double)maxPlayouts - numPlayouts);
        upperBoundVisitsLeft = std::min(upperBoundVisitsLeft, (double)maxVisits - numPlayouts - numNonPlayoutVisits);

        runSinglePlayout(*stbuf, upperBoundVisitsLeft);

        numPlayouts = numPlayoutsShared.fetch_add((int64_t)1, std::memory_order_relaxed);
        numPlayouts += 1;
      }
    }
    catch(const exception& e) {
      logger.write(string("ERROR: Search thread failed: ") + e.what());
      delete stbuf;
      throw;
    }
    catch(const string& e) {
      logger.write("ERROR: Search thread failed: " + e);
      delete stbuf;
      throw;
    }
    catch(...) {
      logger.write("ERROR: Search thread failed with unexpected throw");
      delete stbuf;
      throw;
    }

    delete stbuf;
  };

  double actualSearchStartTime = timer.getSeconds();
  if(searchParams.numThreads <= 1)
    searchLoop(0);
  else {
    std::thread* threads = new std::thread[searchParams.numThreads-1];
    for(int i = 0; i<searchParams.numThreads-1; i++)
      threads[i] = std::thread(searchLoop,i+1);
    searchLoop(0);
    for(int i = 0; i<searchParams.numThreads-1; i++)
      threads[i].join();
    delete[] threads;
  }

  //Relaxed load is fine since numPlayoutsShared should be synchronized already due to the joins
  lastSearchNumPlayouts = numPlayoutsShared.load(std::memory_order_relaxed);
  effectiveSearchTimeCarriedOver += timer.getSeconds() - actualSearchStartTime;
}

//If we're being asked to search from a position where the game is over, this is fine. Just keep going, the boardhistory
//should reasonably tolerate just continuing. We do NOT want to clear history because we could inadvertently make a move
//that an external ruleset COULD think violated superko.
void Search::beginSearch(bool pondering) {
  if(rootBoard.x_size > nnXLen || rootBoard.y_size > nnYLen)
    throw StringError("Search got from NNEval nnXLen = " + Global::intToString(nnXLen) +
                      " nnYLen = " + Global::intToString(nnYLen) + " but was asked to search board with larger x or y size");

  numSearchesBegun++;
  searchNodeAge++;
  if(searchNodeAge == 0) //Just in case, as we roll over
    clearSearch();

  if(!pondering)
    plaThatSearchIsFor = rootPla;
  //If we begin the game with a ponder, then assume that "we" are the opposing side until we see otherwise.
  if(plaThatSearchIsFor == C_EMPTY)
    plaThatSearchIsFor = getOpp(rootPla);

  //In the case we are doing playoutDoublingAdvantage without a specific player (so, doing the root player)
  //and the player that the search is for changes, we need to clear the tree since we need new evals for the new way around
  if(plaThatSearchIsForLastSearch != plaThatSearchIsFor &&
     searchParams.playoutDoublingAdvantage != 0 &&
     searchParams.playoutDoublingAdvantagePla == C_EMPTY)
    clearSearch();
  plaThatSearchIsForLastSearch = plaThatSearchIsFor;
  //cout << "BEGINSEARCH " << PlayerIO::playerToString(rootPla) << " " << PlayerIO::playerToString(plaThatSearchIsFor) << endl;

  computeRootValues();
  maybeRecomputeNormToTApproxTable();

  SearchThread dummyThread(-1, *this, NULL);

  if(rootNode == NULL) {
    Loc prevMoveLoc = rootHistory.moveHistory.size() <= 0 ? Board::NULL_LOC : rootHistory.moveHistory[rootHistory.moveHistory.size()-1].loc;
    rootNode = new SearchNode(*this, getOpp(rootPla), dummyThread.rand, prevMoveLoc, NULL);
  }
  else {
    //If the root node has any existing children, then prune things down if there are moves that should not be allowed at the root.
    SearchNode& node = *rootNode;
    int numChildren = node.numChildren;
    if(node.children != NULL && numChildren > 0) {
      assert(node.nnOutput != NULL);

      //Perform the filtering
      int numGoodChildren = 0;
      for(int i = 0; i<numChildren; i++) {
        SearchNode* child = node.children[i];
        node.children[i] = NULL;
        if(isAllowedRootMove(child->prevMoveLoc))
          node.children[numGoodChildren++] = child;
        else {
          recursivelyRemoveSubtreeValueBiasBeforeDeleteSynchronous(child);
          delete child;
        }
      }
      bool anyFiltered = numChildren != numGoodChildren;
      node.numChildren = numGoodChildren;
      numChildren = numGoodChildren;

      if(anyFiltered) {
        //Fix up the number of visits of the root node after doing this filtering
        int64_t newNumVisits = 0;
        for(int i = 0; i<numChildren; i++) {
          const SearchNode* child = node.children[i];
          while(child->statsLock.test_and_set(std::memory_order_acquire));
          int64_t childVisits = child->stats.visits;
          child->statsLock.clear(std::memory_order_release);
          newNumVisits += childVisits;
        }
        //For the node's own visit itself
        newNumVisits += 1;

        //Set the visits in place
        while(node.statsLock.test_and_set(std::memory_order_acquire));
        node.stats.visits = newNumVisits;
        node.statsLock.clear(std::memory_order_release);

        //Update all other stats
        recomputeNodeStats(node, dummyThread, 0, 0, true);
      }
    }
  }
}

//Recursively walk over part of the tree that we are about to delete and remove its contribution to the value bias in the table
//Assumes we aren't doing any multithreadingy stuff, so doesn't bother with locks.
void Search::recursivelyRemoveSubtreeValueBiasBeforeDeleteSynchronous(SearchNode* node) {
    return;
}


void Search::recursivelyRecomputeStats(SearchNode& node, SearchThread& thread, bool isRoot) {
  //First, recompute all children.
  vector<SearchNode*> children;
  children.reserve(rootBoard.x_size * rootBoard.y_size + 1);

  int numChildren;
  bool noNNOutput;
  {
    std::mutex& mutex = mutexPool->getMutex(node.lockIdx);
    lock_guard<std::mutex> lock(mutex);
    numChildren = node.numChildren;
    for(int i = 0; i<numChildren; i++)
      children.push_back(node.children[i]);

    noNNOutput = node.nnOutput == nullptr;
  }

  for(int i = 0; i<numChildren; i++) {
    recursivelyRecomputeStats(*(children[i]),thread,false);
  }

  //If this node has no nnOutput, then it must also have no children, because it's
  //a terminal node
  assert(!(noNNOutput && numChildren > 0));
  (void)noNNOutput; //avoid warning when we have no asserts

  //If the node has no children, then just update its utility directly
  if(numChildren <= 0) {
    while(node.statsLock.test_and_set(std::memory_order_acquire));
    double resultUtilitySum = node.stats.getResultUtilitySum(searchParams);
    double scoreMeanSum = node.stats.scoreMeanSum;
    double scoreMeanSqSum = node.stats.scoreMeanSqSum;
    double weightSum = node.stats.weightSum;
    int64_t numVisits = node.stats.visits;
    node.statsLock.clear(std::memory_order_release);

    //It's possible that this node has 0 weight in the case where it's the root node
    //and has 0 visits because we began a search and then stopped it before any playouts happened.
    //In that case, there's not much to recompute.
    if(weightSum <= 0.0) {
      assert(numVisits == 0);
      assert(isRoot);
    }
    else {
      double scoreUtility = getScoreUtility(scoreMeanSum, scoreMeanSqSum, weightSum);

      double newUtility = resultUtilitySum / weightSum + scoreUtility;
      double newUtilitySum = newUtility * weightSum;
      double newUtilitySqSum = newUtility * newUtility * weightSum;

      while(node.statsLock.test_and_set(std::memory_order_acquire));
      node.stats.utilitySum = newUtilitySum;
      node.stats.utilitySqSum = newUtilitySqSum;
      node.statsLock.clear(std::memory_order_release);
    }
  }
  else {
    //Otherwise recompute it using the usual method
    recomputeNodeStats(node, thread, 0, 0, isRoot);
  }
}

void Search::computeRootNNEvaluation(NNResultBuf& nnResultBuf, bool includeOwnerMap) {
  Board board = rootBoard;
  const BoardHistory& hist = rootHistory;
  Player pla = rootPla;
  bool skipCache = false;
  // bool isRoot = true;
  MiscNNInputParams nnInputParams;
  nnInputParams.drawEquivalentWinsForWhite = searchParams.drawEquivalentWinsForWhite;
  nnInputParams.conservativePass = searchParams.conservativePass;
  nnInputParams.nnPolicyTemperature = searchParams.nnPolicyTemperature;
  nnInputParams.avoidMYTDaggerHack = searchParams.avoidMYTDaggerHackPla == pla;
  nnInputParams.policyOptimism = searchParams.rootPolicyOptimism;

  nnEvaluator->evaluate(
    board, hist, pla,
    nnInputParams,
    nnResultBuf, includeOwnerMap
  );
}

void Search::computeRootValues() {
  //rootSafeArea is strictly pass-alive groups and strictly safe territory.
  bool nonPassAliveStones = false;
  bool safeBigTerritories = false;
  bool unsafeBigTerritories = false;
  bool isMultiStoneSuicideLegal = rootHistory.rules.multiStoneSuicideLegal;

  //Figure out how to set recentScoreCenter
  {
    bool foundExpectedScoreFromTree = false;
    double expectedScore = 0.0;
    if(rootNode != NULL) {
      const SearchNode& node = *rootNode;
      while(node.statsLock.test_and_set(std::memory_order_acquire));
      double scoreMeanSum = node.stats.scoreMeanSum;
      double weightSum = node.stats.weightSum;
      int64_t numVisits = node.stats.visits;
      node.statsLock.clear(std::memory_order_release);
      if(numVisits > 0 && weightSum > 0) {
        foundExpectedScoreFromTree = true;
        expectedScore = scoreMeanSum / weightSum;
      }
    }

    //Grab a neural net evaluation for the current position and use that as the center
    if(!foundExpectedScoreFromTree) {
      NNResultBuf nnResultBuf;
      bool includeOwnerMap = true;
      computeRootNNEvaluation(nnResultBuf,includeOwnerMap);
      expectedScore = nnResultBuf.result->whiteScoreMean;
    }

    recentScoreCenter = expectedScore * (1.0 - searchParams.dynamicScoreCenterZeroWeight);
    double cap =  sqrt(rootBoard.x_size * rootBoard.y_size) * searchParams.dynamicScoreCenterScale;
    if(recentScoreCenter > expectedScore + cap)
      recentScoreCenter = expectedScore + cap;
    if(recentScoreCenter < expectedScore - cap)
      recentScoreCenter = expectedScore - cap;
  }
}

int64_t Search::getRootVisits() const {
  if(rootNode == NULL)
    return 0;
  while(rootNode->statsLock.test_and_set(std::memory_order_acquire));
  int64_t n = rootNode->stats.visits;
  rootNode->statsLock.clear(std::memory_order_release);
  return n;
}

void Search::computeDirichletAlphaDistribution(int policySize, const float* policyProbs, double* alphaDistr) {
  int legalCount = 0;
  for(int i = 0; i<policySize; i++) {
    if(policyProbs[i] >= 0)
      legalCount += 1;
  }

  if(legalCount <= 0)
    throw StringError("computeDirichletAlphaDistribution: No move with nonnegative policy value - can't even pass?");

  //We're going to generate a gamma draw on each move with alphas that sum up to searchParams.rootDirichletNoiseTotalConcentration.
  //Half of the alpha weight are uniform.
  //The other half are shaped based on the log of the existing policy.
  double logPolicySum = 0.0;
  for(int i = 0; i<policySize; i++) {
    if(policyProbs[i] >= 0) {
      alphaDistr[i] = log(std::min(0.01, (double)policyProbs[i]) + 1e-20);
      logPolicySum += alphaDistr[i];
    }
  }
  double logPolicyMean = logPolicySum / legalCount;
  double alphaPropSum = 0.0;
  for(int i = 0; i<policySize; i++) {
    if(policyProbs[i] >= 0) {
      alphaDistr[i] = std::max(0.0, alphaDistr[i] - logPolicyMean);
      alphaPropSum += alphaDistr[i];
    }
  }
  double uniformProb = 1.0 / legalCount;
  if(alphaPropSum <= 0.0) {
    for(int i = 0; i<policySize; i++) {
      if(policyProbs[i] >= 0)
        alphaDistr[i] = uniformProb;
    }
  }
  else {
    for(int i = 0; i<policySize; i++) {
      if(policyProbs[i] >= 0)
        alphaDistr[i] = 0.5 * (alphaDistr[i] / alphaPropSum + uniformProb);
    }
  }
}

void Search::addDirichletNoise(const SearchParams& searchParams, Rand& rand, int policySize, float* policyProbs) {
  double r[NNPos::MAX_NN_POLICY_SIZE];
  Search::computeDirichletAlphaDistribution(policySize, policyProbs, r);

  //r now contains the proportions with which we would like to split the alpha
  //The total of the alphas is searchParams.rootDirichletNoiseTotalConcentration
  //Generate gamma draw on each move
  double rSum = 0.0;
  for(int i = 0; i<policySize; i++) {
    if(policyProbs[i] >= 0) {
      r[i] = rand.nextGamma(r[i] * searchParams.rootDirichletNoiseTotalConcentration);
      rSum += r[i];
    }
    else
      r[i] = 0.0;
  }

  //Normalized gamma draws -> dirichlet noise
  for(int i = 0; i<policySize; i++)
    r[i] /= rSum;

  //At this point, r[i] contains a dirichlet distribution draw, so add it into the nnOutput.
  for(int i = 0; i<policySize; i++) {
    if(policyProbs[i] >= 0) {
      double weight = searchParams.rootDirichletNoiseWeight;
      policyProbs[i] = (float)(r[i] * weight + policyProbs[i] * (1.0-weight));
    }
  }
}


//Assumes node is locked
void Search::maybeAddPolicyNoiseAndTempAlreadyLocked(SearchThread& thread, SearchNode& node, bool isRoot) const {
  if(!isRoot)
    return;
  if(!searchParams.rootNoiseEnabled && searchParams.rootPolicyTemperature == 1.0 && searchParams.rootPolicyTemperatureEarly == 1.0 && rootHintLoc == Board::NULL_LOC)
    return;
  if(node.nnOutput->noisedPolicyProbs != NULL)
    return;

  //Copy nnOutput as we're about to modify its policy to add noise or temperature
  {
    shared_ptr<NNOutput> newNNOutput = std::make_shared<NNOutput>(*(node.nnOutput));
    //Replace the old pointer
    node.nnOutput = newNNOutput;
  }

  float* noisedPolicyProbs = new float[NNPos::MAX_NN_POLICY_SIZE];
  node.nnOutput->noisedPolicyProbs = noisedPolicyProbs;
  std::copy(node.nnOutput->policyProbs, node.nnOutput->policyProbs + NNPos::MAX_NN_POLICY_SIZE, noisedPolicyProbs);

  if(searchParams.rootPolicyTemperature != 1.0 || searchParams.rootPolicyTemperatureEarly != 1.0) {
    double rootPolicyTemperature = interpolateEarly(
      searchParams.chosenMoveTemperatureHalflife, searchParams.rootPolicyTemperatureEarly, searchParams.rootPolicyTemperature
    );

    double maxValue = 0.0;
    for(int i = 0; i<policySize; i++) {
      double prob = noisedPolicyProbs[i];
      if(prob > maxValue)
        maxValue = prob;
    }
    assert(maxValue > 0.0);

    double logMaxValue = log(maxValue);
    double invTemp = 1.0 / rootPolicyTemperature;
    double sum = 0.0;

    for(int i = 0; i<policySize; i++) {
      if(noisedPolicyProbs[i] > 0) {
        //Numerically stable way to raise to power and normalize
        float p = (float)exp((log((double)noisedPolicyProbs[i]) - logMaxValue) * invTemp);
        noisedPolicyProbs[i] = p;
        sum += p;
      }
    }
    assert(sum > 0.0);
    for(int i = 0; i<policySize; i++) {
      if(noisedPolicyProbs[i] >= 0) {
        noisedPolicyProbs[i] = (float)(noisedPolicyProbs[i] / sum);
      }
    }
  }

  if(searchParams.rootNoiseEnabled) {
    addDirichletNoise(searchParams, thread.rand, policySize, noisedPolicyProbs);
  }

  //Move a small amount of policy to the hint move, around the same level that noising it would achieve
  if(rootHintLoc != Board::NULL_LOC) {
    const float propToMove = 0.02f;
    int pos = getPos(rootHintLoc);
    if(noisedPolicyProbs[pos] >= 0) {
      double amountToMove = 0.0;
      for(int i = 0; i<policySize; i++) {
        if(noisedPolicyProbs[i] >= 0) {
          amountToMove += noisedPolicyProbs[i] * propToMove;
          noisedPolicyProbs[i] *= (1.0f-propToMove);
        }
      }
      noisedPolicyProbs[pos] += (float)amountToMove;
    }
  }
}

bool Search::isAllowedRootMove(Loc moveLoc) const {
  assert(rootBoard.isOnBoard(moveLoc));

  //A bad situation that can happen that unnecessarily prolongs training games is where one player
  //repeatedly passes and the other side repeatedly fills the opponent's space and/or suicides over and over.
  //To mitigate some of this and save computation, we make it so that at the root, if the last four moves by the opponent
  //were passes, we will never play a move in either player's pass-alive area. In theory this could prune
  //a good move in situations like https://senseis.xmp.net/?1EyeFlaw, but this should be extraordinarly rare,
  return true;
}

void Search::getValueChildWeights(
  int numChildren,
  //Unlike everywhere else where values are from white's perspective, values here are from one's own perspective
  const vector<double>& childSelfValuesBuf,
  const vector<int64_t>& childVisitsBuf,
  vector<double>& resultBuf
) const {
  resultBuf.clear();
  if(numChildren <= 0)
    return;
  if(numChildren == 1) {
    resultBuf.push_back(1.0);
    return;
  }

  assert(numChildren <= NNPos::MAX_NN_POLICY_SIZE);
  double stdevs[NNPos::MAX_NN_POLICY_SIZE];
  for(int i = 0; i<numChildren; i++) {
    int64_t numVisits = childVisitsBuf[i];
    assert(numVisits >= 0);
    if(numVisits == 0) {
      stdevs[i] = 0.0; //Unused
      continue;
    }

    double precision = 1.5 * sqrt((double)numVisits);

    //Ensure some minimum variance for stability regardless of how we change the above formula
    static const double minVariance = 0.00000001;
    stdevs[i] = sqrt(minVariance + 1.0 / precision);
  }

  double simpleValueSum = 0.0;
  int64_t numChildVisits = 0;
  for(int i = 0; i<numChildren; i++) {
    simpleValueSum += childSelfValuesBuf[i] * childVisitsBuf[i];
    numChildVisits += childVisitsBuf[i];
  }

  double simpleValue = simpleValueSum / numChildVisits;

  double weight[NNPos::MAX_NN_POLICY_SIZE];
  for(int i = 0; i<numChildren; i++) {
    if(childVisitsBuf[i] == 0) {
      weight[i] = 0.0;
      continue;
    }
    else {
      double z = (childSelfValuesBuf[i] - simpleValue) / stdevs[i];
      //Also just for numeric sanity, make sure everything has some tiny minimum value.
      weight[i] = valueWeightDistribution->getCdf(z) + 0.0001;
    }
  }

  //Post-process and normalize, to make sure we exactly have a probability distribution and sum exactly to 1.
  double totalWeight = 0.0;
  for(int i = 0; i<numChildren; i++) {
    double p = weight[i];
    totalWeight += p;
    resultBuf.push_back(p);
  }

  assert(totalWeight >= 0.0);
  if(totalWeight > 0) {
    for(int i = 0; i<numChildren; i++) {
      resultBuf[i] /= totalWeight;
    }
  }

}

static double cpuctExploration(int64_t totalChildVisits, const SearchParams& searchParams) {
  return searchParams.cpuctExploration +
    searchParams.cpuctExplorationLog * log((totalChildVisits + searchParams.cpuctExplorationBase) / searchParams.cpuctExplorationBase);
}

double Search::getExploreSelectionValue(
  double nnPolicyProb, int64_t totalChildVisits, int64_t childVisits,
  double childUtility, Player pla
) const {
  if(nnPolicyProb < 0)
    return POLICY_ILLEGAL_SELECTION_VALUE;

  double exploreComponent =
    cpuctExploration(totalChildVisits,searchParams)
    * nnPolicyProb
    * sqrt((double)totalChildVisits + 0.01) //TODO this is weird when totalChildVisits == 0, first exploration
    / (1.0 + childVisits);

  //At the last moment, adjust value to be from the player's perspective, so that players prefer values in their favor
  //rather than in white's favor
  double valueComponent = pla == P_WHITE ? childUtility : -childUtility;
  return exploreComponent + valueComponent;
}

//Return the childVisits that would make Search::getExploreSelectionValue return the given explore selection value.
//Or return 0, if it would be less than 0.
double Search::getExploreSelectionValueInverse(
  double exploreSelectionValue, double nnPolicyProb, int64_t totalChildVisits,
  double childUtility, Player pla
) const {
  if(nnPolicyProb < 0)
    return 0;
  double valueComponent = pla == P_WHITE ? childUtility : -childUtility;

  double exploreComponent = exploreSelectionValue - valueComponent;
  double exploreComponentScaling =
    cpuctExploration(totalChildVisits,searchParams)
    * nnPolicyProb
    * sqrt((double)totalChildVisits + 0.01); //TODO this is weird when totalChildVisits == 0, first exploration

  //Guard against float weirdness
  if(exploreComponent <= 0)
    return 1e100;

  double childVisits = exploreComponentScaling / exploreComponent - 1;
  if(childVisits < 0)
    childVisits = 0;
  return childVisits;
}


//Parent must be locked
double Search::getEndingWhiteScoreBonus(const SearchNode& parent, const SearchNode* child) const {
    return 0.0;
}

int Search::getPos(Loc moveLoc) const {
  return NNPos::locToPos(moveLoc,rootBoard.x_size,nnXLen,nnYLen);
}

static void maybeApplyWideRootNoise(
  double& childUtility,
  float& nnPolicyProb,
  const SearchParams& searchParams,
  SearchThread* thread,
  const SearchNode& parent
) {
  //For very large wideRootNoise, go ahead and also smooth out the policy
  nnPolicyProb = (float)pow(nnPolicyProb, 1.0 / (4.0*searchParams.wideRootNoise + 1.0));
  if(thread->rand.nextBool(0.5)) {
    double bonus = searchParams.wideRootNoise * abs(thread->rand.nextGaussian());
    if(parent.nextPla == P_WHITE)
      childUtility += bonus;
    else
      childUtility -= bonus;
  }
}

static double square(double x) {
  return x * x;
}

//Parent must be locked
double Search::getExploreSelectionValue(
  const SearchNode& parent, const float* parentPolicyProbs, const SearchNode* child,
  int64_t totalChildVisits, double fpuValue, double parentUtility,
  bool isDuringSearch, int64_t maxChildVisits, SearchThread* thread
) const {
  (void)parentUtility;
  Loc moveLoc = child->prevMoveLoc;
  int movePos = getPos(moveLoc);
  float nnPolicyProb = parentPolicyProbs[movePos];

  while(child->statsLock.test_and_set(std::memory_order_acquire));
  int64_t childVisits = child->stats.visits;
  double utilitySum = child->stats.utilitySum;
  double scoreMeanSum = child->stats.scoreMeanSum;
  double scoreMeanSqSum = child->stats.scoreMeanSqSum;
  double weightSum = child->stats.weightSum;
  int32_t childVirtualLosses = child->virtualLosses;
  child->statsLock.clear(std::memory_order_release);

  //It's possible that childVisits is actually 0 here with multithreading because we're visiting this node while a child has
  //been expanded but its thread not yet finished its first visit
  double childUtility;
  if(childVisits <= 0)
    childUtility = fpuValue;
  else {
    assert(weightSum > 0.0);
    childUtility = utilitySum / weightSum;

    //Tiny adjustment for passing
    double endingScoreBonus = getEndingWhiteScoreBonus(parent,child);
    if(endingScoreBonus != 0)
      childUtility += getScoreUtilityDiff(scoreMeanSum, scoreMeanSqSum, weightSum, endingScoreBonus);
  }

  //When multithreading, totalChildVisits could be out of sync with childVisits, so if they provably are, then fix that up
  if(totalChildVisits < childVisits)
    totalChildVisits = childVisits;

  //Virtual losses to direct threads down different paths
  if(childVirtualLosses > 0) {
    //totalChildVisits += childVirtualLosses; //Should get better thread dispersal without this
    childVisits += childVirtualLosses;
    double utilityRadius = searchParams.winLossUtilityFactor + searchParams.staticScoreUtilityFactor + searchParams.dynamicScoreUtilityFactor;
    double virtualLossUtility = (parent.nextPla == P_WHITE ? -utilityRadius : utilityRadius);
    double virtualLossVisitFrac = (double)childVirtualLosses / childVisits;
    childUtility = childUtility + (virtualLossUtility - childUtility) * virtualLossVisitFrac;
  }

  if(isDuringSearch && (&parent == rootNode)) {
    //Futile visits pruning - skip this move if the amount of time we have left to search is too small
    if(searchParams.futileVisitsThreshold > 0) {
      double requiredVisits = searchParams.futileVisitsThreshold * maxChildVisits;
      if(childVisits + thread->upperBoundVisitsLeft < requiredVisits)
        return FUTILE_VISITS_PRUNE_VALUE;
    }
    //Hack to get the root to funnel more visits down child branches
    if(searchParams.rootDesiredPerChildVisitsCoeff > 0.0) {
      if(childVisits < sqrt(nnPolicyProb * totalChildVisits * searchParams.rootDesiredPerChildVisitsCoeff)) {
        return 1e20;
      }
    }
    //Hack for hintloc - must search this move almost as often as the most searched move
    if(rootHintLoc != Board::NULL_LOC && moveLoc == rootHintLoc) {
      for(int i = 0; i<parent.numChildren; i++) {
        const SearchNode* c = parent.children[i];
        while(c->statsLock.test_and_set(std::memory_order_acquire));
        int64_t cVisits = c->stats.visits;
        c->statsLock.clear(std::memory_order_release);
        if(childVisits+1 < cVisits * 0.8)
          return 1e20;
      }
    }

    if(searchParams.wideRootNoise > 0.0) {
      maybeApplyWideRootNoise(childUtility, nnPolicyProb, searchParams, thread, parent);
    }
  }

  return getExploreSelectionValue(nnPolicyProb,totalChildVisits,childVisits,childUtility,parent.nextPla);
}

double Search::getNewExploreSelectionValue(
  const SearchNode& parent, float nnPolicyProb,
  int64_t totalChildVisits, double fpuValue,
  int64_t maxChildVisits, SearchThread* thread
) const {
  int64_t childVisits = 0;
  double childUtility = fpuValue;
  if(&parent == rootNode) {
    //Futile visits pruning - skip this move if the amount of time we have left to search is too small
    if(searchParams.futileVisitsThreshold > 0) {
      double requiredVisits = searchParams.futileVisitsThreshold * maxChildVisits;
      if(thread->upperBoundVisitsLeft < requiredVisits)
        return FUTILE_VISITS_PRUNE_VALUE;
    }
    if(searchParams.wideRootNoise > 0.0) {
      maybeApplyWideRootNoise(childUtility, nnPolicyProb, searchParams, thread, parent);
    }
  }
  return getExploreSelectionValue(nnPolicyProb,totalChildVisits,childVisits,childUtility,parent.nextPla);
}

//Parent must be locked
int64_t Search::getReducedPlaySelectionVisits(
  const SearchNode& parent, const float* parentPolicyProbs, const SearchNode* child,
  int64_t totalChildVisits, double bestChildExploreSelectionValue
) const {
  assert(&parent == rootNode);
  Loc moveLoc = child->prevMoveLoc;
  int movePos = getPos(moveLoc);
  float nnPolicyProb = parentPolicyProbs[movePos];

  while(child->statsLock.test_and_set(std::memory_order_acquire));
  int64_t childVisits = child->stats.visits;
  double utilitySum = child->stats.utilitySum;
  double scoreMeanSum = child->stats.scoreMeanSum;
  double scoreMeanSqSum = child->stats.scoreMeanSqSum;
  double weightSum = child->stats.weightSum;
  child->statsLock.clear(std::memory_order_release);

  //Child visits may be 0 if this function is called in a multithreaded context, such as during live analysis
  if(childVisits <= 0)
    return 0;
  assert(weightSum > 0.0);

  //Tiny adjustment for passing
  double endingScoreBonus = getEndingWhiteScoreBonus(parent,child);
  double childUtility = utilitySum / weightSum;
  if(endingScoreBonus != 0)
    childUtility += getScoreUtilityDiff(scoreMeanSum, scoreMeanSqSum, weightSum, endingScoreBonus);

  double childVisitsWeRetrospectivelyWanted = getExploreSelectionValueInverse(
    bestChildExploreSelectionValue, nnPolicyProb, totalChildVisits, childUtility, parent.nextPla
  );
  if(childVisits > childVisitsWeRetrospectivelyWanted)
    childVisits = (int64_t)ceil(childVisitsWeRetrospectivelyWanted);
  return childVisits;
}

double Search::getFpuValueForChildrenAssumeVisited(const SearchNode& node, Player pla, bool isRoot, double policyProbMassVisited, double& parentUtility) const {
  if(searchParams.fpuParentWeight < 1.0) {
    while(node.statsLock.test_and_set(std::memory_order_acquire));
    double utilitySum = node.stats.utilitySum;
    double weightSum = node.stats.weightSum;
    node.statsLock.clear(std::memory_order_release);

    assert(weightSum > 0.0);
    parentUtility = utilitySum / weightSum;
    if(searchParams.fpuParentWeight > 0.0) {
      parentUtility = searchParams.fpuParentWeight * getUtilityFromNN(*node.nnOutput) + (1.0 - searchParams.fpuParentWeight) * parentUtility;
    }
  }
  else {
    parentUtility = getUtilityFromNN(*node.nnOutput);
  }

  double fpuValue;
  {
    double fpuReductionMax = isRoot ? searchParams.rootFpuReductionMax : searchParams.fpuReductionMax;
    double fpuLossProp = isRoot ? searchParams.rootFpuLossProp : searchParams.fpuLossProp;
    double utilityRadius = searchParams.winLossUtilityFactor + searchParams.staticScoreUtilityFactor + searchParams.dynamicScoreUtilityFactor;

    double reduction = fpuReductionMax * sqrt(policyProbMassVisited);
    fpuValue = pla == P_WHITE ? parentUtility - reduction : parentUtility + reduction;
    double lossValue = pla == P_WHITE ? -utilityRadius : utilityRadius;
    fpuValue = fpuValue + (lossValue - fpuValue) * fpuLossProp;
  }

  return fpuValue;
}


//Assumes node is locked
void Search::selectBestChildToDescend(
  SearchThread& thread, const SearchNode& node, int& bestChildIdx, Loc& bestChildMoveLoc,
  bool posesWithChildBuf[NNPos::MAX_NN_POLICY_SIZE],
  bool isRoot) const
{
  assert(thread.pla == node.nextPla);

  double maxSelectionValue = POLICY_ILLEGAL_SELECTION_VALUE;
  bestChildIdx = -1;
  bestChildMoveLoc = Board::NULL_LOC;

  int numChildren = node.numChildren;

  double policyProbMassVisited = 0.0;
  int64_t maxChildVisits = 0;
  int64_t totalChildVisits = 0;
  float* policyProbs = node.nnOutput->getPolicyProbsMaybeNoised();
  for(int i = 0; i<numChildren; i++) {
    const SearchNode* child = node.children[i];
    Loc moveLoc = child->prevMoveLoc;
    int movePos = getPos(moveLoc);
    float nnPolicyProb = policyProbs[movePos];
    policyProbMassVisited += nnPolicyProb;

    while(child->statsLock.test_and_set(std::memory_order_acquire));
    int64_t childVisits = child->stats.visits;
    child->statsLock.clear(std::memory_order_release);

    totalChildVisits += childVisits;
    if(childVisits > maxChildVisits)
      maxChildVisits = childVisits;
  }
  //Probability mass should not sum to more than 1, giving a generous allowance
  //for floating point error.
  assert(policyProbMassVisited <= 1.0001);

  //First play urgency
  double parentUtility;
  double fpuValue = getFpuValueForChildrenAssumeVisited(node, thread.pla, isRoot, policyProbMassVisited, parentUtility);

  std::fill(posesWithChildBuf,posesWithChildBuf+NNPos::MAX_NN_POLICY_SIZE,false);

  //Try all existing children
  for(int i = 0; i<numChildren; i++) {
    const SearchNode* child = node.children[i];
    Loc moveLoc = child->prevMoveLoc;
    bool isDuringSearch = true;
    double selectionValue = getExploreSelectionValue(node,policyProbs,child,totalChildVisits,fpuValue,parentUtility,isDuringSearch,maxChildVisits,&thread);
    if(selectionValue > maxSelectionValue) {
      maxSelectionValue = selectionValue;
      bestChildIdx = i;
      bestChildMoveLoc = moveLoc;
    }

    posesWithChildBuf[getPos(moveLoc)] = true;
  }

  const std::vector<int>& avoidMoveUntilByLoc = thread.pla == P_BLACK ? avoidMoveUntilByLocBlack : avoidMoveUntilByLocWhite;

  //Try the new child with the best policy value
  Loc bestNewMoveLoc = Board::NULL_LOC;
  float bestNewNNPolicyProb = -1.0f;
  for(int movePos = 0; movePos<policySize; movePos++) {
    bool alreadyTried = posesWithChildBuf[movePos];
    if(alreadyTried)
      continue;

    Loc moveLoc = NNPos::posToLoc(movePos,thread.board.x_size,thread.board.y_size,nnXLen,nnYLen);
    if(moveLoc == Board::NULL_LOC)
      continue;

    //Special logic for the root
    if(isRoot) {
      assert(thread.board.pos_hash == rootBoard.pos_hash);
      assert(thread.pla == rootPla);
      if(!isAllowedRootMove(moveLoc))
        continue;
    }
    if(avoidMoveUntilByLoc.size() > 0) {
      assert(avoidMoveUntilByLoc.size() >= Board::MAX_ARR_SIZE);
      int untilDepth = avoidMoveUntilByLoc[moveLoc];
      if(thread.history.moveHistory.size() - rootHistory.moveHistory.size() < untilDepth)
        continue;
    }

    float nnPolicyProb = policyProbs[movePos];

    if(nnPolicyProb > bestNewNNPolicyProb) {
      bestNewNNPolicyProb = nnPolicyProb;
      bestNewMoveLoc = moveLoc;
    }
  }
  if(bestNewMoveLoc != Board::NULL_LOC) {
    double selectionValue = getNewExploreSelectionValue(node,bestNewNNPolicyProb,totalChildVisits,fpuValue,maxChildVisits,&thread);
    if(selectionValue > maxSelectionValue) {
      maxSelectionValue = selectionValue;
      bestChildIdx = numChildren;
      bestChildMoveLoc = bestNewMoveLoc;
    }
  }

}
void Search::updateStatsAfterPlayout(SearchNode& node, SearchThread& thread, int32_t virtualLossesToSubtract, bool isRoot) {
  recomputeNodeStats(node,thread,1,virtualLossesToSubtract,isRoot);
}

//Recompute all the stats of this node based on its children, except its visits and virtual losses, which are not child-dependent and
//are updated in the manner specified.
//Assumes this node has an nnOutput
void Search::recomputeNodeStats(SearchNode& node, SearchThread& thread, int numVisitsToAdd, int32_t virtualLossesToSubtract, bool isRoot) {
  //Find all children and compute weighting of the children based on their values
  vector<double>& weightFactors = thread.weightFactorBuf;
  vector<double>& winValues = thread.winValuesBuf;
  vector<double>& noResultValues = thread.noResultValuesBuf;
  vector<double>& scoreMeans = thread.scoreMeansBuf;
  vector<double>& scoreMeanSqs = thread.scoreMeanSqsBuf;
  vector<double>& leads = thread.leadsBuf;
  vector<double>& utilitySums = thread.utilityBuf;
  vector<double>& utilitySqSums = thread.utilitySqBuf;
  vector<double>& selfUtilities = thread.selfUtilityBuf;
  vector<double>& weightSums = thread.weightBuf;
  vector<double>& weightSqSums = thread.weightSqBuf;
  vector<int64_t>& visits = thread.visitsBuf;

  int64_t totalChildVisits = 0;
  int64_t maxChildVisits = 0;

  std::mutex& mutex = mutexPool->getMutex(node.lockIdx);
  unique_lock<std::mutex> lock(mutex);

  int numChildren = node.numChildren;
  int numGoodChildren = 0;
  for(int i = 0; i<numChildren; i++) {
    const SearchNode* child = node.children[i];

    while(child->statsLock.test_and_set(std::memory_order_acquire));
    int64_t childVisits = child->stats.visits;
    double winValueSum = child->stats.winValueSum;
    double noResultValueSum = child->stats.noResultValueSum;
    double scoreMeanSum = child->stats.scoreMeanSum;
    double scoreMeanSqSum = child->stats.scoreMeanSqSum;
    double leadSum = child->stats.leadSum;
    double weightSum = child->stats.weightSum;
    double weightSqSum = child->stats.weightSqSum;
    double utilitySum = child->stats.utilitySum;
    double utilitySqSum = child->stats.utilitySqSum;
    child->statsLock.clear(std::memory_order_release);

    if(childVisits <= 0)
      continue;
    assert(weightSum > 0.0);

    double childUtility = utilitySum / weightSum;

    winValues[numGoodChildren] = winValueSum / weightSum;
    noResultValues[numGoodChildren] = noResultValueSum / weightSum;
    scoreMeans[numGoodChildren] = scoreMeanSum / weightSum;
    scoreMeanSqs[numGoodChildren] = scoreMeanSqSum / weightSum;
    leads[numGoodChildren] = leadSum / weightSum;
    utilitySums[numGoodChildren] = utilitySum;
    utilitySqSums[numGoodChildren] = utilitySqSum;
    selfUtilities[numGoodChildren] = node.nextPla == P_WHITE ? childUtility : -childUtility;
    weightSums[numGoodChildren] = weightSum;
    weightSqSums[numGoodChildren] = weightSqSum;
    visits[numGoodChildren] = childVisits;
    totalChildVisits += childVisits;

    if(childVisits > maxChildVisits)
      maxChildVisits = childVisits;
    numGoodChildren++;
  }
  lock.unlock();

  if(searchParams.valueWeightExponent > 0)
    getValueChildWeights(numGoodChildren,selfUtilities,visits,weightFactors);

  //In the case we're enabling noise at the root node, also apply the slight subtraction
  //of visits from the root node's children so as to downweight the effect of the few dozen visits
  //we send towards children that are so bad that we never try them even once again.

  //One slightly surprising behavior is that this slight subtraction won't happen in the case where
  //we have just promoted a child to the root due to preservation of the tree across moves
  //but we haven't sent any playouts through the root yet. But having rootNoiseEnabled without
  //clearing the tree every search is a bit weird anyways.
  double amountToSubtract = 0.0;
  double amountToPrune = 0.0;
  if(isRoot && searchParams.rootNoiseEnabled) {
    amountToSubtract = std::min(searchParams.chosenMoveSubtract, maxChildVisits/64.0);
    amountToPrune = std::min(searchParams.chosenMovePrune, maxChildVisits/64.0);
  }

  double winValueSum = 0.0;
  double noResultValueSum = 0.0;
  double scoreMeanSum = 0.0;
  double scoreMeanSqSum = 0.0;
  double leadSum = 0.0;
  double utilitySum = 0.0;
  double utilitySqSum = 0.0;
  double weightSum = 0.0;
  double weightSqSum = 0.0;
  for(int i = 0; i<numGoodChildren; i++) {
    if(visits[i] < amountToPrune)
      continue;
    double desiredWeight = (double)visits[i] - amountToSubtract;
    if(desiredWeight < 0.0)
      continue;

    if(searchParams.valueWeightExponent > 0)
      desiredWeight *= pow(weightFactors[i], searchParams.valueWeightExponent);

    double weightScaling = desiredWeight / weightSums[i];

    winValueSum += desiredWeight * winValues[i];
    noResultValueSum += desiredWeight * noResultValues[i];
    scoreMeanSum += desiredWeight * scoreMeans[i];
    scoreMeanSqSum += desiredWeight * scoreMeanSqs[i];
    leadSum += desiredWeight * leads[i];
    utilitySum += weightScaling * utilitySums[i];
    utilitySqSum += weightScaling * utilitySqSums[i];
    weightSum += desiredWeight;
    weightSqSum += weightScaling * weightScaling * weightSqSums[i];
  }

  //Also add in the direct evaluation of this node.
  {
    //Since we've scaled all the child weights in some arbitrary way, adjust and make sure
    //that the direct evaluation of the node still has precisely 1/N weight.
    //Do some things to carefully avoid divide by 0.
    double desiredWeight = (totalChildVisits > 0) ? weightSum / totalChildVisits : weightSum;
    if(desiredWeight < 0.0001) //Just in case
      desiredWeight = 0.0001;

    desiredWeight *= searchParams.parentValueWeightFactor;

    double winProb = (double)node.nnOutput->whiteWinProb;
    double noResultProb = (double)node.nnOutput->whiteNoResultProb;
    double scoreMean = (double)node.nnOutput->whiteScoreMean;
    double scoreMeanSq = (double)node.nnOutput->whiteScoreMeanSq;
    double lead = (double)node.nnOutput->whiteLead;
    double utility =
      getResultUtility(winProb, noResultProb)
      + getScoreUtility(scoreMean, scoreMeanSq, 1.0);

    winValueSum += winProb * desiredWeight;
    noResultValueSum += noResultProb * desiredWeight;
    scoreMeanSum += scoreMean * desiredWeight;
    scoreMeanSqSum += scoreMeanSq * desiredWeight;
    leadSum += lead * desiredWeight;
    utilitySum += utility * desiredWeight;
    utilitySqSum += utility * utility * desiredWeight;
    weightSum += desiredWeight;
    weightSqSum += desiredWeight * desiredWeight;
  }

  while(node.statsLock.test_and_set(std::memory_order_acquire));
  node.stats.visits += numVisitsToAdd;
  //It's possible that these values are a bit wrong if there's a race and two threads each try to update this
  //each of them only having some of the latest updates for all the children. We just accept this and let the
  //error persist, it will get fixed the next time a visit comes through here and the values will at least
  //be consistent with each other within this node, since statsLock at least ensures these three are set atomically.
  node.stats.winValueSum = winValueSum;
  node.stats.noResultValueSum = noResultValueSum;
  node.stats.scoreMeanSum = scoreMeanSum;
  node.stats.scoreMeanSqSum = scoreMeanSqSum;
  node.stats.leadSum = leadSum;
  node.stats.utilitySum = utilitySum;
  node.stats.utilitySqSum = utilitySqSum;
  node.stats.weightSum = weightSum;
  node.stats.weightSqSum = weightSqSum;
  node.virtualLosses -= virtualLossesToSubtract;
  node.statsLock.clear(std::memory_order_release);
}

void Search::runSinglePlayout(SearchThread& thread, double upperBoundVisitsLeft) {
  //Store this value, used for futile-visit pruning this thread's root children selections.
  thread.upperBoundVisitsLeft = upperBoundVisitsLeft;

  bool posesWithChildBuf[NNPos::MAX_NN_POLICY_SIZE];
  playoutDescend(thread,*rootNode,posesWithChildBuf,true,0);

  //Restore thread state back to the root state
  thread.pla = rootPla;
  thread.board = rootBoard;
  thread.history = rootHistory;
}

void Search::addLeafValue(SearchNode& node, double winValue, double noResultValue, double scoreMean, double scoreMeanSq, double lead, int32_t virtualLossesToSubtract, bool isTerminal) {
  double utility =
    getResultUtility(winValue, noResultValue)
    + getScoreUtility(scoreMean, scoreMeanSq, 1.0);

  while(node.statsLock.test_and_set(std::memory_order_acquire));
  node.stats.visits += 1;
  node.stats.winValueSum += winValue;
  node.stats.noResultValueSum += noResultValue;
  node.stats.scoreMeanSum += scoreMean;
  node.stats.scoreMeanSqSum += scoreMeanSq;
  node.stats.leadSum += lead;
  node.stats.utilitySum += utility;
  node.stats.utilitySqSum += utility * utility;
  node.stats.weightSum += 1.0;
  node.stats.weightSqSum += 1.0;
  node.virtualLosses -= virtualLossesToSubtract;
  node.statsLock.clear(std::memory_order_release);
}

//Assumes node is locked
//Assumes node already has an nnOutput
void Search::maybeRecomputeExistingNNOutput(
  SearchThread& thread, SearchNode& node, bool isRoot
) {
  //Right now only the root node currently ever needs to recompute once in a search (low prion)
  if(isRoot && node.nnOutputAge != searchNodeAge) {
    node.nnOutputAge = searchNodeAge;
    initNodeNNOutput(thread,node,isRoot,false,0,true);  
  }
}

//Assumes node is locked
void Search::initNodeNNOutput(
  SearchThread& thread, SearchNode& node,
  bool isRoot, bool skipCache, int32_t virtualLossesToSubtract, bool isReInit
) {
  bool includeOwnerMap = isRoot || alwaysIncludeOwnerMap;
  MiscNNInputParams nnInputParams;
  nnInputParams.drawEquivalentWinsForWhite = searchParams.drawEquivalentWinsForWhite;
  nnInputParams.conservativePass = searchParams.conservativePass;
  nnInputParams.nnPolicyTemperature = searchParams.nnPolicyTemperature;
  nnInputParams.avoidMYTDaggerHack = searchParams.avoidMYTDaggerHackPla == thread.pla;
  nnInputParams.policyOptimism = isRoot ? searchParams.rootPolicyOptimism : searchParams.policyOptimism;

  Hash128 nnHash;
  if (nnEvaluator->isInCacheTable(thread.board, thread.history, thread.pla,
	  nnInputParams,
	  thread.nnResultBuf, nnHash) == false)
  {   
	  if (true)
	  {
		  if (thread.surewinSearch(node) == true)
		  {
        if (isRoot)
        {
            nnEvaluator->evaluate(
            thread.board, thread.history, thread.pla,
            nnInputParams,
            thread.nnResultBuf, includeOwnerMap
          );
          node.nnOutput = std::move(thread.nnResultBuf.result);
        }
			  double winValue = ScoreValue::whiteWinsOfWinner(thread.pla);
			  addLeafValue(node, winValue, 0.0, 0.0, 0.0, 0.0, virtualLossesToSubtract, true);
			  node.stats.sureWinStatus = 1;
			  node.stats.sureWinLoc = Location::getLoc(thread.surewin_winner_move % BOARDS, thread.surewin_winner_move / BOARDS, BOARDS);
			  return;
		  }
		  node.stats.sureWinStatus = 0;
	  }
  }
  if(isRoot && searchParams.rootNumSymmetriesToSample > 1) {
    vector<shared_ptr<NNOutput>> ptrs;
    std::array<int, NNInputs::NUM_SYMMETRY_COMBINATIONS> symmetryIndexes;
    std::iota(symmetryIndexes.begin(), symmetryIndexes.end(), 0);
    for(int i = 0; i<searchParams.rootNumSymmetriesToSample; i++) {
      std::swap(symmetryIndexes[i], symmetryIndexes[thread.rand.nextInt(i,NNInputs::NUM_SYMMETRY_COMBINATIONS-1)]);
      nnInputParams.symmetry = symmetryIndexes[i];
      bool skipCacheThisIteration = true; //Skip cache since there's no guarantee which symmetry is in the cache
      nnEvaluator->evaluate(
        thread.board, thread.history, thread.pla,
        nnInputParams,
        thread.nnResultBuf, includeOwnerMap
      );
      ptrs.push_back(std::move(thread.nnResultBuf.result));
    }
    node.nnOutput = std::shared_ptr<NNOutput>(new NNOutput(ptrs));
  }
  else {
    nnEvaluator->evaluate(
      thread.board, thread.history, thread.pla,
      nnInputParams,
      thread.nnResultBuf, includeOwnerMap
    );
    node.nnOutput = std::move(thread.nnResultBuf.result);
  }

  assert(node.nnOutput->noisedPolicyProbs == NULL);
  maybeAddPolicyNoiseAndTempAlreadyLocked(thread,node,isRoot);
  node.nnOutputAge = searchNodeAge;

  //If this is a re-initialization of the nnOutput, we don't want to add any visits or anything.
  //Also don't bother updating any of the stats. Technically we should do so because winValueSum
  //and such will have changed potentially due to a new orientation of the neural net eval
  //slightly affecting the evals, but this is annoying to recompute from scratch, and on the next
  //visit updateStatsAfterPlayout should fix it all up anyways.
  if(isReInit)
    return;
  if(thread.history.isGameFinished == false)
  {
    addCurentNNOutputAsLeafValue(node,virtualLossesToSubtract);
  }
  else
  {
      double winValue = ScoreValue::whiteWinsOfWinner(thread.history.winner);
      double noResultValue = 0.0;
      double scoreMean = 0.0;
      //double scoreMean = ScoreValue::whiteScoreDrawAdjust(thread.history.finalWhiteMinusBlackScore,searchParams.drawEquivalentWinsForWhite,thread.history);
      double scoreMeanSq = 0.0;
      //double scoreMeanSq = ScoreValue::whiteScoreMeanSqOfScoreGridded(thread.history.finalWhiteMinusBlackScore,searchParams.drawEquivalentWinsForWhite);
      double lead = scoreMean;
      addLeafValue(node, winValue, noResultValue, scoreMean, scoreMeanSq, lead, virtualLossesToSubtract,true);    
  }
}

void Search::addCurentNNOutputAsLeafValue(SearchNode& node, int32_t virtualLossesToSubtract) {
  //Values in the search are from the perspective of white positive always
  double winProb = (double)node.nnOutput->whiteWinProb;
  double noResultProb = (double)node.nnOutput->whiteNoResultProb;
  double scoreMean = (double)node.nnOutput->whiteScoreMean;
  double scoreMeanSq = (double)node.nnOutput->whiteScoreMeanSq;
  double lead = (double)node.nnOutput->whiteLead;
  addLeafValue(node,winProb,noResultProb,scoreMean,scoreMeanSq,lead,virtualLossesToSubtract,false);
}

void Search::playoutDescend(
  SearchThread& thread, SearchNode& node,
  bool posesWithChildBuf[NNPos::MAX_NN_POLICY_SIZE],
  bool isRoot, int32_t virtualLossesToSubtract
) {
  if(thread.history.isGameFinished)
  {
    if(thread.history.isNoResult) {
      double winValue = 0.0;
      double noResultValue = 1.0;
      double scoreMean = 0.0;
      double scoreMeanSq = 0.0;
      double lead = 0.0;
      if (!isRoot)
      {
        addLeafValue(node, winValue, noResultValue, scoreMean, scoreMeanSq, lead, virtualLossesToSubtract,true);
      }
      else
      {
        initNodeNNOutput(thread,node,isRoot,false,virtualLossesToSubtract,false);
      }
      return;
    }
    else {
      double winValue = ScoreValue::whiteWinsOfWinner(thread.history.winner);
      double noResultValue = 0.0;
      double scoreMean = 0.0;
      //double scoreMean = ScoreValue::whiteScoreDrawAdjust(thread.history.finalWhiteMinusBlackScore,searchParams.drawEquivalentWinsForWhite,thread.history);
      double scoreMeanSq = 0.0;
      //double scoreMeanSq = ScoreValue::whiteScoreMeanSqOfScoreGridded(thread.history.finalWhiteMinusBlackScore,searchParams.drawEquivalentWinsForWhite);
      double lead = scoreMean;
      if (!isRoot)
      {
        addLeafValue(node, winValue, noResultValue, scoreMean, scoreMeanSq, lead, virtualLossesToSubtract,true);
      }
      else
      {
        //initNodeNNOutput(thread,node,isRoot,false,virtualLossesToSubtract,false);
      }
      return;
    }
  }
  if(node.stats.sureWinStatus == 1)
  {
    double winValue = ScoreValue::whiteWinsOfWinner(thread.pla);
    addLeafValue(node, winValue, 0.0, 0.0, 0.0, 0.0, virtualLossesToSubtract,true);
    return;
  }

  std::mutex& mutex = mutexPool->getMutex(node.lockIdx);
  unique_lock<std::mutex> lock(mutex);

  //Hit leaf node, finish
  if(node.nnOutput == nullptr) {
    initNodeNNOutput(thread,node,isRoot,false,virtualLossesToSubtract,false);
    return;
  }

  maybeRecomputeExistingNNOutput(thread,node,isRoot);

  //Not leaf node, so recurse

  //Find the best child to descend down
  int bestChildIdx;
  Loc bestChildMoveLoc;
  selectBestChildToDescend(thread,node,bestChildIdx,bestChildMoveLoc,posesWithChildBuf,isRoot);

  //The absurdly rare case that the move chosen is not legal
  //(this should only happen either on a bug or where the nnHash doesn't have full legality information or when there's an actual hash collision).
  //Regenerate the neural net call and continue
  if(bestChildIdx >= 0 && !thread.history.isLegal(thread.board,bestChildMoveLoc,thread.pla)) {
    bool isReInit = true;
    initNodeNNOutput(thread,node,isRoot,true,0,isReInit);

    if(thread.logStream != NULL)
      (*thread.logStream) << "WARNING: Chosen move not legal so regenerated nn output, nnhash=" << node.nnOutput->nnHash << endl;

    //As isReInit is true, we don't return, just keep going, since we didn't count this as a true visit in the node stats
    selectBestChildToDescend(thread,node,bestChildIdx,bestChildMoveLoc,posesWithChildBuf,isRoot);
    if(bestChildIdx >= 0) {
      //We should absolutely be legal this time
      assert(thread.history.isLegal(thread.board,bestChildMoveLoc,thread.pla));
    }
  }

  if(bestChildIdx <= -1) {
    //This might happen if all moves have been forbidden. The node will just get stuck at 1 visit forever then
    //and we won't do any search.
    lock.unlock();
    addCurentNNOutputAsLeafValue(node,virtualLossesToSubtract);
    return;
  }

  //Reallocate the children array to increase capacity if necessary
  if(bestChildIdx >= node.childrenCapacity) {
    int newCapacity = node.childrenCapacity + (node.childrenCapacity / 4) + 1;
    assert(newCapacity < 0x3FFF);
    SearchNode** newArr = new SearchNode*[newCapacity];
    for(int i = 0; i<node.numChildren; i++) {
      newArr[i] = node.children[i];
      node.children[i] = NULL;
    }
    SearchNode** oldArr = node.children;
    node.children = newArr;
    node.childrenCapacity = (uint16_t)newCapacity;
    delete[] oldArr;
  }

  Loc moveLoc = bestChildMoveLoc;

  //Allocate a new child node if necessary
  SearchNode* child;
  if(bestChildIdx == node.numChildren) {
    node.numChildren++;
    child = new SearchNode(*this,thread.pla,thread.rand,moveLoc,&node);
    node.children[bestChildIdx] = child;
  }
  else {
    child = node.children[bestChildIdx];
  }

  while(child->statsLock.test_and_set(std::memory_order_acquire));
  child->virtualLosses += searchParams.numVirtualLossesPerThread;
  child->statsLock.clear(std::memory_order_release);

  //Unlock before making moves if the child already exists since we don't depend on it at this point
  lock.unlock();

  thread.history.makeBoardMoveAssumeLegal(thread.board,moveLoc,thread.pla);
  thread.pla = getOpp(thread.pla);

  //Recurse!
  playoutDescend(thread,*child,posesWithChildBuf,false,searchParams.numVirtualLossesPerThread);

  //Update this node stats
  updateStatsAfterPlayout(node,thread,virtualLossesToSubtract,isRoot);
}



















int SearchThread::surewin_tt_setsize (int size) 
{ 
	int i;
	
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
	
	surewin_tt_size = size;
	surewin_tt = (surewin_tt_entry *) malloc((surewin_tt_size + 1) * sizeof(surewin_tt_entry));
	//printf("tt_size:%d\n", tt_size);
	//printf("size stt_entry:%ld\n", sizeof(stt_entry));
	return 0;
}

int SearchThread::surewin_attack (Position * p, int8_t depth_left, uint8_t ply, table attackline, bool easy)
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
	if (surewin_tt_probe(p, &tt_move, &tt_depth, &easy_tt) == 0)
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
				surewin_tt_save(p, 0, sq, 0, easy);
				return 1;		
			}
			if (surewin_defense(p, depth_left - PLY_SUREWIN - (pick.stage_curr == INTERESTING_NOT_ATTACKLINE_OK_ATT || pick.stage_curr == NOT_INTERESTING_NOT_ATTACKLINE_OK_ATT) * ATTACKLINE_PENALITY - (pick.stage_curr == NOT_INTERESTING_ATTACKLINE_OK_ATT || pick.stage_curr == NOT_INTERESTING_NOT_ATTACKLINE_OK_ATT) * INTERESTING_PENALITY, ply + 1, new_attackline, easy) != 1)
			{
				if (ply == 0)
				{
					surewin_winner_move = sq;
				}
				p -> undo_move();
				surewin_tt_save(p, 0, sq, 0, easy);
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

int SearchThread::surewin_defense (Position * p, int8_t depth_left, uint8_t ply, table attackline, bool easy)
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
	if (surewin_tt_probe(p, &tt_move, &tt_depth, &easy_tt) == 1)
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
				surewin_tt_save(p, 1, sq, 100, easy);	
				return 1;		
			}
			if (surewin_attack(p, depth_left - PLY_SUREWIN  + ((pick.stage_curr == REST_FT_DEF) * 2), ply + 1, attackline, easy) != 1)
			{
				p -> undo_move();
				surewin_tt_save(p, 1, sq, depth_left, easy);
				return 1;
			}
			p -> undo_move();		
			loop.t[sq >> 6] ^= (1ULL << (sq - ((sq >> 6) << 6)));
		}
	}
	return 0;		
}

int16_t SearchThread::surewin_tt_probe(Position * p, uint16_t * best, uint8_t * depth, bool * easy) 
{
	struct surewin_tt_entry * phashe;
	phashe = &surewin_tt[((p->st)->key) & surewin_tt_size];
 
	if (phashe->hash == (p->st)->key) 
	{ 
		if ((phashe->square[0] != p-> square[0]) || (phashe->square[1] != p-> square[1]) || (p -> turn_glob != phashe -> turn))
		{
			std::cerr << "TT keey coll\n";
		}
		*best = phashe->bestmove;
		*easy = phashe->easy;
		*depth = phashe->depth;
		return phashe->val;
	}
	return 32001;
}

void SearchThread::surewin_tt_save(Position * p, int16_t val, uint16_t best, uint8_t depth, bool easy)
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

void SearchThread::surewin_tt_kill()
{
	memset(surewin_tt, 0, (surewin_tt_size+1)*sizeof(struct surewin_tt_entry));
}

void SearchThread::initSurewinRoot(Board& board, int pla)
{
  root.square[0].t[0] = board.threatHandler.square[0].t[0];
  root.square[0].t[1] = board.threatHandler.square[0].t[1];
  root.square[0].t[2] = board.threatHandler.square[0].t[2];
  root.square[0].t[3] = board.threatHandler.square[0].t[3];
  root.square[0].t[4] = board.threatHandler.square[0].t[4];
  root.square[0].t[5] = board.threatHandler.square[0].t[5];
  root.square[0].t[6] = board.threatHandler.square[0].t[6];
  root.square[0].t[7] = board.threatHandler.square[0].t[7];
  root.square[1].t[0] = board.threatHandler.square[1].t[0];
  root.square[1].t[1] = board.threatHandler.square[1].t[1];
  root.square[1].t[2] = board.threatHandler.square[1].t[2];
  root.square[1].t[3] = board.threatHandler.square[1].t[3];
  root.square[1].t[4] = board.threatHandler.square[1].t[4];
  root.square[1].t[5] = board.threatHandler.square[1].t[5];
  root.square[1].t[6] = board.threatHandler.square[1].t[6];
  root.square[1].t[7] = board.threatHandler.square[1].t[7];
  for (int i = 0; i < 2; i++)
  {
  	for (int j = 0; j < 4; j++)
  	{
  		for (int k = 0; k < BOARDS + BOARDS - 1; k++)
  		{
  			root.linear_bit[i][j][k] = board.threatHandler.linear_bit[i][j][k]; 
  		}
  	}
  }

  root.turn_glob = pla - 1;
  root.five_threat = board.threatHandler.five_threat;
  root.st->key = root.compute_key();
  root.st->previous = NULL;
  root.st->move = END_OBJECT;
  root.st->five_threat = board.threatHandler.five_threat;

  for (int i = 0; i < 2; i++)
  {
  	for (int j = 0; j < 3; j++)
  	{
  		for (int k = 0; k < 4; k++)
  		{
  			root.st->threat[i][j][k].t[0] = board.threatHandler.threat[i][j][k].t[0]; 
  			root.st->threat[i][j][k].t[1] = board.threatHandler.threat[i][j][k].t[1]; 
  			root.st->threat[i][j][k].t[2] = board.threatHandler.threat[i][j][k].t[2]; 
  			root.st->threat[i][j][k].t[3] = board.threatHandler.threat[i][j][k].t[3]; 
  			root.st->threat[i][j][k].t[4] = board.threatHandler.threat[i][j][k].t[4]; 
  			root.st->threat[i][j][k].t[5] = board.threatHandler.threat[i][j][k].t[5]; 
  			root.st->threat[i][j][k].t[6] = board.threatHandler.threat[i][j][k].t[6]; 
  			root.st->threat[i][j][k].t[7] = board.threatHandler.threat[i][j][k].t[7];  
  		}
  	}
  }

  root.generate_threat_unio();
  root.generate_threat_unio(root.turn_glob ^ 1); 
  root.nodes = 0;
}

bool SearchThread::surewinSearch(SearchNode& node)
{
  initSurewinRoot(board, pla);
  root.surewin_search++;
  table t;
  t.null();
  t = (~t);
  for (int i = 1 * PLY_SUREWIN; true; i += (2 * PLY_SUREWIN))
  {
    if (i >= 21)
    {
      return false;
    }
    if (surewin_attack(&root, i, 0, t, false) == 1)
    {
      root.surewin_win++;
      return true;
	}
  }
}






