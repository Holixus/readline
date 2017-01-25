cmake_minimum_required(VERSION 2.8.9)
project(readline)

OPTION(BUILD_EXAMPLES     "build examples" OFF)

set(RL_MAX_LENGTH      "1024"  CACHE STRING "maximum length of input line")
set(RL_HISTORY_HEIGHT  "32"    CACHE STRING "Height of history file")
set(RL_HISTORY_FILE    "/tmp/.rl_history" CACHE FILEPATH "History file")
set(RL_WINDOW_WIDTH    "80"    CACHE STRING "Default window width")
set(RL_SORT_HINTS      ON      CACHE BOOL "Sort <tab> hints list")
set(RL_USE_WRITE       ON      CACHE BOOL "Use write() instead of fwrite()")

configure_file(config.h.in config.h @ONLY)

set(sources readline.c)

add_library(readline SHARED ${sources})
add_library(readline-static STATIC ${sources})
SET_TARGET_PROPERTIES(readline-static PROPERTIES OUTPUT_NAME readline)


ADD_SUBDIRECTORY(examples)
