# Shared bot core: facts and hands used by every bot layer (Commander, Life, Companions, Population).
# Depends on nothing of those layers. Included from src/game/CMakeLists.txt after add_library(game ...).

set(BOTSHARED_DIR ${CMAKE_CURRENT_LIST_DIR})

set(BOTSHARED_SRCS
  ${BOTSHARED_DIR}/AuctionMarket.h
  ${BOTSHARED_DIR}/AuctionMarket.cpp
  ${BOTSHARED_DIR}/BotChat.h
  ${BOTSHARED_DIR}/BotChat.cpp
  ${BOTSHARED_DIR}/ClassTraining.h
  ${BOTSHARED_DIR}/ClassTraining.cpp
  ${BOTSHARED_DIR}/ClassQuestAbilities.h
  ${BOTSHARED_DIR}/ClassQuestAbilities.cpp
  ${BOTSHARED_DIR}/ClassQuestChains.h
  ${BOTSHARED_DIR}/ClassQuestChains.cpp
  ${BOTSHARED_DIR}/Crafting.h
  ${BOTSHARED_DIR}/Crafting.cpp
  ${BOTSHARED_DIR}/FoodAndDrink.h
  ${BOTSHARED_DIR}/FoodAndDrink.cpp
  ${BOTSHARED_DIR}/GearSources.h
  ${BOTSHARED_DIR}/GearSources.cpp
  ${BOTSHARED_DIR}/GearUpgrade.h
  ${BOTSHARED_DIR}/GearUpgrade.cpp
  ${BOTSHARED_DIR}/Goods.h
  ${BOTSHARED_DIR}/Goods.cpp
  ${BOTSHARED_DIR}/Inventory.h
  ${BOTSHARED_DIR}/Inventory.cpp
  ${BOTSHARED_DIR}/ItemUse.h
  ${BOTSHARED_DIR}/ItemUse.cpp
  ${BOTSHARED_DIR}/LootFacts.h
  ${BOTSHARED_DIR}/LootFacts.cpp
  ${BOTSHARED_DIR}/Mailbox.h
  ${BOTSHARED_DIR}/Mailbox.cpp
  ${BOTSHARED_DIR}/NpcDirectory.h
  ${BOTSHARED_DIR}/NpcDirectory.cpp
  ${BOTSHARED_DIR}/ObjectUse.h
  ${BOTSHARED_DIR}/ObjectUse.cpp
  ${BOTSHARED_DIR}/PetFood.h
  ${BOTSHARED_DIR}/PetFood.cpp
  ${BOTSHARED_DIR}/Provisions.h
  ${BOTSHARED_DIR}/Provisions.cpp
  ${BOTSHARED_DIR}/Poisons.h
  ${BOTSHARED_DIR}/Poisons.cpp
  ${BOTSHARED_DIR}/QuestRewards.h
  ${BOTSHARED_DIR}/QuestRewards.cpp
  ${BOTSHARED_DIR}/QuestGrant.h
  ${BOTSHARED_DIR}/QuestGrant.cpp
  ${BOTSHARED_DIR}/QuestMirror.h
  ${BOTSHARED_DIR}/QuestMirror.cpp
  ${BOTSHARED_DIR}/QuestPlaces.h
  ${BOTSHARED_DIR}/QuestPlaces.cpp
  ${BOTSHARED_DIR}/Purchasing.h
  ${BOTSHARED_DIR}/Purchasing.cpp
  ${BOTSHARED_DIR}/WalkLength.h
  ${BOTSHARED_DIR}/WalkLength.cpp
  ${BOTSHARED_DIR}/TownGates.h
  ${BOTSHARED_DIR}/TownGates.cpp
  ${BOTSHARED_DIR}/Trade.h
  ${BOTSHARED_DIR}/Trade.cpp
  ${BOTSHARED_DIR}/Supplies.h
  ${BOTSHARED_DIR}/Supplies.cpp
  ${BOTSHARED_DIR}/Vendor.h
  ${BOTSHARED_DIR}/Vendor.cpp
)

target_sources(game PRIVATE ${BOTSHARED_SRCS})
source_group(TREE ${BOTSHARED_DIR} PREFIX "PlayerBots\\Shared" FILES ${BOTSHARED_SRCS})
