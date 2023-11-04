#include "../command/commandline.h"
#include "../core/config_parser.h"
#include "../core/datetime.h"
#include "../core/global.h"
#include "../core/makedir.h"
#include "../core/paths.h"
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

  NNOutput getNNOutput()
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
    NNOutput nnOutput = *(buf.result);
    return nnOutput;
  }

  void compareOutput(const NNOutput& nnOutput, int posLen)
  {
    string outputDir = barbakan_zero::getBuildTestDataFolder() + "compare_nnue_output/";
    string pyValuePath = outputDir + "nn_py_value.bin";
    string pyPolicyPath = outputDir + "nn_py_policy.bin";
    std::ifstream pyValueFile(pyValuePath, std::ios::binary);
    std::ifstream pyPolicyFile(pyPolicyPath, std::ios::binary);
    std::string pyStr;
    std::vector<double> pyValues;
    std::vector<double> pyPolicies;
    while (getline(pyValueFile, pyStr)) {
      double v = std::stod(pyStr);
      pyValues.push_back(v);
    }
    while (getline(pyPolicyFile, pyStr)) {
      double v = std::stod(pyStr);
      pyPolicies.push_back(v);
    }
    if (pyValues.size() != 3) {
      std::cerr << "pyValues size is not 3. pyValues size=" << pyValues.size() << "\n";
      exit(1);
    }
    if (pyPolicies.size() != posLen * posLen) {
      std::cerr << "pyPolicies size is not " << posLen * posLen << ". pyPolicies size=" << pyPolicies.size() << "\n";
      exit(1);
    }

    std::vector<double> cppValues;
    cppValues.push_back(nnOutput.rawWinLogit);
    cppValues.push_back(nnOutput.rawLossLogit);
    cppValues.push_back(nnOutput.rawNoResultLogit);
    std::vector<double> cppPolicies;
    for (size_t idx = 0; idx < posLen * posLen; ++idx) {
      cppPolicies.push_back(nnOutput.rawPolicyLogits[idx]);
    }

    double tolerance = 0.0001;
    for (size_t idx = 0; idx < 3; ++idx) {
      if (std::abs(cppValues[idx] - pyValues[idx]) > tolerance) {
        std::cerr << "Value diff is larger than tolerance. cppValue=" << cppValues[idx] << " pyValue=" << pyValues[idx]
                  << " idx=" << idx << "\n";
        exit(1);
      }
    }
    for (size_t idx = 0; idx < posLen * posLen; ++idx) {
      if (std::abs(cppPolicies[idx] - pyPolicies[idx]) > tolerance) {
        std::cerr << "Policy diff is larger than tolerance. cppPolicy=" << cppPolicies[idx]
                  << " pyPolicy=" << pyPolicies[idx] << " idx=" << idx << "\n";
        exit(1);
      }
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
};
}  // namespace nnueoutputtest

int MainCmds::testnnueoutput(int /*argc*/, const char* const* argv)
{
  Board::initBoardStruct();
  string outputDir = barbakan_zero::getBuildTestDataFolder() + "compare_nnue_output/";
  string posLenStr(argv[1]);
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
    nnModelFile = barbakan_zero::getBuildTestDataFolder() + "compare_nnue_output/model.bin.gz";
    std::cout << nnModelFile << "\n";
    overrideVersion = overrideVersionArg.getValue();

    std::string cfgPath = barbakan_zero::getBuildDataFolder() + "configs/gtp/default_gtp.cfg";
    cmd.getConfig(cfg, cfgPath);
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

  std::vector<int> player;
  std::vector<int> waiter;
  player.push_back(191);
  player.push_back(189);
  waiter.push_back(210);
  waiter.push_back(231);
  waiter.push_back(252);
  if (((player.size() + waiter.size()) % 2) == 0) {
    engine->setPosition(player, waiter, posLen);
  } else {
    engine->setPosition(waiter, player, posLen);
  }
  auto nnOutput = engine->getNNOutput();
  engine->compareOutput(nnOutput, posLen);
  delete engine;
  engine = NULL;
  NeuralNet::globalCleanup();
  return 0;
}
