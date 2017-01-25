cmake_minimum_required(VERSION 2.8.9)
project(readline)

OPTION(BUILD_EXAMPLES "build examples" OFF)

set(sources readline.c)

set(RL_MAX_LENGTH      1024)
set(RL_HISTORY_HEIGHT  32)
set(RL_HISTORY_FILE    "/tmp/.rl_history")
set(RL_WINDOW_WIDTH    80)
set(RL_SORT_HINTS ON)
set(RL_USE_WRITE ON)

configure_file(config.h.in config.h @ONLY)

add_library(readline SHARED ${sources})
add_library(readline-static STATIC ${sources})
SET_TARGET_PROPERTIES(readline-static PROPERTIES OUTPUT_NAME readline)


ADD_SUBDIRECTORY(examples)
