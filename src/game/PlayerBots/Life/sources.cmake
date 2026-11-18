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
  ${LIFE_DIR}/Errands/NpcVisit.h
  ${LIFE_DIR}/Errands/NpcVisit.cpp
  ${LIFE_DIR}/Errands/MerchantTasks.h
  ${LIFE_DIR}/Errands/MerchantTasks.cpp
  ${LIFE_DIR}/Errands/TownTrip.h
  ${LIFE_DIR}/Errands/TownTrip.cpp
  ${LIFE_DIR}/Errands/Trip/TripPlan.h
  ${LIFE_DIR}/Errands/Trip/RouteOrder.h
  ${LIFE_DIR}/Errands/Trip/RouteOrder.cpp
  ${LIFE_DIR}/Errands/Trip/ShoppingPlanner.h
  ${LIFE_DIR}/Errands/Trip/ShoppingPlanner.cpp
  ${LIFE_DIR}/Errands/Trip/TrainingPlanner.h
  ${LIFE_DIR}/Errands/Trip/TrainingPlanner.cpp
  ${LIFE_DIR}/Requests/Intents.h
  ${LIFE_DIR}/Requests/Intents.cpp
  ${LIFE_DIR}/Requests/RequestBoard.h
  ${LIFE_DIR}/Requests/RequestBoard.cpp
  ${LIFE_DIR}/Requests/Phrases.h
  ${LIFE_DIR}/Requests/Phrases.cpp
)

target_sources(game PRIVATE ${LIFE_SRCS})
source_group(TREE ${LIFE_DIR} PREFIX "PlayerBots\\Life" FILES ${LIFE_SRCS})
