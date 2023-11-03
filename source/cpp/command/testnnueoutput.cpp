#include "../command/commandline.h"
#include "../core/config_parser.h"
#include "../core/datetime.h"
#include "../core/global.h"
#include "../core/makedir.h"
#include "../core/timer.h"
#include "../dataio/sgf.h"
#include "../main.h"
#include "../program/play.h"
#include "../program/playutils.h"
#include "../program/setup.h"
#include "../search/asyncbot.h"

using namespace std;

namespace nnueoutputtest
{
constexpr float evalNormalizer = 128.0f;           // DEFINED IN BARBAKAN PROJECT constants.py (nn_scale)
constexpr float moveCandidateNormalizer = 127.0f;  // DEFINED IN BARBAKAN PROJECT constants.py (ft_scale)
constexpr size_t trainingRowSize = 512;            // DEFINED in BARBAKAN PROJECT loaders
constexpr size_t D4Size = 8;
constexpr float trainRatio = 0.95f;

struct NNUEOutputEngine
{
  NNUEOutputEngine(const NNUEOutputEngine&) = delete;
  NNUEOutputEngine& operator=(const NNUEOutputEngine&) = delete;

  const string nnModelFile;
  const bool assumeMultipleStartingBlackMovesAreHandicap;
  const int analysisPVLen;
  const bool preventEncore;

  const double dynamicPlayoutDoublingAdvantageCapPerOppLead;
  double staticPlayoutDoublingAdvantage;
  bool staticPDATakesPrecedence;
  double genmoveWideRootNoise;
  double analysisWideRootNoise;
  bool genmoveAntiMirror;
  bool analysisAntiMirror;

  NNEvaluator* nnEval;
  AsyncBot* bot;
  Rules currentRules;  // Should always be the same as the rules in bot, if bot is not NULL.

  // Stores the params we want to be using during genmoves or analysis
  SearchParams params;

  TimeControls bTimeControls;
  TimeControls wTimeControls;

  // This move history doesn't get cleared upon consecutive moves by the same side, and is used
  // for undo, whereas the one in search does.
  Board initialBoard;
  Player initialPla;
  vector<Move> moveHistory;

  vector<double> recentWinLossValues;
  double lastSearchFactor;
  double desiredDynamicPDAForWhite;
  bool avoidMYTDaggerHack;

  Player perspective;

  double genmoveTimeSum;

  NNUEOutputEngine(const string& modelFile, SearchParams initialParams, Rules initialRules,
                   bool assumeMultiBlackHandicap, bool prevtEncore, double dynamicPDACapPerOppLead, double staticPDA,
                   bool staticPDAPrecedence, bool avoidDagger, double genmoveWRN, double analysisWRN,
                   bool genmoveAntiMir, bool analysisAntiMir, Player persp, int pvLen)
      : nnModelFile(modelFile),
        assumeMultipleStartingBlackMovesAreHandicap(assumeMultiBlackHandicap),
        analysisPVLen(pvLen),
        preventEncore(prevtEncore),
        dynamicPlayoutDoublingAdvantageCapPerOppLead(dynamicPDACapPerOppLead),
        staticPlayoutDoublingAdvantage(staticPDA),
        staticPDATakesPrecedence(staticPDAPrecedence),
        genmoveWideRootNoise(genmoveWRN),
        analysisWideRootNoise(analysisWRN),
        genmoveAntiMirror(genmoveAntiMir),
        analysisAntiMirror(analysisAntiMir),
        nnEval(NULL),
        bot(NULL),
        currentRules(initialRules),
        params(initialParams),
        bTimeControls(),
        wTimeControls(),
        initialBoard(),
        initialPla(P_BLACK),
        moveHistory(),
        recentWinLossValues(),
        lastSearchFactor(1.0),
        desiredDynamicPDAForWhite(0.0),
        avoidMYTDaggerHack(avoidDagger),
        perspective(persp),
        genmoveTimeSum(0.0)
  {}

