# Bot daily life: errands (loot, later vendor, trainer, quests) any bot kind can run, outside the
# Commander's party fight. Included from src/game/CMakeLists.txt after add_library(game ...).

set(LIFE_DIR ${CMAKE_CURRENT_LIST_DIR})

set(LIFE_SRCS
  ${LIFE_DIR}/Errands/ErrandCommon.h
  ${LIFE_DIR}/Errands/ErrandCommon.cpp
  ${LIFE_DIR}/Errands/LootRun.h
  ${LIFE_DIR}/Errands/LootRun.cpp
  ${LIFE_DIR}/Errands/QuestObjectRun.h
  ${LIFE_DIR}/Errands/QuestObjectRun.cpp
  ${LIFE_DIR}/Errands/RewardChooser.h
  ${LIFE_DIR}/Errands/QuestTurnIn.h
  ${LIFE_DIR}/Errands/QuestTurnIn.cpp
)

target_sources(game PRIVATE ${LIFE_SRCS})
source_group(TREE ${LIFE_DIR} PREFIX "PlayerBots\\Life" FILES ${LIFE_SRCS})
