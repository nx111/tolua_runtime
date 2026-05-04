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

static int run_getrrevrole_harness(lua_State *L)
{
  const char *harness =
      "local raw_pairs = pairs\n"
      "local function list(items)\n"
      "  local t = { Count = #items }\n"
      "  for i = 1, #items do t[i - 1] = items[i] end\n"
      "  return t\n"
      "end\n"
      "local function zero_list() return list({}) end\n"
      "local skill_talents = list({'offline-talent'})\n"
      "skill_talents.__create_lua_table_ok = true\n"
      "function LuaTool.CreateLuaTable(v)\n"
      "  if type(v) ~= 'table' or not v.__create_lua_table_ok then\n"
      "    error('CreateLuaTable arg type=' .. type(v) .. ' value=' .. tostring(v), 2)\n"
      "  end\n"
      "  return { 'offline-talent' }\n"
      "end\n"
      "BattleFieldMonitor.getInstance = function()\n"
      "  return { helpResetAllSkills = function() return nil end, helpChangeAttribute = function() end, GetAddMaxMp = function() return 0 end }\n"
      "end\n"
      "RuntimeData.Instance = RuntimeData.Instance or {}\n"
      "RuntimeData.Instance.gameEngine = { CurrentSceneValue = '', battleType = '' }\n"
      "RuntimeData.Instance.GameMode = ''\n"
      "RuntimeData.Instance.Round = 1\n"
      "local role = {\n"
      "  Name = 'offline-role', Key = 'offline-key', Level = 1,\n"
      "  Attributes = { female = 0, gengu = 0 }, AttributesFinal = { gengu = 0, wuxing = 0 },\n"
      "  Talents = zero_list(), InternalSkills = list({{ Name = 'offline-skill', Level = 1, IsUsed = true, Talents = skill_talents, InternalSkill = { Triggers = zero_list() } }}),\n"
      "  Skills = zero_list(), SpecialSkills = zero_list(), EquipmentTalents = skill_talents\n"
      "}\n"
      "function role:GetEquipment() return nil end\n"
      "local bf = { BattleTimestamp = 1, SpritesTable = {} }\n"
      "local sprite = { ParentBattleField = bf, Role = role, Team = 1, Name = 'offline-sprite' }\n"
      "role.Sprite = sprite\n"
      "bf.SpritesTable = { sprite }\n"
      "BattleUtil.GetRrevRole(sprite)\n";

  return dostring(L, harness, "@bcrun_getrrevrole");
}

static int run_checktrigger_harness(lua_State *L)
{
  const char *harness =
      "local equip = { level = 1, item = { type = 1 } }\n"
      "local trigger = { Name = 'attribute', ArgvsString = 'atk,999' }\n"
      "assert(type(EquipUtil) == 'table', 'EquipUtil missing')\n"
      "assert(type(EquipUtil.CheckTriggerAllowed) == 'function', 'CheckTriggerAllowed missing')\n"
      "EquipUtil.CheckTriggerAllowed(equip, trigger, 1)\n";
  return dostring(L, harness, "@bcrun_checktrigger");
}

static int run_registerprevrole_trigger_harness(lua_State *L)
{
  const char *harness =
      "local function list(items)\n"
      "  local t = { Count = #items }\n"
      "  for i = 1, #items do t[i - 1] = items[i] end\n"
      "  return t\n"
      "end\n"
      "local function zero_list() return list({}) end\n"
      "local trigger = { Name = 'powerup_special', Argvs = { [0] = 'offline_attr', [1] = '7' } }\n"
      "local add_trigger = { Name = 'powerup_special', Argvs = { [0] = 'offline_attr2', [1] = '3' } }\n"
      "local equip = {\n"
      "  Name = 'offline-equip',\n"
      "  Triggers = list({ trigger }),\n"
      "  AdditionTriggers = list({ add_trigger })\n"
      "}\n"
      "local skill_talents = list({'offline-talent'})\n"
      "skill_talents.__create_lua_table_ok = true\n"
      "function LuaTool.CreateLuaTable(v)\n"
      "  if type(v) ~= 'table' or not v.__create_lua_table_ok then\n"
      "    error('CreateLuaTable arg type=' .. type(v) .. ' value=' .. tostring(v), 2)\n"
      "  end\n"
      "  return { 'offline-talent' }\n"
      "end\n"
      "BattleFieldMonitor.getInstance = function()\n"
      "  return { helpResetAllSkills = function() return nil end, helpChangeAttribute = function() end, GetAddMaxMp = function() return 0 end }\n"
      "end\n"
      "RuntimeData.Instance = RuntimeData.Instance or {}\n"
      "RuntimeData.Instance.gameEngine = { CurrentSceneValue = '', battleType = '' }\n"
      "RuntimeData.Instance.GameMode = ''\n"
      "RuntimeData.Instance.Round = 1\n"
      "local role = {\n"
      "  Name = 'offline-role', Key = 'offline-key', Level = 1,\n"
      "  Attributes = { female = 0, gengu = 0 }, AttributesFinal = { gengu = 0, wuxing = 0 },\n"
      "  Talents = zero_list(), InternalSkills = list({{ Name = 'offline-skill', Level = 1, IsUsed = true, Talents = skill_talents, InternalSkill = { Triggers = zero_list() } }}),\n"
      "  Skills = zero_list(), SpecialSkills = zero_list(), EquipmentTalents = skill_talents\n"
      "}\n"
      "function role:GetEquipment(i)\n"
      "  if i == 1 then return equip end\n"
      "  return nil\n"
      "end\n"
      "local bf = { BattleTimestamp = 1, SpritesTable = {} }\n"
      "local sprite = { ParentBattleField = bf, Role = role, Team = 1, Name = 'offline-sprite' }\n"
      "role.Sprite = sprite\n"
      "bf.SpritesTable = { sprite }\n"
      "BattleUtil.RegisterPrevRole(bf, role, sprite)\n"
      "local prev = BattleUtil.PrevRoles[sprite]\n"
      "assert(type(prev) == 'table', 'PrevRoles entry missing')\n"
      "assert(prev.attributes.offline_attr == 7, 'offline_attr=' .. tostring(prev.attributes.offline_attr))\n"
      "assert(prev.attributes.offline_attr2 == 3, 'offline_attr2=' .. tostring(prev.attributes.offline_attr2))\n";

  return dostring(L, harness, "@bcrun_registerprevrole");
}