  ~NNUEOutputEngine()
  {
    stopAndWait();
    delete bot;
    delete nnEval;
  }

  void stopAndWait() { bot->stopAndWait(); }

  Rules getCurrentRules() { return currentRules; }

  // Specify -1 for the sizes for a default
  void setOrResetBoardSize(ConfigParser& cfg, Logger& logger, Rand& seedRand, int boardXSize, int boardYSize)
  {
    if (nnEval != NULL && boardXSize == nnEval->getNNXLen() && boardYSize == nnEval->getNNYLen())
      return;
    if (nnEval != NULL) {
      assert(bot != NULL);
      bot->stopAndWait();
      delete bot;
      delete nnEval;
      bot = NULL;
      nnEval = NULL;
      logger.write("Cleaned up old neural net and bot");
    }

    bool wasDefault = false;
    if (boardXSize == -1 || boardYSize == -1) {
      boardXSize = NNPos::MAX_BOARD_LEN;
      boardYSize = NNPos::MAX_BOARD_LEN;
      wasDefault = true;
    }

    int maxConcurrentEvals = params.numThreads * 2 + 16;  // * 2 + 16 just to give plenty of headroom
    int expectedConcurrentEvals = params.numThreads;
    int defaultMaxBatchSize = std::max(8, ((params.numThreads + 3) / 4) * 4);
    string expectedSha256 = "";
    nnEval = Setup::initializeNNEvaluator(nnModelFile, nnModelFile, expectedSha256, cfg, logger, seedRand,
                                          maxConcurrentEvals, expectedConcurrentEvals, boardXSize, boardYSize,
                                          defaultMaxBatchSize, Setup::SETUP_FOR_GTP);
    logger.write("Loaded neural net with nnXLen " + Global::intToString(nnEval->getNNXLen()) + " nnYLen " +
                 Global::intToString(nnEval->getNNYLen()));

    {
      bool rulesWereSupported;
      nnEval->getSupportedRules(currentRules, rulesWereSupported);
      if (!rulesWereSupported) {
        throw StringError("Rules " + currentRules.toJsonStringNoKomi() + " from config file " + cfg.getFileName() +
                          " are NOT supported by neural net");
      }
    }

    // On default setup, also override board size to whatever the neural net was initialized with
    // So that if the net was initalized smaller, we don't fail with a big board
    if (wasDefault) {
      boardXSize = nnEval->getNNXLen();
      boardYSize = nnEval->getNNYLen();
    }

    string searchRandSeed;
    if (cfg.contains("searchRandSeed"))
      searchRandSeed = cfg.getString("searchRandSeed");
    else
      searchRandSeed = Global::uint64ToString(seedRand.nextUInt64());

    bot = new AsyncBot(params, nnEval, &logger, searchRandSeed);

    Board board(boardXSize, boardYSize);
    Player pla = P_BLACK;
    BoardHistory hist(board, pla, currentRules);
    vector<Move> newMoveHistory;
    setPositionAndRules(pla, board, hist, board, pla, newMoveHistory);
  }

  void setPositionAndRules(Player pla, const Board& board, const BoardHistory& h, const Board& newInitialBoard,
                           Player newInitialPla, const vector<Move> newMoveHistory)
  {
    BoardHistory hist(h);
    hist.isGameFinished = true;  // This will turn off last move nn input
    currentRules = hist.rules;
    bot->setPosition(pla, board, hist);
    initialBoard = newInitialBoard;
    initialPla = newInitialPla;
    moveHistory = newMoveHistory;
    recentWinLossValues.clear();
  }

  void clearBoard()
  {
    // assert(bot->getRootHist().rules == currentRules); Komi can be 0 at the left side. Once rules do not have komi
    // this can be turned on
    int newXSize = bot->getRootBoard().x_size;
    int newYSize = bot->getRootBoard().y_size;
    Board board(newXSize, newYSize);
    Player pla = P_BLACK;
    BoardHistory hist(board, pla, currentRules);
    vector<Move> newMoveHistory;
    setPositionAndRules(pla, board, hist, board, pla, newMoveHistory);
  }

