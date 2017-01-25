cmake_minimum_required(VERSION 3.0)
project(readline VERSION 1.1.7)

OPTION(BUILD_EXAMPLES     "build examples" OFF)

set(RL_MAX_LENGTH      "1024"  CACHE STRING "maximum length of input line")
set(RL_HISTORY_HEIGHT  "32"    CACHE STRING "Height of history file")
set(RL_HISTORY_FILE    "/tmp/.rl_history" CACHE FILEPATH "Default history file")
set(RL_WINDOW_WIDTH    "80"    CACHE STRING "Default window width")
set(RL_SORT_HINTS      ON      CACHE BOOL "Sort <tab> hints list")
set(RL_USE_WRITE       ON      CACHE BOOL "Use write() instead of fwrite()")

configure_file(config.h.in config.h @ONLY)

set(sources readline.c)

add_library(readline SHARED ${sources})
SET_TARGET_PROPERTIES(readline PROPERTIES VERSION ${PROJECT_VERSION_MAJOR})

add_library(readline-static STATIC ${sources})
SET_TARGET_PROPERTIES(readline-static PROPERTIES OUTPUT_NAME readline)

INSTALL(FILES readline.h
	DESTINATION include
)
INSTALL(TARGETS readline readline-static
	ARCHIVE DESTINATION lib
	LIBRARY DESTINATION lib
)

ADD_SUBDIRECTORY(examples)