static int run_installyinjian_harness(lua_State *L)
{
  const char *harness =
      "local old_import = luanet.import_type\n"
      "local function make_cs_type(tag)\n"
      "  local t = { __tag = tag }\n"
      "  function t:GetType() return t end\n"
      "  function t:GetMethod(name)\n"
      "    return {\n"
      "      MakeGenericMethod = function(_, type_array)\n"
      "        assert(type(type_array) == 'table', 'MakeGenericMethod type_array=' .. type(type_array))\n"
      "        return {\n"
      "          Invoke = function(_, target, obj_array)\n"
      "            assert(obj_array == nil or type(obj_array) == 'table', 'Invoke obj_array=' .. type(obj_array))\n"
      "            return { __create_lua_table_ok = true }\n"
      "          end\n"
      "        }\n"
      "      end\n"
      "    }\n"
      "  end\n"
      "  return t\n"
      "end\n"
      "local fake_assembly_inst = {}\n"
      "function fake_assembly_inst:GetType(name)\n"
      "  assert(type(name) == 'string', 'Assembly:GetType arg=' .. type(name))\n"
      "  return make_cs_type(name)\n"
      "end\n"
      "local fake_assembly = {\n"
      "  GetExecutingAssembly = function() return fake_assembly_inst end,\n"
      "  Load = function(name)\n"
      "    assert(type(name) == 'string', 'Assembly.Load arg=' .. type(name))\n"
      "    local t = {}\n"
      "    function t:GetType(type_name)\n"
      "      assert(type(type_name) == 'string', 'systemAssembly:GetType arg=' .. type(type_name))\n"
      "      return make_cs_type(type_name)\n"
      "    end\n"
      "    return t\n"
      "  end\n"
      "}\n"
      "local fake_array = {\n"
      "  CreateInstance = function(type_obj, n)\n"
      "    assert(type(type_obj) == 'table', 'Array.CreateInstance type_obj=' .. type(type_obj))\n"
      "    assert(type(n) == 'number', 'Array.CreateInstance n=' .. type(n))\n"
      "    local arr = { Length = n }\n"
      "    for i = 0, n - 1 do arr[i] = nil end\n"
      "    return arr\n"
      "  end\n"
      "}\n"
      "luanet.import_type = function(name)\n"
      "  if name == 'System.Reflection.Assembly' then return fake_assembly end\n"
      "  if name == 'System.Array' then return fake_array end\n"
      "  return old_import(name)\n"
      "end\n"
      "local old_create = LuaTool.CreateLuaTable\n"
      "local create_call = 0\n"
      "LuaTool.CreateLuaTable = function(v)\n"
      "  assert(type(v) == 'table', 'CreateLuaTable arg=' .. type(v) .. ' value=' .. tostring(v))\n"
      "  create_call = create_call + 1\n"
      "  if create_call == 1 then\n"
      "    return {\n"
      "      {\n"
      "        Name = 'aoyi_offline',\n"
      "        DisplayName = 'aoyi_offline',\n"
      "        start = 'start_skill',\n"
      "        GetStartSkillHard = function() return 1 end\n"
      "      }\n"
      "    }\n"
      "  elseif create_call == 2 then\n"
      "    return { { Name = 'skill_offline', Hard = 1 } }\n"
      "  elseif create_call == 3 then\n"
      "    return { { Name = 'internal_offline', Hard = 1 } }\n"
      "  else\n"
      "    return { { Name = 'unique_offline', Hard = 1 } }\n"
      "  end\n"
      "end\n"
      "assert(type(EquipUtil) == 'table', 'EquipUtil missing')\n"
      "assert(type(EquipUtil.InstallYinjian) == 'function', 'InstallYinjian missing')\n"
      "local ok, err = xpcall(function() EquipUtil.InstallYinjian() end, debug.traceback)\n"
      "LuaTool.CreateLuaTable = old_create\n"
      "if not ok then error(err, 0) end\n";
  return dostring(L, harness, "@bcrun_installyinjian");
}