  bool setPosition(const vector<int>& blackStones, const vector<int>& whiteStones, int posLen)
  {
    Board board(posLen, posLen);
    bool isSanePosition = board.setStartPosition(blackStones, whiteStones, posLen);
    Player pla = ((blackStones.size() + whiteStones.size()) % 2 == 0) ? P_BLACK : P_WHITE;
    BoardHistory hist(board, pla, currentRules);
    hist.setInitialTurnNumber(board.numStonesOnBoard());  // Heuristic to guess at what turn this is
    vector<Move> newMoveHistory;
    setPositionAndRules(pla, board, hist, board, pla, newMoveHistory);
    return isSanePosition;
  }

  pair<float, vector<float>> getNNUETargets()
  {
    Board board = bot->getRootBoard();
    BoardHistory hist = bot->getRootHist();
    Player nextPla = bot->getRootPla();

    MiscNNInputParams nnInputParams;
    nnInputParams.playoutDoublingAdvantage =
        (params.playoutDoublingAdvantagePla == C_EMPTY || params.playoutDoublingAdvantagePla == nextPla)
            ? staticPlayoutDoublingAdvantage
            : -staticPlayoutDoublingAdvantage;
    nnInputParams.symmetry = 0;
    nnInputParams.policyOptimism = 0;
    NNResultBuf buf;
    bool includeOwnerMap = true;
    nnEval->evaluate(board, hist, nextPla, nnInputParams, buf, includeOwnerMap);
    NNOutput* nnOutput = buf.result.get();
    if (0.0001 <= nnOutput->whiteNoResultProb) {
      ASSERT_UNREACHABLE;
    }
    float rawPlayerWinProb = (nextPla == P_BLACK) ? nnOutput->whiteLossProb : nnOutput->whiteWinProb;
    float playerWinProb = min(1.0f, max(rawPlayerWinProb, 0.0f));
    vector<float> posProbs;
    for (int y = 0; y < board.y_size; y++) {
      for (int x = 0; x < board.x_size; x++) {
        int pos = NNPos::xyToPos(x, y, nnOutput->nnXLen);
        float prob = nnOutput->policyProbs[pos];
        prob = min(1.0f, max(prob, 0.0f));
        posProbs.push_back(prob);
      }
    }
    return {playerWinProb, posProbs};
  }

  string rawNN()
  {
    auto targets = getNNUETargets();

    if (nnEval == NULL)
      return "";
    ostringstream out;
    Board::printBoard(out, bot->getRootBoard(), Loc(-1), NULL);
    out << "playerWin=" << Global::strprintf("%.6f", targets.first) << endl;
    out << "policy" << endl;

    auto policyProbs = targets.second;
    vector<pair<int, float>> posProbs;
    for (int idx = 0; idx < policyProbs.size(); ++idx) {
      posProbs.push_back({idx, policyProbs[idx]});
    }
    std::sort(posProbs.begin(), posProbs.end(), [](auto& left, auto& right) { return left.second > right.second; });
    size_t idx = 0;
    for (const auto& posProb : posProbs) {
      out << "pos=" << posProb.first << " prob=" << Global::strprintf("%.6f", posProb.second) << endl;
      ++idx;
      if (idx == 10) {
        break;
      }
    }
    return Global::trim(out.str());
  }

  std::vector<uint8_t> getPackedPos(std::vector<char> pAsVec)
  {
    // This logic is only tested for board size 20. For less even board sizes pack cycle might fail, but test should
    // indicate this possible error.
    std::vector<uint8_t> ret;
    int gap = pAsVec.size() % 8;
    for (int i = 0; i < gap; i++) {
      pAsVec.push_back(0);
    }
    for (size_t i = 0; i < pAsVec.size() / 8; ++i) {
      uint8_t packData = 0;
      for (int j = 0; j < 8; ++j) {
        if (pAsVec[i * 8 + j]) {
          packData += 1 << j;
        }
      }
      ret.push_back(packData);
    }
    return ret;
  }

