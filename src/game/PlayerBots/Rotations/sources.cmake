# PartyBot class rotations (Rotations/ClassRotation.h). Included from src/game/CMakeLists.txt
# after add_library(game ...). Add a class file here when its rotation moves out of PartyBotAI.cpp.

set(ROTATIONS_DIR ${CMAKE_CURRENT_LIST_DIR})

set(ROTATIONS_SRCS
  ${ROTATIONS_DIR}/ClassRotation.h
  ${ROTATIONS_DIR}/ClassRotation.cpp
  ${ROTATIONS_DIR}/MageRotation.cpp
)

target_sources(game PRIVATE ${ROTATIONS_SRCS})
source_group(TREE ${ROTATIONS_DIR} PREFIX "PlayerBots\\Rotations" FILES ${ROTATIONS_SRCS})