int main(int argc, char **argv)
{
  const char *input = NULL;
  const char *mode = NULL;
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
      "    if key == 'CurrentSceneValue' or key == 'GameMode' or key == 'battleType' then return '' end\n"
      "    if key == 'ContainsKey' then return function() return false end end\n"
      "    if key == 'Clear' then return function() end end\n"
      "    local child = proxy(name .. '.' .. tostring(key))\n"
      "    rawset(self, key, child)\n"
      "    return child\n"
      "  end\n"
      "  function mt.__call() return nil end\n"
      "  function mt.__tostring() return name end\n"
      "  return setmetatable(t, mt)\n"
      "end\n"
      "local import_cache = {}\n"
      "luanet = { import_type = function(name)\n"
      "  if name == 'JyGame.Trigger' then\n"
      "    if not import_cache[name] then\n"
      "      import_cache[name] = setmetatable({}, { __call = function() return {} end, __index = function() return nil end })\n"
      "    end\n"
      "    return import_cache[name]\n"
      "  end\n"
      "  if not import_cache[name] then import_cache[name] = proxy(name) end\n"
      "  return import_cache[name]\n"
      "end, enum = function() return 0 end }\n"
      "Tools = luanet.import_type('JyGame.Tools')\n"
      "function Tools.GetRandomInt(a) return a or 1 end\n"
      "function Tools.GetRandom(a) return a or 1 end\n"
      "function Tools.ProbabilityTest() return false end\n"
      "LuaTool = luanet.import_type('JyGame.LuaTool')\n"
      "function LuaTool.CreateLuaTable(v) return v end\n"
      "function LuaTool.MakeStringArray(v) return v end\n"
      "BattleFieldMonitor = luanet.import_type('JygameSpecialLib.BattleFieldMonitor')\n"
      "RuntimeData = luanet.import_type('JyGame.RuntimeData')\n"
      "ModData = luanet.import_type('JyGame.ModData')\n"
      "ModData.SkillMaxLevels = { Clear = function() end, ContainsKey = function() return false end }\n"
      "function ModData.SetParam() end\n"
      "function ModData.AddNick() end\n"
      "Debug = luanet.import_type('UnityEngine.Debug')\n"
      "function Debug.LogError() end\n"
      "ResourceStrings = { ResStrings = setmetatable({}, { __index = function(_, k) return tostring(k) end }) }\n"
      "_G.TRIGGER_LUA_MASK = '2022-09-01-FLAG'\n"
      "_G.MAIN_LUA_MASK = '2022-09-01-FLAG'\n";
  size_t input_size = 0;
  uint8_t *input_buf = NULL;
  lua_State *L = NULL;
  int status = 0;

  if (argc != 2 && argc != 3) {
    fprintf(stderr, "usage: %s <bytecode_file> [getrrevrole|registerprevrole|checktrigger|installyinjian]\n", argv[0]);
    return 2;
  }

  input = argv[1];
  mode = argc == 3 ? argv[2] : NULL;
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
    const char *errmsg = lua_tostring(L, -1);
    fprintf(stderr, "run failed: %s\n", errmsg);
    if (errmsg != NULL && strstr(errmsg, "incompatible bytecode") != NULL) {
      fprintf(stderr,
              "hint: incompatible bytecode usually means runtime/build mismatch.\n"
              "hint: rebuild offline tools with LUAJIT_ENABLE_GC64 and run via WSL gc64 toolchain.\n");
    }
    lua_pop(L, 1);
    if (mode == NULL) {
      lua_close(L);
      free(input_buf);
      return 1;
    }
  }

  if (mode != NULL &&
      (strcmp(mode, "getrrevrole") == 0 ||
       strcmp(mode, "registerprevrole") == 0 ||
       strcmp(mode, "checktrigger") == 0 ||
       strcmp(mode, "installyinjian") == 0)) {
    if (strcmp(mode, "getrrevrole") == 0) {
      status = run_getrrevrole_harness(L);
    } else if (strcmp(mode, "registerprevrole") == 0) {
      status = run_registerprevrole_trigger_harness(L);
    } else if (strcmp(mode, "installyinjian") == 0) {
      status = run_installyinjian_harness(L);
    } else {
      status = run_checktrigger_harness(L);
    }
    if (status == 0) printf("run ok: %s mode=%s\n", input, mode);
  } else if (status == 0) {
    printf("run ok: %s\n", input);
  }

  lua_close(L);
  free(input_buf);
  return status == 0 ? 0 : 1;
}
