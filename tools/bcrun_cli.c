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

static int run_bytecode_file(lua_State *L, const char *input)
{
  size_t input_size = 0;
  uint8_t *input_buf = read_file(input, &input_size);
  int status = 0;

  if (!input_buf) {
    fprintf(stderr, "read failed: %s (%s)\n", input, strerror(errno));
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
  }

  free(input_buf);
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

static int run_battle_rest_harness(lua_State *L)
{
  const char *harness =
      "local function list(items)\n"
      "  local t = { Count = #items, Length = #items, __items = items, __create_lua_table_ok = true }\n"
      "  for i = 1, #items do t[i - 1] = items[i] end\n"
      "  return t\n"
      "end\n"
      "local function zero_list() return list({}) end\n"
      "function LuaTool.CreateLuaTable(v)\n"
      "  assert(type(v) == 'table' and v.__create_lua_table_ok, 'CreateLuaTable arg=' .. type(v) .. ' value=' .. tostring(v))\n"
      "  local out = {}\n"
      "  for i = 1, #v.__items do out[i] = v.__items[i] end\n"
      "  return out\n"
      "end\n"
      "BattleFieldMonitor.getInstance = function()\n"
      "  return { helpResetAllSkills = function() return nil end, helpChangeAttribute = function() end, GetAddMaxMp = function() return 0 end }\n"
      "end\n"
      "RuntimeData.Instance = RuntimeData.Instance or {}\n"
      "RuntimeData.Instance.gameEngine = { CurrentSceneValue = '', battleType = '' }\n"
      "RuntimeData.Instance.GameMode = ''\n"
      "RuntimeData.Instance.Round = 1\n"
      "local talent_map = { ['淫娃荡妇'] = true }\n"
      "local role = {\n"
      "  Name = 'offline-role', Key = 'offline-key', Level = 10,\n"
      "  Attributes = { female = 0, gengu = 0 }, AttributesFinal = { gengu = 0, wuxing = 0 },\n"
      "  Talents = list({ '淫娃荡妇' }), InternalSkills = zero_list(), Skills = zero_list(), SpecialSkills = zero_list(), EquipmentTalents = zero_list()\n"
      "}\n"
      "function role:GetEquipment() return nil end\n"
      "function role:HasTalent(name) return talent_map[name] or false end\n"
      "function role:RemoveTalent(name) talent_map[name] = nil end\n"
      "function role:AddTalent(name) talent_map[name] = true end\n"
      "local bf = { BattleTimestamp = 1, SpritesTable = {} }\n"
      "function bf:Log() end\n"
      "local sprite = {\n"
      "  ParentBattleField = bf, Role = role, Team = 1, Name = 'offline-sprite',\n"
      "  X = 3, Y = 4, Hp = 60, MaxHp = 100, Mp = 20, MaxMp = 50, Balls = 0, Sp = 0\n"
      "}\n"
      "function sprite:HasBuff() return false end\n"
      "function sprite:DeleteBuff() end\n"
      "function sprite:GetBuff() return { Level = 0, LeftRound = 0, Leftround = 0 } end\n"
      "function sprite:Set_needRefresh() end\n"
      "function sprite:SkillCdRecover() end\n"
      "function sprite:AddBuff() end\n"
      "function sprite:AddBuffOnly2() end\n"
      "function sprite:Say() end\n"
      "role.Sprite = sprite\n"
      "bf.SpritesTable = { sprite }\n"
      "assert(type(BattleUtil.PrevWorkflows) == 'table', 'PrevWorkflows missing')\n"
      "assert(type(BattleUtil.PrevWorkflows['Rest_forTalent']) == 'table', 'Rest_forTalent=' .. type(BattleUtil.PrevWorkflows['Rest_forTalent']))\n"
      "assert(BattleUtil.PrevWorkflows['Rest_forTalent'].name == 'Rest', 'Rest_forTalent.name=' .. tostring(BattleUtil.PrevWorkflows['Rest_forTalent'].name))\n"
      "BattleUtil.RegisterPrevRole(bf, role, sprite)\n"
      "local prev = BattleUtil.PrevRoles[sprite]\n"
      "assert(type(prev) == 'table', 'PrevRoles entry missing')\n"
      "assert(type(prev.workflows) == 'table', 'prev.workflows missing')\n"
      "assert(type(prev.workflows.Rest) == 'table', 'prev.workflows.Rest=' .. type(prev.workflows.Rest))\n"
      "assert(#prev.workflows.Rest >= 1, 'prev.workflows.Rest count=' .. tostring(#prev.workflows.Rest))\n"
      "local ret = BATTLE_Rest(bf, sprite, 10, 20)\n"
      "local expected_hp = 10 + math.ceil((sprite.MaxHp - sprite.Hp) * 0.1)\n"
      "assert(prev.AddHp == expected_hp, 'prev.AddHp=' .. tostring(prev.AddHp))\n"
      "assert(prev.AddMp == 20, 'prev.AddMp=' .. tostring(prev.AddMp))\n"
      "assert(ret == string.format('%s,%s', expected_hp, 20), 'ret=' .. tostring(ret))\n";

  return dostring(L, harness, "@bcrun_battle_rest");
}

static int run_attacklogic_tempvalue_harness(lua_State *L)
{
  const char *harness =
      "local function count_keys(t)\n"
      "  local n = 0\n"
      "  for _ in pairs(t) do n = n + 1 end\n"
      "  return n\n"
      "end\n"
      "local function join_keys(t)\n"
      "  local out = {}\n"
      "  for k in pairs(t) do out[#out + 1] = tostring(k) end\n"
      "  table.sort(out)\n"
      "  return table.concat(out, ',')\n"
      "end\n"
      "local function assert_talent_flow(key, flow_name, expected_count)\n"
      "  local wf = BattleUtil.PrevWorkflows[key]\n"
      "  assert(type(wf) == 'table', key .. '=' .. type(wf) .. ' keys=' .. join_keys(BattleUtil.PrevWorkflows))\n"
      "  assert(wf.name == flow_name, key .. '.name=' .. tostring(wf.name))\n"
      "  assert(type(wf.talents) == 'table', key .. '.talents=' .. type(wf.talents))\n"
      "  assert(#wf.talents == expected_count, key .. '.talents.count=' .. tostring(#wf.talents))\n"
      "  assert(type(wf.callbacks) == 'table', key .. '.callbacks=' .. type(wf.callbacks))\n"
      "  for _, talent in ipairs(wf.talents) do\n"
      "    assert(type(wf.callbacks[talent]) == 'function', key .. '.callback[' .. tostring(talent) .. ']=' .. type(wf.callbacks[talent]))\n"
      "  end\n"
      "end\n"
      "assert(type(BattleUtil) == 'table', 'BattleUtil=' .. type(BattleUtil))\n"
      "assert(type(BattleUtil.PrevWorkflows) == 'table', 'PrevWorkflows=' .. type(BattleUtil.PrevWorkflows))\n"
      "assert(count_keys(BattleUtil.PrevWorkflows) > 0, 'PrevWorkflows empty')\n"
      "assert_talent_flow('extendTalents2_forAttackerTempValue_forTalent', 'extendTalents2_forAttackerTempValue', 2)\n"
      "assert_talent_flow('extendTalents2_forDefencerTempValue_forTalent', 'extendTalents2_forDefencerTempValue', 2)\n";

  return dostring(L, harness, "@bcrun_attacklogic_tempvalue");
}

static int run_attacklogic_tempvalue_chain_harness(lua_State *L)
{
  const char *harness =
      "local function list(items)\n"
      "  local t = { Count = #items, Length = #items, __items = items, __create_lua_table_ok = true }\n"
      "  for i = 1, #items do t[i - 1] = items[i] end\n"
      "  return t\n"
      "end\n"
      "local function zero_list() return list({}) end\n"
      "local function assert_nonempty(name, value)\n"
      "  assert(type(value) == 'table' and next(value) ~= nil, name .. '=' .. type(value))\n"
      "end\n"
      "local function filter_keys(t, needles)\n"
      "  local out = {}\n"
      "  if type(t) ~= 'table' then return '' end\n"
      "  for k, _ in pairs(t) do\n"
      "    local s = tostring(k)\n"
      "    for _, needle in ipairs(needles) do\n"
      "      if string.find(s, needle, 1, true) then out[#out + 1] = s break end\n"
      "    end\n"
      "  end\n"
      "  table.sort(out)\n"
      "  return table.concat(out, '|')\n"
      "end\n"
      "local function make_internal_skill(name)\n"
      "  return {\n"
      "    Name = name,\n"
      "    Level = 1,\n"
      "    IsUsed = true,\n"
      "    Talents = zero_list(),\n"
      "    UniqueSkills = zero_list(),\n"
      "    InternalSkill = { Triggers = zero_list(), UniqueSkills = zero_list() }\n"
      "  }\n"
      "end\n"
      "local function make_skill(name)\n"
      "  return {\n"
      "    Name = name,\n"
      "    name = name,\n"
      "    Level = 1,\n"
      "    IsUsed = true,\n"
      "    Talents = zero_list(),\n"
      "    UniqueSkills = zero_list(),\n"
      "    Type = 1,\n"
      "    Skill = {\n"
      "      Triggers = zero_list(),\n"
      "      UniqueSkills = zero_list(),\n"
      "      CastSize = 1,\n"
      "      CoverSize = 1,\n"
      "      GetCastSize = function() return 1 end,\n"
      "      GetCoverSize = function() return 1 end\n"
      "    },\n"
      "    SkillType = { GetHashCode = function() return 1 end },\n"
      "    IsAoyi = false,\n"
      "    IsUnique = false,\n"
      "    IsSpecial = false,\n"
      "    HitSelf = false,\n"
      "    CurrentCd = 0\n"
      "  }\n"
      "end\n"
      "local function make_role(name, key, talent_names, female)\n"
      "  local talent_map = {}\n"
      "  for _, talent in ipairs(talent_names) do talent_map[talent] = true end\n"
      "  local skill = make_skill('offline-skill')\n"
      "  local role = {\n"
      "    Name = name,\n"
      "    Key = key,\n"
      "    Level = 10,\n"
      "    level = 10,\n"
      "    Attributes = { female = female or 0, gengu = 120, wuxing = 120, shenfa = 120, bili = 120, dingli = 120 },\n"
      "    AttributesFinal = { gengu = 120, wuxing = 120, shenfa = 120, bili = 120, dingli = 120 },\n"
      "    Talents = list(talent_names),\n"
      "    EquipmentTalents = zero_list(),\n"
      "    InternalSkills = zero_list(),\n"
      "    Skills = list({ skill }),\n"
      "    SpecialSkills = zero_list(),\n"
      "    EquippedInternalSkill = make_internal_skill('offline-internal'),\n"
      "    HiddenWeapon = ''\n"
      "  }\n"
      "  function role:GetEquipment()\n"
      "    assert(self == role, 'GetEquipment self=' .. tostring(self))\n"
      "    return nil\n"
      "  end\n"
      "  function role:HasTalent(talent)\n"
      "    assert(self == role, 'HasTalent self=' .. tostring(self))\n"
      "    return talent_map[talent] or false\n"
      "  end\n"
      "  return role, skill\n"
      "end\n"
      "function LuaTool.CreateLuaTable(v)\n"
      "  assert(type(v) == 'table' and v.__create_lua_table_ok, 'CreateLuaTable arg=' .. type(v) .. ' value=' .. tostring(v))\n"
      "  local out = {}\n"
      "  for i = 1, #v.__items do out[i] = v.__items[i] end\n"
      "  return out\n"
      "end\n"
      "BattleFieldMonitor.getInstance = function()\n"
      "  return { helpResetAllSkills = function() return nil end, helpChangeAttribute = function() end, GetAddMaxMp = function() return 0 end }\n"
      "end\n"
      "RuntimeData.Instance = RuntimeData.Instance or {}\n"
      "RuntimeData.Instance.gameEngine = { CurrentSceneValue = '', battleType = '' }\n"
      "RuntimeData.Instance.GameMode = ''\n"
      "RuntimeData.Instance.Round = 1\n"
      "RuntimeData.Instance.isAttackAnalog = false\n"
      "BattleUtil.FlagLianji = BattleUtil.FlagLianji or {}\n"
      "BattleUtil.isFanji = false\n"
      "BattleUtil.DoDirectDamage = function() end\n"
      "local bf = { BattleTimestamp = 1, SpritesTable = {} }\n"
      "function bf:Log(msg)\n"
      "  assert(self == bf, 'bf.Log self=' .. tostring(self))\n"
      "  assert(type(msg) == 'string', 'bf.Log msg=' .. type(msg))\n"
      "end\n"
      "local atk_role, atk_skill = make_role('atk-role', 'atk-key', { '奇招怪式', '孤独求败' }, 0)\n"
      "local def_role = make_role('def-role', 'def-key', { '斗转星移', '心心相印' }, 1)\n"
      "local function make_sprite(role, team)\n"
      "  local sprite = {\n"
      "    ParentBattleField = bf,\n"
      "    Role = role,\n"
      "    Team = team,\n"
      "    Name = role.Name,\n"
      "    X = 3,\n"
      "    Y = team,\n"
      "    Hp = 100,\n"
      "    MaxHp = 100,\n"
      "    Mp = 50,\n"
      "    MaxMp = 50,\n"
      "    Balls = 0,\n"
      "    Sp = 0\n"
      "  }\n"
      "  function sprite:HasBuff()\n"
      "    assert(self == sprite, 'HasBuff self=' .. tostring(self))\n"
      "    return false\n"
      "  end\n"
      "  function sprite:DeleteBuff()\n"
      "    assert(self == sprite, 'DeleteBuff self=' .. tostring(self))\n"
      "  end\n"
      "  function sprite:GetBuff()\n"
      "    assert(self == sprite, 'GetBuff self=' .. tostring(self))\n"
      "    return nil\n"
      "  end\n"
      "  function sprite:Set_needRefresh()\n"
      "    assert(self == sprite, 'Set_needRefresh self=' .. tostring(self))\n"
      "  end\n"
      "  function sprite:SkillCdRecover()\n"
      "    assert(self == sprite, 'SkillCdRecover self=' .. tostring(self))\n"
      "  end\n"
      "  function sprite:AddBuff()\n"
      "    assert(self == sprite, 'AddBuff self=' .. tostring(self))\n"
      "  end\n"
      "  function sprite:AddBuffOnly2()\n"
      "    assert(self == sprite, 'AddBuffOnly2 self=' .. tostring(self))\n"
      "  end\n"
      "  function sprite:Say()\n"
      "    assert(self == sprite, 'Say self=' .. tostring(self))\n"
      "  end\n"
      "  return sprite\n"
      "end\n"
      "local sourceSprite = make_sprite(atk_role, 1)\n"
      "local targetSprite = make_sprite(def_role, 2)\n"
      "atk_role.Sprite = sourceSprite\n"
      "def_role.Sprite = targetSprite\n"
      "bf.SpritesTable = { sourceSprite, targetSprite }\n"
      "BattleUtil.RegisterPrevRole(bf, atk_role, sourceSprite)\n"
      "BattleUtil.RegisterPrevRole(bf, def_role, targetSprite)\n"
      "local prevRoleAtk = BattleUtil.PrevRoles[sourceSprite]\n"
      "local prevRoleDef = BattleUtil.PrevRoles[targetSprite]\n"
      "assert(type(prevRoleAtk) == 'table', 'prevRoleAtk=' .. type(prevRoleAtk))\n"
      "assert(type(prevRoleDef) == 'table', 'prevRoleDef=' .. type(prevRoleDef))\n"
      "assert(type(prevRoleAtk.workflows) == 'table', 'prevRoleAtk.workflows=' .. type(prevRoleAtk.workflows))\n"
      "assert(type(prevRoleDef.workflows) == 'table', 'prevRoleDef.workflows=' .. type(prevRoleDef.workflows))\n"
      "assert_nonempty('attacker_temp', prevRoleAtk.workflows.extendTalents2_forAttackerTempValue)\n"
      "assert_nonempty('defencer_temp', prevRoleDef.workflows.extendTalents2_forDefencerTempValue)\n"
      "assert(type(prevRoleAtk.workflows.extendTalents2_forAttacker) == 'table', 'attacker=' .. type(prevRoleAtk.workflows.extendTalents2_forAttacker))\n"
      "assert(type(prevRoleDef.workflows.extendTalents2_forDefencer) == 'table', 'defencer=' .. type(prevRoleDef.workflows.extendTalents2_forDefencer))\n"
      "if not (type(prevRoleAtk.workflows.extendTalents3_forAttacker) == 'table' and next(prevRoleAtk.workflows.extendTalents3_forAttacker) ~= nil) then\n"
      "  error('attacker3=' .. type(prevRoleAtk.workflows.extendTalents3_forAttacker) .. ' atk=' .. filter_keys(prevRoleAtk.workflows, {'extendTalents3'}) .. ' prev=' .. filter_keys(BattleUtil.PrevWorkflows or {}, {'extendTalents3', '奇招怪式', '心心相印'}))\n"
      "end\n"
      "if not (type(prevRoleDef.workflows.extendTalents3_forDefencer) == 'table' and next(prevRoleDef.workflows.extendTalents3_forDefencer) ~= nil) then\n"
      "  error('defencer3=' .. type(prevRoleDef.workflows.extendTalents3_forDefencer) .. ' def=' .. filter_keys(prevRoleDef.workflows, {'extendTalents3'}) .. ' prev=' .. filter_keys(BattleUtil.PrevWorkflows or {}, {'extendTalents3', '斗转星移', '心心相印'}))\n"
      "end\n"
      "if type(prevRoleAtk.workflows.extend_RoleValuesAtk) ~= 'table' then\n"
      "  error('rolevalues_atk=' .. type(prevRoleAtk.workflows.extend_RoleValuesAtk) .. ' atk=' .. filter_keys(prevRoleAtk.workflows, {'extend_RoleValues'}) .. ' prev=' .. filter_keys(BattleUtil.PrevWorkflows or {}, {'extend_RoleValues', '出奇制胜', '伏地蟆'}))\n"
      "end\n"
      "if type(prevRoleDef.workflows.extend_RoleValuesDef) ~= 'table' then\n"
      "  error('rolevalues_def=' .. type(prevRoleDef.workflows.extend_RoleValuesDef) .. ' def=' .. filter_keys(prevRoleDef.workflows, {'extend_RoleValues'}) .. ' prev=' .. filter_keys(BattleUtil.PrevWorkflows or {}, {'extend_RoleValues', '沾衣十八跌', '音律干扰', '神行百变', '逆运九阴'}))\n"
      "end\n"
      "atk_role.Level = 20\n"
      "atk_role.level = 20\n"
      "def_role.Level = 20\n"
      "def_role.level = 20\n"
      "atk_role.RoleValues = {}\n"
      "def_role.RoleValues = {}\n"
      "RuntimeData.Instance.GameMode = 'crazy'\n"
      "AttackLogic_RoleValues(atk_role, def_role, atk_skill)\n"
      "assert(type(atk_role.RoleValues.mingzhongValue) == 'number', 'mingzhongValue=' .. type(atk_role.RoleValues.mingzhongValue))\n"
      "assert(type(def_role.RoleValues.SubCriticalPercent) == 'number', 'SubCriticalPercent=' .. type(def_role.RoleValues.SubCriticalPercent))\n"
      "prevRoleAtk.workflows.extendTalents1_forAttacker = {}\n"
      "prevRoleAtk.workflows.extendTalents1_forAttackerReal = {}\n"
      "prevRoleDef.workflows.extendTalents1_forDefencer = {}\n"
      "prevRoleDef.workflows.extendTalents1_forDefencerReal = {}\n"
      "atk_skill.attackResult_Hp = 100\n"
      "atk_skill.Tiaohe = false\n"
      "atk_skill.Suit = 1\n"
      "RuntimeData.Instance.isAttackAnalog = false\n"
      "AttackLogic_extendTalents(sourceSprite, targetSprite, atk_skill, bf, {})\n"
      "assert(type(prevRoleAtk.MulDmg) == 'number', 'extendtalents_mul=' .. type(prevRoleAtk.MulDmg))\n";

  return dostring(L, harness, "@bcrun_attacklogic_tempvalue_chain");
}

int main(int argc, char **argv)
{
  const char *input = NULL;
  const char *mode = NULL;
  const char *extra_input = NULL;
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
      "BattleUtil = {\n"
      "  PrevWorkflows = {}, PrevTalents = {}, PrevTalentMap = {},\n"
      "  PrevRoleTalents = {}, PrevRoleTalentMap = {}\n"
      "}\n"
      "function BattleUtil.List2Map(l)\n"
      "  local ret = {}\n"
      "  for i, v in ipairs(l) do ret[v] = i end\n"
      "  return ret\n"
      "end\n"
      "function BattleUtil.RegisterTalent(talent, isRoleOnly)\n"
      "  local list = isRoleOnly and BattleUtil.PrevRoleTalents or BattleUtil.PrevTalents\n"
      "  local map = isRoleOnly and BattleUtil.PrevRoleTalentMap or BattleUtil.PrevTalentMap\n"
      "  if not map[talent] then\n"
      "    list[#list + 1] = talent\n"
      "    map[talent] = #list\n"
      "  end\n"
      "end\n"
      "function BattleUtil.RegisterWorkflowForTalent(flowName, talent, isRoleOnly, callback)\n"
      "  BattleUtil.RegisterTalent(talent, isRoleOnly)\n"
      "  local key = tostring(flowName) .. '_forTalent'\n"
      "  local wf = BattleUtil.PrevWorkflows[key]\n"
      "  if not wf then\n"
      "    wf = { flowType = 'talent', name = flowName, talentMap = {}, talents = {}, callbacks = {} }\n"
      "    BattleUtil.PrevWorkflows[key] = wf\n"
      "  end\n"
      "  if wf.talentMap[talent] then return end\n"
      "  wf.talentMap[talent] = true\n"
      "  wf.talents[#wf.talents + 1] = talent\n"
      "  wf.callbacks[talent] = callback\n"
      "end\n"
      "function BattleUtil.GetWorkflowForTalent(flowName)\n"
      "  return BattleUtil.PrevWorkflows[tostring(flowName) .. '_forTalent']\n"
      "end\n"
      "function BattleUtil.RegisterWorkflowForSkills(flowName, skills, callback, talent, isRoleOnly)\n"
      "  if talent ~= nil then BattleUtil.RegisterTalent(talent, isRoleOnly) end\n"
      "  local key = tostring(flowName) .. '_forSkill'\n"
      "  local wf = BattleUtil.PrevWorkflows[key]\n"
      "  if not wf then\n"
      "    wf = { flowType = 'skill', name = flowName, skills = {} }\n"
      "    BattleUtil.PrevWorkflows[key] = wf\n"
      "  end\n"
      "  for _, skillName in ipairs(skills) do\n"
      "    if not wf.skills[skillName] then wf.skills[skillName] = {} end\n"
      "    wf.skills[skillName][#wf.skills[skillName] + 1] = { callback = callback, talent = talent }\n"
      "  end\n"
      "end\n"
      "function BattleUtil.GetWorkflowForSkill(flowName, skillName)\n"
      "  local wf = BattleUtil.PrevWorkflows[tostring(flowName) .. '_forSkill']\n"
      "  return wf and wf.skills[skillName] or nil\n"
      "end\n"
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
  lua_State *L = NULL;
  int status = 0;

  if (argc != 2 && argc != 3 && argc != 4) {
    fprintf(stderr, "usage: %s <bytecode_file> [getrrevrole|registerprevrole|checktrigger|installyinjian|battle_rest|attacklogic_tempvalue|attacklogic_tempvalue_chain] [extra_bytecode_file]\n", argv[0]);
    return 2;
  }

  input = argv[1];
  mode = argc >= 3 ? argv[2] : NULL;
  if (argc == 4) extra_input = argv[3];

  L = luaL_newstate();
  if (!L) {
    fprintf(stderr, "luaL_newstate failed\n");
    return 1;
  }

  luaL_openlibs(L);
  if (dostring(L, prelude, "@bcrun_prelude")) {
    lua_close(L);
    return 1;
  }

  status = run_bytecode_file(L, input);
  if (status != 0 && mode == NULL) {
    lua_close(L);
    return 1;
  }
  if (extra_input != NULL) {
    status = run_bytecode_file(L, extra_input);
    if (status != 0 && mode == NULL) {
      lua_close(L);
      return 1;
    }
  }

  if (mode != NULL &&
      (strcmp(mode, "getrrevrole") == 0 ||
       strcmp(mode, "registerprevrole") == 0 ||
       strcmp(mode, "checktrigger") == 0 ||
       strcmp(mode, "installyinjian") == 0 ||
       strcmp(mode, "battle_rest") == 0 ||
       strcmp(mode, "attacklogic_tempvalue") == 0 ||
       strcmp(mode, "attacklogic_tempvalue_chain") == 0)) {
    if (strcmp(mode, "getrrevrole") == 0) {
      status = run_getrrevrole_harness(L);
    } else if (strcmp(mode, "registerprevrole") == 0) {
      status = run_registerprevrole_trigger_harness(L);
    } else if (strcmp(mode, "installyinjian") == 0) {
      status = run_installyinjian_harness(L);
    } else if (strcmp(mode, "battle_rest") == 0) {
      status = run_battle_rest_harness(L);
    } else if (strcmp(mode, "attacklogic_tempvalue_chain") == 0) {
      status = run_attacklogic_tempvalue_chain_harness(L);
    } else if (strcmp(mode, "attacklogic_tempvalue") == 0) {
      status = run_attacklogic_tempvalue_harness(L);
    } else {
      status = run_checktrigger_harness(L);
    }
    if (status == 0) printf("run ok: %s mode=%s\n", input, mode);
  } else if (status == 0) {
    printf("run ok: %s\n", input);
  }

  lua_close(L);
  return status == 0 ? 0 : 1;
}
