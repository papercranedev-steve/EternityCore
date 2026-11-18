# World population bots (see .kiro/docs/2026-11-01-world-population-bots.md in the workspace root).
# Included from src/game/CMakeLists.txt after add_library(game ...).

set(POPULATION_DIR ${CMAKE_CURRENT_LIST_DIR})

set(POPULATION_SRCS
  ${POPULATION_DIR}/PopulationTypes.h
  ${POPULATION_DIR}/PopulationMgr.h
  ${POPULATION_DIR}/PopulationMgr.cpp
  ${POPULATION_DIR}/PopulationBotAI.h
  ${POPULATION_DIR}/PopulationBotAI.cpp
  ${POPULATION_DIR}/PopulationIdentity.h
  ${POPULATION_DIR}/PopulationIdentity.cpp
  ${POPULATION_DIR}/ScenePoints.h
  ${POPULATION_DIR}/ScenePoints.cpp
  ${POPULATION_DIR}/Observers.h
  ${POPULATION_DIR}/Observers.cpp
  ${POPULATION_DIR}/FieldSpots.h
  ${POPULATION_DIR}/FieldSpots.cpp
  ${POPULATION_DIR}/FieldPopulation.h
  ${POPULATION_DIR}/FieldPopulation.cpp
  ${POPULATION_DIR}/CityPopulation.h
  ${POPULATION_DIR}/CityPopulation.cpp
  ${POPULATION_DIR}/Roster.h
  ${POPULATION_DIR}/Roster.cpp
  ${POPULATION_DIR}/Growth.h
  ${POPULATION_DIR}/Growth.cpp
  ${POPULATION_DIR}/PopulationCommands.cpp
)

target_sources(game PRIVATE ${POPULATION_SRCS})
source_group(TREE ${POPULATION_DIR} PREFIX "PlayerBots\\Population" FILES ${POPULATION_SRCS})
