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

struct GTPEngine
{
  GTPEngine(const GTPEngine&) = delete;
  GTPEngine& operator=(const GTPEngine&) = delete;

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

  GTPEngine(const string& modelFile, SearchParams initialParams, Rules initialRules, bool assumeMultiBlackHandicap,
            bool prevtEncore, double dynamicPDACapPerOppLead, double staticPDA, bool staticPDAPrecedence,
            bool avoidDagger, double genmoveWRN, double analysisWRN, bool genmoveAntiMir, bool analysisAntiMir,
            Player persp, int pvLen)
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

  ~GTPEngine()
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
    hist.isGameFinished = true; // This will turn off last move nn input
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
    Board::printBoard(std::cout, bot->getRootBoard(), Loc(.1), NULL);
    return isSanePosition;
  }

  string rawNN(int whichSymmetry, double policyOptimism)
  {
    if (nnEval == NULL)
      return "";
    ostringstream out;

    for (int symmetry = 0; symmetry < NNInputs::NUM_SYMMETRY_COMBINATIONS; symmetry++) {
      if (whichSymmetry == NNInputs::SYMMETRY_ALL || whichSymmetry == symmetry) {
        Board board = bot->getRootBoard();
        BoardHistory hist = bot->getRootHist();
        Player nextPla = bot->getRootPla();

        MiscNNInputParams nnInputParams;
        nnInputParams.playoutDoublingAdvantage =
            (params.playoutDoublingAdvantagePla == C_EMPTY || params.playoutDoublingAdvantagePla == nextPla)
                ? staticPlayoutDoublingAdvantage
                : -staticPlayoutDoublingAdvantage;
        nnInputParams.symmetry = symmetry;
        nnInputParams.policyOptimism = policyOptimism;
        NNResultBuf buf;
        bool includeOwnerMap = true;
        nnEval->evaluate(board, hist, nextPla, nnInputParams, buf, includeOwnerMap);

        NNOutput* nnOutput = buf.result.get();
        out << "symmetry " << symmetry << endl;
        out << "whiteWin " << Global::strprintf("%.6f", nnOutput->whiteWinProb) << endl;
        out << "whiteLoss " << Global::strprintf("%.6f", nnOutput->whiteLossProb) << endl;
        out << "noResult " << Global::strprintf("%.6f", nnOutput->whiteNoResultProb) << endl;
        out << "whiteLead " << Global::strprintf("%.3f", nnOutput->whiteLead) << endl;
        out << "whiteScoreSelfplay " << Global::strprintf("%.3f", nnOutput->whiteScoreMean) << endl;
        out << "whiteScoreSelfplaySq " << Global::strprintf("%.3f", nnOutput->whiteScoreMeanSq) << endl;
        out << "varTimeLeft " << Global::strprintf("%.3f", nnOutput->varTimeLeft) << endl;
        out << "shorttermWinlossError " << Global::strprintf("%.3f", nnOutput->shorttermWinlossError) << endl;
        out << "shorttermScoreError " << Global::strprintf("%.3f", nnOutput->shorttermScoreError) << endl;

        out << "policy" << endl;
        for (int y = 0; y < board.y_size; y++) {
          for (int x = 0; x < board.x_size; x++) {
            int pos = NNPos::xyToPos(x, y, nnOutput->nnXLen);
            float prob = nnOutput->policyProbs[pos];
            if (prob < 0)
              out << "    NAN ";
            else
              out << Global::strprintf("%8.6f ", prob);
          }
          out << endl;
        }
        out << "policyPass ";
        {
          // int pos = NNPos::locToPos(Board::PASS_LOC,board.x_size,nnOutput->nnXLen,nnOutput->nnYLen);
          // float prob = nnOutput->policyProbs[pos];
          // if(prob < 0)
          // out << "    NAN "; // Probably shouldn't ever happen for pass unles the rules change, but we handle it
          // anyways
          // else
          out << Global::strprintf("%8.6f ", 0, 0);
          out << endl;
        }

        out << "whiteOwnership" << endl;
        for (int y = 0; y < board.y_size; y++) {
          for (int x = 0; x < board.x_size; x++) {
            float whiteOwn = 0;
            out << Global::strprintf("%9.7f ", whiteOwn);
          }
          out << endl;
        }
        out << endl;
      }
    }

    return Global::trim(out.str());
  }

  SearchParams getParams() { return params; }

  void setParams(SearchParams p)
  {
    params = p;
    bot->setParams(params);
  }
};

int MainCmds::generatennuedata(int /*argc*/, const char* const* argv)
{
  Board::initBoardStruct();
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
    MakeDir::make(cfg.getString("logDir"));
    Rand rand;
    logger.addFile(cfg.getString("logDir") + "/" + DateTime::getCompactDateTimeString() + "-" +
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

  GTPEngine* engine =
      new GTPEngine(nnModelFile, initialParams, initialRules, assumeMultipleStartingBlackMovesAreHandicap,
                    preventEncore, dynamicPlayoutDoublingAdvantageCapPerOppLead, staticPlayoutDoublingAdvantage,
                    staticPDATakesPrecedence, avoidMYTDaggerHack, genmoveWideRootNoise, analysisWideRootNoise,
                    genmoveAntiMirror, analysisAntiMirror, perspective, analysisPVLen);
  engine->setOrResetBoardSize(cfg, logger, seedRand, defaultBoardXSize, defaultBoardYSize);

  string line;
  string outputDir(argv[1]);
  string posLenStr(argv[2]);
  int posLen = stoi(posLenStr);
  stringstream ss;
  ss.str("");
  ss << outputDir << "dump_positions_out";
  string positionsPath = ss.str();
  ss.str("");
  ss << outputDir << "move_candidate";
  string moveCandidatePath = ss.str();
  ss.str("");
  ss << outputDir << "move_candidate_val";
  string moveCandidateValPath = ss.str();
  ss.str("");
  ss << outputDir << "eval";
  string evalPath = ss.str();
  ss.str("");
  ss << outputDir << "eval_val";
  string evalValPath = ss.str();

  std::ifstream positions(positionsPath, std::ios::binary);
  std::ofstream moveCandidate(moveCandidatePath, std::ios::binary);
  std::ofstream moveCandidateVal(moveCandidateValPath, std::ios::binary);
  std::ofstream eval(evalPath, std::ios::binary);
  std::ofstream evalVal(evalValPath, std::ios::binary);
  size_t bufSize = 2 * posLen * posLen;
  std::vector<char> buf(bufSize);
  while (positions.read(&buf[0], bufSize)) {
    std::streamsize bytes = positions.gcount();
    auto b = static_cast<size_t>(bytes);
    if (b != bufSize) {
      std::cerr << "Failure while reading " << positionsPath << ". b=" << b << " expected=" << bufSize << "\n";
      exit(1);
    }

    std::vector<int> player;
    std::vector<int> waiter;
    for (int idx = 0; idx < posLen * posLen; ++idx) {
      if (buf[idx] == 1) {
        player.push_back(idx);
      }
      if (buf[idx + posLen * posLen] == 1) {
        waiter.push_back(idx);
      }
    }
    bool isSanePosition = true;
    if (((player.size() + waiter.size()) % 2) == 0) {
      isSanePosition = engine->setPosition(player, waiter, posLen);
    } else {
      isSanePosition = engine->setPosition(waiter, player, posLen);
    }
    if (isSanePosition == false)
    {
      continue;
    }
    auto response = engine->rawNN(0, 0);
  }
  delete engine;
  engine = NULL;
  NeuralNet::globalCleanup();

  logger.write("All cleaned up, quitting");
  return 0;
}
