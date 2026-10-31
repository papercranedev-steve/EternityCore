# Dev console: Lua scripts run inside mangosd for debugging, test scenarios and combat recording.
# Included from src/game/CMakeLists.txt after add_library(game ...).

set(DEVCONSOLE_DIR ${CMAKE_CURRENT_LIST_DIR})

set(DEVCONSOLE_SRCS
  ${DEVCONSOLE_DIR}/DevConsole.h
  ${DEVCONSOLE_DIR}/DevConsole.cpp
  ${DEVCONSOLE_DIR}/LuaJson.h
  ${DEVCONSOLE_DIR}/LuaJson.cpp
  ${DEVCONSOLE_DIR}/LuaUnit.h
  ${DEVCONSOLE_DIR}/LuaUnit.cpp
  ${DEVCONSOLE_DIR}/LuaWorld.cpp
  ${DEVCONSOLE_DIR}/CombatEvents.h
  ${DEVCONSOLE_DIR}/CombatEvents.cpp
  ${DEVCONSOLE_DIR}/DevConsoleCommands.cpp
)

target_sources(game PRIVATE ${DEVCONSOLE_SRCS})
target_link_libraries(game lua)
source_group(TREE ${DEVCONSOLE_DIR} PREFIX "DevConsole" FILES ${DEVCONSOLE_SRCS})
