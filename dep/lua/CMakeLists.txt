# Lua 5.4.7 (https://www.lua.org/ftp/lua-5.4.7.tar.gz, sha256 9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30)
# Used by the dev console (src/game/DevConsole). Built as C++ so that lua_error unwinds
# with exceptions and C++ destructors in binding functions still run.

file(GLOB LUA_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
set_source_files_properties(${LUA_SRCS} PROPERTIES LANGUAGE CXX)

add_library(lua STATIC ${LUA_SRCS})
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
  target_compile_options(lua PRIVATE /TP /EHsc /W0)
  target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(lua PRIVATE -w)
endif()
set_target_properties(lua PROPERTIES FOLDER "3rd Party")
