set(SGF_FOLDER "${PROJECT_BINARY_DIR}/data/sgf")
configure_file("${PROJECT_BINARY_DIR}/data/configs/selfplay/selfplay_test.cfg.in" "${PROJECT_BINARY_DIR}/data/configs/selfplay/selfplay_test.cfg" @ONLY)
configure_file("${PROJECT_BINARY_DIR}/data/configs/selfplay/gatekeeper_test.cfg.in" "${PROJECT_BINARY_DIR}/data/configs/selfplay/gatekeeper_test.cfg" @ONLY)