  std::vector<uint8_t> getTrainingRow(std::vector<uint8_t> packedPos, std::vector<uint8_t> target)
  {
    auto ret = packedPos;
    ret.insert(ret.end(), target.begin(), target.end());
    size_t gapSize = trainingRowSize - ret.size();
    std::vector<uint8_t> gap(gapSize, 0);
    ret.insert(ret.end(), gap.begin(), gap.end());
    return ret;
  }

  pair<pair<float, vector<float>>, std::vector<char>> actF(const pair<float, vector<float>>& targets,
                                                           const vector<char>& buf, int posLen)
  {
    vector<float> targetsRet(targets.second.size());
    vector<char> bufRet(buf.size());

    for (int i = 0; i < posLen * posLen; ++i) {
      int x = i / posLen;
      int y = i % posLen;
      int xNew = y;
      int yNew = posLen - 1 - x;
      int iNew = xNew * posLen + yNew;
      targetsRet[iNew] = targets.second[i];
      bufRet[iNew] = buf[i];
      bufRet[iNew + posLen * posLen] = buf[i + posLen * posLen];
    }
    return {{targets.first, targetsRet}, bufRet};
  }

  pair<pair<float, vector<float>>, std::vector<char>> actT(const pair<float, vector<float>>& targets,
                                                           const vector<char>& buf, int posLen)
  {
    vector<float> targetsRet(targets.second.size());
    vector<char> bufRet(buf.size());

    for (int i = 0; i < posLen * posLen; ++i) {
      int x = i / posLen;
      int y = i % posLen;
      int xNew = x;
      int yNew = posLen - 1 - y;
      int iNew = xNew * posLen + yNew;
      targetsRet[iNew] = targets.second[i];
      bufRet[iNew] = buf[i];
      bufRet[iNew + posLen * posLen] = buf[i + posLen * posLen];
    }
    return {{targets.first, targetsRet}, bufRet};
  }

  pair<pair<float, vector<float>>, vector<char>> actD4(const pair<float, vector<float>>& targets,
                                                       const vector<char>& buf, size_t symm, int posLen)
  {
    auto t = symm / 4;
    auto f = symm % 4;
    pair<float, vector<float>> currTargets = targets;
    std::vector<char> currBuf = buf;
    for (int i = 0; i < f; ++i) {
      auto curr = actF(currTargets, currBuf, posLen);
      currTargets = curr.first;
      currBuf = curr.second;
    }
    for (int i = 0; i < t; ++i) {
      auto curr = actT(currTargets, currBuf, posLen);
      currTargets = curr.first;
      currBuf = curr.second;
    }
    return {currTargets, currBuf};
  }

  void dumpTargets(const pair<float, vector<float>>& targets, const std::vector<char>& buf, std::ofstream& evalTmp,
                   std::ofstream& moveCandidateTmp, int posLen)
  {
    for (int symm = 0; symm < D4Size; ++symm) {
      auto symmetrizedTarBuf = actD4(targets, buf, symm, posLen);
      auto packedPos = getPackedPos(symmetrizedTarBuf.second);
      std::vector<uint8_t> dumpEval;
      dumpEval.push_back(static_cast<uint8_t>(symmetrizedTarBuf.first.first * evalNormalizer + 0.5f));
      std::vector<uint8_t> dumpMoveDist;
      for (auto f : symmetrizedTarBuf.first.second) {
        dumpMoveDist.push_back(static_cast<uint8_t>(f * moveCandidateNormalizer + 0.5f));
      }
      auto evalTrainingRow = getTrainingRow(packedPos, dumpEval);
      auto moveCandidateTrainingRow = getTrainingRow(packedPos, dumpMoveDist);
      evalTmp.write(reinterpret_cast<const char*>(evalTrainingRow.data()), trainingRowSize);
      moveCandidateTmp.write(reinterpret_cast<const char*>(moveCandidateTrainingRow.data()), trainingRowSize);
    }
  }

