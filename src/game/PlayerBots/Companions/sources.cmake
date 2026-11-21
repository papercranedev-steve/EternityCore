# Companions: recruited roster members (see .kiro/docs/2026-11-17-soul-companions.md in the workspace root).
# Included from src/game/CMakeLists.txt after add_library(game ...).

set(COMPANIONS_DIR ${CMAKE_CURRENT_LIST_DIR})

set(COMPANIONS_SRCS
  ${COMPANIONS_DIR}/CompanionMgr.h
  ${COMPANIONS_DIR}/CompanionMgr.cpp
  ${COMPANIONS_DIR}/CompanionCommands.cpp
  ${COMPANIONS_DIR}/CompanionSheet.cpp
  ${COMPANIONS_DIR}/RewardChoice.h
  ${COMPANIONS_DIR}/RewardChoice.cpp
  ${COMPANIONS_DIR}/Personality.h
  ${COMPANIONS_DIR}/Personality.cpp
  ${COMPANIONS_DIR}/PastRoll.h
  ${COMPANIONS_DIR}/PastProfessions.h
  ${COMPANIONS_DIR}/PastProfessions.cpp
  ${COMPANIONS_DIR}/OfflineLife.h
  ${COMPANIONS_DIR}/OfflineLife.cpp
  ${COMPANIONS_DIR}/ArtisanTrip.h
  ${COMPANIONS_DIR}/ArtisanTrip.cpp
  ${COMPANIONS_DIR}/Affinity/AffinitySources.h
  ${COMPANIONS_DIR}/Affinity/AffinitySources.cpp
  ${COMPANIONS_DIR}/Affinity/Gates.h
  ${COMPANIONS_DIR}/Affinity/Gates.cpp
  ${COMPANIONS_DIR}/Affinity/TradeGate.h
  ${COMPANIONS_DIR}/Affinity/TradeGate.cpp
  ${COMPANIONS_DIR}/ClassQuests/PastQuests.h
  ${COMPANIONS_DIR}/ClassQuests/PastQuests.cpp
  ${COMPANIONS_DIR}/ClassQuests/MirrorSync.h
  ${COMPANIONS_DIR}/ClassQuests/MirrorSync.cpp
  ${COMPANIONS_DIR}/ClassQuests/ClassQuestFlow.h
  ${COMPANIONS_DIR}/ClassQuests/ClassQuestFlow.cpp
  ${COMPANIONS_DIR}/Talk/TalkService.h
  ${COMPANIONS_DIR}/Talk/Talk.h
  ${COMPANIONS_DIR}/Talk/Talk.cpp
  ${COMPANIONS_DIR}/Talk/Services.cpp
  ${COMPANIONS_DIR}/Talk/TalkCommands.cpp
  ${COMPANIONS_DIR}/Talk/Services/ServiceList.h
  ${COMPANIONS_DIR}/Talk/Services/StayService.cpp
  ${COMPANIONS_DIR}/Talk/Services/BagsService.cpp
)

target_sources(game PRIVATE ${COMPANIONS_SRCS})
source_group(TREE ${COMPANIONS_DIR} PREFIX "PlayerBots\\Companions" FILES ${COMPANIONS_SRCS})
