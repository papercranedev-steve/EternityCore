# PartyBot class rotations (Rotations/ClassRotation.h), one file per class (and role).
# Included from src/game/CMakeLists.txt after add_library(game ...).

set(ROTATIONS_DIR ${CMAKE_CURRENT_LIST_DIR})

set(ROTATIONS_SRCS
  ${ROTATIONS_DIR}/ClassRotation.h
  ${ROTATIONS_DIR}/ClassRotation.cpp
  ${ROTATIONS_DIR}/DruidRotation.cpp
  ${ROTATIONS_DIR}/HunterRotation.cpp
  ${ROTATIONS_DIR}/MageRotation.cpp
  ${ROTATIONS_DIR}/PaladinRotation.cpp
  ${ROTATIONS_DIR}/PriestRotation.cpp
  ${ROTATIONS_DIR}/RogueRotation.cpp
  ${ROTATIONS_DIR}/ShamanRotation.cpp
  ${ROTATIONS_DIR}/WarlockRotation.cpp
  ${ROTATIONS_DIR}/WarriorRotation.cpp
  ${ROTATIONS_DIR}/WarriorTankRotation.cpp
)

target_sources(game PRIVATE ${ROTATIONS_SRCS})
source_group(TREE ${ROTATIONS_DIR} PREFIX "PlayerBots\\Rotations" FILES ${ROTATIONS_SRCS})