  SearchParams getParams() { return params; }

  void setParams(SearchParams p)
  {
    params = p;
    bot->setParams(params);
  }

  static void logMessage(const std::string& message)
  {
    auto t = std::chrono::system_clock::now();
    std::time_t t_t = std::chrono::system_clock::to_time_t(t);
    std::string timeStr(std::ctime(&t_t));
    timeStr = timeStr.substr(0, timeStr.length() - 1);
    std::cout << "[" << timeStr << "] " << message << std::endl;
  }

  static std::string getEvalTmpPath(const string& outputDir)
  {
    stringstream ss;
    ss.str("");
    ss << outputDir << "eval_tmp.bin";
    return ss.str();
  }

  static std::string getMoveCandidateTmpPath(const string& outputDir)
  {
    stringstream ss;
    ss.str("");
    ss << outputDir << "move_candidate_tmp.bin";
    return ss.str();
  }

  static void shuffleData(const string& outputDir)
  {
    stringstream ss;
    ss.str("");
    ss << "Shuffle data\n";
    NNUEOutputEngine::logMessage(ss.str());
    string evalTmpPath = getEvalTmpPath(outputDir);
    string moveCandidateTmpPath = getMoveCandidateTmpPath(outputDir);
    ss.str("");
    ss << outputDir << "eval.bin";
    string evalPath = ss.str();
    ss.str("");
    ss << outputDir << "eval_val.bin";
    string evalValPath = ss.str();
    ss.str("");
    ss << outputDir << "move_candidate.bin";
    string moveCandidatePath = ss.str();
    ss.str("");
    ss << outputDir << "move_candidate_val.bin";
    string moveCandidateValPath = ss.str();
    std::ifstream evalTmp(evalTmpPath, std::ios::binary);
    std::ifstream moveCandidateTmp(moveCandidateTmpPath, std::ios::binary);
    std::ofstream eval(evalPath, std::ios::binary);
    std::ofstream evalVal(evalValPath, std::ios::binary);
    std::ofstream moveCandidate(moveCandidatePath, std::ios::binary);
    std::ofstream moveCandidateVal(moveCandidateValPath, std::ios::binary);

    size_t allReadRows1 = 0;
    {
      ifstream in(evalTmpPath, ifstream::ate | ifstream::binary);
      size_t fileSize = in.tellg();
      if (fileSize % trainingRowSize != 0) {
        ASSERT_UNREACHABLE;
      }
      allReadRows1 = fileSize / trainingRowSize;
    }
    size_t allReadRows2 = 0;
    {
      ifstream in(moveCandidateTmpPath, ifstream::ate | ifstream::binary);
      size_t fileSize = in.tellg();
      if (fileSize % trainingRowSize != 0) {
        ASSERT_UNREACHABLE;
      }
      allReadRows2 = fileSize / trainingRowSize;
    }
    if (allReadRows1 != allReadRows2) {
      ASSERT_UNREACHABLE;
    }
    size_t allReadRows = allReadRows1;
    vector<size_t> perm;
    perm.reserve(allReadRows);
    std::srand(unsigned(std::time(0)));
    for (size_t i = 0; i < allReadRows; ++i) {
      perm.push_back(i);
    }
    ss.str("");
    ss << "shuffle choose start\n";
    NNUEOutputEngine::logMessage(ss.str());
    std::random_shuffle(perm.begin(), perm.end());
    ss.str("");
    ss << "shuffle is choosen\n";
    NNUEOutputEngine::logMessage(ss.str());
    std::vector<char> bufEval(trainingRowSize);
    std::vector<char> bufMoveCandidate(trainingRowSize);
    size_t trainingRowsDumped = 0;
    size_t validationRowsDumped = 0;
    for (size_t idx = 0; idx < perm.size(); ++idx) {
      if (idx % 10000 == 0) {
        ss.str("");
        ss << "currShuffe/allShuffle=" << idx << "/" << perm.size();
        NNUEOutputEngine::logMessage(ss.str());
      }
      size_t p = perm[idx];
      evalTmp.seekg(p * trainingRowSize, ios::beg);
      moveCandidateTmp.seekg(p * trainingRowSize, ios::beg);
      evalTmp.read(&bufEval[0], trainingRowSize);
      moveCandidateTmp.read(&bufMoveCandidate[0], trainingRowSize);
      if (idx < perm.size() * trainRatio) {
        eval.write(reinterpret_cast<const char*>(bufEval.data()), trainingRowSize);
        moveCandidate.write(reinterpret_cast<const char*>(bufMoveCandidate.data()), trainingRowSize);
        ++trainingRowsDumped;
      } else {
        evalVal.write(reinterpret_cast<const char*>(bufEval.data()), trainingRowSize);
        moveCandidateVal.write(reinterpret_cast<const char*>(bufMoveCandidate.data()), trainingRowSize);
        ++validationRowsDumped;
      }
    }
    ss.str("");
    ss << "Shuffle ready. rowsDumped=" << trainingRowsDumped + validationRowsDumped
       << " (trainingRowsDumped=" << trainingRowsDumped << " validationRowsDumped=" << validationRowsDumped << ")\n";
    nnueoutputtest::NNUEOutputEngine::logMessage(ss.str());
    ss.str("");
    ss << "Clean tmp files\n";
    nnueoutputtest::NNUEOutputEngine::logMessage(ss.str());
    evalTmp.close();
    moveCandidateTmp.close();
    std::ofstream cleanEvalTmp(evalTmpPath, std::ofstream::out | std::ofstream::trunc);
    std::ofstream cleanMoveCandidateTmp(moveCandidateTmpPath, std::ofstream::out | std::ofstream::trunc);
    cleanEvalTmp.close();
    cleanMoveCandidateTmp.close();
  }
};
}  // namespace nnueoutputtest

