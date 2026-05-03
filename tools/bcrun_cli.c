#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

static uint8_t *read_file(const char *path, size_t *out_size)
{
  FILE *fp = NULL;
  long len = 0;
  uint8_t *buf = NULL;

  *out_size = 0;
  fp = fopen(path, "rb");
  if (!fp) return NULL;

  if (fseek(fp, 0, SEEK_END) != 0) goto fail;
  len = ftell(fp);
  if (len < 0) goto fail;
  if (fseek(fp, 0, SEEK_SET) != 0) goto fail;

  buf = (uint8_t *)malloc((size_t)len);
  if (!buf) goto fail;
  if ((size_t)len != 0 && fread(buf, 1, (size_t)len, fp) != (size_t)len) goto fail;

  fclose(fp);
  *out_size = (size_t)len;
  return buf;

fail:
  if (fp) fclose(fp);
  free(buf);
  return NULL;
}

static int dostring(lua_State *L, const char *chunk, const char *name)
{
  int status = luaL_loadbuffer(L, chunk, strlen(chunk), name);
  if (status == 0) status = lua_pcall(L, 0, 0, 0);
  if (status != 0) {
    fprintf(stderr, "%s failed: %s\n", name, lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  return status;
}

static int traceback(lua_State *L)
{
  const char *msg = lua_tostring(L, 1);
  if (msg) {
    luaL_traceback(L, L, msg, 1);
  } else {
    lua_pushliteral(L, "(error object is not a string)");
  }
  return 1;
}

static int pcall_trace(lua_State *L, int nargs, int nresults)
{
  int base = lua_gettop(L) - nargs;
  int status;
  lua_pushcfunction(L, traceback);
  lua_insert(L, base);
  status = lua_pcall(L, nargs, nresults, base);
  lua_remove(L, base);
  return status;
}

int main(int argc, char **argv)
{
  const char *input = NULL;
  const char *prelude =
      "local raw_ipairs = ipairs\n"
      "function ipairs(v)\n"
      "  if type(v) ~= 'table' then\n"
      "    error('ipairs arg type=' .. type(v) .. ' value=' .. tostring(v), 2)\n"
      "  end\n"
      "  return raw_ipairs(v)\n"
      "end\n"
      "local function proxy(name)\n"
      "  local t = {}\n"
      "  local mt = {}\n"
      "  function mt.__index(self, key)\n"
      "    if key == 'Instance' then return self end\n"
      "    if key == 'Count' or key == 'Length' or key == 'Round' or key == 'Team' or key == 'Level' then return 0 end\n"
      "    if key == 'CurrentSceneValue' or key == 'GameMode' then return '' end\n"
      "    local child = proxy(name .. '.' .. tostring(key))\n"
      "    rawset(self, key, child)\n"
      "    return child\n"
      "  end\n"
      "  function mt.__call() return nil end\n"
      "  function mt.__tostring() return name end\n"
      "  return setmetatable(t, mt)\n"
      "end\n"
      "luanet = { import_type = function(name) return proxy(name) end, enum = function() return 0 end }\n"
      "Tools = proxy('JyGame.Tools')\n"
      "function Tools.GetRandomInt(a) return a or 1 end\n"
      "function Tools.GetRandom(a) return a or 1 end\n"
      "function Tools.ProbabilityTest() return false end\n"
      "LuaTool = proxy('JyGame.LuaTool')\n"
      "function LuaTool.CreateLuaTable(v) return v end\n"
      "function LuaTool.MakeStringArray(v) return v end\n"
      "ModData = proxy('JyGame.ModData')\n"
      "ModData.SkillMaxLevels = { Clear = function() end }\n"
      "function ModData.SetParam() end\n"
      "function ModData.AddNick() end\n"
      "Debug = proxy('UnityEngine.Debug')\n"
      "function Debug.LogError() end\n"
      "ResourceStrings = { ResStrings = setmetatable({}, { __index = function(_, k) return tostring(k) end }) }\n"
      "_G.TRIGGER_LUA_MASK = '2022-09-01-FLAG'\n"
      "_G.MAIN_LUA_MASK = '2022-09-01-FLAG'\n";
  size_t input_size = 0;
  uint8_t *input_buf = NULL;
  lua_State *L = NULL;
  int status = 0;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <bytecode_file>\n", argv[0]);
    return 2;
  }

  input = argv[1];
  input_buf = read_file(input, &input_size);
  if (!input_buf) {
    fprintf(stderr, "read failed: %s (%s)\n", input, strerror(errno));
    return 1;
  }

  L = luaL_newstate();
  if (!L) {
    fprintf(stderr, "luaL_newstate failed\n");
    free(input_buf);
    return 1;
  }

  luaL_openlibs(L);
  if (dostring(L, prelude, "@bcrun_prelude")) {
    lua_close(L);
    free(input_buf);
    return 1;
  }

  status = luaL_loadbuffer(L, (const char *)input_buf, input_size, "@tmp.lua");
  if (status == 0) status = pcall_trace(L, 0, 0);
  if (status != 0) {
    fprintf(stderr, "run failed: %s\n", lua_tostring(L, -1));
    lua_pop(L, 1);
  } else {
    printf("run ok: %s\n", input);
  }

  lua_close(L);
  free(input_buf);
  return status == 0 ? 0 : 1;
}
