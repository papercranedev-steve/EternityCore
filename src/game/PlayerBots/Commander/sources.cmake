# PartyBot Commander sources. Included from src/game/CMakeLists.txt after add_library(game ...).
# Add new module files here; cmake --build re-runs the configure step automatically.

set(COMMANDER_DIR ${CMAKE_CURRENT_LIST_DIR})

set(COMMANDER_SRCS
  ${COMMANDER_DIR}/CommanderTypes.h
  ${COMMANDER_DIR}/BotDirectives.h
  ${COMMANDER_DIR}/Registry.h
  ${COMMANDER_DIR}/Registry.cpp
  ${COMMANDER_DIR}/CommanderMgr.h
  ${COMMANDER_DIR}/CommanderMgr.cpp
  ${COMMANDER_DIR}/PartyCommander.h
  ${COMMANDER_DIR}/PartyCommander.cpp
  ${COMMANDER_DIR}/CommanderCommands.cpp
  ${COMMANDER_DIR}/Perception/WorldModel.h
  ${COMMANDER_DIR}/Perception/WorldModelBuilder.h
  ${COMMANDER_DIR}/Perception/WorldModelBuilder.cpp
  ${COMMANDER_DIR}/Perception/ThreatView.h
  ${COMMANDER_DIR}/Perception/ThreatView.cpp
  ${COMMANDER_DIR}/Perception/PackResolver.h
  ${COMMANDER_DIR}/Perception/PackResolver.cpp
  ${COMMANDER_DIR}/Perception/ThreatHistory.h
  ${COMMANDER_DIR}/Perception/ThreatHistory.cpp
  ${COMMANDER_DIR}/Behaviors/ThreatBehavior.h
  ${COMMANDER_DIR}/Behaviors/ThreatBehavior.cpp
  ${COMMANDER_DIR}/Coordination/TeamPlan.h
  ${COMMANDER_DIR}/Coordination/IAssigner.h
  ${COMMANDER_DIR}/Coordination/InterruptAssigner.h
  ${COMMANDER_DIR}/Coordination/InterruptAssigner.cpp
  ${COMMANDER_DIR}/Perception/SpellTags.h
  ${COMMANDER_DIR}/Perception/SpellTags.cpp
  ${COMMANDER_DIR}/Behaviors/InterruptBehavior.h
  ${COMMANDER_DIR}/Behaviors/InterruptBehavior.cpp
  ${COMMANDER_DIR}/Behaviors/IBehavior.h
  ${COMMANDER_DIR}/Behaviors/Arbiter.h
  ${COMMANDER_DIR}/Behaviors/Arbiter.cpp
)

target_sources(game PRIVATE ${COMMANDER_SRCS})
source_group(TREE ${COMMANDER_DIR} PREFIX "PlayerBots\\Commander" FILES ${COMMANDER_SRCS})