int MainCmds::testnnueoutput(int /*argc*/, const char* const* argv)
{
  Board::initBoardStruct();
  string outputDir(argv[1]);
  string posLenStr(argv[2]);
  int posLen = stoi(posLenStr);
  Rand seedRand;

  ConfigParser cfg;
  string nnModelFile;
  string overrideVersion;
  try {
    KataGoCommandLine cmd("Run KataGo main GTP engine for playing games or casual analysis.");
    cmd.addConfigFileArg(KataGoCommandLine::defaultGtpConfigFileName(), "gtp_example.cfg");
    cmd.addModelFileArg();
    cmd.setShortUsageArgLimit();
    cmd.addOverrideConfigArg();

    TCLAP::ValueArg<string> overrideVersionArg("", "override-version",
                                               "Force KataGo to say a certain value in response to gtp version command",
                                               false, string(), "VERSION");
    cmd.add(overrideVersionArg);
    // cmd.parse(argc, argv);
    nnModelFile = cmd.getModelFile();
    overrideVersion = overrideVersionArg.getValue();

    cmd.getConfig(cfg);
  } catch (TCLAP::ArgException& e) {
    cerr << "Error: " << e.error() << " for argument " << e.argId() << endl;
    return 1;
  }

  Logger logger;
  if (cfg.contains("logFile") && cfg.contains("logDir"))
    throw StringError("Cannot specify both logFile and logDir in config");
  else if (cfg.contains("logFile"))
    logger.addFile(cfg.getString("logFile"));
  else if (cfg.contains("logDir")) {
    stringstream ss;
    ss.str("");
    ss << outputDir << "gtp_logs";
    MakeDir::make(ss.str());
    Rand rand;
    logger.addFile(ss.str() + "/" + DateTime::getCompactDateTimeString() + "-" +
                   Global::uint32ToHexString(rand.nextUInt()) + ".log");
  }
  bool loggingToStderr = false;

  const bool logTimeStamp = cfg.contains("logTimeStamp") ? cfg.getBool("logTimeStamp") : true;
  if (!logTimeStamp)
    logger.setLogTime(false);

  bool startupPrintMessageToStderr = true;
  if (cfg.contains("startupPrintMessageToStderr"))
    startupPrintMessageToStderr = cfg.getBool("startupPrintMessageToStderr");

  if (cfg.contains("logToStderr") && cfg.getBool("logToStderr")) {
    loggingToStderr = true;
    logger.setLogToStderr(true);
  }

  logger.write("GTP Engine starting...");
  logger.write(Version::getKataGoVersionForHelp());
  // Also check loggingToStderr so that we don't duplicate the message from the log file
  if (startupPrintMessageToStderr && !loggingToStderr) {
    cerr << Version::getKataGoVersionForHelp() << endl;
  }

  // Defaults to 7.5 komi, gtp will generally override this
  Rules initialRules = Setup::loadSingleRulesExceptForKomi(cfg);
  logger.write("Using " + initialRules.toStringNoKomiMaybeNice() + " rules initially, unless GTP/GUI overrides this");
  if (startupPrintMessageToStderr && !loggingToStderr) {
    cerr << "Using " + initialRules.toStringNoKomiMaybeNice() + " rules initially, unless GTP/GUI overrides this"
         << endl;
  }
  float forcedKomi = 0;
  if (cfg.contains("ignoreGTPAndForceKomi")) {
    forcedKomi = cfg.getFloat("ignoreGTPAndForceKomi", Rules::MIN_USER_KOMI, Rules::MAX_USER_KOMI);
    initialRules.komi = forcedKomi;
  }

  SearchParams initialParams = Setup::loadSingleParams(cfg, Setup::SETUP_FOR_GTP);
  logger.write("Using " + Global::intToString(initialParams.numThreads) + " CPU thread(s) for search");
  // Set a default for conservativePass that differs from matches or selfplay
  if (!cfg.contains("conservativePass") && !cfg.contains("conservativePass0"))
    initialParams.conservativePass = true;
  if (!cfg.contains("fillDameBeforePass") && !cfg.contains("fillDameBeforePass0"))
    initialParams.fillDameBeforePass = true;

  Setup::initializeSession(cfg);
  const int analysisPVLen = cfg.contains("analysisPVLen") ? cfg.getInt("analysisPVLen", 1, 1000) : 13;
  const bool assumeMultipleStartingBlackMovesAreHandicap =
      cfg.contains("assumeMultipleStartingBlackMovesAreHandicap")
          ? cfg.getBool("assumeMultipleStartingBlackMovesAreHandicap")
          : true;
  const bool preventEncore = cfg.contains("preventCleanupPhase") ? cfg.getBool("preventCleanupPhase") : true;
  const double dynamicPlayoutDoublingAdvantageCapPerOppLead =
      cfg.contains("dynamicPlayoutDoublingAdvantageCapPerOppLead")
          ? cfg.getDouble("dynamicPlayoutDoublingAdvantageCapPerOppLead", 0.0, 0.5)
          : 0.045;
  double staticPlayoutDoublingAdvantage = initialParams.playoutDoublingAdvantage;
  const bool staticPDATakesPrecedence =
      cfg.contains("playoutDoublingAdvantage") && !cfg.contains("dynamicPlayoutDoublingAdvantageCapPerOppLead");
  const bool avoidMYTDaggerHack = cfg.contains("avoidMYTDaggerHack") ? cfg.getBool("avoidMYTDaggerHack") : false;

  const int defaultBoardXSize = cfg.contains("defaultBoardXSize")  ? cfg.getInt("defaultBoardXSize", 2, Board::MAX_LEN)
                                : cfg.contains("defaultBoardSize") ? cfg.getInt("defaultBoardSize", 2, Board::MAX_LEN)
                                                                   : -1;
  const int defaultBoardYSize = cfg.contains("defaultBoardYSize")  ? cfg.getInt("defaultBoardYSize", 2, Board::MAX_LEN)
                                : cfg.contains("defaultBoardSize") ? cfg.getInt("defaultBoardSize", 2, Board::MAX_LEN)
                                                                   : -1;
  const bool forDeterministicTesting =
      cfg.contains("forDeterministicTesting") ? cfg.getBool("forDeterministicTesting") : false;

  if (forDeterministicTesting)
    seedRand.init("forDeterministicTesting");

  const double genmoveWideRootNoise = initialParams.wideRootNoise;
  const double analysisWideRootNoise =
      cfg.contains("analysisWideRootNoise") ? cfg.getDouble("analysisWideRootNoise", 0.0, 5.0) : genmoveWideRootNoise;
  const double analysisAntiMirror = initialParams.antiMirror;
  const double genmoveAntiMirror = cfg.contains("genmoveAntiMirror") ? cfg.getBool("genmoveAntiMirror")
                                   : cfg.contains("antiMirror")      ? cfg.getBool("antiMirror")
                                                                     : true;

  Player perspective = Setup::parseReportAnalysisWinrates(cfg, C_EMPTY);
  nnueoutputtest::NNUEOutputEngine* engine = new nnueoutputtest::NNUEOutputEngine(
      nnModelFile, initialParams, initialRules, assumeMultipleStartingBlackMovesAreHandicap, preventEncore,
      dynamicPlayoutDoublingAdvantageCapPerOppLead, staticPlayoutDoublingAdvantage, staticPDATakesPrecedence,
      avoidMYTDaggerHack, genmoveWideRootNoise, analysisWideRootNoise, genmoveAntiMirror, analysisAntiMirror,
      perspective, analysisPVLen);
  engine->setOrResetBoardSize(cfg, logger, seedRand, defaultBoardXSize, defaultBoardYSize);

  stringstream ss;
  ss.str("");
  ss << outputDir << "dump_positions_out";
  string positionsPath = ss.str();
  string evalTmpPath = nnueoutputtest::NNUEOutputEngine::getEvalTmpPath(outputDir);
  string moveCandidateTmpPath = nnueoutputtest::NNUEOutputEngine::getMoveCandidateTmpPath(outputDir);
  std::ifstream positions(positionsPath, std::ios::binary);
  std::ofstream evalTmp(evalTmpPath, std::ios::binary);
  std::ofstream moveCandidateTmp(moveCandidateTmpPath, std::ios::binary);
  size_t bufSize = 2 * posLen * posLen;
  std::vector<char> buf(bufSize);
  size_t allReadRows = 0;
  size_t currReadRow = 0;  // for validation set creation
  size_t rowsDumped = 0;
  while (true) {
    if (currReadRow % 10000 == 0) {
      ss.str("");
      ss << "currReadRow/allReadRows=" << currReadRow << "/" << allReadRows;
      nnueoutputtest::NNUEOutputEngine::logMessage(ss.str());
    }
    ++currReadRow;
    std::vector<int> player;
    std::vector<int> waiter;
    player.push_back(191);
    player.push_back(189);
    waiter.push_back(210);
    waiter.push_back(231);
    waiter.push_back(252);
    bool isSanePosition = true;
    if (((player.size() + waiter.size()) % 2) == 0) {
      isSanePosition = engine->setPosition(player, waiter, posLen);
    } else {
      isSanePosition = engine->setPosition(waiter, player, posLen);
    }
    if (isSanePosition == false) {
      continue;
    }
    auto targets = engine->getNNUETargets();
    engine->dumpTargets(targets, buf, evalTmp, moveCandidateTmp, posLen);
    rowsDumped += nnueoutputtest::D4Size;
  }
  ss.str("");
  ss << "Ready. rowsDumped=" << rowsDumped << "/n";
  nnueoutputtest::NNUEOutputEngine::logMessage(ss.str());
  delete engine;
  engine = NULL;
  NeuralNet::globalCleanup();
  nnueoutputtest::NNUEOutputEngine::shuffleData(outputDir);
  return 0;
}
