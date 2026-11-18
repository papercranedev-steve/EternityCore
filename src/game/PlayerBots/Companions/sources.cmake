# Companions: recruited roster members (see .kiro/docs/2026-11-17-soul-companions.md in the workspace root).
# Included from src/game/CMakeLists.txt after add_library(game ...).

set(COMPANIONS_DIR ${CMAKE_CURRENT_LIST_DIR})

set(COMPANIONS_SRCS
  ${COMPANIONS_DIR}/CompanionMgr.h
  ${COMPANIONS_DIR}/CompanionMgr.cpp
  ${COMPANIONS_DIR}/CompanionCommands.cpp
  ${COMPANIONS_DIR}/RewardChoice.h
  ${COMPANIONS_DIR}/RewardChoice.cpp
  ${COMPANIONS_DIR}/Personality.h
  ${COMPANIONS_DIR}/Personality.cpp
)

target_sources(game PRIVATE ${COMPANIONS_SRCS})
source_group(TREE ${COMPANIONS_DIR} PREFIX "PlayerBots\\Companions" FILES ${COMPANIONS_SRCS})
