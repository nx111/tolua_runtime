/*
The MIT License (MIT)

Copyright (c) 2015-2016 topameng(topameng@qq.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <string.h>
#if !defined __APPLE__
#include <malloc.h>
#endif
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "luajit.h"
#include "tolua.h"
#if defined(LUAJIT_VERSION)
#include "lj_obj.h"
#include "lj_bc.h"
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <sys/time.h>
#endif

int toluaflags = FLAG_INDEX_ERROR;
static int tag = 0;  
static int gettag = 0;
static int settag = 0;
static int vptr = 1;
static char tolua_last_bytecode_debug[1024];
static int tolua_bytecode_build_logged = 0;
static const char *tolua_bytecode_build_tag = "arm64fr2-20260322-rootcall6";

static void tolua_emitlogv(const char *fmt, va_list argp)
{
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), fmt, argp);
#ifdef _WIN32
	OutputDebugStringA(buffer);
	OutputDebugStringA("\n");
#endif
	fprintf(stderr, "%s\n", buffer);
}

static void tolua_emitlog(const char *fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	tolua_emitlogv(fmt, argp);
	va_end(argp);
}

static void tolua_setbytecodedebugv(const char *fmt, va_list argp)
{
	vsnprintf(tolua_last_bytecode_debug, sizeof(tolua_last_bytecode_debug), fmt, argp);
#ifdef _WIN32
	OutputDebugStringA(tolua_last_bytecode_debug);
	OutputDebugStringA("\n");
#endif
	fprintf(stderr, "%s\n", tolua_last_bytecode_debug);
}

static void tolua_setbytecodedebug(const char *fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	tolua_setbytecodedebugv(fmt, argp);
	va_end(argp);
}

static void tolua_clearbytecodedebug(void)
{
	tolua_last_bytecode_debug[0] = '\0';
}

LUALIB_API const char* tolua_getlastbytecodedebug(void)
{
	return tolua_last_bytecode_debug;
}

LUALIB_API const char* tolua_getbytecodeerrorstr(int error_code)
{
	switch (error_code)
	{
		case TOLUA_BCCONV_OK:
			return "ok";
		case TOLUA_BCCONV_ERR_INVALID_ARGS:
			return "invalid_args";
		case TOLUA_BCCONV_ERR_OUT_OF_MEMORY:
			return "out_of_memory";
		case TOLUA_BCCONV_ERR_NOT_BYTECODE:
			return "not_bytecode";
		case TOLUA_BCCONV_ERR_UNSUPPORTED_VERSION:
			return "unsupported_version";
		case TOLUA_BCCONV_ERR_INVALID_FLAGS:
			return "invalid_flags";
		case TOLUA_BCCONV_ERR_SOURCE_FR2:
			return "source_fr2";
		case TOLUA_BCCONV_ERR_MALFORMED_CHUNK:
			return "malformed_chunk";
		case TOLUA_BCCONV_ERR_UNSUPPORTED_OPCODE:
			return "unsupported_opcode";
		case TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT:
			return "unsupported_layout";
		case TOLUA_BCCONV_ERR_REGISTER_OVERFLOW:
			return "register_overflow";
		case TOLUA_BCCONV_ERR_UNSUPPORTED_RUNTIME:
			return "unsupported_runtime";
		default:
			return "unknown_error";
	}
}

/*---------------------------tolua extend functions--------------------------------*/
LUALIB_API void* tolua_tag()
{
	return &tag;
}

LUALIB_API void tolua_newudata(lua_State *L, int val)
{
	int* pointer = (int*)lua_newuserdata(L, sizeof(int));    
    lua_pushvalue(L, TOLUA_NOPEER);            
    lua_setfenv(L, -2);                        
	*pointer = val;
}

LUALIB_API int tolua_rawnetobj(lua_State *L, int index)
{
    int* udata = (int*)lua_touserdata(L, index);
    
    if (udata != NULL) 
    {
        return *udata;
    }
    else if (lua_istable(L, index))
    {
        lua_pushvalue(L, index);        
        lua_pushlightuserdata(L, &vptr);
        lua_rawget(L, -2);                

        if (lua_isuserdata(L, -1))
        {
            lua_replace(L, index);
            udata = (int*)lua_touserdata(L, index);

            if (udata != NULL) 
            {
                return *udata;
            }
        }    
        else
        {
            lua_pop(L, 1);
        }               
    }    

	return -1;
}

LUALIB_API char* tolua_tocbuffer(const char *csBuffer, int sz)
{
	char* buffer = (char*)malloc(sz+1);
	memcpy(buffer, csBuffer, sz);	
	buffer[sz] = '\0';
	return buffer;
}

LUALIB_API void tolua_freebuffer(void* buffer)
{
  free(buffer);
}

LUALIB_API void tolua_getvec2(lua_State *L, int pos, float* x, float* y)
{
	lua_getref(L, LUA_RIDX_UNPACKVEC2);
	lua_pushvalue(L, pos);
	lua_call(L, 1, 2);
	*x = (float)lua_tonumber(L, -2);
	*y = (float)lua_tonumber(L, -1);
	lua_pop(L, 2);
}

LUALIB_API void tolua_getvec3(lua_State *L, int pos, float* x, float* y, float* z)
{
	lua_getref(L, LUA_RIDX_UNPACKVEC3);
	lua_pushvalue(L, pos);
	lua_call(L, 1, 3);
    *x = (float)lua_tonumber(L, -3);
    *y = (float)lua_tonumber(L, -2);
    *z = (float)lua_tonumber(L, -1);
    lua_pop(L, 3);
}

LUALIB_API void tolua_getvec4(lua_State *L, int pos, float* x, float* y, float* z, float* w)
{
	lua_getref(L, LUA_RIDX_UNPACKVEC4);
	lua_pushvalue(L, pos);
	lua_call(L, 1, 4);
	*x = (float)lua_tonumber(L, -4);
	*y = (float)lua_tonumber(L, -3);
	*z = (float)lua_tonumber(L, -2);
	*w = (float)lua_tonumber(L, -1);
	lua_pop(L, 4);
}

LUALIB_API void tolua_getquat(lua_State *L, int pos, float* x, float* y, float* z, float* w)
{
	lua_getref(L, LUA_RIDX_UNPACKQUAT);
	lua_pushvalue(L, pos);
	lua_call(L, 1, 4);
	*x = (float)lua_tonumber(L, -4);
	*y = (float)lua_tonumber(L, -3);
	*z = (float)lua_tonumber(L, -2);
	*w = (float)lua_tonumber(L, -1);
	lua_pop(L, 4);
}

LUALIB_API void tolua_getclr(lua_State *L, int pos, float* r, float* g, float* b, float* a)
{
	lua_getref(L, LUA_RIDX_UNPACKCLR);
	lua_pushvalue(L, pos);
	lua_call(L, 1, 4);
	*r = (float)lua_tonumber(L, -4);
	*g = (float)lua_tonumber(L, -3);
	*b = (float)lua_tonumber(L, -2);
	*a = (float)lua_tonumber(L, -1);
	lua_pop(L, 4);
}

LUALIB_API int tolua_getlayermask(lua_State *L, int pos)
{
    if (lua_isnumber(L, pos))
    {
        return (int)lua_tointeger(L, pos);
    }

    lua_getref(L, LUA_RIDX_UNPACKLAYERMASK);
    lua_pushvalue(L, pos);
    lua_call(L, 1, 1);
    int mask = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return mask;
}

LUALIB_API void tolua_pushvec2(lua_State *L, float x, float y)
{
	lua_getref(L, LUA_RIDX_PACKVEC2);
	lua_pushnumber(L, x);
	lua_pushnumber(L, y);
	lua_call(L, 2, 1);
}

LUALIB_API void tolua_pushvec3(lua_State *L, float x, float y, float z)
{
	lua_getref(L, LUA_RIDX_PACKVEC3);
	lua_pushnumber(L, x);
	lua_pushnumber(L, y);
	lua_pushnumber(L, z);
	lua_call(L, 3, 1);
}

LUALIB_API void tolua_pushvec4(lua_State *L, float x, float y, float z, float w)
{
	lua_getref(L, LUA_RIDX_PACKVEC4);
	lua_pushnumber(L, x);
	lua_pushnumber(L, y);
	lua_pushnumber(L, z);
	lua_pushnumber(L, w);
	lua_call(L, 4, 1);
}

LUALIB_API void tolua_pushquat(lua_State *L, float x, float y, float z, float w)
{
	lua_getref(L, LUA_RIDX_PACKQUAT);
	lua_pushnumber(L, x);
	lua_pushnumber(L, y);
	lua_pushnumber(L, z);
	lua_pushnumber(L, w);
	lua_call(L, 4, 1);
}

LUALIB_API void tolua_pushclr(lua_State *L, float r, float g, float b, float a)
{
	lua_getref(L, LUA_RIDX_PACKCLR);
	lua_pushnumber(L, r);
	lua_pushnumber(L, g);
	lua_pushnumber(L, b);
	lua_pushnumber(L, a);
	lua_call(L, 4, 1);
}

LUALIB_API void tolua_pushlayermask(lua_State *L, int mask)
{
    lua_getref(L, LUA_RIDX_PACKLAYERMASK);
    lua_pushnumber(L, mask);
    lua_call(L, 1, 1);
}


LUA_API const char* tolua_tolstring(lua_State *L, int index, int* len) 
{
    size_t sz;
    const char *ret = lua_tolstring(L, index, &sz);
    *len = (int)sz;
    return ret;
}

LUA_API void tolua_pushlstring(lua_State *L, const char *s, int l)
{
    lua_pushlstring(L, s, (size_t)l);
}

LUA_API void* tolua_newuserdata(lua_State *L, int sz)
{
    return lua_newuserdata(L, (size_t)sz);    
}

LUA_API int tolua_objlen(lua_State *L, int idx)
{
    size_t len = lua_objlen(L, idx);
    return (int)len;
}

LUA_API bool tolua_toboolean(lua_State *L, int idx) 
{
    int value = lua_toboolean(L, idx);
    return value == 0 ? false : true;
}

LUA_API int32_t tolua_tointeger(lua_State *L, int idx) 
{
    return (int32_t)lua_tointeger(L, idx);
}

#if defined(LUAJIT_VERSION)
#define TOLUA_BCDUMP_HEAD1 0x1b
#define TOLUA_BCDUMP_HEAD2 0x4c
#define TOLUA_BCDUMP_HEAD3 0x4a
#define TOLUA_BCDUMP_VERSION 2
#define TOLUA_BCDUMP_F_BE 0x01
#define TOLUA_BCDUMP_F_STRIP 0x02
#define TOLUA_BCDUMP_F_FR2 0x08

/* uLua (LuaJIT 2.0 based) -> standard LuaJIT 2.1 opcode mapping. */
static const uint8_t tolua_ulua_bc_map[] = {
  BC_ISLT, BC_ISGE, BC_ISLE, BC_ISGT, BC_ISEQV, BC_ISNEV, BC_ISEQS, BC_ISNES,
  BC_ISEQN, BC_ISNEN, BC_ISEQP, BC_ISNEP, BC_ISTC, BC_ISFC, BC_IST, BC_ISF,
  BC_MOV, BC_NOT, BC_UNM, BC_LEN,
  BC_ADDVN, BC_SUBVN, BC_MULVN, BC_DIVVN, BC_MODVN,
  BC_ADDNV, BC_SUBNV, BC_MULNV, BC_DIVNV, BC_MODNV,
  BC_ADDVV, BC_SUBVV, BC_MULVV, BC_DIVVV, BC_MODVV,
  BC_POW, BC_CAT, BC_KSTR, BC_KCDATA, BC_KSHORT, BC_KNUM, BC_KPRI, BC_KNIL,
  BC_UGET, BC_USETV, BC_USETS, BC_USETN, BC_USETP, BC_UCLO, BC_FNEW, BC_TNEW,
  BC_TDUP, BC_GGET, BC_GSET, BC_TGETV, BC_TGETS, BC_TGETB,
  BC_TSETV, BC_TSETS, BC_TSETB, BC_TSETM,
  BC_CALLM, BC_CALL, BC_CALLMT, BC_CALLT, BC_ITERC, BC_ITERN, BC_VARG, BC_ISNEXT,
  BC_RETM, BC_RET, BC_RET0, BC_RET1, BC_FORI, BC_JFORI, BC_FORL, BC_IFORL,
  BC_JFORL, BC_ITERL, BC_IITERL, BC_JITERL, BC_LOOP, BC_ILOOP, BC_JLOOP, BC_JMP,
  BC_FUNCF, BC_IFUNCF, BC_JFUNCF, BC_FUNCV, BC_IFUNCV, BC_JFUNCV, BC_FUNCC, BC_FUNCCW
};

#define TOLUA_BCNAME(name, ma, mb, mc, mm) #name,
static const char *const tolua_bc_names[] = { BCDEF(TOLUA_BCNAME) };
#undef TOLUA_BCNAME

typedef struct tolua_bcdebug_ctx {
  const char *chunk_name;
  size_t chunk_name_len;
  uint32_t proto_index;
  uint8_t proto_flags;
} tolua_bcdebug_ctx;

#define TOLUA_BCCONV_INTERNAL_INSERT_COPY 1001
#define TOLUA_MAX_INSERT_COPIES 255

typedef struct tolua_insert_copy_request {
  int active;
  uint32_t proto_index;
  uint32_t insert_pc;
  uint8_t copy_count;
  uint8_t allow_target_entry;
  BCReg copy_dst[TOLUA_MAX_INSERT_COPIES];
  BCReg copy_src[TOLUA_MAX_INSERT_COPIES];
} tolua_insert_copy_request;

typedef struct tolua_call_repack_tracker {
  int active;
  uint32_t numbc;
  uint8_t *seen;
  BCReg *original_base;
} tolua_call_repack_tracker;

typedef struct tolua_proto_layout {
  size_t len_field_pos;
  size_t len_field_size;
  size_t body_pos;
  size_t header_end;
  size_t bc_pos;
  size_t bc_end;
  size_t uvkgc_pos;
  size_t debug_pos;
  size_t end;
  size_t framesize_pos;
  uint8_t flags;
  uint8_t numparams;
  uint8_t framesize;
  uint8_t sizeuv;
  uint32_t sizekgc;
  uint32_t sizekn;
  uint32_t numbc;
  uint32_t sizedbg;
  uint32_t firstline;
  uint32_t numline;
} tolua_proto_layout;

static tolua_insert_copy_request tolua_pending_insert_copy;
static tolua_call_repack_tracker tolua_call_repack_state;

static void tolua_clear_pending_insert_copy(void)
{
  memset(&tolua_pending_insert_copy, 0, sizeof(tolua_pending_insert_copy));
}

static void tolua_clear_call_repack_state(void)
{
  free(tolua_call_repack_state.original_base);
  free(tolua_call_repack_state.seen);
  memset(&tolua_call_repack_state, 0, sizeof(tolua_call_repack_state));
}

static int tolua_init_call_repack_state(uint32_t numbc)
{
  tolua_clear_call_repack_state();
  tolua_call_repack_state.seen = (uint8_t *)calloc((size_t)numbc, 1);
  tolua_call_repack_state.original_base = (BCReg *)calloc((size_t)numbc, sizeof(BCReg));
  if (!tolua_call_repack_state.seen || !tolua_call_repack_state.original_base) {
    tolua_clear_call_repack_state();
    return 0;
  }
  tolua_call_repack_state.active = 1;
  tolua_call_repack_state.numbc = numbc;
  return 1;
}

static int tolua_schedule_insert_copies(const tolua_bcdebug_ctx *ctx, uint32_t pc, uint32_t insert_pc,
                                        const BCReg *copy_dst, const BCReg *copy_src, uint8_t copy_count)
{
  uint8_t i = 0;

  if (tolua_pending_insert_copy.active) return 0;
  if (copy_count == 0 || copy_count > TOLUA_MAX_INSERT_COPIES) return 0;

  tolua_pending_insert_copy.active = 1;
  tolua_pending_insert_copy.proto_index = (ctx != NULL) ? ctx->proto_index : 0u;
  tolua_pending_insert_copy.insert_pc = insert_pc;
  tolua_pending_insert_copy.copy_count = copy_count;
  tolua_pending_insert_copy.allow_target_entry = 0;
  for (i = 0; i < copy_count; i++) {
    tolua_pending_insert_copy.copy_dst[i] = copy_dst[i];
    tolua_pending_insert_copy.copy_src[i] = copy_src[i];
  }
  for (; i < TOLUA_MAX_INSERT_COPIES; i++) {
    tolua_pending_insert_copy.copy_dst[i] = 0;
    tolua_pending_insert_copy.copy_src[i] = 0;
  }
  (void)pc;
  return 1;
}

#ifdef TOLUA_REPACK_DEBUG
#define TOLUA_REPACK_LOG(ctx, pc, fmt, ...) \
  fprintf(stderr, "[repack] proto=%u pc=%u " fmt "\n", \
          (unsigned int)((ctx) != NULL ? (ctx)->proto_index : 0u), \
          (unsigned int)(pc), ##__VA_ARGS__)
#else
#define TOLUA_REPACK_LOG(ctx, pc, fmt, ...) ((void)0)
#endif

static const char *tolua_bc_opname(BCOp op)
{
  return (op < BC__MAX) ? tolua_bc_names[op] : "UNKNOWN";
}

static int tolua_failbytecode(int error_code, const char *fmt, ...)
{
  va_list argp;
  char detail[512];

  va_start(argp, fmt);
  vsnprintf(detail, sizeof(detail), fmt, argp);
  va_end(argp);
  tolua_setbytecodedebug("bytecode conversion failed (%s): %s",
                         tolua_getbytecodeerrorstr(error_code), detail);
  return error_code;
}

static int tolua_failbytecodeproto(const tolua_bcdebug_ctx *ctx, uint32_t pc,
                                   BCIns ins, BCOp op, int error_code,
                                   const char *fmt, ...)
{
  va_list argp;
  char detail[384];

  va_start(argp, fmt);
  vsnprintf(detail, sizeof(detail), fmt, argp);
  va_end(argp);

  tolua_setbytecodedebug(
      "bytecode conversion failed (%s): %s [chunk=%.*s, proto=%u, pc=%u, op=%s, raw=0x%08x, a=%u, b=%u, c=%u, d=%u]",
      tolua_getbytecodeerrorstr(error_code),
      detail,
      (int)((ctx != NULL && ctx->chunk_name != NULL) ? ctx->chunk_name_len : 10),
      (ctx != NULL && ctx->chunk_name != NULL) ? ctx->chunk_name : "<stripped>",
      (ctx != NULL) ? ctx->proto_index : 0u,
      pc,
      tolua_bc_opname(op),
      (unsigned int)ins,
      (unsigned int)bc_a(ins),
      (unsigned int)bc_b(ins),
      (unsigned int)bc_c(ins),
      (unsigned int)bc_d(ins));
  return error_code;
}

static int tolua_read_uleb128(const uint8_t *buf, size_t len, size_t *pos, uint32_t *out)
{
  uint32_t v = 0;
  uint32_t shift = 0;

  while (*pos < len) {
    uint8_t b = buf[(*pos)++];
    v |= (uint32_t)(b & 0x7f) << shift;
    if ((b & 0x80) == 0) {
      *out = v;
      return 1;
    }
    shift += 7;
    if (shift > 28) return 0;
  }
  return 0;
}

static size_t tolua_uleb128_size(uint32_t v)
{
  size_t n = 1;

  while (v >= 0x80) {
    v >>= 7;
    n++;
  }

  return n;
}

static uint8_t *tolua_write_uleb128(uint8_t *dst, uint32_t v)
{
  do {
    uint8_t b = (uint8_t)(v & 0x7f);
    v >>= 7;
    if (v != 0) b = (uint8_t)(b | 0x80);
    *dst++ = b;
  } while (v != 0);

  return dst;
}

typedef struct tolua_bcshift_map {
  uint8_t hole[BCMAX_A + 1];
  uint16_t prefix[BCMAX_A + 1];
} tolua_bcshift_map;

static uint32_t tolua_read_ins(const uint8_t *buf, int be)
{
  if (be) {
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) |
           (uint32_t)buf[3];
  }

  return ((uint32_t)buf[0]) |
         ((uint32_t)buf[1] << 8) |
         ((uint32_t)buf[2] << 16) |
         ((uint32_t)buf[3] << 24);
}

static void tolua_write_ins(uint8_t *buf, uint32_t ins, int be)
{
  if (be) {
    buf[0] = (uint8_t)(ins >> 24);
    buf[1] = (uint8_t)(ins >> 16);
    buf[2] = (uint8_t)(ins >> 8);
    buf[3] = (uint8_t)ins;
    return;
  }

  buf[0] = (uint8_t)ins;
  buf[1] = (uint8_t)(ins >> 8);
  buf[2] = (uint8_t)(ins >> 16);
  buf[3] = (uint8_t)(ins >> 24);
}

static int tolua_is_reg_mode(BCMode mode)
{
  return mode == BCMdst || mode == BCMbase || mode == BCMvar || mode == BCMrbase;
}

static int tolua_op_a_is_reg(BCOp op, BCMode mode)
{
  if (tolua_is_reg_mode(mode)) return 1;
  switch (op) {
    case BC_IST:
    case BC_ISF:
    case BC_ISTC:
    case BC_ISFC:
      return 1;
    default:
      return 0;
  }
}

static int tolua_op_a_is_source_reg(BCOp op, BCMode mode)
{
  if (mode != BCMdst && tolua_is_reg_mode(mode)) return 1;
  switch (op) {
    case BC_IST:
    case BC_ISF:
    case BC_ISTC:
    case BC_ISFC:
      return 1;
    default:
      return 0;
  }
}

static void tolua_build_shift_map(tolua_bcshift_map *map)
{
  uint16_t count = 0;
  int i = 0;

  for (i = 0; i <= BCMAX_A; i++) {
    map->prefix[i] = count;
    if (map->hole[i]) count++;
  }
}

static int tolua_map_reg(const tolua_bcshift_map *map, BCReg reg, BCReg *out)
{
  uint32_t mapped = (uint32_t)reg + (uint32_t)map->prefix[reg];
  if (mapped > BCMAX_A) return 0;
  *out = (BCReg)mapped;
  return 1;
}

static int tolua_find_range_hole(const tolua_bcshift_map *map, int first, int last, int *hole_reg)
{
  int reg = 0;

  if (last <= first) return 0;
  if (first < 0) first = 0;
  if (last > BCMAX_A + 1) last = BCMAX_A + 1;

  for (reg = first; reg < last; reg++) {
    if (map->hole[reg]) {
      if (hole_reg) *hole_reg = reg;
      return 1;
    }
  }

  return 0;
}

static int tolua_find_closed_range_hole(const tolua_bcshift_map *map, int first, int last, int *hole_reg)
{
  if (last < first) return 0;
  return tolua_find_range_hole(map, first, last + 1, hole_reg);
}

static int tolua_update_framesize_checked(uint8_t *framesize_io, BCReg new_last,
                                          const tolua_bcdebug_ctx *ctx, uint32_t pc,
                                          BCIns ins, BCOp op)
{
  uint32_t new_framesize = (uint32_t)new_last + 1u;

  if (new_framesize > BCMAX_A) {
    return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                   "repack frame size %u exceeds register limit",
                                   (unsigned int)new_framesize);
  }
  if (new_framesize > (uint32_t)*framesize_io) {
    *framesize_io = (uint8_t)new_framesize;
  }
  return TOLUA_BCCONV_OK;
}

static int tolua_reg_in_closed_range(BCReg reg, BCReg first, BCReg last)
{
  return first <= last && reg >= first && reg <= last;
}

static int tolua_op_is_simple_repack_def(BCOp op)
{
  switch (op) {
    case BC_MOV:
    case BC_NOT:
    case BC_UNM:
    case BC_LEN:
    case BC_ADDVN:
    case BC_SUBVN:
    case BC_MULVN:
    case BC_DIVVN:
    case BC_MODVN:
    case BC_ADDNV:
    case BC_SUBNV:
    case BC_MULNV:
    case BC_DIVNV:
    case BC_MODNV:
    case BC_ADDVV:
    case BC_SUBVV:
    case BC_MULVV:
    case BC_DIVVV:
    case BC_MODVV:
    case BC_POW:
    case BC_CAT:
    case BC_KSTR:
    case BC_KCDATA:
    case BC_KSHORT:
    case BC_KNUM:
    case BC_KPRI:
    case BC_UGET:
    case BC_FNEW:
    case BC_TNEW:
    case BC_TDUP:
    case BC_GGET:
    case BC_TGETV:
    case BC_TGETS:
    case BC_TGETB:
    case BC_TGETR:
      return 1;
    default:
      return 0;
  }
}

static int tolua_get_simple_repack_def_reg(BCOp op, BCIns ins, BCReg *out)
{
  if (!tolua_op_is_simple_repack_def(op)) return 0;
  *out = bc_a(ins);
  return 1;
}

static int tolua_get_slice_def_reg(BCOp op, BCIns ins, BCReg old_first, BCReg old_last, BCReg *out)
{
  if (tolua_get_simple_repack_def_reg(op, ins, out)) return 1;

  switch (op) {
    case BC_CALL: {
      BCReg a = bc_a(ins);
      BCReg last = (BCReg)(a + bc_c(ins) - 1);

      if (bc_b(ins) != 2) return 0;
      if (a != old_first) return 0;
      if (!tolua_reg_in_closed_range(a, old_first, old_last)) return 0;
      if (!tolua_reg_in_closed_range(last, old_first, old_last)) return 0;
      *out = a;
      return 1;
    }
    default:
      return 0;
  }
}

static int tolua_ins_reads_reg(BCOp op, BCIns ins, BCReg reg)
{
  BCReg a = bc_a(ins);

  switch (op) {
    case BC_CALL:
    case BC_CALLM:
      return tolua_reg_in_closed_range(reg, a, (BCReg)(a + bc_c(ins) - 1));
    case BC_CALLT:
    case BC_CALLMT:
      return tolua_reg_in_closed_range(reg, a, (BCReg)(a + bc_d(ins) - 1));
    case BC_ITERC:
    case BC_ITERN:
      return tolua_reg_in_closed_range(reg, a, (BCReg)(a + bc_c(ins) - 1));
    case BC_RET:
    case BC_RETM:
      return bc_d(ins) >= 2 && tolua_reg_in_closed_range(reg, a, (BCReg)(a + bc_d(ins) - 2));
    case BC_CAT:
      return tolua_reg_in_closed_range(reg, bc_b(ins), bc_c(ins));
    case BC_FORI:
    case BC_JFORI:
    case BC_FORL:
    case BC_IFORL:
    case BC_JFORL:
    case BC_ITERL:
    case BC_IITERL:
    case BC_JITERL:
      return tolua_reg_in_closed_range(reg, a, (BCReg)(a + 3));
    case BC_VARG:
    case BC_ISNEXT:
    case BC_JMP:
    case BC_UCLO:
      return 0;
    default:
      break;
  }

  if (bcmode_hasd(op)) {
    BCMode mode = bcmode_d(op);
    if (tolua_is_reg_mode(mode) && bc_d(ins) == reg) return 1;
  } else {
    BCMode mode = bcmode_b(op);
    if (tolua_is_reg_mode(mode) && bc_b(ins) == reg) return 1;
    mode = bcmode_c(op);
    if (tolua_is_reg_mode(mode) && bc_c(ins) == reg) return 1;
  }

  {
    BCMode mode = bcmode_a(op);
    if (tolua_op_a_is_source_reg(op, mode) && a == reg) return 1;
  }

  return 0;
}

static int tolua_ins_writes_reg(BCOp op, BCIns ins, BCReg reg)
{
  BCReg a = bc_a(ins);

  switch (op) {
    case BC_CALL:
    case BC_CALLM:
    case BC_ITERC:
    case BC_ITERN:
    case BC_VARG:
      return bc_b(ins) >= 2 && tolua_reg_in_closed_range(reg, a, (BCReg)(a + bc_b(ins) - 2));
    case BC_FORI:
    case BC_JFORI:
    case BC_FORL:
    case BC_IFORL:
    case BC_JFORL:
    case BC_ISNEXT:
    case BC_ITERL:
    case BC_IITERL:
    case BC_JITERL:
      return tolua_reg_in_closed_range(reg, a, (BCReg)(a + 3));
    case BC_KNIL:
      return tolua_reg_in_closed_range(reg, a, bc_d(ins));
    default:
      break;
  }

  return bcmode_a(op) == BCMdst && a == reg;
}

static void tolua_repack_remap_reg_range(BCIns *ins, BCOp op, BCReg old_first, BCReg old_last, BCReg new_first)
{
  BCMode mode = bcmode_a(op);
  BCReg reg = 0;

  if (tolua_op_a_is_reg(op, mode)) {
    reg = bc_a(*ins);
    if (tolua_reg_in_closed_range(reg, old_first, old_last)) {
      setbc_a(ins, (BCReg)(new_first + (reg - old_first)));
    }
  }

  if (bcmode_hasd(op)) {
    mode = bcmode_d(op);
    if (tolua_is_reg_mode(mode)) {
      reg = bc_d(*ins);
      if (tolua_reg_in_closed_range(reg, old_first, old_last)) {
        setbc_d(ins, (BCReg)(new_first + (reg - old_first)));
      }
    }
  } else {
    mode = bcmode_b(op);
    if (tolua_is_reg_mode(mode)) {
      reg = bc_b(*ins);
      if (tolua_reg_in_closed_range(reg, old_first, old_last)) {
        setbc_b(ins, (BCReg)(new_first + (reg - old_first)));
      }
    }

    mode = bcmode_c(op);
    if (tolua_is_reg_mode(mode)) {
      reg = bc_c(*ins);
      if (tolua_reg_in_closed_range(reg, old_first, old_last)) {
        setbc_c(ins, (BCReg)(new_first + (reg - old_first)));
      }
    }
  }
}

static void tolua_sync_open_tsetm_after_repack(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                               uint32_t pc, BCIns ins,
                                               BCReg old_first, BCReg old_last, BCReg new_first)
{
  BCOp op = bc_op(ins);

  (void)old_first;
  (void)old_last;
  (void)new_first;

  if (pc + 1 >= numbc) return;
  if (!((op == BC_CALL || op == BC_VARG) && bc_b(ins) == 0)) return;

  {
    uint8_t *next_slot = buf + bc_pos + (size_t)(pc + 1) * 4;
    BCIns next = (BCIns)tolua_read_ins(next_slot, be);

    if (bc_op(next) != BC_TSETM) return;
#ifdef TOLUA_REPACK_DEBUG
    fprintf(stderr, "[repack] pc=%u sync tsetm call_a=%u old_tsetm_a=%u\n",
            (unsigned int)pc, (unsigned int)bc_a(ins), (unsigned int)bc_a(next));
#endif
    setbc_a(&next, bc_a(ins));
    tolua_write_ins(next_slot, (uint32_t)next, be);
  }
}

#ifdef TOLUA_REPACK_DEBUG
static void tolua_debug_log_open_tsetm_pairs(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                             const char *tag, uint32_t owner_pc)
{
  uint32_t scan = 0;

  for (scan = 0; scan + 1 < numbc; scan++) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCIns next = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(scan + 1) * 4, be);
    BCOp op = bc_op(ins);

    if (!((op == BC_CALL || op == BC_VARG) && bc_b(ins) == 0)) continue;
    if (bc_op(next) != BC_TSETM) continue;
    if (bc_a(ins) != bc_a(next)) {
      fprintf(stderr,
              "[repack] owner_pc=%u %s mismatch pair_pc=%u call_a=%u tsetm_a=%u raw_call=0x%08x raw_tsetm=0x%08x\n",
              (unsigned int)owner_pc, tag, (unsigned int)(scan + 1),
              (unsigned int)bc_a(ins), (unsigned int)bc_a(next),
              (unsigned int)ins, (unsigned int)next);
      break;
    }
  }
}
#endif

static int tolua_rewrite_ins_source_reg(BCIns *ins, BCOp op, BCReg old_reg, BCReg new_reg)
{
  int changed = 0;

  switch (op) {
    case BC_CALL:
    case BC_CALLM:
    case BC_CALLT:
    case BC_CALLMT:
    case BC_ITERC:
    case BC_ITERN:
    case BC_VARG:
    case BC_FORI:
    case BC_JFORI:
    case BC_FORL:
    case BC_IFORL:
    case BC_JFORL:
    case BC_ITERL:
    case BC_IITERL:
    case BC_JITERL:
    case BC_LOOP:
    case BC_ILOOP:
    case BC_JLOOP:
      return 0;
    case BC_RET:
    case BC_RETM:
      if (bc_d(*ins) == 2 && bc_a(*ins) == old_reg) {
        setbc_a(ins, new_reg);
        return 1;
      }
      return 0;
    case BC_CAT:
      if (bc_b(*ins) == old_reg) {
        setbc_b(ins, new_reg);
        changed = 1;
      }
      if (bc_c(*ins) == old_reg) {
        setbc_c(ins, new_reg);
        changed = 1;
      }
      return changed;
    default:
      break;
  }

  if (bcmode_hasd(op)) {
    BCMode mode = bcmode_d(op);
    if (tolua_is_reg_mode(mode) && bc_d(*ins) == old_reg) {
      setbc_d(ins, new_reg);
      changed = 1;
    }
  } else {
    BCMode mode = bcmode_b(op);
    if (tolua_is_reg_mode(mode) && bc_b(*ins) == old_reg) {
      setbc_b(ins, new_reg);
      changed = 1;
    }
    mode = bcmode_c(op);
    if (tolua_is_reg_mode(mode) && bc_c(*ins) == old_reg) {
      setbc_c(ins, new_reg);
      changed = 1;
    }
  }

  {
    BCMode mode = bcmode_a(op);
    if (tolua_op_a_is_source_reg(op, mode) && bc_a(*ins) == old_reg) {
      setbc_a(ins, new_reg);
      changed = 1;
    }
  }

  return changed;
}

static int tolua_can_rewrite_ins_source_reg(BCIns ins, BCOp op, BCReg old_reg)
{
  BCReg probe_reg = old_reg < BCMAX_A ? (BCReg)(old_reg + 1) : (BCReg)(old_reg - 1);
  return probe_reg != old_reg && tolua_rewrite_ins_source_reg(&ins, op, old_reg, probe_reg);
}

static int tolua_get_jump_target(BCOp op, BCIns ins, uint32_t pc, uint32_t numbc, uint32_t *out_target)
{
  ptrdiff_t target = 0;

  switch (op) {
    case BC_UCLO:
    case BC_JMP:
    case BC_FORI:
    case BC_JFORI:
    case BC_FORL:
    case BC_IFORL:
    case BC_JFORL:
    case BC_ITERL:
    case BC_IITERL:
    case BC_JITERL:
    case BC_LOOP:
    case BC_ILOOP:
    case BC_JLOOP:
      target = (ptrdiff_t)pc + 1 + bc_j(ins);
      if (target < 0 || (uint32_t)target >= numbc) return 0;
      *out_target = (uint32_t)target;
      return 1;
    default:
      return 0;
  }
}

static int tolua_fill_successors(BCOp op, BCIns ins, uint32_t pc, uint32_t numbc, uint32_t succ[2])
{
  int count = 0;
  uint32_t target = 0;

  switch (op) {
    case BC_RET:
    case BC_RETM:
    case BC_RET0:
    case BC_RET1:
    case BC_CALLT:
    case BC_CALLMT:
      return 0;
    case BC_UCLO:
    case BC_JMP:
      if (tolua_get_jump_target(op, ins, pc, numbc, &target)) succ[count++] = target;
      return count;
    default:
      break;
  }

  if (pc + 1 < numbc) succ[count++] = pc + 1;
  if (tolua_get_jump_target(op, ins, pc, numbc, &target)) {
    if (count == 0 || succ[0] != target) succ[count++] = target;
  }
  return count;
}

static int tolua_parse_proto_layout(const uint8_t *buf, size_t len, size_t len_field_pos,
                                    size_t body_pos, size_t proto_end, int strip,
                                    tolua_proto_layout *layout)
{
  size_t p = body_pos;
  uint32_t sizekgc = 0;
  uint32_t sizekn = 0;
  uint32_t numbc = 0;
  uint32_t sizedbg = 0;
  uint32_t firstline = 0;
  uint32_t numline = 0;

  memset(layout, 0, sizeof(*layout));
  layout->len_field_pos = len_field_pos;
  layout->len_field_size = body_pos - len_field_pos;
  layout->body_pos = body_pos;
  layout->end = proto_end;

  if (p + 4 > proto_end) return 0;
  layout->flags = buf[p++];
  layout->numparams = buf[p++];
  layout->framesize_pos = body_pos + 2;
  layout->framesize = buf[p++];
  layout->sizeuv = buf[p++];

  if (!tolua_read_uleb128(buf, len, &p, &sizekgc) ||
      !tolua_read_uleb128(buf, len, &p, &sizekn) ||
      !tolua_read_uleb128(buf, len, &p, &numbc)) {
    return 0;
  }

  if (!strip) {
    if (!tolua_read_uleb128(buf, len, &p, &sizedbg)) return 0;
    if (sizedbg) {
      if (!tolua_read_uleb128(buf, len, &p, &firstline) ||
          !tolua_read_uleb128(buf, len, &p, &numline)) {
        return 0;
      }
    }
  }

  layout->sizekgc = sizekgc;
  layout->sizekn = sizekn;
  layout->numbc = numbc;
  layout->sizedbg = sizedbg;
  layout->firstline = firstline;
  layout->numline = numline;
  layout->header_end = p;
  layout->bc_pos = p;
  layout->bc_end = p + (size_t)numbc * 4;
  if (layout->bc_end > proto_end) return 0;
  if ((size_t)sizedbg > proto_end - layout->bc_end) return 0;
  layout->uvkgc_pos = layout->bc_end;
  layout->debug_pos = proto_end - (size_t)sizedbg;
  return 1;
}

static uint8_t *tolua_rebuild_chunk_with_insert_copy(const uint8_t *buf, size_t len,
                                                     size_t *out_len, int *out_status)
{
  size_t pos = 0;
  size_t chunk_start = 0;
  size_t target_proto_len_pos = 0;
  size_t target_proto_body_pos = 0;
  size_t target_proto_end = 0;
  size_t new_len = 0;
  size_t before_len = 0;
  size_t after_len = 0;
  size_t tail_len = 0;
  size_t old_proto_payload = 0;
  size_t old_proto_total = 0;
  size_t new_header_len = 0;
  size_t new_proto_payload = 0;
  size_t new_proto_total = 0;
  uint32_t flags = 0;
  uint32_t name_len = 0;
  int be = 0;
  int strip = 0;
  uint32_t insert_count = 0;
  uint8_t *out = NULL;
  uint8_t *dst = NULL;
  tolua_proto_layout layout;

  *out_len = 0;
  *out_status = TOLUA_BCCONV_OK;

  if (!tolua_pending_insert_copy.active) {
    *out_status = TOLUA_BCCONV_ERR_INVALID_ARGS;
    return NULL;
  }
  if (len < 5) {
    *out_status = TOLUA_BCCONV_ERR_MALFORMED_CHUNK;
    return NULL;
  }

  pos = 4;
  if (!tolua_read_uleb128(buf, len, &pos, &flags)) {
    *out_status = TOLUA_BCCONV_ERR_MALFORMED_CHUNK;
    return NULL;
  }
  be = (flags & TOLUA_BCDUMP_F_BE) ? 1 : 0;
  strip = (flags & TOLUA_BCDUMP_F_STRIP) ? 1 : 0;
  if (!strip) {
    if (!tolua_read_uleb128(buf, len, &pos, &name_len) ||
        (size_t)name_len > len - pos) {
      *out_status = TOLUA_BCCONV_ERR_MALFORMED_CHUNK;
      return NULL;
    }
    pos += (size_t)name_len;
  }
  chunk_start = pos;

  for (uint32_t proto_index = 0;; proto_index++) {
    size_t len_pos = pos;
    uint32_t proto_len = 0;

    if (!tolua_read_uleb128(buf, len, &pos, &proto_len)) {
      *out_status = TOLUA_BCCONV_ERR_MALFORMED_CHUNK;
      return NULL;
    }
    if (proto_len == 0) break;
    if ((size_t)proto_len > len - pos) {
      *out_status = TOLUA_BCCONV_ERR_MALFORMED_CHUNK;
      return NULL;
    }
    if (proto_index == tolua_pending_insert_copy.proto_index) {
      target_proto_len_pos = len_pos;
      target_proto_body_pos = pos;
      target_proto_end = pos + (size_t)proto_len;
      break;
    }
    pos += (size_t)proto_len;
  }

  if (target_proto_body_pos == 0 ||
      !tolua_parse_proto_layout(buf, len, target_proto_len_pos, target_proto_body_pos,
                                target_proto_end, strip, &layout)) {
    *out_status = TOLUA_BCCONV_ERR_MALFORMED_CHUNK;
    return NULL;
  }
  if (tolua_pending_insert_copy.insert_pc > layout.numbc) {
    *out_status = TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT;
    return NULL;
  }
  insert_count = tolua_pending_insert_copy.copy_count;
  if (insert_count == 0 || insert_count > TOLUA_MAX_INSERT_COPIES) {
    *out_status = TOLUA_BCCONV_ERR_INVALID_ARGS;
    return NULL;
  }

  before_len = target_proto_len_pos;
  after_len = len - target_proto_end;
  tail_len = layout.debug_pos - layout.bc_end;
  old_proto_payload = layout.end - layout.body_pos;
  old_proto_total = layout.len_field_size + old_proto_payload;
  new_header_len = 4 +
                   tolua_uleb128_size(layout.sizekgc) +
                   tolua_uleb128_size(layout.sizekn) +
                   tolua_uleb128_size(layout.numbc + insert_count) +
                   (strip ? 0 : tolua_uleb128_size(0));
  new_proto_payload = new_header_len + ((size_t)layout.numbc + insert_count) * 4 + tail_len;
  new_proto_total = tolua_uleb128_size((uint32_t)new_proto_payload) + new_proto_payload;
  if (new_proto_total >= old_proto_total) {
    new_len = len + (new_proto_total - old_proto_total);
  } else {
    new_len = len - (old_proto_total - new_proto_total);
  }

#ifdef TOLUA_REPACK_DEBUG
  fprintf(stderr,
          "[rebuild] proto=%u insert_pc=%u old_proto_total=%u new_proto_total=%u old_payload=%u new_payload=%u debug_drop=%u\n",
          (unsigned int)tolua_pending_insert_copy.proto_index,
          (unsigned int)tolua_pending_insert_copy.insert_pc,
          (unsigned int)old_proto_total, (unsigned int)new_proto_total,
          (unsigned int)old_proto_payload, (unsigned int)new_proto_payload,
          (unsigned int)layout.sizedbg);
#endif

  out = (uint8_t *)malloc(new_len);
  if (!out) {
    *out_status = TOLUA_BCCONV_ERR_OUT_OF_MEMORY;
    return NULL;
  }

  memcpy(out, buf, before_len);
  dst = out + before_len;
  dst = tolua_write_uleb128(dst, (uint32_t)new_proto_payload);
  *dst++ = layout.flags;
  *dst++ = layout.numparams;
  *dst++ = layout.framesize;
  *dst++ = layout.sizeuv;
  dst = tolua_write_uleb128(dst, layout.sizekgc);
  dst = tolua_write_uleb128(dst, layout.sizekn);
  dst = tolua_write_uleb128(dst, layout.numbc + insert_count);
  if (!strip) dst = tolua_write_uleb128(dst, 0);

  {
    BCIns *new_bc = NULL;
    uint32_t old_pc = 0;
    uint32_t insert_index = 0;

    new_bc = (BCIns *)calloc((size_t)layout.numbc + insert_count, sizeof(BCIns));
    if (!new_bc) {
      free(out);
      *out_status = TOLUA_BCCONV_ERR_OUT_OF_MEMORY;
      return NULL;
    }

    for (old_pc = 0; old_pc < layout.numbc; old_pc++) {
      uint32_t new_pc = old_pc >= tolua_pending_insert_copy.insert_pc ? old_pc + insert_count : old_pc;
      new_bc[new_pc] = (BCIns)tolua_read_ins(buf + layout.bc_pos + (size_t)old_pc * 4, be);
    }

    for (insert_index = 0; insert_index < insert_count; insert_index++) {
      new_bc[tolua_pending_insert_copy.insert_pc + insert_index] =
        BCINS_ABC(BC_MOV,
                  tolua_pending_insert_copy.copy_dst[insert_index],
                  0,
                  tolua_pending_insert_copy.copy_src[insert_index]);
    }

    for (old_pc = 0; old_pc < layout.numbc; old_pc++) {
      BCIns old_ins = (BCIns)tolua_read_ins(buf + layout.bc_pos + (size_t)old_pc * 4, be);
      BCOp op = bc_op(old_ins);
      uint32_t target = 0;
      uint32_t new_pc = old_pc >= tolua_pending_insert_copy.insert_pc ? old_pc + insert_count : old_pc;

      if (!tolua_get_jump_target(op, old_ins, old_pc, layout.numbc, &target)) continue;
      if (target == tolua_pending_insert_copy.insert_pc &&
          !tolua_pending_insert_copy.allow_target_entry) {
        free(new_bc);
        free(out);
        *out_status = TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT;
        return NULL;
      }

      target += target > tolua_pending_insert_copy.insert_pc ? insert_count : 0;
      setbc_j(&new_bc[new_pc], (ptrdiff_t)target - (ptrdiff_t)new_pc - 1);
    }

    for (old_pc = 0; old_pc < layout.numbc + insert_count; old_pc++) {
      tolua_write_ins(dst + (size_t)old_pc * 4, (uint32_t)new_bc[old_pc], be);
    }
    dst += ((size_t)layout.numbc + insert_count) * 4;
    free(new_bc);
  }

  memcpy(dst, buf + layout.bc_end, tail_len);
  dst += tail_len;
  memcpy(dst, buf + layout.end, after_len);
  dst += after_len;

  *out_len = new_len;
#ifdef TOLUA_REPACK_DEBUG
  {
    FILE *fp = fopen("tmp\\last_insert_rebuild.bin", "wb");
    if (fp) {
      fwrite(out, 1, new_len, fp);
      fclose(fp);
    }
  }
#endif
  return out;
}

static void tolua_mark_proto_targets(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be, uint8_t *targets)
{
  uint32_t pc = 0;

  memset(targets, 0, (size_t)numbc);
  for (pc = 0; pc < numbc; pc++) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
    uint32_t target = 0;
    if (tolua_get_jump_target(bc_op(ins), ins, pc, numbc, &target)) {
      targets[target] = 1;
    }
  }
}

static int tolua_target_has_external_entry(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                           int first_pc, uint32_t stop_pc, uint32_t target_pc)
{
  uint32_t pc = 0;

  for (pc = 0; pc < numbc; pc++) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
    uint32_t target = 0;

    if (!tolua_get_jump_target(bc_op(ins), ins, pc, numbc, &target)) continue;
    if (target != target_pc) continue;
    if (pc < (uint32_t)first_pc || pc >= stop_pc) return 1;
  }

  return 0;
}

static int tolua_select_repack_slice(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                     uint32_t pc, BCReg old_first, BCReg old_last,
                                     const uint8_t *targets, uint8_t *selected, int *out_min_window,
                                     uint32_t *out_interference_pc, BCOp *out_interference_op,
                                     BCReg *out_interference_reg)
{
  uint8_t live[BCMAX_A + 1];
  int min_window = (int)pc;
  int scan = 0;
  int live_count = 0;

  memset(selected, 0, (size_t)numbc);
  memset(live, 0, sizeof(live));
  if (out_interference_pc) *out_interference_pc = UINT32_MAX;
  if (out_interference_op) *out_interference_op = BC__MAX;
  if (out_interference_reg) *out_interference_reg = 0;
  if (old_first > old_last || old_last > BCMAX_A) {
#ifdef TOLUA_REPACK_DEBUG
    fprintf(stderr, "[repack] slice fail pc=%u invalid-range=[%u,%u]\n",
            (unsigned int)pc, (unsigned int)old_first, (unsigned int)old_last);
#endif
    return 0;
  }

  for (scan = old_first; scan <= old_last; scan++) {
    live[scan] = 1;
    live_count++;
  }

  for (scan = (int)pc - 1; scan >= 0 && live_count > 0; scan--) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp ins_op = bc_op(ins);
    BCReg def = 0;

    if (!tolua_get_slice_def_reg(ins_op, ins, old_first, old_last, &def) || !live[def]) continue;

    selected[scan] = 1;
    min_window = scan;

    live[def] = 0;
    live_count--;

    {
      BCReg reg = 0;
      for (reg = old_first; reg <= old_last; reg++) {
        if (tolua_ins_reads_reg(ins_op, ins, reg) && !live[reg]) {
          live[reg] = 1;
          live_count++;
        }
      }
    }
  }

  if (live_count != 0) {
#ifdef TOLUA_REPACK_DEBUG
    BCReg reg = 0;
    for (reg = old_first; reg <= old_last; reg++) {
      if (live[reg]) {
        fprintf(stderr, "[repack] slice fail pc=%u unresolved reg=%u range=[%u,%u]\n",
                (unsigned int)pc, (unsigned int)reg,
                (unsigned int)old_first, (unsigned int)old_last);
        break;
      }
    }
#endif
    return 0;
  }

  memset(live, 0, sizeof(live));
  for (scan = old_first; scan <= old_last; scan++) {
    live[scan] = 1;
  }

  for (scan = (int)pc - 1; scan >= min_window; scan--) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp ins_op = bc_op(ins);

    if (scan != min_window && targets[scan] &&
        tolua_target_has_external_entry(buf, bc_pos, numbc, be, min_window, pc, (uint32_t)scan)) {
#ifdef TOLUA_REPACK_DEBUG
      fprintf(stderr, "[repack] slice fail pc=%u target-at=%d range=[%u,%u]\n",
              (unsigned int)pc, scan,
              (unsigned int)old_first, (unsigned int)old_last);
#endif
      return 0;
    }

    if (selected[scan]) {
      BCReg def = 0;
      BCReg reg = 0;

      if (!tolua_get_slice_def_reg(ins_op, ins, old_first, old_last, &def)) {
#ifdef TOLUA_REPACK_DEBUG
        fprintf(stderr, "[repack] slice fail pc=%u selected-nondef-at=%d op=%s range=[%u,%u]\n",
                (unsigned int)pc, scan, tolua_bc_opname(ins_op),
                (unsigned int)old_first, (unsigned int)old_last);
#endif
        return 0;
      }
      live[def] = 0;
      for (reg = old_first; reg <= old_last; reg++) {
        if (tolua_ins_reads_reg(ins_op, ins, reg)) live[reg] = 1;
      }
    } else {
      BCReg reg = 0;
      for (reg = old_first; reg <= old_last; reg++) {
        if (!live[reg]) continue;
        if (tolua_ins_reads_reg(ins_op, ins, reg) ||
            tolua_ins_writes_reg(ins_op, ins, reg)) {
          if (out_interference_pc) *out_interference_pc = (uint32_t)scan;
          if (out_interference_op) *out_interference_op = ins_op;
          if (out_interference_reg) *out_interference_reg = reg;
#ifdef TOLUA_REPACK_DEBUG
          fprintf(stderr,
                  "[repack] slice fail pc=%u interference-at=%d op=%s reg=%u range=[%u,%u]\n",
                  (unsigned int)pc, scan, tolua_bc_opname(ins_op), (unsigned int)reg,
                  (unsigned int)old_first, (unsigned int)old_last);
#endif
          return 0;
        }
      }
    }
  }

  *out_min_window = min_window;
  return 1;
}

static int tolua_retry_repack_slice_with_readonly_interference(const uint8_t *buf, size_t bc_pos,
                                                               uint32_t numbc, int be, uint32_t pc,
                                                               BCReg old_first, BCReg old_last,
                                                               const uint8_t *targets,
                                                               uint8_t *selected, int *out_min_window,
                                                               uint32_t *out_interference_pc,
                                                               BCOp *out_interference_op,
                                                               BCReg *out_interference_reg);
static int tolua_future_fr2_arg_shift_writes_reg(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                 uint32_t start_pc, BCReg reg);
static int tolua_reg_live_after_pc(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                   uint32_t start_pc, BCReg reg);
static int tolua_collect_proto_holes(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                     int remap_v1, int target_fr2, tolua_bcshift_map *map,
                                     const tolua_bcdebug_ctx *ctx);
static BCOp tolua_remap_bc_op(BCOp op, int remap_v1);
static int tolua_collapse_multires_producer(uint8_t *buf, size_t bc_pos, uint32_t pc,
                                            int be, const tolua_bcdebug_ctx *ctx);
static int tolua_prepare_proto_bytecode(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                        int remap_v1, int target_fr2,
                                        const tolua_bcdebug_ctx *ctx);
static int tolua_try_repack_adjacent_cat_call_chain(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                    uint32_t producer_pc, BCIns producer,
                                                    uint32_t consumer_pc, BCIns consumer,
                                                    uint8_t *framesize_io, const uint8_t *targets,
                                                    const tolua_bcshift_map *map,
                                                    const tolua_bcdebug_ctx *ctx, int *changed);
static int tolua_try_repack_adjacent_call_chain(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                uint32_t producer_pc, BCIns producer,
                                                uint32_t consumer_pc, BCIns consumer,
                                                uint8_t *framesize_io, const uint8_t *targets,
                                                const tolua_bcshift_map *map,
                                                const tolua_bcdebug_ctx *ctx, int *changed);
static int tolua_try_repack_first_arg_call_chain(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                 uint32_t producer_pc, BCIns producer,
                                                 uint32_t consumer_pc, BCIns consumer,
                                                 uint8_t *framesize_io, const uint8_t *targets,
                                                 const tolua_bcshift_map *map,
                                                 const tolua_bcdebug_ctx *ctx, int *changed);
static int tolua_try_fix_cat_call_chain_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                uint32_t pc, uint8_t *framesize_io,
                                                const uint8_t *targets,
                                                const tolua_bcdebug_ctx *ctx, int *handled);
static int tolua_try_fix_cat_arg_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                         uint32_t pc, uint8_t *framesize_io,
                                         const uint8_t *targets,
                                         const tolua_bcdebug_ctx *ctx, int *handled);
static int tolua_try_accept_existing_fr2_slice(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                               uint32_t pc, BCReg new_first, BCReg new_last,
                                               uint8_t *framesize_io, const uint8_t *targets,
                                               const tolua_bcdebug_ctx *ctx, int *handled);
static int tolua_try_select_simple_local_defs(const uint8_t *buf, size_t bc_pos, int be,
                                              uint32_t pc, BCReg old_first, BCReg old_last,
                                              uint8_t *selected, int *out_min_window);
static int tolua_try_insert_copy_fallback_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                   uint32_t pc, BCReg old_first, BCReg old_last,
                                                   BCReg new_first, BCReg new_last,
                                                   uint8_t *framesize_io, const uint8_t *targets,
                                                   const tolua_bcdebug_ctx *ctx,
                                                   const char *reason, int *handled);
static int tolua_try_fix_call_intermediate_producer_chain_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                                   uint32_t pc, uint8_t *framesize_io,
                                                                   const uint8_t *targets,
                                                                   const tolua_bcdebug_ctx *ctx, int *handled);
static int tolua_try_fix_iterc_adjacent_call_arg_chain_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                                uint32_t pc, uint8_t *framesize_io,
                                                                const uint8_t *targets,
                                                                const tolua_bcdebug_ctx *ctx, int *handled);
static int tolua_try_fix_call_method_basecopy_chain_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                             uint32_t pc, uint8_t *framesize_io,
                                                             const uint8_t *targets,
                                                             const tolua_bcdebug_ctx *ctx, int *handled);
static int tolua_try_fix_call_selfdef_chain_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                     uint32_t pc, uint8_t *framesize_io,
                                                     const uint8_t *targets,
                                                     const tolua_bcdebug_ctx *ctx, int *handled);
static int tolua_try_fix_tail_call_arg_chain_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                      uint32_t pc, uint8_t *framesize_io,
                                                      const uint8_t *targets,
                                                      const tolua_bcdebug_ctx *ctx, int *handled);
static int tolua_window_touches_range(const uint8_t *buf, size_t bc_pos, int be,
                                      int first_pc, uint32_t stop_pc,
                                      BCReg first, BCReg last);

static int tolua_window_has_nonselected_touch(const uint8_t *buf, size_t bc_pos, int be,
                                              int first_pc, uint32_t stop_pc,
                                              const uint8_t *selected,
                                              BCReg first, BCReg last,
                                              uint32_t *out_pc, BCOp *out_op, BCReg *out_reg)
{
  int scan = 0;

  if (out_pc) *out_pc = UINT32_MAX;
  if (out_op) *out_op = BC__MAX;
  if (out_reg) *out_reg = 0;

  for (scan = first_pc; scan < (int)stop_pc; scan++) {
    BCIns ins = 0;
    BCOp op = BC__MAX;
    BCReg reg = 0;

    if (selected[scan]) continue;
    ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    op = bc_op(ins);
    for (reg = first; reg <= last; reg++) {
      if (!tolua_ins_reads_reg(op, ins, reg) && !tolua_ins_writes_reg(op, ins, reg)) continue;
      if (out_pc) *out_pc = (uint32_t)scan;
      if (out_op) *out_op = op;
      if (out_reg) *out_reg = reg;
      return 1;
    }
  }

  return 0;
}

static int tolua_shift_proto_slice_right_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                 uint32_t pc, BCReg old_first, BCReg old_last,
                                                 uint8_t *framesize_io, const uint8_t *targets,
                                                 const tolua_bcdebug_ctx *ctx)
{
  BCIns consumer_ins = 0;
  BCOp consumer_op = BC__MAX;
  int allow_existing_slice = 1;
  BCReg new_first = 0;
  BCReg new_last = 0;
  uint8_t *selected = NULL;
  int min_window = (int)pc;
  uint32_t interference_pc = UINT32_MAX;
  BCOp interference_op = BC__MAX;
  BCReg interference_reg = 0;
  int status = TOLUA_BCCONV_OK;
  int force_copy_fallback = 0;
  int scan = 0;

  if (old_first > old_last) return TOLUA_BCCONV_OK;
  consumer_ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  consumer_op = bc_op(consumer_ins);
  if (old_first == old_last && pc > 0) {
    BCIns prev = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
    BCOp prev_op = bc_op(prev);
    if (prev_op == BC_CALL && bc_b(prev) == 2 && bc_a(prev) == (BCReg)(old_first + 1)) {
      return TOLUA_BCCONV_OK;
    }
    if (tolua_ins_writes_reg(prev_op, prev, old_first) &&
        prev_op != BC_CALL &&
        prev_op != BC_CALLM &&
        prev_op != BC_CALLT &&
        prev_op != BC_CALLMT &&
        prev_op != BC_VARG &&
        prev_op != BC_ITERC &&
        prev_op != BC_ITERN) {
      allow_existing_slice = 0;
    }
  }
  if (old_last >= BCMAX_A) {
    return tolua_failbytecodeproto(ctx, pc,
                                   (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be),
                                   bc_op((BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be)),
                                   TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                   "FR2 argument range [%u,%u] exceeds register limit",
                                   (unsigned int)old_first, (unsigned int)old_last);
  }

  new_first = (BCReg)(old_first + 1);
  new_last = (BCReg)(old_last + 1);
  if (consumer_op == BC_CALL && bc_c(consumer_ins) == 4 && pc >= 2) {
    BCIns prev1 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
    BCIns prev2 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 2) * 4, be);

    /* Guard a known false-positive shape:
       old args are prepared as rX=TGETS, rX+1=MOV, rX+2=MOV right before CALL.
       In this shape, accepting "existing FR2 slice" may silently shift arg order. */
    if (bc_op(prev1) == BC_MOV &&
        bc_op(prev2) == BC_MOV &&
        bc_a(prev2) == (BCReg)(old_first + 1) &&
        bc_a(prev1) == (BCReg)(old_first + 2) &&
        bc_d(prev2) != old_first) {
      allow_existing_slice = 0;
    }
  }
  if (consumer_op == BC_CALL && bc_c(consumer_ins) == 4 &&
      ctx != NULL && ctx->proto_flags == 0x03 &&
      old_last == (BCReg)(old_first + 2) && pc >= 4) {
    BCIns prev1 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
    BCIns prev2 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 2) * 4, be);
    BCIns prev3 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 3) * 4, be);
    BCIns prev4 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 4) * 4, be);
    BCReg func_reg = bc_a(consumer_ins);

    /* Root-chunk RegisterWorkflowForSkills-like 3-arg call site:
       TGETS func; KSTR; TDUP; FNEW; CALL(C=4).
       Force copy-fallback here to keep arg1/arg2/arg3 ordering stable on FR2. */
    if (bc_op(prev4) == BC_TGETS &&
        bc_a(prev4) == func_reg &&
        bc_op(prev3) == BC_KSTR &&
        bc_a(prev3) == old_first &&
        (bc_op(prev2) == BC_TDUP || bc_op(prev2) == BC_MOV) &&
        bc_a(prev2) == (BCReg)(old_first + 1) &&
        bc_op(prev1) == BC_FNEW &&
        bc_a(prev1) == old_last) {
      allow_existing_slice = 0;
      force_copy_fallback = 1;
    }
  }
  if (consumer_op == BC_CALL && bc_c(consumer_ins) == 3 && pc >= 2) {
    BCIns prev1 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
    BCIns prev2 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 2) * 4, be);
    BCOp prev2_op = bc_op(prev2);

    /* Two-arg CALL shape: old arg1 from TGET* or UGET/GGET into old_first and
       old arg2 from MOV into old_last right before CALL. Reusing existing FR2
       slice here keeps arg1 at A+1 and can swap/drop call args on FR2. */
    if (bc_op(prev1) == BC_MOV &&
        bc_a(prev1) == old_last &&
        (prev2_op == BC_TGETS || prev2_op == BC_TGETV || prev2_op == BC_TGETB ||
         prev2_op == BC_UGET || prev2_op == BC_GGET) &&
        bc_a(prev2) == old_first &&
        bc_d(prev1) != old_first) {
      allow_existing_slice = 0;
    }
  }
  if (consumer_op == BC_CALL && bc_c(consumer_ins) == 3 && pc >= 4) {
    BCIns prev1 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
    BCIns prev2 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 2) * 4, be);
    BCIns prev3 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 3) * 4, be);
    BCIns prev4 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 4) * 4, be);
    BCOp prev3_op = bc_op(prev3);
    BCOp prev4_op = bc_op(prev4);
    BCReg func_reg = bc_a(consumer_ins);

    /* Guard table-style two-arg calls prepared as:
       GGET/TGET* func; TGET* method; MOV arg1; MOV arg2; CALL.
       Existing FR2 slice reuse here can keep args at A+1/A+2 (FR1 layout). */
    if (bc_op(prev1) == BC_MOV &&
        bc_op(prev2) == BC_MOV &&
        bc_a(prev1) == old_last &&
        bc_a(prev2) == old_first &&
        (prev3_op == BC_TGETS || prev3_op == BC_TGETV || prev3_op == BC_TGETB) &&
        bc_a(prev3) == func_reg &&
        bc_b(prev3) == func_reg &&
        bc_a(prev4) == func_reg &&
        (prev4_op == BC_GGET || prev4_op == BC_UGET ||
         prev4_op == BC_TGETS || prev4_op == BC_TGETV || prev4_op == BC_TGETB)) {
      allow_existing_slice = 0;
    }
  }
  if (consumer_op == BC_CALL && bc_c(consumer_ins) == 6 &&
      ctx != NULL && ctx->proto_flags == 0x03 &&
      old_last == (BCReg)(old_first + 4) && pc >= 6) {
    BCIns prev1 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
    BCIns prev2 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 2) * 4, be);
    BCIns prev3 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 3) * 4, be);
    BCIns prev4 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 4) * 4, be);
    BCIns prev5 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 5) * 4, be);
    BCIns prev6 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 6) * 4, be);
    BCReg func_reg = bc_a(consumer_ins);

    /* RegisterWorkflowForSkills-like call site:
       TGETS func; KSTR; (TDUP|MOV); FNEW; KSTR; KPRI; CALL(C=6).
       Force copy-fallback to avoid residual arg aliasing in long repeated chains. */
    if (bc_op(prev6) == BC_TGETS &&
        bc_a(prev6) == func_reg &&
        bc_op(prev5) == BC_KSTR &&
        bc_a(prev5) == old_first &&
        (bc_op(prev4) == BC_TDUP || bc_op(prev4) == BC_MOV) &&
        bc_a(prev4) == (BCReg)(old_first + 1) &&
        bc_op(prev3) == BC_FNEW &&
        bc_a(prev3) == (BCReg)(old_first + 2) &&
        bc_op(prev2) == BC_KSTR &&
        bc_a(prev2) == (BCReg)(old_first + 3) &&
        bc_op(prev1) == BC_KPRI &&
        bc_a(prev1) == old_last) {
      allow_existing_slice = 0;
      force_copy_fallback = 1;
    }
  }
  if (new_last > BCMAX_A) {
    return tolua_failbytecodeproto(ctx, pc,
                                   (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be),
                                   bc_op((BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be)),
                                   TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                   "FR2 argument shift [%u,%u] -> [%u,%u] exceeds register limit",
                                   (unsigned int)old_first, (unsigned int)old_last,
                                   (unsigned int)new_first, (unsigned int)new_last);
  }
  if (allow_existing_slice &&
      ((consumer_op == BC_CALL && bc_c(consumer_ins) >= 2) ||
       (consumer_op == BC_CALLT && bc_d(consumer_ins) >= 2))) {
    int handled = 0;
    status = tolua_try_accept_existing_fr2_slice(buf, bc_pos, numbc, be, pc,
                                                 new_first, new_last,
                                                 framesize_io, targets, ctx, &handled);
    if (status != TOLUA_BCCONV_OK) return status;
    if (handled) return TOLUA_BCCONV_OK;
  }
  if (force_copy_fallback) {
    int handled = 0;

    status = tolua_try_insert_copy_fallback_for_fr2(buf, bc_pos, numbc, be, pc,
                                                    old_first, old_last, new_first, new_last,
                                                    framesize_io, targets, ctx,
                                                    "call6-kstr-tdup-fnew-chain", &handled);
    if (status == TOLUA_BCCONV_INTERNAL_INSERT_COPY) return status;
    if (status != TOLUA_BCCONV_OK) return status;
    if (handled) {
      TOLUA_REPACK_LOG(ctx, pc,
                       "force copy-fallback handled old=[%u,%u] new=[%u,%u]",
                       (unsigned int)old_first, (unsigned int)old_last,
                       (unsigned int)new_first, (unsigned int)new_last);
      return TOLUA_BCCONV_OK;
    }
    TOLUA_REPACK_LOG(ctx, pc,
                     "force copy-fallback unavailable, continue slice remap old=[%u,%u] new=[%u,%u]",
                     (unsigned int)old_first, (unsigned int)old_last,
                     (unsigned int)new_first, (unsigned int)new_last);
  }

  selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!selected) {
    return tolua_failbytecodeproto(ctx, pc,
                                   (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be),
                                   bc_op((BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be)),
                                   TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate FR2 slice buffer");
  }

  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc, old_first, old_last,
                                 targets, selected, &min_window,
                                 &interference_pc, &interference_op, &interference_reg) &&
      !tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, pc,
                                                           old_first, old_last, targets,
                                                           selected, &min_window,
                                                           &interference_pc, &interference_op,
                                                           &interference_reg)) {
    int handled = 0;

    memset(selected, 0, (size_t)numbc);
    if (tolua_try_select_simple_local_defs(buf, bc_pos, be, pc, old_first, old_last,
                                           selected, &min_window)) {
      interference_pc = UINT32_MAX;
      interference_op = BC__MAX;
      interference_reg = 0;
    } else {
      free(selected);
      if (allow_existing_slice) {
        status = tolua_try_accept_existing_fr2_slice(buf, bc_pos, numbc, be, pc, new_first, new_last,
                                                     framesize_io, targets, ctx, &handled);
        if (status != TOLUA_BCCONV_OK) return status;
        if (handled) return TOLUA_BCCONV_OK;
      }
      status = tolua_try_fix_call_selfdef_chain_for_fr2(buf, bc_pos, numbc, be, pc,
                                                        framesize_io, targets, ctx, &handled);
      if (status != TOLUA_BCCONV_OK) return status;
      if (handled) return TOLUA_BCCONV_OK;
      status = tolua_try_fix_call_method_basecopy_chain_for_fr2(buf, bc_pos, numbc, be, pc,
                                                                framesize_io, targets, ctx, &handled);
      if (status != TOLUA_BCCONV_OK) return status;
      if (handled) return TOLUA_BCCONV_OK;
      status = tolua_try_fix_call_intermediate_producer_chain_for_fr2(buf, bc_pos, numbc, be, pc,
                                                                      framesize_io, targets, ctx, &handled);
      if (status != TOLUA_BCCONV_OK) return status;
      if (handled) return TOLUA_BCCONV_OK;
      status = tolua_try_fix_iterc_adjacent_call_arg_chain_for_fr2(buf, bc_pos, numbc, be, pc,
                                                                   framesize_io, targets, ctx, &handled);
      if (status != TOLUA_BCCONV_OK) return status;
      if (handled) return TOLUA_BCCONV_OK;
      status = tolua_try_fix_tail_call_arg_chain_for_fr2(buf, bc_pos, numbc, be, pc,
                                                         framesize_io, targets, ctx, &handled);
      if (status != TOLUA_BCCONV_OK) return status;
      if (handled) return TOLUA_BCCONV_OK;
      status = tolua_try_insert_copy_fallback_for_fr2(buf, bc_pos, numbc, be, pc,
                                                      old_first, old_last, new_first, new_last,
                                                      framesize_io, targets, ctx,
                                                      "slice-select-fail", &handled);
      if (status == TOLUA_BCCONV_INTERNAL_INSERT_COPY) return status;
      if (status != TOLUA_BCCONV_OK) return status;
      if (handled) return TOLUA_BCCONV_OK;
      return tolua_failbytecodeproto(ctx, pc,
                                     (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be),
                                     bc_op((BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be)),
                                     TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                     "failed to select FR2 argument slice [%u,%u] (blocker pc=%u op=%s reg=%u)",
                                     (unsigned int)old_first, (unsigned int)old_last,
                                     (unsigned int)interference_pc,
                                     tolua_bc_opname(interference_op),
                                     (unsigned int)interference_reg);
    }
  }

  if (tolua_window_has_nonselected_touch(buf, bc_pos, be, min_window, pc, selected,
                                         new_last, new_last,
                                         &interference_pc, &interference_op, &interference_reg)) {
    int handled = 0;

    if (allow_existing_slice) {
      status = tolua_try_accept_existing_fr2_slice(buf, bc_pos, numbc, be, pc, new_first, new_last,
                                                   framesize_io, targets, ctx, &handled);
      if (status != TOLUA_BCCONV_OK) return status;
      if (handled) {
        free(selected);
        return TOLUA_BCCONV_OK;
      }
    }
    status = tolua_try_fix_call_selfdef_chain_for_fr2(buf, bc_pos, numbc, be, pc,
                                                      framesize_io, targets, ctx, &handled);
    if (status != TOLUA_BCCONV_OK) return status;
    if (handled) {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
    status = tolua_try_fix_call_method_basecopy_chain_for_fr2(buf, bc_pos, numbc, be, pc,
                                                              framesize_io, targets, ctx, &handled);
    if (status != TOLUA_BCCONV_OK) return status;
    if (handled) {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
    status = tolua_try_fix_call_intermediate_producer_chain_for_fr2(buf, bc_pos, numbc, be, pc,
                                                                    framesize_io, targets, ctx, &handled);
    if (status != TOLUA_BCCONV_OK) return status;
    if (handled) {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
    status = tolua_try_fix_iterc_adjacent_call_arg_chain_for_fr2(buf, bc_pos, numbc, be, pc,
                                                                 framesize_io, targets, ctx, &handled);
    if (status != TOLUA_BCCONV_OK) return status;
    if (handled) {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
    status = tolua_try_fix_tail_call_arg_chain_for_fr2(buf, bc_pos, numbc, be, pc,
                                                       framesize_io, targets, ctx, &handled);
    if (status != TOLUA_BCCONV_OK) return status;
    if (handled) {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
    status = tolua_try_fix_cat_call_chain_for_fr2(buf, bc_pos, numbc, be, pc,
                                                  framesize_io, targets, ctx, &handled);
    if (status != TOLUA_BCCONV_OK) return status;
    if (handled) {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
    status = tolua_try_fix_cat_arg_for_fr2(buf, bc_pos, numbc, be, pc,
                                           framesize_io, targets, ctx, &handled);
    if (status == TOLUA_BCCONV_INTERNAL_INSERT_COPY) {
      free(selected);
      return status;
    }
    if (status != TOLUA_BCCONV_OK) {
      free(selected);
      return status;
    }
    if (handled) {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
    status = tolua_try_insert_copy_fallback_for_fr2(buf, bc_pos, numbc, be, pc,
                                                    old_first, old_last, new_first, new_last,
                                                    framesize_io, targets, ctx,
                                                    "target-touch-fail", &handled);
    free(selected);
    if (status == TOLUA_BCCONV_INTERNAL_INSERT_COPY) return status;
    if (status != TOLUA_BCCONV_OK) return status;
    if (handled) return TOLUA_BCCONV_OK;
    return tolua_failbytecodeproto(ctx, pc,
                                   (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be),
                                   bc_op((BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be)),
                                   TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                   "FR2 argument target register %u is touched by pc=%u op=%s before call",
                                   (unsigned int)new_last, (unsigned int)interference_pc,
                                   tolua_bc_opname(interference_op));
  }

  if (tolua_reg_live_after_pc(buf, bc_pos, numbc, be, pc + 1, new_last)) {
    int handled = 0;
    int deferred_overwrite = 0;
    uint32_t first_touch_pc = UINT32_MAX;
    BCOp first_touch_op = BC__MAX;
    const char *first_touch_kind = "none";
    uint32_t live_scan = 0;

    for (live_scan = pc + 1; live_scan < numbc; live_scan++) {
      BCIns live_ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)live_scan * 4, be);
      BCOp live_op = bc_op(live_ins);

      if (tolua_ins_reads_reg(live_op, live_ins, new_last)) {
        first_touch_pc = live_scan;
        first_touch_op = live_op;
        first_touch_kind = "read";
        break;
      }
      if (tolua_ins_writes_reg(live_op, live_ins, new_last)) {
        first_touch_pc = live_scan;
        first_touch_op = live_op;
        first_touch_kind = "write";
        break;
      }
    }

    if (allow_existing_slice) {
      status = tolua_try_accept_existing_fr2_slice(buf, bc_pos, numbc, be, pc, new_first, new_last,
                                                   framesize_io, targets, ctx, &handled);
      if (status != TOLUA_BCCONV_OK) {
        free(selected);
        return status;
      }
      if (handled) {
        free(selected);
        return TOLUA_BCCONV_OK;
      }
    }
    deferred_overwrite = tolua_future_fr2_arg_shift_writes_reg(buf, bc_pos, numbc, be, pc + 1, new_last);
    if (!deferred_overwrite &&
        first_touch_pc == pc + 1 &&
        first_touch_kind[0] == 'r' &&
        (first_touch_op == BC_ITERL || first_touch_op == BC_IITERL || first_touch_op == BC_JITERL)) {
      BCIns consumer_ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
      BCIns loop_ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)first_touch_pc * 4, be);
      if (bc_a(loop_ins) == bc_a(consumer_ins)) {
        int copy_handled = 0;
        int copy_status = TOLUA_BCCONV_OK;

        copy_status = tolua_try_insert_copy_fallback_for_fr2(buf, bc_pos, numbc, be, pc,
                                                             old_first, old_last, new_first, new_last,
                                                             framesize_io, targets, ctx,
                                                             "paired-iterl-live-after", &copy_handled);
        if (copy_status == TOLUA_BCCONV_INTERNAL_INSERT_COPY) {
          free(selected);
          return copy_status;
        }
        if (copy_status != TOLUA_BCCONV_OK) {
          free(selected);
          return copy_status;
        }
        if (copy_handled) {
          free(selected);
          return TOLUA_BCCONV_OK;
        }

        deferred_overwrite = 1;
        TOLUA_REPACK_LOG(ctx, pc,
                         "allow live-after reg=%u due to paired %s at pc=%u",
                         (unsigned int)new_last, tolua_bc_opname(first_touch_op),
                         (unsigned int)first_touch_pc);
      }
    }
    if (deferred_overwrite) {
      TOLUA_REPACK_LOG(ctx, pc,
                       "allow live-after reg=%u due to future FR2 arg shift write (first_touch=%s pc=%u op=%s)",
                       (unsigned int)new_last, first_touch_kind,
                       (unsigned int)first_touch_pc, tolua_bc_opname(first_touch_op));
    } else {
      uint32_t copy_count = (uint32_t)(old_last - old_first + 1);

      if (!targets[pc] &&
          !tolua_pending_insert_copy.active &&
          first_touch_pc != UINT32_MAX &&
          first_touch_kind[0] == 'r' &&
          copy_count + 1 <= TOLUA_MAX_INSERT_COPIES) {
        uint32_t first_write_pc = UINT32_MAX;
        uint32_t stop_pc = numbc;
        BCReg spill = BCMAX_A;
        BCReg cand = 0;
        int rewrite_ok = 1;

        for (live_scan = pc + 1; live_scan < numbc; live_scan++) {
          BCIns live_ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)live_scan * 4, be);
          BCOp live_op = bc_op(live_ins);
          if (tolua_ins_writes_reg(live_op, live_ins, new_last)) {
            first_write_pc = live_scan;
            stop_pc = live_scan;
            break;
          }
        }

        for (cand = *framesize_io; cand <= BCMAX_A; cand++) {
          if (cand >= new_first && cand <= new_last) continue;
          if (tolua_window_touches_range(buf, bc_pos, be, (int)(pc + 1), stop_pc, cand, cand)) continue;
          spill = cand;
          break;
        }

        if (spill <= BCMAX_A) {
          for (live_scan = pc + 1; live_scan < stop_pc; live_scan++) {
            BCIns live_ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)live_scan * 4, be);
            BCOp live_op = bc_op(live_ins);
            if (!tolua_ins_reads_reg(live_op, live_ins, new_last)) continue;
            if (!tolua_can_rewrite_ins_source_reg(live_ins, live_op, new_last)) {
              rewrite_ok = 0;
              break;
            }
          }

          if (rewrite_ok) {
            BCReg copy_dst[TOLUA_MAX_INSERT_COPIES];
            BCReg copy_src[TOLUA_MAX_INSERT_COPIES];
            BCReg max_last = new_last;
            uint32_t i = 0;

            for (live_scan = pc + 1; live_scan < stop_pc; live_scan++) {
              uint8_t *slot = buf + bc_pos + (size_t)live_scan * 4;
              BCIns live_ins = (BCIns)tolua_read_ins(slot, be);
              BCOp live_op = bc_op(live_ins);
              if (!tolua_rewrite_ins_source_reg(&live_ins, live_op, new_last, spill)) continue;
              tolua_write_ins(slot, (uint32_t)live_ins, be);
            }

            copy_dst[0] = spill;
            copy_src[0] = new_last;
            for (i = 0; i < copy_count; i++) {
              uint32_t rev = copy_count - 1 - i;
              copy_dst[i + 1] = (BCReg)(new_first + rev);
              copy_src[i + 1] = (BCReg)(old_first + rev);
            }

            if (tolua_schedule_insert_copies(ctx, pc, pc, copy_dst, copy_src, (uint8_t)(copy_count + 1))) {
              if (spill > max_last) max_last = spill;
              status = tolua_update_framesize_checked(framesize_io, max_last, ctx, pc,
                                                      (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be),
                                                      bc_op((BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be)));
              free(selected);
              if (status != TOLUA_BCCONV_OK) return status;
              TOLUA_REPACK_LOG(ctx, pc,
                               "live-after spill fallback spill=%u read_pc=%u write_pc=%u shift_count=%u",
                               (unsigned int)spill,
                               (unsigned int)first_touch_pc,
                               (unsigned int)(first_write_pc == UINT32_MAX ? numbc : first_write_pc),
                               (unsigned int)copy_count);
              return TOLUA_BCCONV_INTERNAL_INSERT_COPY;
            }
          }
        }
      }

      if (!targets[pc] &&
          !tolua_pending_insert_copy.active &&
          copy_count > 0 &&
          copy_count <= TOLUA_MAX_INSERT_COPIES) {
        BCReg copy_dst[TOLUA_MAX_INSERT_COPIES];
        BCReg copy_src[TOLUA_MAX_INSERT_COPIES];
        uint32_t i = 0;

        for (i = 0; i < copy_count; i++) {
          uint32_t rev = copy_count - 1 - i;
          copy_dst[i] = (BCReg)(new_first + rev);
          copy_src[i] = (BCReg)(old_first + rev);
        }

        if (tolua_schedule_insert_copies(ctx, pc, pc, copy_dst, copy_src, (uint8_t)copy_count)) {
          status = tolua_update_framesize_checked(framesize_io, new_last, ctx, pc,
                                                  (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be),
                                                  bc_op((BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be)));
          free(selected);
          if (status != TOLUA_BCCONV_OK) return status;
          TOLUA_REPACK_LOG(ctx, pc,
                           "live-after fallback copy insert count=%u range=[%u,%u]->[%u,%u]",
                           (unsigned int)copy_count,
                           (unsigned int)old_first, (unsigned int)old_last,
                           (unsigned int)new_first, (unsigned int)new_last);
          return TOLUA_BCCONV_INTERNAL_INSERT_COPY;
        }
      }

      TOLUA_REPACK_LOG(ctx, pc,
                       "live-after reject detail reg=%u first_touch=%s pc=%u op=%s",
                       (unsigned int)new_last, first_touch_kind,
                       (unsigned int)first_touch_pc, tolua_bc_opname(first_touch_op));
      free(selected);
      return tolua_failbytecodeproto(ctx, pc,
                                     (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be),
                                     bc_op((BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be)),
                                     TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                     "FR2 argument target register %u stays live after pc=%u",
                                     (unsigned int)new_last, (unsigned int)pc);
    }
  }

  for (scan = min_window; scan < (int)pc; scan++) {
    uint8_t *slot = NULL;
    BCIns ins = 0;
    BCOp op = BC__MAX;

    if (!selected[scan]) continue;
    slot = buf + bc_pos + (size_t)scan * 4;
    ins = (BCIns)tolua_read_ins(slot, be);
    op = bc_op(ins);
    tolua_repack_remap_reg_range(&ins, op, old_first, old_last, new_first);
    tolua_write_ins(slot, (uint32_t)ins, be);
    tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, ins,
                                       old_first, old_last, new_first);
  }

  status = tolua_update_framesize_checked(framesize_io, new_last, ctx, pc,
                                          (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be),
                                          bc_op((BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be)));
  free(selected);
  return status;
}

static int tolua_find_nearest_reg_writer(const uint8_t *buf, size_t bc_pos, int be,
                                         uint32_t pc, BCReg reg,
                                         uint32_t *out_pc, BCOp *out_op, BCIns *out_ins)
{
  int scan = 0;

  if (out_pc) *out_pc = UINT32_MAX;
  if (out_op) *out_op = BC__MAX;
  if (out_ins) *out_ins = 0;
  if (pc == 0) return 0;

  for (scan = (int)pc - 1; scan >= 0; scan--) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp op = bc_op(ins);

    if (!tolua_ins_writes_reg(op, ins, reg)) continue;
    if (out_pc) *out_pc = (uint32_t)scan;
    if (out_op) *out_op = op;
    if (out_ins) *out_ins = ins;
    return 1;
  }

  return 0;
}

static int tolua_existing_fr2_call_args_are_aligned(const uint8_t *buf, size_t bc_pos, int be,
                                                    uint32_t pc, BCReg old_first, BCReg old_last,
                                                    BCReg new_first, BCReg new_last,
                                                    const tolua_bcdebug_ctx *ctx)
{
  BCReg old_reg = old_first;
  BCReg new_reg = new_first;
  uint32_t old_writer_pc = UINT32_MAX;
  uint32_t new_writer_pc = UINT32_MAX;
  uint32_t old_next_writer_pc = UINT32_MAX;
  BCOp old_writer_op = BC__MAX;
  BCOp new_writer_op = BC__MAX;
  BCOp old_next_writer_op = BC__MAX;
  BCIns old_writer_ins = 0;
  BCIns new_writer_ins = 0;
  BCIns old_next_writer_ins = 0;
  int have_old = 0;
  int have_new = 0;
  int have_old_next = 0;

  (void)new_last;
  if (old_first > old_last || new_first > new_last) return 1;

  have_old = tolua_find_nearest_reg_writer(buf, bc_pos, be, pc, old_reg,
                                           &old_writer_pc, &old_writer_op, &old_writer_ins);
  have_new = tolua_find_nearest_reg_writer(buf, bc_pos, be, pc, new_reg,
                                           &new_writer_pc, &new_writer_op, &new_writer_ins);
  if (!have_old || !have_new) return 1;
  if (old_last > old_first) {
    have_old_next = tolua_find_nearest_reg_writer(buf, bc_pos, be, pc, (BCReg)(old_reg + 1),
                                                  &old_next_writer_pc, &old_next_writer_op, &old_next_writer_ins);
    if (have_old_next &&
        ((((old_writer_op == BC_TGETS || old_writer_op == BC_TGETV || old_writer_op == BC_TGETB ||
            old_writer_op == BC_GGET || old_writer_op == BC_UGET) &&
           (new_writer_op == BC_TNEW || new_writer_op == BC_TDUP ||
            new_writer_op == BC_TGETS || new_writer_op == BC_TGETV || new_writer_op == BC_TGETB) &&
           old_writer_pc + 12 >= pc)) ||
         (old_writer_op == BC_MOV &&
          (new_writer_op == BC_CAT || new_writer_op == BC_KSTR) &&
          old_writer_pc + 32 >= pc)) &&
        old_next_writer_pc == new_writer_pc &&
        old_next_writer_pc != old_writer_pc &&
        old_next_writer_op == new_writer_op &&
        old_next_writer_op != BC_CALL &&
        old_next_writer_op != BC_CALLM &&
        old_next_writer_op != BC_CALLT &&
        old_next_writer_op != BC_CALLMT &&
        old_next_writer_op != BC_VARG &&
        old_next_writer_op != BC_ITERC &&
        old_next_writer_op != BC_ITERN) {
      TOLUA_REPACK_LOG(ctx, pc,
                       "reject existing FR2 slice: new-first writer matches old-second old=%u(pc=%u,%s) old2=%u(pc=%u,%s) new=%u(pc=%u,%s)",
                       (unsigned int)old_reg, (unsigned int)old_writer_pc, tolua_bc_opname(old_writer_op),
                       (unsigned int)(old_reg + 1), (unsigned int)old_next_writer_pc, tolua_bc_opname(old_next_writer_op),
                       (unsigned int)new_reg, (unsigned int)new_writer_pc, tolua_bc_opname(new_writer_op));
      return 0;
    }
    if (have_old_next &&
        old_last >= (BCReg)(old_first + 2) &&
        old_writer_op == BC_KSTR &&
        old_next_writer_op == BC_TDUP &&
        new_writer_op == BC_TDUP &&
        old_next_writer_pc == new_writer_pc &&
        old_next_writer_pc != old_writer_pc &&
        old_writer_pc + 12 >= pc &&
        old_next_writer_pc + 8 >= pc) {
      TOLUA_REPACK_LOG(ctx, pc,
                       "reject existing FR2 slice: KSTR+TDUP new-first aliases old-second old=%u(pc=%u,%s) old2=%u(pc=%u,%s) new=%u(pc=%u,%s)",
                       (unsigned int)old_reg, (unsigned int)old_writer_pc, tolua_bc_opname(old_writer_op),
                       (unsigned int)(old_reg + 1), (unsigned int)old_next_writer_pc, tolua_bc_opname(old_next_writer_op),
                       (unsigned int)new_reg, (unsigned int)new_writer_pc, tolua_bc_opname(new_writer_op));
      return 0;
    }
    if (have_old_next &&
        old_last == (BCReg)(old_first + 1) &&
        old_writer_op == BC_MOV &&
        old_next_writer_op == BC_MOV &&
        new_writer_op == BC_MOV &&
        old_next_writer_pc == new_writer_pc &&
        old_next_writer_pc != old_writer_pc &&
        old_next_writer_pc <= old_writer_pc + 2 &&
        bc_d(old_writer_ins) == 0 &&
        (bc_d(old_next_writer_ins) == 1 || bc_d(old_next_writer_ins) == 3) &&
        bc_d(old_writer_ins) != bc_d(old_next_writer_ins) &&
        old_writer_pc + 12 >= pc &&
        old_next_writer_pc + 8 >= pc) {
      TOLUA_REPACK_LOG(ctx, pc,
                       "reject existing FR2 slice: two-arg low-src MOV chain new-first aliases old-second old=%u(pc=%u,%s src=%u) old2=%u(pc=%u,%s src=%u) new=%u(pc=%u,%s src=%u)",
                       (unsigned int)old_reg, (unsigned int)old_writer_pc, tolua_bc_opname(old_writer_op),
                       (unsigned int)bc_d(old_writer_ins),
                       (unsigned int)(old_reg + 1), (unsigned int)old_next_writer_pc, tolua_bc_opname(old_next_writer_op),
                       (unsigned int)bc_d(old_next_writer_ins),
                       (unsigned int)new_reg, (unsigned int)new_writer_pc, tolua_bc_opname(new_writer_op),
                       (unsigned int)bc_d(new_writer_ins));
      return 0;
    }
    if (have_old_next &&
        old_last >= (BCReg)(old_first + 2) &&
        old_writer_op == BC_MOV &&
        old_next_writer_op == BC_MOV &&
        old_next_writer_pc == new_writer_pc &&
        old_next_writer_pc != old_writer_pc &&
        old_next_writer_pc <= old_writer_pc + 2 &&
        bc_d(old_writer_ins) != bc_d(old_next_writer_ins) &&
        old_next_writer_op == new_writer_op &&
        old_next_writer_op != BC_CALL &&
        old_next_writer_op != BC_CALLM &&
        old_next_writer_op != BC_CALLT &&
        old_next_writer_op != BC_CALLMT &&
        old_next_writer_op != BC_VARG &&
        old_next_writer_op != BC_ITERC &&
        old_next_writer_op != BC_ITERN &&
        old_writer_pc + 12 >= pc &&
        old_next_writer_pc + 8 >= pc) {
      BCReg func_reg = old_reg > 0 ? (BCReg)(old_reg - 1) : 0;
      uint32_t func_writer_pc = UINT32_MAX;
      BCOp func_writer_op = BC__MAX;
      BCIns func_writer_ins = 0;
      int have_func_writer = 0;
      int method_self_seed = 0;

      if (old_reg > 0) {
        have_func_writer = tolua_find_nearest_reg_writer(buf, bc_pos, be, pc, func_reg,
                                                         &func_writer_pc, &func_writer_op, &func_writer_ins);
      }
      if (have_func_writer &&
          (func_writer_op == BC_TGETS || func_writer_op == BC_TGETV || func_writer_op == BC_TGETB) &&
          bc_d(old_writer_ins) == bc_b(func_writer_ins)) {
        method_self_seed = 1;
      }

      if (method_self_seed) {
        TOLUA_REPACK_LOG(ctx, pc,
                         "reject existing FR2 slice: generic new-first matches old-second old=%u(pc=%u,%s) old2=%u(pc=%u,%s) new=%u(pc=%u,%s)",
                         (unsigned int)old_reg, (unsigned int)old_writer_pc, tolua_bc_opname(old_writer_op),
                         (unsigned int)(old_reg + 1), (unsigned int)old_next_writer_pc, tolua_bc_opname(old_next_writer_op),
                         (unsigned int)new_reg, (unsigned int)new_writer_pc, tolua_bc_opname(new_writer_op));
        return 0;
      }
    }
    if (have_old_next &&
        new_writer_op == BC_MOV) {
      BCReg mov_src = bc_d(new_writer_ins);
      uint32_t src_writer_pc = UINT32_MAX;
      BCOp src_writer_op = BC__MAX;
      BCIns src_writer_ins = 0;
      int have_src_writer = 0;

      have_src_writer = tolua_find_nearest_reg_writer(buf, bc_pos, be, pc, mov_src,
                                                      &src_writer_pc, &src_writer_op, &src_writer_ins);
      (void)src_writer_ins;
      if (have_src_writer &&
          src_writer_pc == old_next_writer_pc &&
          src_writer_op == old_next_writer_op &&
          old_next_writer_pc + 12 >= pc &&
          old_next_writer_op != BC_CALL &&
          old_next_writer_op != BC_CALLM &&
          old_next_writer_op != BC_CALLT &&
          old_next_writer_op != BC_CALLMT &&
          old_next_writer_op != BC_VARG &&
          old_next_writer_op != BC_ITERC &&
          old_next_writer_op != BC_ITERN) {
        TOLUA_REPACK_LOG(ctx, pc,
                         "reject existing FR2 slice: MOV first-arg pulls old-second old=%u(pc=%u,%s) old2=%u(pc=%u,%s) new=%u(pc=%u,%s src=%u)",
                         (unsigned int)old_reg, (unsigned int)old_writer_pc, tolua_bc_opname(old_writer_op),
                         (unsigned int)(old_reg + 1), (unsigned int)old_next_writer_pc, tolua_bc_opname(old_next_writer_op),
                         (unsigned int)new_reg, (unsigned int)new_writer_pc, tolua_bc_opname(new_writer_op),
                         (unsigned int)mov_src);
        return 0;
      }
    }
  }
  if (new_writer_pc == old_writer_pc) return 1;
  if (tolua_ins_reads_reg(new_writer_op, new_writer_ins, old_reg) &&
      new_writer_op != BC_CALL &&
      new_writer_op != BC_CALLM &&
      new_writer_op != BC_VARG &&
      new_writer_op != BC_ITERL &&
      new_writer_op != BC_IITERL &&
      new_writer_op != BC_JITERL &&
      new_writer_op != BC_ITERC &&
      new_writer_op != BC_ITERN) return 1;

  /* Source FR1 call arg1 lives in old_reg(A+1). If old_reg is written much
     later than new_reg(A+2) right before the call, reusing the existing FR2
     slice would keep a stale value in new_reg and drop the actual arg write. */
  if (old_writer_pc > new_writer_pc &&
      old_writer_pc + 8 >= pc) {
    TOLUA_REPACK_LOG(ctx, pc,
                     "reject existing FR2 slice: stale new-first old=%u(pc=%u,%s) new=%u(pc=%u,%s)",
                     (unsigned int)old_reg, (unsigned int)old_writer_pc, tolua_bc_opname(old_writer_op),
                     (unsigned int)new_reg, (unsigned int)new_writer_pc, tolua_bc_opname(new_writer_op));
    return 0;
  }

  /* Guard method-style calls seeded by MOV self (usually from reg 0) and
     function loaded from object field (TGETS base=0). Reusing existing FR2
     slice here can silently drop/reorder self. */
  if (old_writer_op == BC_MOV && bc_d(old_writer_ins) == 0 &&
      (new_writer_op == BC_KSTR || new_writer_op == BC_CAT ||
       new_writer_op == BC_TGETS || new_writer_op == BC_TGETV ||
       new_writer_op == BC_TGETB || new_writer_op == BC_GGET ||
       new_writer_op == BC_UGET) &&
      (new_writer_op != BC_MOV || bc_d(new_writer_ins) != bc_d(old_writer_ins))) {
    BCReg func_reg = old_reg > 0 ? (BCReg)(old_reg - 1) : 0;
    uint32_t func_writer_pc = UINT32_MAX;
    BCOp func_writer_op = BC__MAX;
    BCIns func_writer_ins = 0;
    int have_func_writer = 0;

    if (old_reg > 0) {
      have_func_writer = tolua_find_nearest_reg_writer(buf, bc_pos, be, pc, func_reg,
                                                       &func_writer_pc, &func_writer_op, &func_writer_ins);
    }

    if (have_func_writer &&
        func_writer_op == BC_TGETS &&
        bc_b(func_writer_ins) == 0 &&
        func_writer_pc + 8 >= pc) {
      TOLUA_REPACK_LOG(ctx, pc,
                       "reject existing FR2 slice: method-like MOV-self seed old=%u(pc=%u,%s src=%u) new=%u(pc=%u,%s)",
                       (unsigned int)old_reg, (unsigned int)old_writer_pc, tolua_bc_opname(old_writer_op),
                       (unsigned int)bc_d(old_writer_ins),
                       (unsigned int)new_reg, (unsigned int)new_writer_pc, tolua_bc_opname(new_writer_op));
      return 0;
    }
  }

  if ((old_writer_op == BC_TGETS || old_writer_op == BC_TGETV || old_writer_op == BC_TGETB ||
       old_writer_op == BC_UGET || old_writer_op == BC_GGET) &&
      (new_writer_op == BC_KPRI || new_writer_op == BC_KNIL || new_writer_op == BC_KSTR ||
       new_writer_op == BC_KSHORT || new_writer_op == BC_KNUM) &&
      old_writer_pc + 4 >= pc) {
    TOLUA_REPACK_LOG(ctx, pc,
                     "reject existing FR2 slice: first-arg seed old=%u(pc=%u,%s) new=%u(pc=%u,%s)",
                     (unsigned int)old_reg, (unsigned int)old_writer_pc, tolua_bc_opname(old_writer_op),
                     (unsigned int)new_reg, (unsigned int)new_writer_pc, tolua_bc_opname(new_writer_op));
    return 0;
  }

  return 1;
}

static int tolua_try_accept_existing_fr2_slice(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                               uint32_t pc, BCReg new_first, BCReg new_last,
                                               uint8_t *framesize_io, const uint8_t *targets,
                                               const tolua_bcdebug_ctx *ctx, int *handled)
{
  BCIns consumer = 0;
  BCOp consumer_op = BC__MAX;
  uint8_t *selected = NULL;
  int min_window = (int)pc;
  uint32_t interference_pc = UINT32_MAX;
  BCOp interference_op = BC__MAX;
  BCReg interference_reg = 0;
  int status = TOLUA_BCCONV_OK;

  *handled = 0;
  if (new_first > new_last || new_last > BCMAX_A) return TOLUA_BCCONV_OK;
  consumer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  consumer_op = bc_op(consumer);
  if (consumer_op == BC_CALL && bc_b(consumer) == 1 && bc_c(consumer) > 2 && pc >= 2) {
    BCReg old_first = (BCReg)(bc_a(consumer) + 1);
    BCReg old_last = (BCReg)(bc_a(consumer) + bc_c(consumer) - 1);
    BCIns prev1 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
    BCIns prev2 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 2) * 4, be);
    if (bc_op(prev1) == BC_FNEW &&
        bc_a(prev1) == old_last &&
        bc_op(prev2) == BC_CAT &&
        bc_a(prev2) == (BCReg)(old_first + 1) &&
        bc_b(prev2) == (BCReg)(old_first + 1) &&
        bc_c(prev2) >= (BCReg)(old_first + 2)) {
      TOLUA_REPACK_LOG(ctx, pc,
                       "skip existing FR2 slice for CAT+FNEW callback call base=%u old=[%u,%u] new=[%u,%u]",
                       (unsigned int)bc_a(consumer),
                       (unsigned int)old_first, (unsigned int)old_last,
                       (unsigned int)new_first, (unsigned int)new_last);
      return TOLUA_BCCONV_OK;
    }
  }
  if (consumer_op == BC_CALL && bc_c(consumer) > 2 && pc >= 1) {
    BCIns prev1 = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
    if (bc_op(prev1) == BC_TSETM) {
      BCReg old_first = new_first > 0 ? (BCReg)(new_first - 1) : 0;
      BCReg old_last = new_last > 0 ? (BCReg)(new_last - 1) : 0;
      TOLUA_REPACK_LOG(ctx, pc,
                       "skip existing FR2 slice for CALL+TSETM variadic chain base=%u old=[%u,%u] new=[%u,%u] tsetm_a=%u",
                       (unsigned int)bc_a(consumer),
                       (unsigned int)old_first, (unsigned int)old_last,
                       (unsigned int)new_first, (unsigned int)new_last,
                       (unsigned int)bc_a(prev1));
      return TOLUA_BCCONV_OK;
    }
  }
  if ((consumer_op == BC_CALL && bc_c(consumer) >= 2) ||
      (consumer_op == BC_CALLT && bc_d(consumer) >= 2)) {
    BCReg old_first = 0;
    BCReg old_last = 0;

    if (new_first == 0 || new_last == 0) return TOLUA_BCCONV_OK;
    old_first = (BCReg)(new_first - 1);
    old_last = (BCReg)(new_last - 1);
    if (!tolua_existing_fr2_call_args_are_aligned(buf, bc_pos, be, pc,
                                                  old_first, old_last,
                                                  new_first, new_last, ctx)) {
      return TOLUA_BCCONV_OK;
    }
  }

  selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!selected) {
    return tolua_failbytecodeproto(ctx, pc,
                                   (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be),
                                   bc_op((BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be)),
                                   TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate existing-FR2 slice buffer");
  }

  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc, new_first, new_last,
                                 targets, selected, &min_window,
                                 &interference_pc, &interference_op, &interference_reg) &&
      !tolua_try_select_simple_local_defs(buf, bc_pos, be, pc, new_first, new_last,
                                          selected, &min_window)) {
    free(selected);
    return TOLUA_BCCONV_OK;
  }

  status = tolua_update_framesize_checked(framesize_io, new_last, ctx, pc,
                                          (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be),
                                          bc_op((BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be)));
  free(selected);
  if (status != TOLUA_BCCONV_OK) return status;

  TOLUA_REPACK_LOG(ctx, pc, "accept existing FR2 slice [%u,%u]",
                   (unsigned int)new_first, (unsigned int)new_last);
  *handled = 1;
  return TOLUA_BCCONV_OK;
}

static int tolua_try_insert_copy_fallback_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                   uint32_t pc, BCReg old_first, BCReg old_last,
                                                   BCReg new_first, BCReg new_last,
                                                   uint8_t *framesize_io, const uint8_t *targets,
                                                   const tolua_bcdebug_ctx *ctx,
                                                   const char *reason, int *handled)
{
  uint32_t copy_count = 0;
  uint32_t insert_pc = pc;
  int allow_target_entry = 0;
  BCReg copy_dst[TOLUA_MAX_INSERT_COPIES];
  BCReg copy_src[TOLUA_MAX_INSERT_COPIES];
  uint32_t i = 0;
  int deferred_overwrite = 0;
  int status = TOLUA_BCCONV_OK;
  uint32_t first_touch_pc = UINT32_MAX;
  BCOp first_touch_op = BC__MAX;
  const char *first_touch_kind = "none";
  uint32_t live_scan = 0;

  *handled = 0;
  if (tolua_pending_insert_copy.active) {
    TOLUA_REPACK_LOG(ctx, pc, "copy-fallback skip reason=%s pending-insert", reason ? reason : "unknown");
    return TOLUA_BCCONV_OK;
  }
  if (targets[pc]) {
    BCIns consumer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
    BCOp consumer_op = bc_op(consumer);

    if (consumer_op == BC_CALL || consumer_op == BC_CALLT) {
      allow_target_entry = 1;
      insert_pc = pc;
      TOLUA_REPACK_LOG(ctx, pc,
                       "copy-fallback allow target-entry reason=%s op=%s insert_pc=%u",
                       reason ? reason : "unknown", tolua_bc_opname(consumer_op),
                       (unsigned int)insert_pc);
    } else if (consumer_op == BC_ITERC && pc > 0 && !targets[pc - 1]) {
      BCIns prev = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
      if (bc_op(prev) == BC_CALL && bc_b(prev) == 1) {
        allow_target_entry = 1;
        insert_pc = pc;
        TOLUA_REPACK_LOG(ctx, pc,
                         "copy-fallback keep target-entry reason=%s protect-adjacent-call insert_pc=%u",
                         reason ? reason : "unknown", (unsigned int)insert_pc);
      } else {
        insert_pc = pc - 1;
        TOLUA_REPACK_LOG(ctx, pc,
                         "copy-fallback retarget reason=%s target-entry insert_pc=%u",
                         reason ? reason : "unknown", (unsigned int)insert_pc);
      }
    } else if (consumer_op == BC_ITERC) {
      allow_target_entry = 1;
      insert_pc = pc;
      TOLUA_REPACK_LOG(ctx, pc,
                       "copy-fallback allow target-entry reason=%s insert_pc=%u",
                       reason ? reason : "unknown", (unsigned int)insert_pc);
    } else {
      TOLUA_REPACK_LOG(ctx, pc, "copy-fallback skip reason=%s target-entry", reason ? reason : "unknown");
      return TOLUA_BCCONV_OK;
    }
  }
  if (old_first > old_last || new_first > new_last) {
    TOLUA_REPACK_LOG(ctx, pc, "copy-fallback skip reason=%s invalid-range old=[%u,%u] new=[%u,%u]",
                     reason ? reason : "unknown",
                     (unsigned int)old_first, (unsigned int)old_last,
                     (unsigned int)new_first, (unsigned int)new_last);
    return TOLUA_BCCONV_OK;
  }

  copy_count = (uint32_t)(old_last - old_first + 1);
  if (copy_count == 0 || copy_count > TOLUA_MAX_INSERT_COPIES) return TOLUA_BCCONV_OK;

  if (tolua_reg_live_after_pc(buf, bc_pos, numbc, be, pc + 1, new_last)) {
    for (live_scan = pc + 1; live_scan < numbc; live_scan++) {
      BCIns live_ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)live_scan * 4, be);
      BCOp live_op = bc_op(live_ins);

      if (tolua_ins_reads_reg(live_op, live_ins, new_last)) {
        first_touch_pc = live_scan;
        first_touch_op = live_op;
        first_touch_kind = "read";
        break;
      }
      if (tolua_ins_writes_reg(live_op, live_ins, new_last)) {
        first_touch_pc = live_scan;
        first_touch_op = live_op;
        first_touch_kind = "write";
        break;
      }
    }

    deferred_overwrite = tolua_future_fr2_arg_shift_writes_reg(buf, bc_pos, numbc, be, pc + 1, new_last);
    if (!deferred_overwrite) {
      if (first_touch_pc != UINT32_MAX &&
          first_touch_kind[0] == 'r' &&
          copy_count + 1 <= TOLUA_MAX_INSERT_COPIES) {
        uint32_t first_write_pc = UINT32_MAX;
        uint32_t stop_pc = numbc;
        BCReg spill = BCMAX_A;
        BCReg cand = 0;
        int rewrite_ok = 1;

        for (live_scan = pc + 1; live_scan < numbc; live_scan++) {
          BCIns live_ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)live_scan * 4, be);
          BCOp live_op = bc_op(live_ins);
          if (tolua_ins_writes_reg(live_op, live_ins, new_last)) {
            first_write_pc = live_scan;
            stop_pc = live_scan;
            break;
          }
        }

        for (cand = *framesize_io; cand <= BCMAX_A; cand++) {
          if (cand >= new_first && cand <= new_last) continue;
          if (tolua_window_touches_range(buf, bc_pos, be, (int)(pc + 1), stop_pc, cand, cand)) continue;
          spill = cand;
          break;
        }

        if (spill <= BCMAX_A) {
          for (live_scan = pc + 1; live_scan < stop_pc; live_scan++) {
            BCIns live_ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)live_scan * 4, be);
            BCOp live_op = bc_op(live_ins);
            if (!tolua_ins_reads_reg(live_op, live_ins, new_last)) continue;
            if (!tolua_can_rewrite_ins_source_reg(live_ins, live_op, new_last)) {
              rewrite_ok = 0;
              break;
            }
          }
        } else {
          rewrite_ok = 0;
        }

        if (rewrite_ok) {
          BCReg max_last = new_last;

          for (live_scan = pc + 1; live_scan < stop_pc; live_scan++) {
            uint8_t *slot = buf + bc_pos + (size_t)live_scan * 4;
            BCIns live_ins = (BCIns)tolua_read_ins(slot, be);
            BCOp live_op = bc_op(live_ins);
            if (!tolua_rewrite_ins_source_reg(&live_ins, live_op, new_last, spill)) continue;
            tolua_write_ins(slot, (uint32_t)live_ins, be);
          }

          copy_dst[0] = spill;
          copy_src[0] = new_last;
          for (i = 0; i < copy_count; i++) {
            uint32_t rev = copy_count - 1 - i;
            copy_dst[i + 1] = (BCReg)(new_first + rev);
            copy_src[i + 1] = (BCReg)(old_first + rev);
          }

          if (tolua_schedule_insert_copies(ctx, pc, insert_pc, copy_dst, copy_src, (uint8_t)(copy_count + 1))) {
            tolua_pending_insert_copy.allow_target_entry = (uint8_t)allow_target_entry;
            if (spill > max_last) max_last = spill;
            status = tolua_update_framesize_checked(framesize_io, max_last, ctx, pc,
                                                    (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be),
                                                    bc_op((BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be)));
            if (status != TOLUA_BCCONV_OK) return status;
            TOLUA_REPACK_LOG(ctx, pc,
                             "copy-fallback spill reason=%s insert_pc=%u spill=%u first_touch=%s pc=%u op=%s shift_count=%u",
                             reason ? reason : "unknown",
                             (unsigned int)insert_pc, (unsigned int)spill, first_touch_kind,
                             (unsigned int)first_touch_pc, tolua_bc_opname(first_touch_op),
                             (unsigned int)copy_count);
            *handled = 1;
            return TOLUA_BCCONV_INTERNAL_INSERT_COPY;
          }
        }
      }
      TOLUA_REPACK_LOG(ctx, pc,
                       "copy-fallback skip reason=%s live-after first_touch=%s pc=%u op=%s",
                       reason ? reason : "unknown", first_touch_kind,
                       (unsigned int)first_touch_pc, tolua_bc_opname(first_touch_op));
      return TOLUA_BCCONV_OK;
    }
  } else {
    first_touch_pc = UINT32_MAX;
    first_touch_op = BC__MAX;
    first_touch_kind = "none";
  }

  for (i = 0; i < copy_count; i++) {
    uint32_t rev = copy_count - 1 - i;
    copy_dst[i] = (BCReg)(new_first + rev);
    copy_src[i] = (BCReg)(old_first + rev);
  }

  if (!tolua_schedule_insert_copies(ctx, pc, insert_pc, copy_dst, copy_src, (uint8_t)copy_count)) {
    return TOLUA_BCCONV_OK;
  }
  tolua_pending_insert_copy.allow_target_entry = (uint8_t)allow_target_entry;

  status = tolua_update_framesize_checked(framesize_io, new_last, ctx, pc,
                                          (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be),
                                          bc_op((BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be)));
  if (status != TOLUA_BCCONV_OK) return status;

  TOLUA_REPACK_LOG(ctx, pc,
                   "copy-fallback reason=%s insert_pc=%u count=%u range=[%u,%u]->[%u,%u] deferred=%u",
                   reason ? reason : "unknown",
                   (unsigned int)insert_pc,
                   (unsigned int)copy_count,
                   (unsigned int)old_first, (unsigned int)old_last,
                   (unsigned int)new_first, (unsigned int)new_last,
                   (unsigned int)deferred_overwrite);
  *handled = 1;
  return TOLUA_BCCONV_INTERNAL_INSERT_COPY;
}

static int tolua_try_fix_cat_call_chain_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                uint32_t pc, uint8_t *framesize_io,
                                                const uint8_t *targets,
                                                const tolua_bcdebug_ctx *ctx, int *handled)
{
  BCIns call = 0;
  BCIns consumer = 0;
  BCIns cat = 0;
  BCReg old_base = 0;
  BCReg old_arg_first = 0;
  BCReg old_cat_last = 0;
  BCReg new_base = 0;
  BCReg new_cat_first = 0;
  BCReg new_cat_last = 0;
  uint8_t *func_selected = NULL;
  uint8_t *cat_selected = NULL;
  uint8_t *combined_selected = NULL;
  int func_min_window = (int)pc;
  int cat_min_window = (int)pc;
  uint32_t interference_pc = UINT32_MAX;
  BCOp interference_op = BC__MAX;
  BCReg interference_reg = 0;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *handled = 0;
  if (pc == 0 || pc + 1 >= numbc) return TOLUA_BCCONV_OK;

  call = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  consumer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc + 1) * 4, be);
  cat = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
  if (bc_op(call) != BC_CALL || bc_op(consumer) != BC_CALL || bc_op(cat) != BC_CAT) {
    return TOLUA_BCCONV_OK;
  }
  if (bc_b(call) != 2 || bc_c(call) != 2 || bc_c(consumer) != 2) {
    return TOLUA_BCCONV_OK;
  }

  old_base = bc_a(call);
  old_arg_first = (BCReg)(old_base + 1);
  old_cat_last = bc_c(cat);
  if (bc_a(consumer) + bc_c(consumer) - 1 != old_base) return TOLUA_BCCONV_OK;
  if (bc_a(cat) != old_arg_first || bc_b(cat) != old_arg_first || old_cat_last < old_arg_first) {
    return TOLUA_BCCONV_OK;
  }

  TOLUA_REPACK_LOG(ctx, pc,
                   "cat-call match producer_base=%u arg=[%u,%u] consumer_base=%u",
                   (unsigned int)old_base, (unsigned int)old_arg_first,
                   (unsigned int)old_cat_last, (unsigned int)bc_a(consumer));

  new_base = (BCReg)(old_base + 1);
  new_cat_first = (BCReg)(old_arg_first + 2);
  new_cat_last = (BCReg)(old_cat_last + 2);
  if (new_cat_last > BCMAX_A) {
    return tolua_failbytecodeproto(ctx, pc, call, BC_CALL, TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                   "FR2 CAT-call chain exceeds register limit");
  }

  func_selected = (uint8_t *)calloc((size_t)numbc, 1);
  cat_selected = (uint8_t *)calloc((size_t)numbc, 1);
  combined_selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!func_selected || !cat_selected || !combined_selected) {
    free(combined_selected);
    free(cat_selected);
    free(func_selected);
    return tolua_failbytecodeproto(ctx, pc, call, BC_CALL, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate FR2 CAT-call chain slices");
  }

  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc, old_base, old_base,
                                 targets, func_selected, &func_min_window,
                                 &interference_pc, &interference_op, &interference_reg)) {
    TOLUA_REPACK_LOG(ctx, pc, "cat-call func-slice reject");
    free(combined_selected);
    free(cat_selected);
    free(func_selected);
    return TOLUA_BCCONV_OK;
  }
  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc - 1, old_arg_first, old_cat_last,
                                 targets, cat_selected, &cat_min_window,
                                 &interference_pc, &interference_op, &interference_reg)) {
    TOLUA_REPACK_LOG(ctx, pc, "cat-call arg-slice reject blocker_pc=%u op=%s reg=%u",
                     (unsigned int)interference_pc, tolua_bc_opname(interference_op),
                     (unsigned int)interference_reg);
    free(combined_selected);
    free(cat_selected);
    free(func_selected);
    return TOLUA_BCCONV_OK;
  }
  cat_selected[pc - 1] = 1;

  for (scan = 0; scan < (int)numbc; scan++) {
    combined_selected[scan] = (uint8_t)(func_selected[scan] || cat_selected[scan]);
  }

  if (tolua_window_has_nonselected_touch(buf, bc_pos, be, func_min_window, pc,
                                         combined_selected, new_base, new_base,
                                         &interference_pc, &interference_op, &interference_reg) ||
      tolua_window_has_nonselected_touch(buf, bc_pos, be, cat_min_window, pc,
                                         combined_selected, new_cat_first, new_cat_last,
                                         &interference_pc, &interference_op, &interference_reg)) {
    TOLUA_REPACK_LOG(ctx, pc, "cat-call target-touch reject blocker_pc=%u op=%s reg=%u",
                     (unsigned int)interference_pc, tolua_bc_opname(interference_op),
                     (unsigned int)interference_reg);
    free(combined_selected);
    free(cat_selected);
    free(func_selected);
    return TOLUA_BCCONV_OK;
  }

  if (tolua_reg_live_after_pc(buf, bc_pos, numbc, be, pc + 1, new_cat_last)) {
    TOLUA_REPACK_LOG(ctx, pc, "cat-call live-after reject reg=%u", (unsigned int)new_cat_last);
    free(combined_selected);
    free(cat_selected);
    free(func_selected);
    return TOLUA_BCCONV_OK;
  }

  for (scan = func_min_window; scan < (int)pc; scan++) {
    uint8_t *slot = NULL;
    BCIns ins = 0;
    BCOp op = BC__MAX;

    if (!func_selected[scan]) continue;
    slot = buf + bc_pos + (size_t)scan * 4;
    ins = (BCIns)tolua_read_ins(slot, be);
    op = bc_op(ins);
    tolua_repack_remap_reg_range(&ins, op, old_base, old_base, new_base);
    tolua_write_ins(slot, (uint32_t)ins, be);
  }

  for (scan = cat_min_window; scan < (int)pc; scan++) {
    uint8_t *slot = NULL;
    BCIns ins = 0;
    BCOp op = BC__MAX;

    if (!cat_selected[scan]) continue;
    slot = buf + bc_pos + (size_t)scan * 4;
    ins = (BCIns)tolua_read_ins(slot, be);
    op = bc_op(ins);
    tolua_repack_remap_reg_range(&ins, op, old_arg_first, old_cat_last, new_cat_first);
    tolua_write_ins(slot, (uint32_t)ins, be);
  }

  setbc_a(&call, new_base);
  tolua_write_ins(buf + bc_pos + (size_t)pc * 4, (uint32_t)call, be);

  status = tolua_update_framesize_checked(framesize_io, new_cat_last, ctx, pc, call, BC_CALL);
  free(combined_selected);
  free(cat_selected);
  free(func_selected);
  if (status != TOLUA_BCCONV_OK) return status;

  TOLUA_REPACK_LOG(ctx, pc, "cat-call success new_base=%u new_arg=[%u,%u]",
                   (unsigned int)new_base, (unsigned int)new_cat_first,
                   (unsigned int)new_cat_last);
  *handled = 1;
  return TOLUA_BCCONV_OK;
}

static int tolua_try_fix_tail_call_arg_chain_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                      uint32_t pc, uint8_t *framesize_io,
                                                      const uint8_t *targets,
                                                      const tolua_bcdebug_ctx *ctx, int *handled)
{
  BCIns consumer = 0;
  BCIns producer = 0;
  BCOp consumer_op = BC__MAX;
  BCReg consumer_base = 0;
  BCReg consumer_old_first = 0;
  BCReg consumer_old_last = 0;
  BCReg prefix_old_last = 0;
  int have_prefix = 0;
  BCReg producer_base = 0;
  BCReg producer_cur_arg_first = 0;
  BCReg producer_cur_arg_last = 0;
  BCReg prefix_new_first = 0;
  BCReg prefix_new_last = 0;
  BCReg producer_new_base = 0;
  BCReg producer_new_arg_first = 0;
  BCReg producer_new_arg_last = 0;
  uint8_t *prefix_selected = NULL;
  uint8_t *producer_base_selected = NULL;
  uint8_t *producer_arg_selected = NULL;
  uint8_t *combined_selected = NULL;
  int prefix_min_window = (int)pc;
  int producer_base_min_window = (int)pc;
  int producer_arg_min_window = (int)pc;
  int start_window = (int)pc;
  uint32_t interference_pc = UINT32_MAX;
  BCOp interference_op = BC__MAX;
  BCReg interference_reg = 0;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *handled = 0;
  if (pc == 0) return TOLUA_BCCONV_OK;

  consumer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  producer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
  consumer_op = bc_op(consumer);
  TOLUA_REPACK_LOG(ctx, pc,
                   "call-chain helper enter consumer=%s producer=%s producer_b=%u consumer_span=%u producer_c=%u",
                   tolua_bc_opname(consumer_op), tolua_bc_opname(bc_op(producer)),
                   (unsigned int)bc_b(producer),
                   (unsigned int)(consumer_op == BC_CALL ? bc_c(consumer) : bc_d(consumer)),
                   (unsigned int)bc_c(producer));
  if ((consumer_op != BC_CALL && consumer_op != BC_CALLT) ||
      bc_op(producer) != BC_CALL || bc_b(producer) != 2) {
    return TOLUA_BCCONV_OK;
  }

  consumer_base = bc_a(consumer);
  if ((consumer_op == BC_CALL ? bc_c(consumer) : bc_d(consumer)) <= 1) return TOLUA_BCCONV_OK;
  consumer_old_first = (BCReg)(consumer_base + 1);
  consumer_old_last = (BCReg)(consumer_base +
                              (consumer_op == BC_CALL ? bc_c(consumer) : bc_d(consumer)) - 1);
  producer_base = bc_a(producer);
  if (producer_base != consumer_old_last || bc_c(producer) <= 1) {
    return TOLUA_BCCONV_OK;
  }

  prefix_old_last = (BCReg)(producer_base - 1);
  have_prefix = consumer_old_first <= prefix_old_last;

  producer_cur_arg_first = (BCReg)(producer_base + 2);
  producer_cur_arg_last = (BCReg)(producer_base + bc_c(producer));
  prefix_new_first = (BCReg)(consumer_old_first + 1);
  prefix_new_last = (BCReg)(prefix_old_last + 1);
  producer_new_base = (BCReg)(producer_base + 1);
  producer_new_arg_first = (BCReg)(producer_cur_arg_first + 1);
  producer_new_arg_last = (BCReg)(producer_cur_arg_last + 1);

  if (producer_new_arg_last > BCMAX_A) {
    return tolua_failbytecodeproto(ctx, pc, consumer, consumer_op,
                                   TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                   "FR2 call-chain exceeds register limit");
  }

  prefix_selected = (uint8_t *)calloc((size_t)numbc, 1);
  producer_base_selected = (uint8_t *)calloc((size_t)numbc, 1);
  producer_arg_selected = (uint8_t *)calloc((size_t)numbc, 1);
  combined_selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!prefix_selected || !producer_base_selected || !producer_arg_selected || !combined_selected) {
    free(combined_selected);
    free(producer_arg_selected);
    free(producer_base_selected);
    free(prefix_selected);
    return tolua_failbytecodeproto(ctx, pc, consumer, consumer_op,
                                   TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate FR2 call-chain slices");
  }

  if (((have_prefix &&
        !tolua_select_repack_slice(buf, bc_pos, numbc, be, pc, consumer_old_first, prefix_old_last,
                                   targets, prefix_selected, &prefix_min_window,
                                   &interference_pc, &interference_op, &interference_reg) &&
        !tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, pc,
                                                             consumer_old_first, prefix_old_last,
                                                             targets, prefix_selected, &prefix_min_window,
                                                             &interference_pc, &interference_op,
                                                             &interference_reg))) ||
      (!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc - 1, producer_base, producer_base,
                                  targets, producer_base_selected, &producer_base_min_window,
                                  &interference_pc, &interference_op, &interference_reg) &&
       !tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, pc - 1,
                                                            producer_base, producer_base,
                                                            targets, producer_base_selected,
                                                            &producer_base_min_window,
                                                            &interference_pc, &interference_op,
                                                            &interference_reg)) ||
      ((!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc - 1,
                                   producer_cur_arg_first, producer_cur_arg_last,
                                   targets, producer_arg_selected, &producer_arg_min_window,
                                   &interference_pc, &interference_op, &interference_reg) &&
        !tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, pc - 1,
                                                             producer_cur_arg_first, producer_cur_arg_last,
                                                             targets, producer_arg_selected,
                                                             &producer_arg_min_window,
                                                             &interference_pc, &interference_op,
                                                             &interference_reg)) &&
       !tolua_try_select_simple_local_defs(buf, bc_pos, be, pc - 1,
                                           producer_cur_arg_first, producer_cur_arg_last,
                                           producer_arg_selected, &producer_arg_min_window))) {
    TOLUA_REPACK_LOG(ctx, pc,
                     "tail-call chain slice reject blocker_pc=%u op=%s reg=%u",
                     (unsigned int)interference_pc, tolua_bc_opname(interference_op),
                     (unsigned int)interference_reg);
    free(combined_selected);
    free(producer_arg_selected);
    free(producer_base_selected);
    free(prefix_selected);
    return TOLUA_BCCONV_OK;
  }
  producer_base_selected[pc - 1] = 1;
  producer_arg_selected[pc - 1] = 1;

  for (scan = 0; scan < (int)numbc; scan++) {
    combined_selected[scan] = (uint8_t)(prefix_selected[scan] ||
                                        producer_base_selected[scan] ||
                                        producer_arg_selected[scan]);
  }

  start_window = have_prefix ? prefix_min_window : producer_base_min_window;
  if (producer_base_min_window < start_window) start_window = producer_base_min_window;
  if (producer_arg_min_window < start_window) start_window = producer_arg_min_window;
  if (((have_prefix &&
        tolua_window_has_nonselected_touch(buf, bc_pos, be, start_window, pc, combined_selected,
                                           prefix_new_first, prefix_new_last,
                                           &interference_pc, &interference_op, &interference_reg))) ||
      tolua_window_has_nonselected_touch(buf, bc_pos, be, start_window, pc, combined_selected,
                                         producer_new_base, producer_new_base,
                                         &interference_pc, &interference_op, &interference_reg) ||
      tolua_window_has_nonselected_touch(buf, bc_pos, be, start_window, pc, combined_selected,
                                         producer_new_arg_first, producer_new_arg_last,
                                         &interference_pc, &interference_op, &interference_reg)) {
    TOLUA_REPACK_LOG(ctx, pc,
                     "tail-call chain target-touch reject blocker_pc=%u op=%s reg=%u",
                     (unsigned int)interference_pc, tolua_bc_opname(interference_op),
                     (unsigned int)interference_reg);
    free(combined_selected);
    free(producer_arg_selected);
    free(producer_base_selected);
    free(prefix_selected);
    return TOLUA_BCCONV_OK;
  }

  for (scan = start_window; scan < (int)pc; scan++) {
    uint8_t *slot = NULL;
    BCIns ins = 0;
    BCOp op = BC__MAX;

    slot = buf + bc_pos + (size_t)scan * 4;
    ins = (BCIns)tolua_read_ins(slot, be);
    op = bc_op(ins);

    if (have_prefix && prefix_selected[scan]) {
      tolua_repack_remap_reg_range(&ins, op, consumer_old_first, prefix_old_last, prefix_new_first);
    }
    if (producer_base_selected[scan]) {
      tolua_repack_remap_reg_range(&ins, op, producer_base, producer_base, producer_new_base);
    }
    if (producer_arg_selected[scan]) {
      tolua_repack_remap_reg_range(&ins, op, producer_cur_arg_first, producer_cur_arg_last,
                                   producer_new_arg_first);
    }
    tolua_write_ins(slot, (uint32_t)ins, be);
  }

  status = tolua_update_framesize_checked(framesize_io, producer_new_arg_last, ctx, pc,
                                          consumer, consumer_op);
  free(combined_selected);
  free(producer_arg_selected);
  free(producer_base_selected);
  free(prefix_selected);
  if (status != TOLUA_BCCONV_OK) return status;

  TOLUA_REPACK_LOG(ctx, pc,
                   "call-chain success prefix=[%u,%u]->[%u,%u] producer=%u args=[%u,%u]->base=%u args=[%u,%u]",
                   (unsigned int)consumer_old_first, (unsigned int)prefix_old_last,
                   (unsigned int)prefix_new_first, (unsigned int)prefix_new_last,
                   (unsigned int)producer_base, (unsigned int)producer_cur_arg_first,
                   (unsigned int)producer_cur_arg_last, (unsigned int)producer_new_base,
                   (unsigned int)producer_new_arg_first, (unsigned int)producer_new_arg_last);
  *handled = 1;
  return TOLUA_BCCONV_OK;
}

static int tolua_try_fix_call_method_basecopy_chain_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                             uint32_t pc, uint8_t *framesize_io,
                                                             const uint8_t *targets,
                                                             const tolua_bcdebug_ctx *ctx, int *handled)
{
  uint32_t producer_pc = 0;
  uint32_t root_pc = UINT32_MAX;
  uint32_t method_pc = UINT32_MAX;
  uint32_t selfcopy_pc = UINT32_MAX;
  BCIns consumer = 0;
  BCIns producer = 0;
  BCOp consumer_op = BC__MAX;
  BCReg consumer_base = 0;
  BCReg consumer_old_first = 0;
  BCReg consumer_old_last = 0;
  BCReg producer_base = 0;
  BCReg producer_cur_arg_first = 0;
  BCReg producer_cur_arg_last = 0;
  BCReg producer_new_base = 0;
  BCReg producer_new_arg_first = 0;
  BCReg producer_new_arg_last = 0;
  uint8_t *producer_base_selected = NULL;
  uint8_t *producer_arg_selected = NULL;
  uint8_t *combined_selected = NULL;
  int producer_arg_min_window = (int)pc;
  int start_window = (int)pc;
  uint32_t interference_pc = UINT32_MAX;
  BCOp interference_op = BC__MAX;
  BCReg interference_reg = 0;
  BCReg copy_dst[1];
  BCReg copy_src[1];
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *handled = 0;
  if (tolua_pending_insert_copy.active) return TOLUA_BCCONV_OK;
  if (pc == 0) return TOLUA_BCCONV_OK;

  consumer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  producer_pc = pc - 1;
  producer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)producer_pc * 4, be);
  consumer_op = bc_op(consumer);

  TOLUA_REPACK_LOG(ctx, pc,
                   "call-method-basecopy helper enter consumer=%s producer=%s producer_b=%u consumer_span=%u producer_c=%u",
                   tolua_bc_opname(consumer_op), tolua_bc_opname(bc_op(producer)),
                   (unsigned int)bc_b(producer),
                   (unsigned int)(consumer_op == BC_CALL ? bc_c(consumer) : bc_d(consumer)),
                   (unsigned int)bc_c(producer));

  if ((consumer_op != BC_CALL && consumer_op != BC_CALLT) ||
      bc_op(producer) != BC_CALL || bc_b(producer) != 2 || bc_c(producer) <= 2) {
    return TOLUA_BCCONV_OK;
  }

  consumer_base = bc_a(consumer);
  if ((consumer_op == BC_CALL ? bc_c(consumer) : bc_d(consumer)) != 2) {
    return TOLUA_BCCONV_OK;
  }
  consumer_old_first = (BCReg)(consumer_base + 1);
  consumer_old_last = (BCReg)(consumer_base +
                              (consumer_op == BC_CALL ? bc_c(consumer) : bc_d(consumer)) - 1);
  producer_base = bc_a(producer);
  if (producer_base != consumer_old_last) return TOLUA_BCCONV_OK;

  producer_cur_arg_first = (BCReg)(producer_base + 2);
  producer_cur_arg_last = (BCReg)(producer_base + bc_c(producer));
  producer_new_base = (BCReg)(producer_base + 1);
  producer_new_arg_first = (BCReg)(producer_cur_arg_first + 1);
  producer_new_arg_last = (BCReg)(producer_cur_arg_last + 1);

  if (producer_new_arg_last > BCMAX_A) {
    return tolua_failbytecodeproto(ctx, pc, consumer, consumer_op,
                                   TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                   "FR2 call-method-basecopy chain exceeds register limit");
  }

  for (scan = (int)producer_pc - 1; scan >= 0; scan--) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp op = bc_op(ins);

    if (method_pc == UINT32_MAX &&
        op == BC_TGETS &&
        bc_a(ins) == producer_base &&
        bc_b(ins) == producer_base) {
      method_pc = (uint32_t)scan;
      continue;
    }
    if (method_pc != UINT32_MAX &&
        selfcopy_pc == UINT32_MAX &&
        op == BC_MOV &&
        bc_a(ins) == producer_cur_arg_first &&
        bc_c(ins) == producer_base) {
      selfcopy_pc = (uint32_t)scan;
      continue;
    }
    if (method_pc != UINT32_MAX &&
        op == BC_CALL &&
        bc_b(ins) == 2 &&
        bc_a(ins) == producer_base) {
      root_pc = (uint32_t)scan;
      break;
    }
    if (method_pc != UINT32_MAX && tolua_ins_writes_reg(op, ins, producer_base)) {
      return TOLUA_BCCONV_OK;
    }
  }

  if (method_pc == UINT32_MAX || selfcopy_pc == UINT32_MAX || root_pc == UINT32_MAX) {
    return TOLUA_BCCONV_OK;
  }
  if (root_pc + 1 >= numbc || targets[root_pc + 1]) {
    return TOLUA_BCCONV_OK;
  }

  producer_base_selected = (uint8_t *)calloc((size_t)numbc, 1);
  producer_arg_selected = (uint8_t *)calloc((size_t)numbc, 1);
  combined_selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!producer_base_selected || !producer_arg_selected || !combined_selected) {
    free(combined_selected);
    free(producer_arg_selected);
    free(producer_base_selected);
    return tolua_failbytecodeproto(ctx, pc, consumer, consumer_op,
                                   TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate FR2 call-method-basecopy slices");
  }

  if ((!tolua_select_repack_slice(buf, bc_pos, numbc, be, producer_pc,
                                  producer_cur_arg_first, producer_cur_arg_last,
                                  targets, producer_arg_selected, &producer_arg_min_window,
                                  &interference_pc, &interference_op, &interference_reg) &&
       !tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, producer_pc,
                                                            producer_cur_arg_first, producer_cur_arg_last,
                                                            targets, producer_arg_selected,
                                                            &producer_arg_min_window,
                                                            &interference_pc, &interference_op,
                                                            &interference_reg) &&
       !tolua_try_select_simple_local_defs(buf, bc_pos, be, producer_pc,
                                           producer_cur_arg_first, producer_cur_arg_last,
                                           producer_arg_selected, &producer_arg_min_window))) {
    TOLUA_REPACK_LOG(ctx, pc,
                     "call-method-basecopy arg-slice reject blocker_pc=%u op=%s reg=%u",
                     (unsigned int)interference_pc, tolua_bc_opname(interference_op),
                     (unsigned int)interference_reg);
    free(combined_selected);
    free(producer_arg_selected);
    free(producer_base_selected);
    return TOLUA_BCCONV_OK;
  }

  for (scan = (int)root_pc + 1; scan < (int)producer_pc; scan++) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp op = bc_op(ins);

    if ((uint32_t)scan == selfcopy_pc ||
        (uint32_t)scan == method_pc ||
        tolua_ins_reads_reg(op, ins, producer_base) ||
        tolua_ins_writes_reg(op, ins, producer_base)) {
      producer_base_selected[scan] = 1;
    }
  }
  producer_base_selected[producer_pc] = 1;
  producer_arg_selected[producer_pc] = 1;

  for (scan = 0; scan < (int)numbc; scan++) {
    combined_selected[scan] = (uint8_t)(producer_base_selected[scan] ||
                                        producer_arg_selected[scan]);
  }

  start_window = (int)root_pc + 1;
  if (producer_arg_min_window < start_window) start_window = producer_arg_min_window;

  if (tolua_window_has_nonselected_touch(buf, bc_pos, be, start_window, pc, combined_selected,
                                         producer_new_base, producer_new_base,
                                         &interference_pc, &interference_op, &interference_reg) ||
      tolua_window_has_nonselected_touch(buf, bc_pos, be, start_window, pc, combined_selected,
                                         producer_new_arg_first, producer_new_arg_last,
                                         &interference_pc, &interference_op, &interference_reg)) {
    TOLUA_REPACK_LOG(ctx, pc,
                     "call-method-basecopy target-touch reject blocker_pc=%u op=%s reg=%u",
                     (unsigned int)interference_pc, tolua_bc_opname(interference_op),
                     (unsigned int)interference_reg);
    free(combined_selected);
    free(producer_arg_selected);
    free(producer_base_selected);
    return TOLUA_BCCONV_OK;
  }

  copy_dst[0] = producer_new_base;
  copy_src[0] = producer_base;
  if (!tolua_schedule_insert_copies(ctx, pc, root_pc + 1, copy_dst, copy_src, 1)) {
    free(combined_selected);
    free(producer_arg_selected);
    free(producer_base_selected);
    return TOLUA_BCCONV_OK;
  }

  for (scan = start_window; scan < (int)pc; scan++) {
    uint8_t *slot = NULL;
    BCIns ins = 0;
    BCOp op = BC__MAX;

    if (!combined_selected[scan]) continue;
    slot = buf + bc_pos + (size_t)scan * 4;
    ins = (BCIns)tolua_read_ins(slot, be);
    op = bc_op(ins);

    if (producer_base_selected[scan]) {
      tolua_repack_remap_reg_range(&ins, op, producer_base, producer_base, producer_new_base);
    }
    if (producer_arg_selected[scan]) {
      tolua_repack_remap_reg_range(&ins, op, producer_cur_arg_first, producer_cur_arg_last,
                                   producer_new_arg_first);
    }
    tolua_write_ins(slot, (uint32_t)ins, be);
  }

  status = tolua_update_framesize_checked(framesize_io, producer_new_arg_last, ctx, pc,
                                          consumer, consumer_op);
  free(combined_selected);
  free(producer_arg_selected);
  free(producer_base_selected);
  if (status != TOLUA_BCCONV_OK) return status;

  TOLUA_REPACK_LOG(ctx, pc,
                   "call-method-basecopy success root_pc=%u producer=%u new_base=%u args=[%u,%u]",
                   (unsigned int)root_pc, (unsigned int)producer_pc,
                   (unsigned int)producer_new_base,
                   (unsigned int)producer_new_arg_first,
                   (unsigned int)producer_new_arg_last);
  *handled = 1;
  return TOLUA_BCCONV_INTERNAL_INSERT_COPY;
}

static int tolua_try_fix_call_intermediate_producer_chain_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                                   uint32_t pc, uint8_t *framesize_io,
                                                                   const uint8_t *targets,
                                                                   const tolua_bcdebug_ctx *ctx, int *handled)
{
  BCIns consumer = 0;
  BCOp consumer_op = BC__MAX;
  BCReg consumer_base = 0;
  BCReg consumer_old_first = 0;
  BCReg consumer_old_last = 0;
  BCReg prefix_old_last = 0;
  BCReg producer_base = 0;
  BCReg producer_cur_arg_first = 0;
  BCReg producer_cur_arg_last = 0;
  BCReg prefix_new_first = 0;
  BCReg prefix_new_last = 0;
  BCReg producer_new_base = 0;
  BCReg producer_new_arg_first = 0;
  BCReg producer_new_arg_last = 0;
  uint32_t producer_pc = UINT32_MAX;
  uint8_t *prefix_selected = NULL;
  uint8_t *producer_base_selected = NULL;
  uint8_t *producer_arg_selected = NULL;
  uint8_t *combined_selected = NULL;
  int have_prefix = 0;
  int prefix_min_window = (int)pc;
  int producer_base_min_window = (int)pc;
  int producer_arg_min_window = (int)pc;
  int start_window = (int)pc;
  uint32_t interference_pc = UINT32_MAX;
  BCOp interference_op = BC__MAX;
  BCReg interference_reg = 0;
  BCReg max_last = 0;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *handled = 0;
  if (tolua_pending_insert_copy.active) return TOLUA_BCCONV_OK;
  if (pc == 0) return TOLUA_BCCONV_OK;

  consumer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  consumer_op = bc_op(consumer);
  if (consumer_op != BC_CALL && consumer_op != BC_CALLT) return TOLUA_BCCONV_OK;
  if ((consumer_op == BC_CALL ? bc_c(consumer) : bc_d(consumer)) <= 1) return TOLUA_BCCONV_OK;

  consumer_base = bc_a(consumer);
  consumer_old_first = (BCReg)(consumer_base + 1);
  consumer_old_last = (BCReg)(consumer_base +
                              (consumer_op == BC_CALL ? bc_c(consumer) : bc_d(consumer)) - 1);
  if (consumer_old_last > BCMAX_A) return TOLUA_BCCONV_OK;

  for (scan = (int)pc - 1; scan >= 0; scan--) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp op = bc_op(ins);

    if (op == BC_CALL &&
        bc_b(ins) == 2 &&
        bc_c(ins) > 1 &&
        bc_a(ins) == consumer_old_last) {
      producer_pc = (uint32_t)scan;
      break;
    }
  }

  if (producer_pc == UINT32_MAX || producer_pc + 1 >= pc) return TOLUA_BCCONV_OK;

  producer_base = consumer_old_last;
  producer_cur_arg_first = (BCReg)(producer_base + 2);
  producer_cur_arg_last = 0;
  {
    BCIns producer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)producer_pc * 4, be);
    producer_cur_arg_last = (BCReg)(producer_base + bc_c(producer));
  }

  if (producer_cur_arg_first > BCMAX_A || producer_cur_arg_last > BCMAX_A ||
      producer_cur_arg_first > producer_cur_arg_last) {
    return TOLUA_BCCONV_OK;
  }

  prefix_old_last = (BCReg)(consumer_old_last - 1);
  have_prefix = consumer_old_first <= prefix_old_last;
  prefix_new_first = (BCReg)(consumer_old_first + 1);
  prefix_new_last = (BCReg)(prefix_old_last + 1);
  producer_new_base = (BCReg)(producer_base + 1);
  producer_new_arg_first = (BCReg)(producer_cur_arg_first + 1);
  producer_new_arg_last = (BCReg)(producer_cur_arg_last + 1);

  if (producer_new_arg_last > BCMAX_A) {
    return tolua_failbytecodeproto(ctx, pc, consumer, consumer_op,
                                   TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                   "FR2 call-intermediate-producer chain exceeds register limit");
  }

  prefix_selected = (uint8_t *)calloc((size_t)numbc, 1);
  producer_base_selected = (uint8_t *)calloc((size_t)numbc, 1);
  producer_arg_selected = (uint8_t *)calloc((size_t)numbc, 1);
  combined_selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!prefix_selected || !producer_base_selected || !producer_arg_selected || !combined_selected) {
    free(combined_selected);
    free(producer_arg_selected);
    free(producer_base_selected);
    free(prefix_selected);
    return tolua_failbytecodeproto(ctx, pc, consumer, consumer_op,
                                   TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate FR2 call-intermediate-producer slices");
  }

  if ((have_prefix &&
       !tolua_select_repack_slice(buf, bc_pos, numbc, be, pc, consumer_old_first, prefix_old_last,
                                  targets, prefix_selected, &prefix_min_window,
                                  &interference_pc, &interference_op, &interference_reg) &&
       !tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, pc,
                                                            consumer_old_first, prefix_old_last,
                                                            targets, prefix_selected, &prefix_min_window,
                                                            &interference_pc, &interference_op,
                                                            &interference_reg) &&
       !tolua_try_select_simple_local_defs(buf, bc_pos, be, pc, consumer_old_first, prefix_old_last,
                                           prefix_selected, &prefix_min_window)) ||
      (!tolua_select_repack_slice(buf, bc_pos, numbc, be, producer_pc, producer_base, producer_base,
                                  targets, producer_base_selected, &producer_base_min_window,
                                  &interference_pc, &interference_op, &interference_reg) &&
       !tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, producer_pc,
                                                            producer_base, producer_base,
                                                            targets, producer_base_selected,
                                                            &producer_base_min_window,
                                                            &interference_pc, &interference_op,
                                                            &interference_reg) &&
       !tolua_try_select_simple_local_defs(buf, bc_pos, be, producer_pc, producer_base, producer_base,
                                           producer_base_selected, &producer_base_min_window)) ||
      (!tolua_select_repack_slice(buf, bc_pos, numbc, be, producer_pc,
                                  producer_cur_arg_first, producer_cur_arg_last,
                                  targets, producer_arg_selected, &producer_arg_min_window,
                                  &interference_pc, &interference_op, &interference_reg) &&
       !tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, producer_pc,
                                                            producer_cur_arg_first, producer_cur_arg_last,
                                                            targets, producer_arg_selected,
                                                            &producer_arg_min_window,
                                                            &interference_pc, &interference_op,
                                                            &interference_reg) &&
       !tolua_try_select_simple_local_defs(buf, bc_pos, be, producer_pc,
                                           producer_cur_arg_first, producer_cur_arg_last,
                                           producer_arg_selected, &producer_arg_min_window))) {
    TOLUA_REPACK_LOG(ctx, pc,
                     "call-intermediate-producer slice reject blocker_pc=%u op=%s reg=%u producer_pc=%u",
                     (unsigned int)interference_pc, tolua_bc_opname(interference_op),
                     (unsigned int)interference_reg, (unsigned int)producer_pc);
    free(combined_selected);
    free(producer_arg_selected);
    free(producer_base_selected);
    free(prefix_selected);
    return TOLUA_BCCONV_OK;
  }

  producer_base_selected[producer_pc] = 1;
  producer_arg_selected[producer_pc] = 1;
  for (scan = (int)producer_pc + 1; scan < (int)pc; scan++) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp op = bc_op(ins);
    if (tolua_ins_reads_reg(op, ins, producer_base) || tolua_ins_writes_reg(op, ins, producer_base)) {
      producer_base_selected[scan] = 1;
    }
  }

  for (scan = 0; scan < (int)numbc; scan++) {
    combined_selected[scan] = (uint8_t)(prefix_selected[scan] ||
                                        producer_base_selected[scan] ||
                                        producer_arg_selected[scan]);
  }

  start_window = have_prefix ? prefix_min_window : producer_base_min_window;
  if (producer_base_min_window < start_window) start_window = producer_base_min_window;
  if (producer_arg_min_window < start_window) start_window = producer_arg_min_window;

  for (scan = start_window + 1; scan < (int)pc; scan++) {
    if (!combined_selected[scan]) continue;
    if (targets[scan] &&
        tolua_target_has_external_entry(buf, bc_pos, numbc, be, start_window, pc, (uint32_t)scan)) {
      TOLUA_REPACK_LOG(ctx, pc,
                       "call-intermediate-producer reject target entry at pc=%d producer_pc=%u",
                       scan, (unsigned int)producer_pc);
      free(combined_selected);
      free(producer_arg_selected);
      free(producer_base_selected);
      free(prefix_selected);
      return TOLUA_BCCONV_OK;
    }
  }

  if ((have_prefix &&
       tolua_window_has_nonselected_touch(buf, bc_pos, be, start_window, pc, combined_selected,
                                          prefix_new_first, prefix_new_last,
                                          &interference_pc, &interference_op, &interference_reg)) ||
      tolua_window_has_nonselected_touch(buf, bc_pos, be, start_window, pc, combined_selected,
                                         producer_new_base, producer_new_base,
                                         &interference_pc, &interference_op, &interference_reg) ||
      tolua_window_has_nonselected_touch(buf, bc_pos, be, start_window, pc, combined_selected,
                                         producer_new_arg_first, producer_new_arg_last,
                                         &interference_pc, &interference_op, &interference_reg)) {
    TOLUA_REPACK_LOG(ctx, pc,
                     "call-intermediate-producer target-touch reject blocker_pc=%u op=%s reg=%u producer_pc=%u",
                     (unsigned int)interference_pc, tolua_bc_opname(interference_op),
                     (unsigned int)interference_reg, (unsigned int)producer_pc);
    free(combined_selected);
    free(producer_arg_selected);
    free(producer_base_selected);
    free(prefix_selected);
    return TOLUA_BCCONV_OK;
  }

  for (scan = start_window; scan < (int)pc; scan++) {
    uint8_t *slot = NULL;
    BCIns ins = 0;
    BCOp op = BC__MAX;
    int remap_base = 0;
    int remap_args = 0;
    int remap_prefix = 0;

    if (!combined_selected[scan]) continue;
    slot = buf + bc_pos + (size_t)scan * 4;
    ins = (BCIns)tolua_read_ins(slot, be);
    op = bc_op(ins);
    remap_base = producer_base_selected[scan];
    remap_args = producer_arg_selected[scan];
    remap_prefix = have_prefix && prefix_selected[scan];

    if (remap_base) {
      tolua_repack_remap_reg_range(&ins, op, producer_base, producer_base, producer_new_base);
    }
    if (remap_args) {
      tolua_repack_remap_reg_range(&ins, op, producer_cur_arg_first, producer_cur_arg_last,
                                   producer_new_arg_first);
    }
    if (remap_prefix) {
      tolua_repack_remap_reg_range(&ins, op, consumer_old_first, prefix_old_last, prefix_new_first);
    }
    tolua_write_ins(slot, (uint32_t)ins, be);

    if (remap_base) {
      tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, ins,
                                         producer_base, producer_base, producer_new_base);
    }
    if (remap_args) {
      tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, ins,
                                         producer_cur_arg_first, producer_cur_arg_last,
                                         producer_new_arg_first);
    }
    if (remap_prefix) {
      tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, ins,
                                         consumer_old_first, prefix_old_last, prefix_new_first);
    }
  }

  max_last = producer_new_arg_last;
  if (have_prefix && prefix_new_last > max_last) max_last = prefix_new_last;
  status = tolua_update_framesize_checked(framesize_io, max_last, ctx, pc, consumer, consumer_op);
  free(combined_selected);
  free(producer_arg_selected);
  free(producer_base_selected);
  free(prefix_selected);
  if (status != TOLUA_BCCONV_OK) return status;

  TOLUA_REPACK_LOG(ctx, pc,
                   "call-intermediate-producer success producer_pc=%u prefix=[%u,%u]->[%u,%u] base=%u->%u args=[%u,%u]->[%u,%u]",
                   (unsigned int)producer_pc,
                   (unsigned int)consumer_old_first, (unsigned int)prefix_old_last,
                   (unsigned int)prefix_new_first, (unsigned int)prefix_new_last,
                   (unsigned int)producer_base, (unsigned int)producer_new_base,
                   (unsigned int)producer_cur_arg_first, (unsigned int)producer_cur_arg_last,
                   (unsigned int)producer_new_arg_first, (unsigned int)producer_new_arg_last);
  *handled = 1;
  return TOLUA_BCCONV_OK;
}

static int tolua_try_fix_iterc_adjacent_call_arg_chain_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                                uint32_t pc, uint8_t *framesize_io,
                                                                const uint8_t *targets,
                                                                const tolua_bcdebug_ctx *ctx, int *handled)
{
  BCIns consumer = 0;
  BCIns call = 0;
  BCOp consumer_op = BC__MAX;
  BCReg iter_old_first = 0;
  BCReg iter_old_last = 0;
  BCReg iter_new_first = 0;
  BCReg iter_new_last = 0;
  BCReg call_base = 0;
  BCReg call_old_arg_first = 0;
  BCReg call_old_arg_last = 0;
  BCReg call_new_arg_first = 0;
  BCReg call_new_arg_last = 0;
  BCReg max_last = 0;
  uint8_t *iter_selected = NULL;
  uint8_t *call_selected = NULL;
  uint8_t *combined_selected = NULL;
  int iter_min_window = (int)pc;
  int call_min_window = (int)pc;
  int start_window = (int)pc;
  int have_call_arg_slice = 0;
  uint32_t interference_pc = UINT32_MAX;
  BCOp interference_op = BC__MAX;
  BCReg interference_reg = 0;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *handled = 0;
  if (tolua_pending_insert_copy.active) return TOLUA_BCCONV_OK;
  if (pc == 0) return TOLUA_BCCONV_OK;

  consumer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  consumer_op = bc_op(consumer);
  if (consumer_op != BC_ITERC || bc_c(consumer) <= 1) return TOLUA_BCCONV_OK;

  call = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
  if (bc_op(call) != BC_CALL || bc_b(call) != 1 || bc_c(call) <= 1) {
    return TOLUA_BCCONV_OK;
  }

  iter_old_first = (BCReg)(bc_a(consumer) + 1);
  iter_old_last = (BCReg)(bc_a(consumer) + bc_c(consumer) - 1);
  call_base = bc_a(call);
  if (call_base != iter_old_last) return TOLUA_BCCONV_OK;

  iter_new_first = (BCReg)(iter_old_first + 1);
  iter_new_last = (BCReg)(iter_old_last + 1);
  call_old_arg_first = (BCReg)(call_base + 1);
  call_old_arg_last = (BCReg)(call_base + bc_c(call) - 1);
  call_new_arg_first = (BCReg)(call_old_arg_first + 1);
  call_new_arg_last = (BCReg)(call_old_arg_last + 1);
  if (call_new_arg_last > BCMAX_A) {
    return tolua_failbytecodeproto(ctx, pc, consumer, consumer_op,
                                   TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                   "FR2 iterc-adjacent-call chain exceeds register limit");
  }

  iter_selected = (uint8_t *)calloc((size_t)numbc, 1);
  call_selected = (uint8_t *)calloc((size_t)numbc, 1);
  combined_selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!iter_selected || !call_selected || !combined_selected) {
    free(combined_selected);
    free(call_selected);
    free(iter_selected);
    return tolua_failbytecodeproto(ctx, pc, consumer, consumer_op,
                                   TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate FR2 iterc-adjacent-call slices");
  }

  if ((!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc, iter_old_first, iter_old_last,
                                  targets, iter_selected, &iter_min_window,
                                  &interference_pc, &interference_op, &interference_reg) &&
       !tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, pc,
                                                            iter_old_first, iter_old_last,
                                                            targets, iter_selected, &iter_min_window,
                                                            &interference_pc, &interference_op,
                                                            &interference_reg)) &&
      !tolua_try_select_simple_local_defs(buf, bc_pos, be, pc, iter_old_first, iter_old_last,
                                          iter_selected, &iter_min_window)) {
    TOLUA_REPACK_LOG(ctx, pc,
                     "iterc-adjacent-call iterc-slice reject blocker_pc=%u op=%s reg=%u",
                     (unsigned int)interference_pc, tolua_bc_opname(interference_op),
                     (unsigned int)interference_reg);
    free(combined_selected);
    free(call_selected);
    free(iter_selected);
    return TOLUA_BCCONV_OK;
  }

  if ((!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc - 1,
                                  call_old_arg_first, call_old_arg_last,
                                  targets, call_selected, &call_min_window,
                                  &interference_pc, &interference_op, &interference_reg) &&
       !tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, pc - 1,
                                                            call_old_arg_first, call_old_arg_last,
                                                            targets, call_selected, &call_min_window,
                                                            &interference_pc, &interference_op,
                                                            &interference_reg)) &&
      !tolua_try_select_simple_local_defs(buf, bc_pos, be, pc - 1,
                                          call_old_arg_first, call_old_arg_last,
                                          call_selected, &call_min_window)) {
    BCReg copy_dst[2];
    BCReg copy_src[2];

    if (!targets[pc - 1]) {
      copy_dst[0] = iter_new_last;
      copy_src[0] = iter_old_last;
      copy_dst[1] = iter_new_first;
      copy_src[1] = iter_old_first;
      if (tolua_schedule_insert_copies(ctx, pc, pc - 1, copy_dst, copy_src, 2)) {
        status = tolua_update_framesize_checked(framesize_io, iter_new_last, ctx, pc, consumer, consumer_op);
        free(combined_selected);
        free(call_selected);
        free(iter_selected);
        if (status != TOLUA_BCCONV_OK) return status;
        TOLUA_REPACK_LOG(ctx, pc,
                         "iterc-adjacent-call copy fallback success call_pc=%u copy %u<- %u, %u<- %u",
                         (unsigned int)(pc - 1),
                         (unsigned int)copy_dst[0], (unsigned int)copy_src[0],
                         (unsigned int)copy_dst[1], (unsigned int)copy_src[1]);
        *handled = 1;
        return TOLUA_BCCONV_INTERNAL_INSERT_COPY;
      }
    }

    TOLUA_REPACK_LOG(ctx, pc,
                     "iterc-adjacent-call proceed without call-arg slice blocker_pc=%u op=%s reg=%u",
                     (unsigned int)interference_pc, tolua_bc_opname(interference_op),
                     (unsigned int)interference_reg);
    memset(call_selected, 0, (size_t)numbc);
    call_min_window = (int)(pc - 1);
  } else {
    have_call_arg_slice = 1;
  }

  iter_selected[pc - 1] = 0;
  call_selected[pc - 1] = 1;

  for (scan = 0; scan < (int)numbc; scan++) {
    combined_selected[scan] = (uint8_t)(iter_selected[scan] || call_selected[scan]);
  }

  start_window = iter_min_window;
  if (call_min_window < start_window) start_window = call_min_window;

  for (scan = start_window + 1; scan < (int)pc; scan++) {
    if (!combined_selected[scan]) continue;
    if (targets[scan] &&
        tolua_target_has_external_entry(buf, bc_pos, numbc, be, start_window, pc, (uint32_t)scan)) {
      if (!targets[pc - 1]) {
        BCReg copy_dst[2];
        BCReg copy_src[2];

        copy_dst[0] = iter_new_last;
        copy_src[0] = iter_old_last;
        copy_dst[1] = iter_new_first;
        copy_src[1] = iter_old_first;
        if (tolua_schedule_insert_copies(ctx, pc, pc - 1, copy_dst, copy_src, 2)) {
          status = tolua_update_framesize_checked(framesize_io, iter_new_last, ctx, pc, consumer, consumer_op);
          free(combined_selected);
          free(call_selected);
          free(iter_selected);
          if (status != TOLUA_BCCONV_OK) return status;
          TOLUA_REPACK_LOG(ctx, pc,
                           "iterc-adjacent-call copy fallback on target-entry at pc=%d insert_pc=%u",
                           scan, (unsigned int)(pc - 1));
          *handled = 1;
          return TOLUA_BCCONV_INTERNAL_INSERT_COPY;
        }
        TOLUA_REPACK_LOG(ctx, pc,
                         "iterc-adjacent-call copy fallback schedule failed active=%u",
                         (unsigned int)tolua_pending_insert_copy.active);
      }
      TOLUA_REPACK_LOG(ctx, pc,
                       "iterc-adjacent-call ignore target entry at pc=%d iter_selected=%u target_pcminus1=%u",
                       scan, (unsigned int)iter_selected[scan], (unsigned int)targets[pc - 1]);
      continue;
    }
  }

  if (tolua_window_has_nonselected_touch(buf, bc_pos, be, start_window, pc, combined_selected,
                                         iter_new_first, iter_new_last,
                                         &interference_pc, &interference_op, &interference_reg) ||
      (have_call_arg_slice &&
       tolua_window_has_nonselected_touch(buf, bc_pos, be, start_window, pc, combined_selected,
                                          call_new_arg_first, call_new_arg_last,
                                          &interference_pc, &interference_op, &interference_reg))) {
    TOLUA_REPACK_LOG(ctx, pc,
                     "iterc-adjacent-call target-touch reject blocker_pc=%u op=%s reg=%u",
                     (unsigned int)interference_pc, tolua_bc_opname(interference_op),
                     (unsigned int)interference_reg);
    free(combined_selected);
    free(call_selected);
    free(iter_selected);
    return TOLUA_BCCONV_OK;
  }

  for (scan = start_window; scan < (int)pc; scan++) {
    uint8_t *slot = NULL;
    BCIns ins = 0;
    BCOp op = BC__MAX;
    int remap_iter = 0;
    int remap_call = 0;

    remap_iter = iter_selected[scan];
    remap_call = call_selected[scan];
    if (!remap_iter && !remap_call) continue;

    slot = buf + bc_pos + (size_t)scan * 4;
    ins = (BCIns)tolua_read_ins(slot, be);
    op = bc_op(ins);

    if (remap_call && have_call_arg_slice) {
      tolua_repack_remap_reg_range(&ins, op, call_old_arg_first, call_old_arg_last,
                                   call_new_arg_first);
    }
    if (remap_iter) {
      tolua_repack_remap_reg_range(&ins, op, iter_old_first, iter_old_last, iter_new_first);
    }
    tolua_write_ins(slot, (uint32_t)ins, be);

    if (remap_call && have_call_arg_slice) {
      tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, ins,
                                         call_old_arg_first, call_old_arg_last,
                                         call_new_arg_first);
    }
    if (remap_iter) {
      tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, ins,
                                         iter_old_first, iter_old_last, iter_new_first);
    }
  }

  max_last = iter_new_last;
  if (have_call_arg_slice && call_new_arg_last > max_last) max_last = call_new_arg_last;
  status = tolua_update_framesize_checked(framesize_io, max_last, ctx, pc, consumer, consumer_op);
  free(combined_selected);
  free(call_selected);
  free(iter_selected);
  if (status != TOLUA_BCCONV_OK) return status;

  TOLUA_REPACK_LOG(ctx, pc,
                   "iterc-adjacent-call success call_pc=%u iterc=[%u,%u]->[%u,%u] call_args=[%u,%u]->[%u,%u] call_slice=%u",
                   (unsigned int)(pc - 1),
                   (unsigned int)iter_old_first, (unsigned int)iter_old_last,
                   (unsigned int)iter_new_first, (unsigned int)iter_new_last,
                   (unsigned int)call_old_arg_first, (unsigned int)call_old_arg_last,
                   (unsigned int)call_new_arg_first, (unsigned int)call_new_arg_last,
                   (unsigned int)have_call_arg_slice);
  *handled = 1;
  return TOLUA_BCCONV_OK;
}

static int tolua_try_fix_call_selfdef_chain_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                     uint32_t pc, uint8_t *framesize_io,
                                                     const uint8_t *targets,
                                                     const tolua_bcdebug_ctx *ctx, int *handled)
{
  uint32_t producer_pc = 0;
  uint32_t self_pc = 0;
  BCIns consumer = 0;
  BCIns producer = 0;
  BCIns self_ins = 0;
  BCOp consumer_op = BC__MAX;
  BCOp self_op = BC__MAX;
  BCReg consumer_base = 0;
  BCReg consumer_old_first = 0;
  BCReg consumer_old_last = 0;
  BCReg prefix_old_last = 0;
  int have_prefix = 0;
  BCReg producer_base = 0;
  BCReg producer_cur_arg_first = 0;
  BCReg producer_cur_arg_last = 0;
  BCReg prefix_new_first = 0;
  BCReg prefix_new_last = 0;
  BCReg producer_new_base = 0;
  BCReg producer_new_arg_first = 0;
  BCReg producer_new_arg_last = 0;
  BCReg self_def = 0;
  uint8_t *prefix_selected = NULL;
  uint8_t *producer_base_selected = NULL;
  uint8_t *producer_arg_selected = NULL;
  uint8_t *combined_selected = NULL;
  int prefix_min_window = (int)pc;
  int producer_base_min_window = (int)pc;
  int producer_arg_min_window = (int)pc;
  int start_window = (int)pc;
  uint32_t interference_pc = UINT32_MAX;
  BCOp interference_op = BC__MAX;
  BCReg interference_reg = 0;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *handled = 0;
  if (pc < 2) return TOLUA_BCCONV_OK;

  producer_pc = pc - 2;
  self_pc = pc - 1;
  consumer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  producer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)producer_pc * 4, be);
  self_ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)self_pc * 4, be);
  consumer_op = bc_op(consumer);
  self_op = bc_op(self_ins);

  TOLUA_REPACK_LOG(ctx, pc,
                   "call-selfdef helper enter consumer=%s producer=%s self=%s producer_b=%u consumer_span=%u producer_c=%u",
                   tolua_bc_opname(consumer_op), tolua_bc_opname(bc_op(producer)),
                   tolua_bc_opname(self_op), (unsigned int)bc_b(producer),
                   (unsigned int)(consumer_op == BC_CALL ? bc_c(consumer) : bc_d(consumer)),
                   (unsigned int)bc_c(producer));

  if ((consumer_op != BC_CALL && consumer_op != BC_CALLT) ||
      bc_op(producer) != BC_CALL || bc_b(producer) != 2) {
    return TOLUA_BCCONV_OK;
  }

  consumer_base = bc_a(consumer);
  if ((consumer_op == BC_CALL ? bc_c(consumer) : bc_d(consumer)) <= 1) return TOLUA_BCCONV_OK;
  consumer_old_first = (BCReg)(consumer_base + 1);
  consumer_old_last = (BCReg)(consumer_base +
                              (consumer_op == BC_CALL ? bc_c(consumer) : bc_d(consumer)) - 1);
  producer_base = bc_a(producer);
  if (producer_base != consumer_old_last || bc_c(producer) <= 1) {
    return TOLUA_BCCONV_OK;
  }

  if (!tolua_get_slice_def_reg(self_op, self_ins, producer_base, producer_base, &self_def) ||
      self_def != producer_base ||
      !tolua_ins_reads_reg(self_op, self_ins, producer_base)) {
    return TOLUA_BCCONV_OK;
  }

  prefix_old_last = (BCReg)(producer_base - 1);
  have_prefix = consumer_old_first <= prefix_old_last;

  producer_cur_arg_first = (BCReg)(producer_base + 2);
  producer_cur_arg_last = (BCReg)(producer_base + bc_c(producer));
  prefix_new_first = (BCReg)(consumer_old_first + 1);
  prefix_new_last = (BCReg)(prefix_old_last + 1);
  producer_new_base = (BCReg)(producer_base + 1);
  producer_new_arg_first = (BCReg)(producer_cur_arg_first + 1);
  producer_new_arg_last = (BCReg)(producer_cur_arg_last + 1);

  if (producer_new_arg_last > BCMAX_A) {
    return tolua_failbytecodeproto(ctx, pc, consumer, consumer_op,
                                   TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                   "FR2 call-selfdef chain exceeds register limit");
  }

  prefix_selected = (uint8_t *)calloc((size_t)numbc, 1);
  producer_base_selected = (uint8_t *)calloc((size_t)numbc, 1);
  producer_arg_selected = (uint8_t *)calloc((size_t)numbc, 1);
  combined_selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!prefix_selected || !producer_base_selected || !producer_arg_selected || !combined_selected) {
    free(combined_selected);
    free(producer_arg_selected);
    free(producer_base_selected);
    free(prefix_selected);
    return tolua_failbytecodeproto(ctx, pc, consumer, consumer_op,
                                   TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate FR2 call-selfdef chain slices");
  }

  if (((have_prefix &&
        !tolua_select_repack_slice(buf, bc_pos, numbc, be, pc, consumer_old_first, prefix_old_last,
                                   targets, prefix_selected, &prefix_min_window,
                                   &interference_pc, &interference_op, &interference_reg) &&
        !tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, pc,
                                                             consumer_old_first, prefix_old_last,
                                                             targets, prefix_selected, &prefix_min_window,
                                                             &interference_pc, &interference_op,
                                                             &interference_reg))) ||
      (!tolua_select_repack_slice(buf, bc_pos, numbc, be, producer_pc, producer_base, producer_base,
                                  targets, producer_base_selected, &producer_base_min_window,
                                  &interference_pc, &interference_op, &interference_reg) &&
       !tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, producer_pc,
                                                            producer_base, producer_base,
                                                            targets, producer_base_selected,
                                                            &producer_base_min_window,
                                                            &interference_pc, &interference_op,
                                                            &interference_reg)) ||
      ((!tolua_select_repack_slice(buf, bc_pos, numbc, be, producer_pc,
                                   producer_cur_arg_first, producer_cur_arg_last,
                                   targets, producer_arg_selected, &producer_arg_min_window,
                                   &interference_pc, &interference_op, &interference_reg) &&
        !tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, producer_pc,
                                                             producer_cur_arg_first, producer_cur_arg_last,
                                                             targets, producer_arg_selected,
                                                             &producer_arg_min_window,
                                                             &interference_pc, &interference_op,
                                                             &interference_reg)) &&
       !tolua_try_select_simple_local_defs(buf, bc_pos, be, producer_pc,
                                           producer_cur_arg_first, producer_cur_arg_last,
                                           producer_arg_selected, &producer_arg_min_window))) {
    TOLUA_REPACK_LOG(ctx, pc,
                     "call-selfdef chain slice reject blocker_pc=%u op=%s reg=%u",
                     (unsigned int)interference_pc, tolua_bc_opname(interference_op),
                     (unsigned int)interference_reg);
    free(combined_selected);
    free(producer_arg_selected);
    free(producer_base_selected);
    free(prefix_selected);
    return TOLUA_BCCONV_OK;
  }

  producer_base_selected[self_pc] = 1;
  producer_base_selected[producer_pc] = 1;
  producer_arg_selected[producer_pc] = 1;

  for (scan = 0; scan < (int)numbc; scan++) {
    combined_selected[scan] = (uint8_t)(prefix_selected[scan] ||
                                        producer_base_selected[scan] ||
                                        producer_arg_selected[scan]);
  }

  start_window = have_prefix ? prefix_min_window : producer_base_min_window;
  if (producer_base_min_window < start_window) start_window = producer_base_min_window;
  if (producer_arg_min_window < start_window) start_window = producer_arg_min_window;

  if (((have_prefix &&
        tolua_window_has_nonselected_touch(buf, bc_pos, be, start_window, pc, combined_selected,
                                           prefix_new_first, prefix_new_last,
                                           &interference_pc, &interference_op, &interference_reg))) ||
      tolua_window_has_nonselected_touch(buf, bc_pos, be, start_window, pc, combined_selected,
                                         producer_new_base, producer_new_base,
                                         &interference_pc, &interference_op, &interference_reg) ||
      tolua_window_has_nonselected_touch(buf, bc_pos, be, start_window, pc, combined_selected,
                                         producer_new_arg_first, producer_new_arg_last,
                                         &interference_pc, &interference_op, &interference_reg)) {
    TOLUA_REPACK_LOG(ctx, pc,
                     "call-selfdef chain target-touch reject blocker_pc=%u op=%s reg=%u",
                     (unsigned int)interference_pc, tolua_bc_opname(interference_op),
                     (unsigned int)interference_reg);
    free(combined_selected);
    free(producer_arg_selected);
    free(producer_base_selected);
    free(prefix_selected);
    return TOLUA_BCCONV_OK;
  }

  for (scan = start_window; scan < (int)pc; scan++) {
    uint8_t *slot = NULL;
    BCIns ins = 0;
    BCOp op = BC__MAX;

    if (!combined_selected[scan]) continue;
    slot = buf + bc_pos + (size_t)scan * 4;
    ins = (BCIns)tolua_read_ins(slot, be);
    op = bc_op(ins);

    if (have_prefix && prefix_selected[scan]) {
      tolua_repack_remap_reg_range(&ins, op, consumer_old_first, prefix_old_last, prefix_new_first);
    }
    if (producer_base_selected[scan]) {
      tolua_repack_remap_reg_range(&ins, op, producer_base, producer_base, producer_new_base);
    }
    if (producer_arg_selected[scan]) {
      tolua_repack_remap_reg_range(&ins, op, producer_cur_arg_first, producer_cur_arg_last,
                                   producer_new_arg_first);
    }
    tolua_write_ins(slot, (uint32_t)ins, be);
  }

  status = tolua_update_framesize_checked(framesize_io, producer_new_arg_last, ctx, pc,
                                          consumer, consumer_op);
  free(combined_selected);
  free(producer_arg_selected);
  free(producer_base_selected);
  free(prefix_selected);
  if (status != TOLUA_BCCONV_OK) return status;

  TOLUA_REPACK_LOG(ctx, pc,
                   "call-selfdef chain success prefix=[%u,%u]->[%u,%u] producer=%u args=[%u,%u]->base=%u args=[%u,%u]",
                   (unsigned int)consumer_old_first, (unsigned int)prefix_old_last,
                   (unsigned int)prefix_new_first, (unsigned int)prefix_new_last,
                   (unsigned int)producer_base, (unsigned int)producer_cur_arg_first,
                   (unsigned int)producer_cur_arg_last, (unsigned int)producer_new_base,
                   (unsigned int)producer_new_arg_first, (unsigned int)producer_new_arg_last);
  *handled = 1;
  return TOLUA_BCCONV_OK;
}

static int tolua_try_select_simple_local_defs(const uint8_t *buf, size_t bc_pos, int be,
                                              uint32_t pc, BCReg old_first, BCReg old_last,
                                              uint8_t *selected, int *out_min_window)
{
  uint8_t need[BCMAX_A + 1];
  int scan = 0;
  BCReg reg = 0;

  if (old_first > old_last || old_last > BCMAX_A) return 0;
  memset(need, 0, sizeof(need));
  for (reg = old_first; reg <= old_last; reg++) {
    need[reg] = 1;
  }
  *out_min_window = (int)pc;

  for (scan = (int)pc - 1; scan >= 0; scan--) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp op = bc_op(ins);
    BCReg def = 0;

    if (!tolua_get_slice_def_reg(op, ins, old_first, old_last, &def) || !need[def]) {
      continue;
    }
#ifdef TOLUA_REPACK_DEBUG
    fprintf(stderr, "[repack] simple-local pick pc=%u op=%s def=%u range=[%u,%u]\n",
            (unsigned int)scan, tolua_bc_opname(op), (unsigned int)def,
            (unsigned int)old_first, (unsigned int)old_last);
#endif
    selected[scan] = 1;
    need[def] = 0;
    *out_min_window = scan;
  }

  for (reg = old_first; reg <= old_last; reg++) {
    if (need[reg]) {
#ifdef TOLUA_REPACK_DEBUG
      fprintf(stderr, "[repack] simple-local missing reg=%u range=[%u,%u]\n",
              (unsigned int)reg, (unsigned int)old_first, (unsigned int)old_last);
#endif
      return 0;
    }
  }
  return 1;
}

static int tolua_try_fix_cat_arg_for_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                         uint32_t pc, uint8_t *framesize_io,
                                         const uint8_t *targets,
                                         const tolua_bcdebug_ctx *ctx, int *handled)
{
  BCIns call = 0;
  BCIns cat = 0;
  BCReg old_arg_first = 0;
  BCReg old_cat_last = 0;
  BCReg new_cat_first = 0;
  BCReg new_cat_last = 0;
  uint8_t *selected = NULL;
  int min_window = (int)pc;
  uint32_t interference_pc = UINT32_MAX;
  BCOp interference_op = BC__MAX;
  BCReg interference_reg = 0;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *handled = 0;
  if (pc == 0) return TOLUA_BCCONV_OK;

  call = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  cat = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
  if (bc_op(call) != BC_CALL || bc_op(cat) != BC_CAT || bc_c(call) != 2) {
    return TOLUA_BCCONV_OK;
  }

  old_arg_first = (BCReg)(bc_a(call) + 1);
  old_cat_last = bc_c(cat);
  if (bc_a(cat) != old_arg_first || bc_b(cat) != old_arg_first || old_cat_last < old_arg_first) {
    return TOLUA_BCCONV_OK;
  }

  new_cat_first = (BCReg)(old_arg_first + 1);
  new_cat_last = (BCReg)(old_cat_last + 1);
  if (new_cat_last > BCMAX_A) {
    return tolua_failbytecodeproto(ctx, pc, call, BC_CALL, TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                   "FR2 CAT-arg shift exceeds register limit");
  }

  selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!selected) {
    return tolua_failbytecodeproto(ctx, pc, call, BC_CALL, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate FR2 CAT-arg slice");
  }

  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc - 1, old_arg_first, old_cat_last,
                                 targets, selected, &min_window,
                                 &interference_pc, &interference_op, &interference_reg)) {
    free(selected);
    return TOLUA_BCCONV_OK;
  }
  selected[pc - 1] = 1;

  if (tolua_window_has_nonselected_touch(buf, bc_pos, be, min_window, pc, selected,
                                         new_cat_first, new_cat_last,
                                         &interference_pc, &interference_op, &interference_reg)) {
    free(selected);
    return TOLUA_BCCONV_OK;
  }

  if (tolua_reg_live_after_pc(buf, bc_pos, numbc, be, pc + 1, new_cat_last)) {
    free(selected);
    return TOLUA_BCCONV_OK;
  }

  for (scan = min_window; scan < (int)pc; scan++) {
    uint8_t *slot = NULL;
    BCIns ins = 0;
    BCOp op = BC__MAX;

    if (!selected[scan]) continue;
    slot = buf + bc_pos + (size_t)scan * 4;
    ins = (BCIns)tolua_read_ins(slot, be);
    op = bc_op(ins);
    tolua_repack_remap_reg_range(&ins, op, old_arg_first, old_cat_last, new_cat_first);
    tolua_write_ins(slot, (uint32_t)ins, be);
  }

  status = tolua_update_framesize_checked(framesize_io, new_cat_last, ctx, pc, call, BC_CALL);
  free(selected);
  if (status != TOLUA_BCCONV_OK) return status;

  *handled = 1;
  return TOLUA_BCCONV_OK;
}

static int tolua_can_collapse_multires_v1(const uint8_t *buf, size_t bc_pos, uint32_t pc, int be)
{
  uint32_t producer_pc = pc;
  BCIns prev = 0;
  BCOp prev_op = BC__MAX;

  if (pc == 0) return 0;
  producer_pc = pc - 1;
  prev = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)producer_pc * 4, be);
  prev_op = bc_op(prev);

  while (prev_op == BC_UCLO) {
    if (producer_pc == 0) return 0;
    if ((ptrdiff_t)producer_pc + 1 + bc_j(prev) != (ptrdiff_t)pc) return 0;
    producer_pc--;
    prev = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)producer_pc * 4, be);
    prev_op = bc_op(prev);
  }

  return prev_op == BC_CALL || prev_op == BC_VARG;
}

static void tolua_collect_proto_holes_v1_fr2_local(const uint8_t *buf, size_t bc_pos, uint32_t numbc,
                                                   int be, tolua_bcshift_map *map)
{
  uint32_t pc = 0;

  memset(map, 0, sizeof(*map));
  for (pc = 0; pc < numbc; pc++) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
    BCOp op = bc_op(ins);

    switch (op) {
      case BC_CALL:
      case BC_CALLM:
      case BC_CALLT:
      case BC_CALLMT:
      case BC_ITERC:
      case BC_ITERN:
        map->hole[bc_a(ins)] = 1;
        break;
      default:
        break;
    }
  }

  tolua_build_shift_map(map);
}

static int tolua_patch_proto_v1_fr2(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                    int remap_v1,
                                    uint8_t *framesize_io,
                                    const tolua_bcdebug_ctx *ctx)
{
  uint8_t *targets = NULL;
  uint32_t pc = 0;
  int status = TOLUA_BCCONV_OK;

  if (remap_v1) {
    for (pc = 0; pc < numbc; pc++) {
      uint8_t *slot = buf + bc_pos + (size_t)pc * 4;
      BCIns ins = (BCIns)tolua_read_ins(slot, be);
      BCOp op = tolua_remap_bc_op(bc_op(ins), 1);

      if (op >= BC__MAX) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_OPCODE,
                                       "opcode %u is not recognized after v1 remap",
                                       (unsigned int)bc_op(ins));
      }

      setbc_op(&ins, op);
      tolua_write_ins(slot, (uint32_t)ins, be);
    }
  }

  for (pc = 0; pc < numbc; pc++) {
    uint8_t *slot = buf + bc_pos + (size_t)pc * 4;
    BCIns ins = (BCIns)tolua_read_ins(slot, be);
    BCOp op = bc_op(ins);

    /* Preserve CALLM/CALLMT with C==1 to keep multires call semantics intact. */
    if (op == BC_CALLM && bc_c(ins) > 1 && tolua_can_collapse_multires_v1(buf, bc_pos, pc, be)) {
      BCReg new_b = bc_b(ins) ? bc_b(ins) : 2;
      BCReg new_c = (BCReg)(bc_c(ins) + 2);

      if ((uint32_t)bc_c(ins) + 2 > BCMAX_C) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "CALLM argument count overflows when downgraded to CALL");
      }
      status = tolua_collapse_multires_producer(buf, bc_pos, pc, be, ctx);
      if (status != TOLUA_BCCONV_OK) return status;

      setbc_op(&ins, BC_CALL);
      setbc_b(&ins, new_b);
      setbc_c(&ins, new_c);
      tolua_write_ins(slot, (uint32_t)ins, be);
    } else if (op == BC_CALLMT && bc_c(ins) > 1 && tolua_can_collapse_multires_v1(buf, bc_pos, pc, be)) {
      BCReg new_d = (BCReg)(bc_c(ins) + 2);

      if ((uint32_t)bc_c(ins) + 2 > BCMAX_D) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "CALLMT argument count overflows when downgraded to CALLT");
      }
      status = tolua_collapse_multires_producer(buf, bc_pos, pc, be, ctx);
      if (status != TOLUA_BCCONV_OK) return status;

      ins = BCINS_AD(BC_CALLT, bc_a(ins), new_d);
      tolua_write_ins(slot, (uint32_t)ins, be);
    }
  }

  targets = (uint8_t *)calloc((size_t)numbc, 1);
  if (!targets) {
    return tolua_failbytecodeproto(ctx, 0, 0, BC__MAX, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate FR2 target buffer");
  }

  tolua_mark_proto_targets(buf, bc_pos, numbc, be, targets);

  for (pc = 0; pc < numbc; pc++) {
    uint8_t *slot = buf + bc_pos + (size_t)pc * 4;
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
    BCOp op = bc_op(ins);
    BCReg a = bc_a(ins);
    int handled_special_open_callm_chain = 0;

    if (op == BC_CALL &&
        bc_b(ins) == 0 &&
        bc_c(ins) == 2 &&
        pc >= 5 &&
        pc + 1 < numbc) {
      BCIns next = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc + 1) * 4, be);

      if (bc_op(next) == BC_CALLM &&
          bc_c(next) == 0 &&
          bc_a(next) + 1 == a) {
        BCIns cat = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
        BCIns kstr = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 2) * 4, be);
        BCIns cat_prefix = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 3) * 4, be);
        BCIns cat_seed = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 4) * 4, be);
        BCIns func_seed = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 5) * 4, be);
        BCReg cat_base = bc_a(cat);
        BCReg cat_last = bc_c(cat);

        if (bc_op(cat) == BC_CAT &&
            bc_op(kstr) == BC_KSTR &&
            bc_op(cat_prefix) == BC_TGETS &&
            bc_op(cat_seed) == BC_TGETS &&
            bc_op(func_seed) == BC_TGETS &&
            bc_a(cat) == bc_b(cat) &&
            cat_last == (BCReg)(cat_base + 1) &&
            cat_base == (BCReg)(a + 1) &&
            bc_a(kstr) == cat_last &&
            bc_a(cat_prefix) == cat_base &&
            bc_b(cat_prefix) == cat_base &&
            bc_a(cat_seed) == cat_base &&
            bc_a(func_seed) == a) {
          BCReg new_fn = (BCReg)(a + 1);
          BCReg new_cat_base = (BCReg)(cat_base + 2);
          BCReg new_cat_last = (BCReg)(cat_last + 2);
          uint8_t *func_seed_slot = buf + bc_pos + (size_t)(pc - 5) * 4;
          uint8_t *cat_seed_slot = buf + bc_pos + (size_t)(pc - 4) * 4;
          uint8_t *cat_prefix_slot = buf + bc_pos + (size_t)(pc - 3) * 4;
          uint8_t *kstr_slot = buf + bc_pos + (size_t)(pc - 2) * 4;
          uint8_t *cat_slot = buf + bc_pos + (size_t)(pc - 1) * 4;

          if (new_cat_last > BCMAX_A) {
            free(targets);
            return tolua_failbytecodeproto(ctx, pc, ins, op,
                                           TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                           "open CALL/CALLM FR2 local align exceeds register limit");
          }

          setbc_a(&func_seed, new_fn);
          tolua_write_ins(func_seed_slot, (uint32_t)func_seed, be);

          setbc_a(&cat_seed, new_cat_base);
          tolua_write_ins(cat_seed_slot, (uint32_t)cat_seed, be);

          setbc_a(&cat_prefix, new_cat_base);
          setbc_b(&cat_prefix, new_cat_base);
          tolua_write_ins(cat_prefix_slot, (uint32_t)cat_prefix, be);

          setbc_a(&kstr, new_cat_last);
          tolua_write_ins(kstr_slot, (uint32_t)kstr, be);

          setbc_a(&cat, new_cat_base);
          setbc_b(&cat, new_cat_base);
          setbc_c(&cat, new_cat_last);
          tolua_write_ins(cat_slot, (uint32_t)cat, be);

          setbc_a(&ins, new_fn);
          tolua_write_ins(slot, (uint32_t)ins, be);

          status = tolua_update_framesize_checked(framesize_io, new_cat_last, ctx, pc, ins, op);
          if (status != TOLUA_BCCONV_OK) {
            free(targets);
            return status;
          }

          TOLUA_REPACK_LOG(ctx, pc,
                           "open CALL/CALLM local align base %u->%u cat [%u,%u]->[%u,%u]",
                           (unsigned int)a, (unsigned int)new_fn,
                           (unsigned int)cat_base, (unsigned int)cat_last,
                           (unsigned int)new_cat_base, (unsigned int)new_cat_last);
          handled_special_open_callm_chain = 1;
        }
      }
    }

    if (!handled_special_open_callm_chain &&
        op == BC_CALL &&
        bc_c(ins) > 1) {
      BCReg first = (BCReg)(a + 1);
      BCReg last = (BCReg)(a + bc_c(ins) - 1);
      if (bc_c(ins) == 2 && pc > 0) {
        BCIns prev = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
        if (bc_op(prev) == BC_CAT &&
            bc_a(prev) == first &&
            bc_b(prev) == first &&
            bc_c(prev) >= first) {
          last = bc_c(prev);
        }
      }
      status = tolua_shift_proto_slice_right_for_fr2(buf, bc_pos, numbc, be, pc,
                                                     first, last,
                                                     framesize_io, targets, ctx);
    } else if (op == BC_CALLT && bc_d(ins) > 1) {
      status = tolua_shift_proto_slice_right_for_fr2(buf, bc_pos, numbc, be, pc,
                                                     (BCReg)(a + 1), (BCReg)(a + bc_d(ins) - 1),
                                                     framesize_io, targets, ctx);
    } else if (op == BC_ITERC && bc_c(ins) > 1) {
      status = tolua_shift_proto_slice_right_for_fr2(buf, bc_pos, numbc, be, pc,
                                                     (BCReg)(a + 1), (BCReg)(a + bc_c(ins) - 1),
                                                     framesize_io, targets, ctx);
    }

    if (status != TOLUA_BCCONV_OK) {
      free(targets);
      return status;
    }
  }

  free(targets);
  return TOLUA_BCCONV_OK;
}

static int tolua_retry_repack_slice_with_readonly_interference(const uint8_t *buf, size_t bc_pos,
                                                               uint32_t numbc, int be, uint32_t pc,
                                                               BCReg old_first, BCReg old_last,
                                                               const uint8_t *targets,
                                                               uint8_t *selected, int *out_min_window,
                                                               uint32_t *out_interference_pc,
                                                               BCOp *out_interference_op,
                                                               BCReg *out_interference_reg)
{
  uint8_t live[BCMAX_A + 1];
  int min_window = (int)pc;
  int scan = 0;

  if (out_interference_pc) *out_interference_pc = UINT32_MAX;
  if (out_interference_op) *out_interference_op = BC__MAX;
  if (out_interference_reg) *out_interference_reg = 0;
  if (old_first > old_last || old_last > BCMAX_A) {
#ifdef TOLUA_REPACK_DEBUG
    fprintf(stderr, "[repack] readonly slice retry fail pc=%u invalid-range=[%u,%u]\n",
            (unsigned int)pc, (unsigned int)old_first, (unsigned int)old_last);
#endif
    return 0;
  }
  memset(live, 0, sizeof(live));
  for (scan = old_first; scan <= old_last; scan++) {
    live[scan] = 1;
  }

  for (scan = 0; scan < (int)pc; scan++) {
    if (selected[scan]) {
      min_window = scan;
      break;
    }
  }
  if (min_window >= (int)pc) {
#ifdef TOLUA_REPACK_DEBUG
    fprintf(stderr, "[repack] readonly slice retry fail pc=%u no-selected-range range=[%u,%u]\n",
            (unsigned int)pc, (unsigned int)old_first, (unsigned int)old_last);
#endif
    return 0;
  }

  for (scan = (int)pc - 1; scan >= min_window; scan--) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp ins_op = bc_op(ins);

    if (scan != min_window && targets[scan] &&
        tolua_target_has_external_entry(buf, bc_pos, numbc, be, min_window, pc, (uint32_t)scan)) {
#ifdef TOLUA_REPACK_DEBUG
      fprintf(stderr, "[repack] readonly slice retry fail pc=%u target-at=%d range=[%u,%u]\n",
              (unsigned int)pc, scan, (unsigned int)old_first, (unsigned int)old_last);
#endif
      return 0;
    }

    if (selected[scan]) {
      BCReg def = 0;
      BCReg reg = 0;

      if (!tolua_get_slice_def_reg(ins_op, ins, old_first, old_last, &def)) {
#ifdef TOLUA_REPACK_DEBUG
        fprintf(stderr, "[repack] readonly slice retry fail pc=%u selected-nondef-at=%d op=%s range=[%u,%u]\n",
                (unsigned int)pc, scan, tolua_bc_opname(ins_op),
                (unsigned int)old_first, (unsigned int)old_last);
#endif
        return 0;
      }
      live[def] = 0;
      for (reg = old_first; reg <= old_last; reg++) {
        if (tolua_ins_reads_reg(ins_op, ins, reg)) live[reg] = 1;
      }
    } else {
      BCReg reg = 0;

      for (reg = old_first; reg <= old_last; reg++) {
        if (!live[reg]) continue;
        if (tolua_ins_writes_reg(ins_op, ins, reg)) {
          if (out_interference_pc) *out_interference_pc = (uint32_t)scan;
          if (out_interference_op) *out_interference_op = ins_op;
          if (out_interference_reg) *out_interference_reg = reg;
#ifdef TOLUA_REPACK_DEBUG
          fprintf(stderr,
                  "[repack] readonly slice retry fail pc=%u write-at=%d op=%s reg=%u range=[%u,%u]\n",
                  (unsigned int)pc, scan, tolua_bc_opname(ins_op), (unsigned int)reg,
                  (unsigned int)old_first, (unsigned int)old_last);
#endif
          return 0;
        }
        if (tolua_ins_reads_reg(ins_op, ins, reg)) {
          if (!tolua_can_rewrite_ins_source_reg(ins, ins_op, reg)) {
            if (out_interference_pc) *out_interference_pc = (uint32_t)scan;
            if (out_interference_op) *out_interference_op = ins_op;
            if (out_interference_reg) *out_interference_reg = reg;
#ifdef TOLUA_REPACK_DEBUG
            fprintf(stderr,
                    "[repack] readonly slice retry fail pc=%u read-at=%d op=%s reg=%u range=[%u,%u]\n",
                    (unsigned int)pc, scan, tolua_bc_opname(ins_op), (unsigned int)reg,
                    (unsigned int)old_first, (unsigned int)old_last);
#endif
            return 0;
          }
        }
      }
    }
  }

  *out_min_window = min_window;
  return 1;
}

static int tolua_ranges_overlap(BCReg first_a, BCReg last_a, BCReg first_b, BCReg last_b)
{
  return first_a <= last_b && first_b <= last_a;
}

static int tolua_window_touches_range(const uint8_t *buf, size_t bc_pos, int be,
                                      int first_pc, uint32_t stop_pc,
                                      BCReg first, BCReg last)
{
  int scan = 0;

  for (scan = first_pc; scan < (int)stop_pc; scan++) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp op = bc_op(ins);
    BCReg reg = 0;

    for (reg = first; reg <= last; reg++) {
      if (tolua_ins_reads_reg(op, ins, reg) || tolua_ins_writes_reg(op, ins, reg)) {
        return 1;
      }
    }
  }

  return 0;
}

static int tolua_flow_touches_reg(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                  uint32_t start_pc, BCReg reg, const uint8_t *state_mask)
{
  uint32_t scan = 0;

  for (scan = start_pc; scan < numbc; scan++) {
    BCIns ins = 0;
    BCOp op = BC__MAX;

    if (!state_mask[scan]) continue;
    ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    op = bc_op(ins);
    if (tolua_ins_reads_reg(op, ins, reg) || tolua_ins_writes_reg(op, ins, reg)) {
      return 1;
    }
  }

  return 0;
}

static int tolua_reg_live_after_pc(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                   uint32_t start_pc, BCReg reg)
{
  uint32_t scan = 0;

  for (scan = start_pc; scan < numbc; scan++) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp op = bc_op(ins);

    if (tolua_ins_reads_reg(op, ins, reg)) {
      return 1;
    }
    if (tolua_ins_writes_reg(op, ins, reg)) {
      return 0;
    }
  }

  return 0;
}

static int tolua_future_fr2_arg_shift_writes_reg(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                 uint32_t start_pc, BCReg reg)
{
  uint32_t scan = 0;

  for (scan = start_pc; scan < numbc; scan++) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp op = bc_op(ins);
    BCReg a = bc_a(ins);
    BCReg old_last = 0;

    if (op == BC_CALL && bc_c(ins) > 1) {
      old_last = (BCReg)(a + bc_c(ins) - 1);
      if ((BCReg)(old_last + 1) == reg) return 1;
    } else if (op == BC_CALLT && bc_d(ins) > 1) {
      old_last = (BCReg)(a + bc_d(ins) - 1);
      if ((BCReg)(old_last + 1) == reg) return 1;
    } else if (op == BC_ITERC && bc_c(ins) > 1) {
      old_last = (BCReg)(a + bc_c(ins) - 1);
      if ((BCReg)(old_last + 1) == reg) return 1;
    }

    if (tolua_ins_reads_reg(op, ins, reg)) {
      return 0;
    }
    if (tolua_ins_writes_reg(op, ins, reg)) {
      continue;
    }
  }

  return 0;
}

static int tolua_hole_breaks_prior_insns(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                         uint32_t stop_pc, BCReg hole_reg, uint32_t skip_pc)
{
  uint32_t scan = 0;

  for (scan = 0; scan < stop_pc && scan < numbc; scan++) {
    BCIns ins = 0;
    BCOp op = BC__MAX;
    BCReg a = 0;

    if (scan == skip_pc) continue;
    ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    op = bc_op(ins);
    a = bc_a(ins);

    switch (op) {
      case BC_CALL:
        if (bc_b(ins) != 0 && bc_b(ins) <= 2 && bc_c(ins) > 1 &&
            hole_reg >= (BCReg)(a + 1) && hole_reg < (BCReg)(a + bc_c(ins) - 1)) {
          return 1;
        }
        if (bc_b(ins) != 0 &&
            hole_reg >= (BCReg)(a + 1) && hole_reg < (BCReg)(a + bc_b(ins) - 1)) {
          return 1;
        }
        break;
      case BC_CALLT:
        if (bc_d(ins) > 1 &&
            hole_reg >= (BCReg)(a + 1) && hole_reg < (BCReg)(a + bc_d(ins) - 1)) {
          return 1;
        }
        break;
      case BC_ITERC:
        if (bc_b(ins) != 0 &&
            hole_reg >= (BCReg)(a + 1) && hole_reg < (BCReg)(a + bc_b(ins) - 1)) {
          return 1;
        }
        if (hole_reg >= (BCReg)(a + 1) && hole_reg < (BCReg)(a + bc_c(ins) - 1)) {
          return 1;
        }
        break;
      case BC_VARG:
        if (bc_b(ins) != 0 &&
            hole_reg >= a && hole_reg < (BCReg)(a + bc_b(ins) - 2)) {
          return 1;
        }
        break;
      case BC_RET:
        if (bc_d(ins) > 1 &&
            hole_reg >= a && hole_reg < (BCReg)(a + bc_d(ins) - 2)) {
          return 1;
        }
        break;
      case BC_CAT:
        if (hole_reg >= bc_b(ins) && hole_reg < bc_c(ins)) {
          return 1;
        }
        break;
      case BC_FORI:
      case BC_JFORI:
      case BC_FORL:
      case BC_IFORL:
      case BC_JFORL:
        if (hole_reg >= a && hole_reg < (BCReg)(a + 3)) {
          return 1;
        }
        break;
      default:
        break;
    }
  }

  return 0;
}

static int tolua_hole_breaks_future_iterator_setup(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                   uint32_t pc, BCReg hole_reg, uint32_t skip_pc)
{
  uint32_t scan = 0;

  for (scan = pc + 1; scan < numbc; scan++) {
    BCIns ins = 0;
    BCOp op = BC__MAX;
    BCReg a = 0;

    if (scan == skip_pc) continue;

    ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    op = bc_op(ins);
    a = bc_a(ins);

    switch (op) {
      case BC_CALL:
        if (bc_b(ins) == 0 || bc_b(ins) > 2) {
          if (bc_b(ins) != 0 &&
              hole_reg >= (BCReg)(a + 1) && hole_reg < (BCReg)(a + bc_b(ins) - 1)) {
            return 1;
          }
          if (bc_c(ins) > 1 &&
              hole_reg >= (BCReg)(a + 1) && hole_reg < (BCReg)(a + bc_c(ins) - 1)) {
            return 1;
          }
        }
        break;
      case BC_ITERC:
        if (bc_b(ins) != 0 &&
            hole_reg >= (BCReg)(a + 1) && hole_reg < (BCReg)(a + bc_b(ins) - 1)) {
          return 1;
        }
        if (hole_reg >= (BCReg)(a + 1) && hole_reg < (BCReg)(a + bc_c(ins) - 1)) {
          return 1;
        }
        break;
      default:
        break;
    }
  }

  return 0;
}

static int tolua_range_breaks_future_calls(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                           uint32_t start_pc, BCReg first, BCReg last,
                                           uint32_t skip_pc)
{
  uint32_t scan = 0;

  for (scan = start_pc; scan < numbc; scan++) {
    BCIns ins = 0;
    BCOp op = BC__MAX;
    BCReg a = 0;

    if (scan == skip_pc) continue;
    ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    op = bc_op(ins);
    if (op != BC_CALL && op != BC_CALLT && op != BC_ITERC) continue;
    a = bc_a(ins);
    if (a < first || a > last) continue;
    return 1;
  }

  return 0;
}

static int tolua_find_call_repack_base(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                       uint32_t pc, BCIns call, BCOp op, BCReg old_base,
                                       BCReg old_last, BCReg min_base, int min_window, uint8_t framesize,
                                       const uint8_t *state_mask, const tolua_bcshift_map *map,
                                       int preserve_live_values,
                                       BCReg *out_new_base)
{
  BCReg nargs = op == BC_CALL ? (BCReg)(bc_c(call) - 1) : (BCReg)(bc_d(call) - 1);
  BCReg first_start = state_mask ? 0 : framesize;
  BCReg second_start = state_mask ? framesize : 0;
  BCReg cand = 0;
  int pass = 0;

  for (pass = 0; pass < 2; pass++) {
    BCReg start = pass == 0 ? first_start : second_start;
    BCReg limit = BCMAX_A;

    for (cand = start; cand <= limit; cand++) {
      BCReg cand_last = (BCReg)(cand + nargs);
      int hole_reg = -1;

      if (cand_last > BCMAX_A) break;
      if (cand < min_base) continue;
      if (cand == old_base) continue;
      if (pass == 0 && cand_last >= framesize) break;
      if (tolua_ranges_overlap(cand, cand_last, old_base, old_last)) continue;

      if (op == BC_CALL) {
        if (tolua_find_range_hole(map, (int)cand + 1, (int)cand + bc_c(call) - 1, &hole_reg)) {
          continue;
        }
      } else {
        if (tolua_find_range_hole(map, (int)cand + 1, (int)cand + bc_d(call) - 1, &hole_reg)) {
          continue;
        }
      }

      if (!map->hole[cand] &&
          tolua_hole_breaks_prior_insns(buf, bc_pos, numbc, be, pc, cand, pc)) {
        continue;
      }
      if (tolua_range_breaks_future_calls(buf, bc_pos, numbc, be, pc + 1,
                                          (BCReg)(cand + 1), cand_last, pc)) {
        continue;
      }

      if (state_mask && cand < framesize &&
          tolua_hole_breaks_future_iterator_setup(buf, bc_pos, numbc, be, pc, cand, pc)) {
        continue;
      }

      if (cand < framesize &&
          tolua_window_touches_range(buf, bc_pos, be, min_window, pc, cand, cand_last)) {
        continue;
      }
      if (state_mask && cand < framesize &&
          tolua_flow_touches_reg(buf, bc_pos, numbc, be, pc + 1, cand, state_mask)) {
        continue;
      }

      if (preserve_live_values && cand < framesize) {
        BCReg live_reg = 0;
        int live_conflict = 0;
        for (live_reg = cand; live_reg <= cand_last; live_reg++) {
          if (tolua_reg_live_after_pc(buf, bc_pos, numbc, be, pc + 1, live_reg)) {
            live_conflict = 1;
            break;
          }
        }
        if (live_conflict) {
          continue;
        }
      }

      *out_new_base = cand;
      return 1;
    }
  }

  return 0;
}

static int tolua_find_call_result_copy_base(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                            uint32_t pc, BCIns call, BCReg old_base,
                                            BCReg old_last, int min_window, uint8_t framesize,
                                            BCReg avoid_first, BCReg avoid_last,
                                            const tolua_bcshift_map *map,
                                            BCReg *out_new_base)
{
  unsigned int nargs = (unsigned int)(bc_c(call) - 1);
  unsigned int pass = 0;

  for (pass = 0; pass < 2; pass++) {
    unsigned int start = pass == 0 ? 0u : (unsigned int)framesize;
    unsigned int cand = 0;

    for (cand = start; cand <= BCMAX_A; cand++) {
      unsigned int cand_last = cand + nargs;
      int hole_reg = -1;

      if (cand_last > BCMAX_A) break;
      if (pass == 0 && cand_last >= (unsigned int)framesize) break;
      if ((BCReg)cand == old_base) continue;
      if (tolua_ranges_overlap((BCReg)cand, (BCReg)cand_last, old_base, old_last)) continue;
      if (avoid_first <= avoid_last &&
          tolua_ranges_overlap((BCReg)cand, (BCReg)cand_last, avoid_first, avoid_last)) {
        continue;
      }
      if (tolua_find_range_hole(map, (int)cand + 1, (int)cand + bc_c(call) - 1, &hole_reg)) {
        continue;
      }
      if (!map->hole[cand] &&
          tolua_hole_breaks_prior_insns(buf, bc_pos, numbc, be, pc, (BCReg)cand, pc)) {
        continue;
      }
      if (tolua_range_breaks_future_calls(buf, bc_pos, numbc, be, pc + 1,
                                          (BCReg)(cand + 1), (BCReg)cand_last, pc)) {
        continue;
      }
      if (cand < (unsigned int)framesize &&
          tolua_window_touches_range(buf, bc_pos, be, min_window, pc,
                                     (BCReg)cand, (BCReg)cand_last)) {
        continue;
      }
      if (cand < (unsigned int)framesize) {
        BCReg live_reg = 0;
        int live_conflict = 0;

        for (live_reg = (BCReg)cand; live_reg <= (BCReg)cand_last; live_reg++) {
          if (tolua_reg_live_after_pc(buf, bc_pos, numbc, be, pc + 1, live_reg)) {
            live_conflict = 1;
            break;
          }
        }
        if (live_conflict) {
          continue;
        }
      }

      *out_new_base = (BCReg)cand;
      return 1;
    }
  }

  return 0;
}

static int tolua_window_moves_call_hole(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                        uint32_t pc, BCReg old_base, BCReg old_last,
                                        int min_window,
                                        uint32_t *out_hole_pc, BCOp *out_hole_op,
                                        BCReg *out_hole_reg)
{
  int scan = 0;

  if (out_hole_pc) *out_hole_pc = UINT32_MAX;
  if (out_hole_op) *out_hole_op = BC__MAX;
  if (out_hole_reg) *out_hole_reg = 0;

  for (scan = min_window; scan < (int)pc; scan++) {
    BCIns ins = 0;
    BCOp op = BC__MAX;
    BCReg a = 0;

    ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    op = bc_op(ins);
    if (op != BC_CALL && op != BC_CALLT && op != BC_ITERC) continue;
    a = bc_a(ins);
    if (a <= old_base || a > old_last) continue;
    if (out_hole_pc) *out_hole_pc = (uint32_t)scan;
    if (out_hole_op) *out_hole_op = op;
    if (out_hole_reg) *out_hole_reg = a;
    return 1;
  }

  return 0;
}

static BCOp tolua_remap_bc_op(BCOp op, int remap_v1)
{
  if (!remap_v1) return op;
  if ((size_t)op >= sizeof(tolua_ulua_bc_map)) return BC__MAX;
  return (BCOp)tolua_ulua_bc_map[op];
}

static int tolua_resolve_proto_op(const uint8_t *buf, size_t bc_pos, uint32_t numbc, uint32_t pc,
                                  int be, int remap_v1, int target_fr2,
                                  BCIns ins, BCOp *out, const tolua_bcdebug_ctx *ctx)
{
  BCOp op = tolua_remap_bc_op(bc_op(ins), remap_v1);

  if (op >= BC__MAX) {
    return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_OPCODE,
                                   "opcode %u is not recognized after remap", (unsigned int)bc_op(ins));
  }

  if (target_fr2) {
    if (op == BC_ISNEXT) {
      ptrdiff_t target = (ptrdiff_t)pc + 1 + bc_j(ins);
      if (target < 0 || (uint32_t)target >= numbc) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "ISNEXT jump target %d is outside the proto",
                                       (int)target);
      }

      {
        BCIns target_ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)target * 4, be);
        BCOp target_op = tolua_remap_bc_op(bc_op(target_ins), remap_v1);
        if (target_op != BC_ITERN && target_op != BC_ITERC) {
          return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                         "ISNEXT target pc=%d is %s instead of ITERN/ITERC",
                                         (int)target, tolua_bc_opname(target_op));
        }
      }

      op = BC_JMP;
    } else if (op == BC_ITERN) {
      op = BC_ITERC;
    }
  }

  *out = op;
  return TOLUA_BCCONV_OK;
}

static int tolua_collapse_multires_producer(uint8_t *buf, size_t bc_pos, uint32_t pc,
                                            int be, const tolua_bcdebug_ctx *ctx)
{
  uint32_t producer_pc = pc;
  uint8_t *prev_slot = NULL;
  BCIns prev = 0;
  BCOp prev_op = BC__MAX;

  if (pc == 0) {
    return tolua_failbytecodeproto(ctx, pc, 0, BC__MAX, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                   "open-result consumer has no preceding producer");
  }

  producer_pc = pc - 1;
  prev_slot = buf + bc_pos + (size_t)producer_pc * 4;
  prev = (BCIns)tolua_read_ins(prev_slot, be);
  prev_op = bc_op(prev);
  if (prev_op == BC_UCLO) {
    if (producer_pc == 0) {
      return tolua_failbytecodeproto(ctx, producer_pc, prev, prev_op,
                                     TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                     "UCLO before open-result consumer has no preceding producer");
    }
    if ((ptrdiff_t)producer_pc + 1 + bc_j(prev) != (ptrdiff_t)pc) {
      return tolua_failbytecodeproto(ctx, producer_pc, prev, prev_op,
                                     TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                     "UCLO before open-result consumer jumps to pc=%d instead of pc=%u",
                                     (int)((ptrdiff_t)producer_pc + 1 + bc_j(prev)),
                                     (unsigned int)pc);
    }
    producer_pc--;
    prev_slot = buf + bc_pos + (size_t)producer_pc * 4;
    prev = (BCIns)tolua_read_ins(prev_slot, be);
    prev_op = bc_op(prev);
  }

  switch (prev_op) {
    case BC_CALL:
    case BC_VARG:
      if (bc_b(prev) == 0) {
        setbc_b(&prev, 2);
        tolua_write_ins(prev_slot, (uint32_t)prev, be);
      }
      return TOLUA_BCCONV_OK;
    default:
      return tolua_failbytecodeproto(ctx, producer_pc, prev, prev_op,
                                     TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                     "preceding %s does not support single-result downgrade",
                                     tolua_bc_opname(prev_op));
  }
}

static int tolua_prepare_proto_bytecode(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                        int remap_v1, int target_fr2,
                                        const tolua_bcdebug_ctx *ctx)
{
  uint32_t i = 0;

  for (i = 0; i < numbc; i++) {
    uint8_t *slot = buf + bc_pos + (size_t)i * 4;
    BCIns ins = (BCIns)tolua_read_ins(slot, be);
    BCOp op = tolua_remap_bc_op(bc_op(ins), remap_v1);

    if (op >= BC__MAX) {
      return tolua_failbytecodeproto(ctx, i, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_OPCODE,
                                     "opcode %u is not recognized after remap", (unsigned int)bc_op(ins));
    }

    if (target_fr2) {
      if (op == BC_CALLM && bc_c(ins) > 0) {
        BCReg new_b = bc_b(ins) ? bc_b(ins) : 2;
        BCReg new_c = (BCReg)(bc_c(ins) + 2);

        if ((uint32_t)bc_c(ins) + 2 > BCMAX_C) {
          return tolua_failbytecodeproto(ctx, i, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                         "CALLM argument count overflows when downgraded to CALL");
        }
        if (tolua_collapse_multires_producer(buf, bc_pos, i, be, ctx) != TOLUA_BCCONV_OK) {
          return TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT;
        }

        setbc_op(&ins, BC_CALL);
        setbc_b(&ins, new_b);
        setbc_c(&ins, new_c);
        op = BC_CALL;
      } else if (op == BC_CALLMT && bc_c(ins) > 0) {
        BCReg new_d = (BCReg)(bc_c(ins) + 2);

        if ((uint32_t)bc_c(ins) + 2 > BCMAX_D) {
          return tolua_failbytecodeproto(ctx, i, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                         "CALLMT argument count overflows when downgraded to CALLT");
        }
        if (tolua_collapse_multires_producer(buf, bc_pos, i, be, ctx) != TOLUA_BCCONV_OK) {
          return TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT;
        }

        ins = BCINS_AD(BC_CALLT, bc_a(ins), new_d);
        op = BC_CALLT;
      } else if (op == BC_ISNEXT) {
        ptrdiff_t target = (ptrdiff_t)i + 1 + bc_j(ins);
        if (target < 0 || (uint32_t)target >= numbc) {
          return tolua_failbytecodeproto(ctx, i, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                         "ISNEXT jump target %d is outside the proto",
                                         (int)target);
        }
        {
          BCIns target_ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)target * 4, be);
          BCOp target_op = tolua_remap_bc_op(bc_op(target_ins), remap_v1);
          if (target_op != BC_ITERN && target_op != BC_ITERC) {
            return tolua_failbytecodeproto(ctx, i, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                           "ISNEXT target pc=%d is %s instead of ITERN/ITERC",
                                           (int)target, tolua_bc_opname(target_op));
          }
        }
        op = BC_JMP;
        setbc_op(&ins, op);
      } else if (op == BC_ITERN) {
        op = BC_ITERC;
        setbc_op(&ins, op);
      } else {
        setbc_op(&ins, op);
      }
    } else {
      setbc_op(&ins, op);
    }

    tolua_write_ins(slot, (uint32_t)ins, be);
  }

  return TOLUA_BCCONV_OK;
}

static int tolua_try_repack_call_result_copy(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                             uint32_t producer_pc, BCIns producer,
                                             uint32_t consumer_pc, int avoid_first, int avoid_last,
                                             uint8_t *framesize_io,
                                             const uint8_t *targets, const tolua_bcshift_map *map,
                                             const tolua_bcdebug_ctx *ctx, int *changed);
static int tolua_try_repack_iterc(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                  uint32_t pc, uint8_t *framesize_io, const uint8_t *targets,
                                  const tolua_bcshift_map *map,
                                  const tolua_bcdebug_ctx *ctx, int *changed);

static int tolua_try_repack_adjacent_call_chain(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                uint32_t producer_pc, BCIns producer,
                                                uint32_t consumer_pc, BCIns consumer,
                                                uint8_t *framesize_io, const uint8_t *targets,
                                                const tolua_bcshift_map *map,
                                                const tolua_bcdebug_ctx *ctx, int *changed)
{
  BCReg producer_base = bc_a(producer);
  BCReg producer_nargs = bc_c(producer) > 0 ? (BCReg)(bc_c(producer) - 1) : 0;
  BCReg producer_last = (BCReg)(producer_base + producer_nargs);
  BCReg consumer_base = bc_a(consumer);
  BCReg consumer_last = (BCReg)(consumer_base + bc_c(consumer) - 1);
  BCReg consumer_nres = bc_b(consumer) > 0 ? (BCReg)(bc_b(consumer) - 1) : 0;
  BCReg consumer_prefix_last = 0;
  BCReg new_consumer_base = 0;
  BCReg new_producer_base = 0;
  BCReg new_producer_last = 0;
  uint8_t *consumer_selected = NULL;
  uint8_t *producer_selected = NULL;
  uint8_t *consumer_state_mask = NULL;
  uint8_t *consumer_rewrite = NULL;
  uint32_t *consumer_queue_pc = NULL;
  uint8_t *consumer_queue_state = NULL;
  size_t consumer_queue_cap = (size_t)numbc * 2;
  size_t consumer_head = 0, consumer_tail = 0;
  int consumer_min_window = (int)consumer_pc;
  int producer_min_window = (int)producer_pc;
  uint32_t consumer_slice_interference_pc = UINT32_MAX;
  BCOp consumer_slice_interference_op = BC__MAX;
  BCReg consumer_slice_interference_reg = 0;
  uint32_t producer_slice_interference_pc = UINT32_MAX;
  BCOp producer_slice_interference_op = BC__MAX;
  BCReg producer_slice_interference_reg = 0;
  int start_window = 0;
  int consumer_flow_failed = 0;
  int consumer_rewrite_failed = 0;
  int hole_reg = -1;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;
  TOLUA_REPACK_LOG(ctx, consumer_pc,
                   "enter adjacent call-chain producer_pc=%u producer_base=%u consumer_base=%u",
                   (unsigned int)producer_pc, (unsigned int)producer_base,
                   (unsigned int)consumer_base);

  if (bc_op(producer) != BC_CALL || bc_b(producer) != 2 || bc_c(producer) == 0) {
    return TOLUA_BCCONV_OK;
  }
  if (bc_op(consumer) != BC_CALL || bc_b(consumer) == 0 || bc_b(consumer) > 2 || bc_c(consumer) <= 1) {
    return TOLUA_BCCONV_OK;
  }
  if (consumer_last != producer_base) {
    return TOLUA_BCCONV_OK;
  }

  consumer_prefix_last = (BCReg)(consumer_last - 1);

  consumer_selected = (uint8_t *)calloc((size_t)numbc, 1);
  producer_selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!consumer_selected || !producer_selected) {
    free(producer_selected);
    free(consumer_selected);
    return tolua_failbytecodeproto(ctx, producer_pc, producer, BC_CALL, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate adjacent call repack buffers");
  }

  if (consumer_nres == 1) {
    consumer_state_mask = (uint8_t *)calloc((size_t)numbc, 1);
    consumer_rewrite = (uint8_t *)calloc((size_t)numbc, 1);
    consumer_queue_pc = (uint32_t *)malloc(sizeof(uint32_t) * consumer_queue_cap);
    consumer_queue_state = (uint8_t *)malloc(consumer_queue_cap);
    if (!consumer_state_mask || !consumer_rewrite || !consumer_queue_pc || !consumer_queue_state) {
      free(consumer_queue_state);
      free(consumer_queue_pc);
      free(consumer_rewrite);
      free(consumer_state_mask);
      free(producer_selected);
      free(consumer_selected);
      return tolua_failbytecodeproto(ctx, consumer_pc, consumer, BC_CALL, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                     "failed to allocate adjacent consumer flow buffers");
    }

    if (consumer_pc + 1 < numbc) {
      consumer_queue_pc[consumer_tail] = consumer_pc + 1;
      consumer_queue_state[consumer_tail++] = 0;
    }

    while (consumer_head < consumer_tail) {
      uint32_t cur_pc = consumer_queue_pc[consumer_head];
      uint8_t cur_state = consumer_queue_state[consumer_head++];
      uint8_t mask = (uint8_t)(1u << cur_state);
      BCIns ins = 0;
      BCOp ins_op = BC__MAX;
      uint32_t succ[2];
      int succ_count = 0;
      uint8_t next_state = cur_state;

      if ((consumer_state_mask[cur_pc] & mask) != 0) continue;
      consumer_state_mask[cur_pc] |= mask;

      ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)cur_pc * 4, be);
      ins_op = bc_op(ins);

      if ((consumer_state_mask[cur_pc] & 0x3u) == 0x3u &&
          tolua_ins_reads_reg(ins_op, ins, consumer_base)) {
        TOLUA_REPACK_LOG(ctx, consumer_pc,
                         "adjacent consumer flow conflict at pc=%u old_base=%u op=%s",
                         (unsigned int)cur_pc, (unsigned int)consumer_base, tolua_bc_opname(ins_op));
        consumer_flow_failed = 1;
        break;
      }

      if (cur_state == 0 && tolua_ins_reads_reg(ins_op, ins, consumer_base)) {
        consumer_rewrite[cur_pc] = 1;
      }
      if (tolua_ins_writes_reg(ins_op, ins, consumer_base)) {
        next_state = 1;
      }

      succ_count = tolua_fill_successors(ins_op, ins, cur_pc, numbc, succ);
      while (succ_count-- > 0) {
        if (consumer_tail >= consumer_queue_cap) {
          TOLUA_REPACK_LOG(ctx, consumer_pc,
                           "adjacent consumer flow queue overflow at pc=%u",
                           (unsigned int)cur_pc);
          consumer_flow_failed = 1;
          break;
        }
        consumer_queue_pc[consumer_tail] = succ[succ_count];
        consumer_queue_state[consumer_tail++] = next_state;
      }

      if (consumer_flow_failed) break;
    }

    if (consumer_flow_failed) {
      free(consumer_queue_state);
      free(consumer_queue_pc);
      free(consumer_rewrite);
      free(consumer_state_mask);
      free(producer_selected);
      free(consumer_selected);
      return TOLUA_BCCONV_OK;
    }
  }

  if (tolua_select_repack_slice(buf, bc_pos, numbc, be, consumer_pc, consumer_base,
                                consumer_prefix_last, targets, consumer_selected,
                                &consumer_min_window, &consumer_slice_interference_pc,
                                &consumer_slice_interference_op,
                                &consumer_slice_interference_reg) &&
      tolua_select_repack_slice(buf, bc_pos, numbc, be, producer_pc, producer_base, producer_last,
                                targets, producer_selected, &producer_min_window,
                                &producer_slice_interference_pc,
                                &producer_slice_interference_op,
                                &producer_slice_interference_reg)) {
    start_window = consumer_min_window < producer_min_window ? consumer_min_window : producer_min_window;
    TOLUA_REPACK_LOG(ctx, consumer_pc, "adjacent chain slices ready start_window=%d",
                     start_window);

    for (scan = start_window; scan < (int)producer_pc; scan++) {
      if (consumer_selected[scan]) {
        BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
        BCOp cur_op = bc_op(cur);
        BCReg cur_a = bc_a(cur);

        if ((cur_op == BC_CALL || cur_op == BC_ITERC) &&
            cur_a > consumer_base && cur_a <= consumer_prefix_last) {
          TOLUA_REPACK_LOG(ctx, consumer_pc,
                           "adjacent chain rejected by nested producer at pc=%d a=%u op=%s",
                           scan, (unsigned int)cur_a, tolua_bc_opname(cur_op));
          free(consumer_queue_state);
          free(consumer_queue_pc);
          free(consumer_rewrite);
          free(consumer_state_mask);
          free(producer_selected);
          free(consumer_selected);
          return TOLUA_BCCONV_OK;
        }
      }
    }

    {
      BCReg first_start = consumer_nres == 1 ? 0 : *framesize_io;
      BCReg second_start = consumer_nres == 1 ? *framesize_io : 0;
      int pass = 0;
      int found = 0;

      for (pass = 0; pass < 2 && !found; pass++) {
        BCReg start = pass == 0 ? first_start : second_start;
        BCReg cand = 0;

        for (cand = start; cand <= BCMAX_A; cand++) {
          BCReg cand_last = (BCReg)(cand + bc_c(consumer) - 1);
          BCReg cand_producer_base = cand_last;
          BCReg cand_producer_last = (BCReg)(cand_producer_base + producer_nargs);

          if (cand_producer_last > BCMAX_A) break;
          if (cand == consumer_base) continue;
          if (pass == 0 && cand_last >= *framesize_io) break;
          if (tolua_ranges_overlap(cand, cand_last, consumer_base, consumer_last)) continue;
          if (tolua_ranges_overlap(cand_producer_base, cand_producer_last,
                                   producer_base, producer_last)) {
            continue;
          }

          if (tolua_find_range_hole(map, (int)cand + 1, (int)cand_producer_base, &hole_reg)) {
            continue;
          }
          if (tolua_find_range_hole(map, (int)cand_producer_base + 1,
                                    (int)cand_producer_base + bc_c(producer) - 1,
                                    &hole_reg)) {
            continue;
          }

          if (!map->hole[cand] &&
              tolua_hole_breaks_prior_insns(buf, bc_pos, numbc, be, consumer_pc, cand,
                                            consumer_pc)) {
            continue;
          }
          if (!map->hole[cand_producer_base] &&
              tolua_hole_breaks_prior_insns(buf, bc_pos, numbc, be, producer_pc,
                                            cand_producer_base, producer_pc)) {
            continue;
          }

          if (consumer_nres == 1 && cand < *framesize_io &&
              tolua_hole_breaks_future_iterator_setup(buf, bc_pos, numbc, be,
                                                      consumer_pc, cand, consumer_pc)) {
            continue;
          }
          if (cand_producer_base < *framesize_io &&
              tolua_hole_breaks_future_iterator_setup(buf, bc_pos, numbc, be,
                                                      consumer_pc, cand_producer_base,
                                                      producer_pc)) {
            continue;
          }

          if (cand < *framesize_io &&
              tolua_window_touches_range(buf, bc_pos, be, start_window, consumer_pc,
                                         cand, cand_producer_last)) {
            continue;
          }
          if (consumer_nres == 1 && cand < *framesize_io &&
              tolua_flow_touches_reg(buf, bc_pos, numbc, be, consumer_pc + 1, cand,
                                     consumer_state_mask)) {
            continue;
          }

          new_consumer_base = cand;
          new_producer_base = cand_producer_base;
          new_producer_last = cand_producer_last;
          found = 1;
          break;
        }
      }

      if (!found) {
        TOLUA_REPACK_LOG(ctx, consumer_pc, "adjacent chain candidate reject frame=%u",
                         (unsigned int)*framesize_io);
        free(consumer_queue_state);
        free(consumer_queue_pc);
        free(consumer_rewrite);
        free(consumer_state_mask);
        free(producer_selected);
        free(consumer_selected);
        return TOLUA_BCCONV_OK;
      }
    }

    TOLUA_REPACK_LOG(ctx, consumer_pc,
                     "adjacent chain candidate consumer_new=%u producer_new=[%u,%u]",
                     (unsigned int)new_consumer_base,
                     (unsigned int)new_producer_base,
                     (unsigned int)new_producer_last);

    if (consumer_nres == 1) {
      for (scan = (int)consumer_pc + 1; scan < (int)numbc; scan++) {
        if (consumer_rewrite[scan]) {
          BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
          BCOp ins_op = bc_op(ins);
          if (!tolua_rewrite_ins_source_reg(&ins, ins_op, consumer_base, new_consumer_base)) {
            TOLUA_REPACK_LOG(ctx, consumer_pc,
                             "adjacent consumer rewrite reject at pc=%d old_base=%u new_base=%u op=%s",
                             scan, (unsigned int)consumer_base, (unsigned int)new_consumer_base,
                             tolua_bc_opname(ins_op));
            consumer_rewrite_failed = 1;
            break;
          }
        }
      }

      if (!consumer_rewrite_failed) {
        for (scan = (int)consumer_pc + 1; scan < (int)numbc; scan++) {
          if (consumer_rewrite[scan]) {
            BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
            BCOp ins_op = bc_op(ins);
            tolua_rewrite_ins_source_reg(&ins, ins_op, consumer_base, new_consumer_base);
            tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)ins, be);
          }
        }
      }
    }

    if (!consumer_rewrite_failed) {
      for (scan = start_window; scan <= (int)producer_pc; scan++) {
        BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
        BCOp cur_op = bc_op(cur);

        if (consumer_selected[scan]) {
          tolua_repack_remap_reg_range(&cur, cur_op, consumer_base, consumer_prefix_last,
                                       new_consumer_base);
        }
        if (producer_selected[scan]) {
          tolua_repack_remap_reg_range(&cur, cur_op, producer_base, producer_last, new_producer_base);
        }
        tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
        if (consumer_selected[scan]) {
          tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                             consumer_base, consumer_prefix_last, new_consumer_base);
        }
        if (producer_selected[scan]) {
          tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                             producer_base, producer_last, new_producer_base);
        }
      }

      setbc_a(&producer, new_producer_base);
      tolua_write_ins(buf + bc_pos + (size_t)producer_pc * 4, (uint32_t)producer, be);
      setbc_a(&consumer, new_consumer_base);
      tolua_write_ins(buf + bc_pos + (size_t)consumer_pc * 4, (uint32_t)consumer, be);
      status = tolua_update_framesize_checked(framesize_io, new_producer_last, ctx,
                                              consumer_pc, consumer, BC_CALL);
      if (status != TOLUA_BCCONV_OK) {
        free(consumer_queue_state);
        free(consumer_queue_pc);
        free(consumer_rewrite);
        free(consumer_state_mask);
        free(producer_selected);
        free(consumer_selected);
        return status;
      }
      TOLUA_REPACK_LOG(ctx, consumer_pc,
                       "adjacent chain success consumer_new=%u producer_new=[%u,%u]",
                       (unsigned int)new_consumer_base,
                       (unsigned int)new_producer_base,
                       (unsigned int)new_producer_last);
      *changed = 1;
    }
  }
  else {
    int inner_changed = 0;

    if (consumer_slice_interference_pc != UINT32_MAX &&
        consumer_slice_interference_pc < consumer_pc &&
        consumer_slice_interference_op == BC_CALL) {
      BCIns interfering = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)consumer_slice_interference_pc * 4, be);
      if (bc_b(interfering) == 2 && bc_c(interfering) != 0 &&
          !targets[consumer_slice_interference_pc]) {
        TOLUA_REPACK_LOG(ctx, consumer_pc,
                         "adjacent chain retry via consumer blocker call at pc=%u reg=%u",
                         (unsigned int)consumer_slice_interference_pc,
                         (unsigned int)consumer_slice_interference_reg);
        status = tolua_try_repack_call_result_copy(buf, bc_pos, numbc, be,
                                                   consumer_slice_interference_pc, interfering,
                                                   consumer_pc, consumer_base, consumer_last,
                                                   framesize_io, targets, map, ctx,
                                                   &inner_changed);
        if (status != TOLUA_BCCONV_OK || inner_changed) {
          *changed = inner_changed;
          free(consumer_queue_state);
          free(consumer_queue_pc);
          free(consumer_rewrite);
          free(consumer_state_mask);
          free(producer_selected);
          free(consumer_selected);
          return status;
        }
      }
    }

    if (producer_slice_interference_pc != UINT32_MAX &&
        producer_slice_interference_pc < producer_pc &&
        producer_slice_interference_op == BC_CALL) {
      BCIns interfering = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)producer_slice_interference_pc * 4, be);
      if (bc_b(interfering) == 2 && bc_c(interfering) != 0 &&
          !targets[producer_slice_interference_pc]) {
        TOLUA_REPACK_LOG(ctx, consumer_pc,
                         "adjacent chain retry via producer blocker call at pc=%u reg=%u",
                         (unsigned int)producer_slice_interference_pc,
                         (unsigned int)producer_slice_interference_reg);
        status = tolua_try_repack_call_result_copy(buf, bc_pos, numbc, be,
                                                   producer_slice_interference_pc, interfering,
                                                   consumer_pc, producer_base, producer_last,
                                                   framesize_io, targets, map, ctx,
                                                   &inner_changed);
        if (status != TOLUA_BCCONV_OK || inner_changed) {
          *changed = inner_changed;
          free(consumer_queue_state);
          free(consumer_queue_pc);
          free(consumer_rewrite);
          free(consumer_state_mask);
          free(producer_selected);
          free(consumer_selected);
          return status;
        }
      }
    }

    TOLUA_REPACK_LOG(ctx, consumer_pc, "adjacent chain slice reject");
  }

  free(consumer_queue_state);
  free(consumer_queue_pc);
  free(consumer_rewrite);
  free(consumer_state_mask);
  free(producer_selected);
  free(consumer_selected);
  return TOLUA_BCCONV_OK;
}

static int tolua_try_repack_first_arg_call_chain(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                 uint32_t producer_pc, BCIns producer,
                                                 uint32_t consumer_pc, BCIns consumer,
                                                 uint8_t *framesize_io, const uint8_t *targets,
                                                 const tolua_bcshift_map *map,
                                                 const tolua_bcdebug_ctx *ctx, int *changed)
{
  BCReg consumer_base = bc_a(consumer);
  BCReg consumer_nargs = bc_c(consumer) > 0 ? (BCReg)(bc_c(consumer) - 1) : 0;
  BCReg consumer_nres = bc_b(consumer) > 0 ? (BCReg)(bc_b(consumer) - 1) : 0;
  BCReg consumer_last = (BCReg)(consumer_base + consumer_nargs);
  BCReg producer_base = bc_a(producer);
  BCReg producer_nargs = bc_c(producer) > 0 ? (BCReg)(bc_c(producer) - 1) : 0;
  BCReg producer_last = (BCReg)(producer_base + producer_nargs);
  BCReg prefix_last = 0;
  BCReg suffix_first = 0;
  BCReg new_consumer_base = 0;
  BCReg new_consumer_last = 0;
  BCReg new_producer_base = 0;
  BCReg new_suffix_base = 0;
  uint8_t *prefix_selected = NULL;
  uint8_t *producer_selected = NULL;
  uint8_t *suffix_selected = NULL;
  uint8_t *state_mask = NULL;
  uint8_t *rewrite = NULL;
  uint32_t *queue_pc = NULL;
  uint8_t *queue_state = NULL;
  size_t queue_cap = (size_t)numbc * 2;
  size_t head = 0, tail = 0;
  int prefix_min_window = (int)consumer_pc;
  int producer_min_window = (int)producer_pc;
  int suffix_min_window = (int)consumer_pc;
  int min_window = 0;
  int have_suffix = 0;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;
  TOLUA_REPACK_LOG(ctx, consumer_pc,
                   "enter first-arg chain producer_pc=%u producer_base=%u consumer_base=%u",
                   (unsigned int)producer_pc, (unsigned int)producer_base,
                   (unsigned int)consumer_base);

  if (bc_op(producer) != BC_CALL || bc_b(producer) != 2 || bc_c(producer) != 2) {
    return TOLUA_BCCONV_OK;
  }
  if (bc_op(consumer) != BC_CALL || bc_b(consumer) == 0 || bc_b(consumer) > 2 || bc_c(consumer) <= 1) {
    return TOLUA_BCCONV_OK;
  }
  if (producer_pc >= consumer_pc) return TOLUA_BCCONV_OK;
  if (producer_base != (BCReg)(consumer_base + 1)) return TOLUA_BCCONV_OK;
  if (consumer_nargs != 2 || producer_nargs != 1 || producer_last != consumer_last) {
    return TOLUA_BCCONV_OK;
  }

  prefix_last = (BCReg)(producer_base - 1);
  suffix_first = (BCReg)(producer_base + 1);
  have_suffix = suffix_first <= consumer_last;

  prefix_selected = (uint8_t *)calloc((size_t)numbc, 1);
  producer_selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!prefix_selected || !producer_selected) {
    free(producer_selected);
    free(prefix_selected);
    return tolua_failbytecodeproto(ctx, consumer_pc, consumer, BC_CALL, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate first-arg call repack buffers");
  }
  if (have_suffix) {
    suffix_selected = (uint8_t *)calloc((size_t)numbc, 1);
    if (!suffix_selected) {
      free(producer_selected);
      free(prefix_selected);
      return tolua_failbytecodeproto(ctx, consumer_pc, consumer, BC_CALL, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                     "failed to allocate first-arg call repack suffix buffer");
    }
  }

  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, consumer_pc, consumer_base, prefix_last,
                                 targets, prefix_selected, &prefix_min_window,
                                 NULL, NULL, NULL) ||
      !tolua_select_repack_slice(buf, bc_pos, numbc, be, producer_pc, producer_base, producer_last,
                                 targets, producer_selected, &producer_min_window,
                                 NULL, NULL, NULL) ||
      (have_suffix &&
       !tolua_select_repack_slice(buf, bc_pos, numbc, be, consumer_pc, suffix_first, consumer_last,
                                  targets, suffix_selected, &suffix_min_window,
                                  NULL, NULL, NULL))) {
    TOLUA_REPACK_LOG(ctx, consumer_pc, "first-arg chain slice reject");
    free(queue_state);
    free(queue_pc);
    free(rewrite);
    free(state_mask);
    free(suffix_selected);
    free(producer_selected);
    free(prefix_selected);
    return TOLUA_BCCONV_OK;
  }

  for (scan = 0; scan < (int)numbc; scan++) {
    if (prefix_selected[scan] && scan >= (int)producer_pc) {
      free(queue_state);
      free(queue_pc);
      free(rewrite);
      free(state_mask);
      free(suffix_selected);
      free(producer_selected);
      free(prefix_selected);
      return TOLUA_BCCONV_OK;
    }
    if (have_suffix && suffix_selected[scan] && scan <= (int)producer_pc) {
      free(queue_state);
      free(queue_pc);
      free(rewrite);
      free(state_mask);
      free(suffix_selected);
      free(producer_selected);
      free(prefix_selected);
      return TOLUA_BCCONV_OK;
    }
  }

  if (consumer_nres == 1) {
    state_mask = (uint8_t *)calloc((size_t)numbc, 1);
    rewrite = (uint8_t *)calloc((size_t)numbc, 1);
    queue_pc = (uint32_t *)malloc(sizeof(uint32_t) * queue_cap);
    queue_state = (uint8_t *)malloc(queue_cap);
    if (!state_mask || !rewrite || !queue_pc || !queue_state) {
      free(queue_state);
      free(queue_pc);
      free(rewrite);
      free(state_mask);
      free(suffix_selected);
      free(producer_selected);
      free(prefix_selected);
      return tolua_failbytecodeproto(ctx, consumer_pc, consumer, BC_CALL, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                     "failed to allocate first-arg consumer flow buffers");
    }

    if (consumer_pc + 1 < numbc) {
      queue_pc[tail] = consumer_pc + 1;
      queue_state[tail++] = 0;
    }

    while (head < tail) {
      uint32_t cur_pc = queue_pc[head];
      uint8_t cur_state = queue_state[head++];
      uint8_t mask = (uint8_t)(1u << cur_state);
      BCIns ins = 0;
      BCOp ins_op = BC__MAX;
      uint32_t succ[2];
      int succ_count = 0;
      uint8_t next_state = cur_state;

      if ((state_mask[cur_pc] & mask) != 0) continue;
      state_mask[cur_pc] |= mask;

      ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)cur_pc * 4, be);
      ins_op = bc_op(ins);

      if ((state_mask[cur_pc] & 0x3u) == 0x3u &&
          tolua_ins_reads_reg(ins_op, ins, consumer_base)) {
        TOLUA_REPACK_LOG(ctx, consumer_pc,
                         "first-arg consumer flow conflict at pc=%u old_base=%u op=%s",
                         (unsigned int)cur_pc, (unsigned int)consumer_base, tolua_bc_opname(ins_op));
        free(queue_state);
        free(queue_pc);
        free(rewrite);
        free(state_mask);
        free(suffix_selected);
        free(producer_selected);
        free(prefix_selected);
        return TOLUA_BCCONV_OK;
      }

      if (cur_state == 0 && tolua_ins_reads_reg(ins_op, ins, consumer_base)) {
        rewrite[cur_pc] = 1;
      }
      if (tolua_ins_writes_reg(ins_op, ins, consumer_base)) {
        next_state = 1;
      }

      succ_count = tolua_fill_successors(ins_op, ins, cur_pc, numbc, succ);
      while (succ_count-- > 0) {
        if (tail >= queue_cap) {
          TOLUA_REPACK_LOG(ctx, consumer_pc,
                           "first-arg consumer flow queue overflow at pc=%u",
                           (unsigned int)cur_pc);
          free(queue_state);
          free(queue_pc);
          free(rewrite);
          free(state_mask);
          free(suffix_selected);
          free(producer_selected);
          free(prefix_selected);
          return TOLUA_BCCONV_OK;
        }
        queue_pc[tail] = succ[succ_count];
        queue_state[tail++] = next_state;
      }
    }
  }

  min_window = prefix_min_window < producer_min_window ? prefix_min_window : producer_min_window;
  if (have_suffix && suffix_min_window < min_window) min_window = suffix_min_window;

  {
    BCReg cand = 0;
    int found = 0;
    int hole_reg = -1;

    for (cand = *framesize_io; cand <= BCMAX_A; cand++) {
      BCReg cand_last = (BCReg)(cand + consumer_nargs);

      if (cand_last > BCMAX_A) break;
      if (cand == consumer_base) continue;
      if (tolua_ranges_overlap(cand, cand_last, consumer_base, consumer_last)) continue;
      if (tolua_find_range_hole(map, (int)cand + 1, (int)cand + bc_c(consumer) - 1, &hole_reg)) {
        continue;
      }

      new_consumer_base = cand;
      found = 1;
      break;
    }

    if (!found) {
      TOLUA_REPACK_LOG(ctx, consumer_pc, "first-arg chain candidate reject frame=%u",
                       (unsigned int)*framesize_io);
      free(queue_state);
      free(queue_pc);
      free(rewrite);
      free(state_mask);
      free(suffix_selected);
      free(producer_selected);
      free(prefix_selected);
      return TOLUA_BCCONV_OK;
    }
  }

  new_consumer_last = (BCReg)(new_consumer_base + consumer_nargs);
  new_producer_base = (BCReg)(new_consumer_base + (producer_base - consumer_base));
  new_suffix_base = (BCReg)(new_consumer_base + (suffix_first - consumer_base));
  TOLUA_REPACK_LOG(ctx, consumer_pc,
                   "first-arg chain candidate consumer_new=[%u,%u] producer_new=%u suffix_new=%u",
                   (unsigned int)new_consumer_base, (unsigned int)new_consumer_last,
                   (unsigned int)new_producer_base, (unsigned int)new_suffix_base);

  if (consumer_nres == 1) {
    for (scan = (int)consumer_pc + 1; scan < (int)numbc; scan++) {
      if (rewrite[scan]) {
        BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
        BCOp ins_op = bc_op(ins);
        if (!tolua_rewrite_ins_source_reg(&ins, ins_op, consumer_base, new_consumer_base)) {
          TOLUA_REPACK_LOG(ctx, consumer_pc,
                           "first-arg consumer rewrite reject at pc=%d old_base=%u new_base=%u op=%s",
                           scan, (unsigned int)consumer_base, (unsigned int)new_consumer_base,
                           tolua_bc_opname(ins_op));
          free(queue_state);
          free(queue_pc);
          free(rewrite);
          free(state_mask);
          free(suffix_selected);
          free(producer_selected);
          free(prefix_selected);
          return TOLUA_BCCONV_OK;
        }
        tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)ins, be);
      }
    }
  }

  for (scan = min_window; scan < (int)producer_pc; scan++) {
    BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp cur_op = bc_op(cur);

    if (prefix_selected[scan]) {
      tolua_repack_remap_reg_range(&cur, cur_op, consumer_base, prefix_last, new_consumer_base);
    }
    if (producer_selected[scan]) {
      tolua_repack_remap_reg_range(&cur, cur_op, producer_base, producer_last, new_producer_base);
    }

    tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
    if (prefix_selected[scan]) {
      tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                         consumer_base, prefix_last, new_consumer_base);
    }
    if (producer_selected[scan]) {
      tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                         producer_base, producer_last, new_producer_base);
    }
  }

  tolua_repack_remap_reg_range(&producer, BC_CALL, producer_base, producer_last, new_producer_base);
  tolua_write_ins(buf + bc_pos + (size_t)producer_pc * 4, (uint32_t)producer, be);

  for (scan = (int)producer_pc + 1; scan < (int)consumer_pc; scan++) {
    BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp cur_op = bc_op(cur);

    if (have_suffix && suffix_selected[scan]) {
      tolua_repack_remap_reg_range(&cur, cur_op, suffix_first, consumer_last, new_suffix_base);
    }

    tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
    if (have_suffix && suffix_selected[scan]) {
      tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                         suffix_first, consumer_last, new_suffix_base);
    }
  }

  setbc_a(&consumer, new_consumer_base);
  tolua_write_ins(buf + bc_pos + (size_t)consumer_pc * 4, (uint32_t)consumer, be);
  status = tolua_update_framesize_checked(framesize_io, new_consumer_last, ctx,
                                          consumer_pc, consumer, BC_CALL);
  if (status != TOLUA_BCCONV_OK) {
    free(queue_state);
    free(queue_pc);
    free(rewrite);
    free(state_mask);
    free(suffix_selected);
    free(producer_selected);
    free(prefix_selected);
    return status;
  }
  *changed = 1;
  TOLUA_REPACK_LOG(ctx, consumer_pc,
                   "first-arg call-chain success producer_pc=%u old=[%u,%u] new=[%u,%u]",
                   (unsigned int)producer_pc,
                   (unsigned int)consumer_base, (unsigned int)consumer_last,
                   (unsigned int)new_consumer_base, (unsigned int)new_consumer_last);

  free(queue_state);
  free(queue_pc);
  free(rewrite);
  free(state_mask);
  free(suffix_selected);
  free(producer_selected);
  free(prefix_selected);
  return TOLUA_BCCONV_OK;
}

static int tolua_try_repack_adjacent_cat_call_chain(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                    uint32_t producer_pc, BCIns producer,
                                                    uint32_t consumer_pc, BCIns consumer,
                                                    uint8_t *framesize_io, const uint8_t *targets,
                                                    const tolua_bcshift_map *map,
                                                    const tolua_bcdebug_ctx *ctx, int *changed)
{
  BCReg producer_first = bc_b(producer);
  BCReg producer_last = bc_c(producer);
  BCReg producer_dest = bc_a(producer);
  BCReg consumer_base = bc_a(consumer);
  BCReg consumer_nargs = 0;
  BCReg consumer_last = 0;
  BCReg consumer_prefix_last = 0;
  BCReg new_consumer_base = 0;
  BCReg new_consumer_last = 0;
  BCReg new_producer_first = 0;
  BCReg new_producer_last = 0;
  uint8_t *consumer_selected = NULL;
  uint8_t *producer_selected = NULL;
  int consumer_min_window = (int)consumer_pc;
  int producer_min_window = (int)producer_pc;
  int start_window = 0;
  int scan = 0;
  int hole_reg = -1;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;

  if (bc_op(producer) != BC_CAT || bc_op(consumer) != BC_CALL) return TOLUA_BCCONV_OK;
  if (bc_b(consumer) != 1 || bc_c(consumer) <= 1) return TOLUA_BCCONV_OK;
  if (producer_last < producer_first) return TOLUA_BCCONV_OK;
  if (producer_dest != producer_first) return TOLUA_BCCONV_OK;

  consumer_nargs = (BCReg)(bc_c(consumer) - 1);
  consumer_last = (BCReg)(consumer_base + consumer_nargs);
  if (producer_dest != consumer_last) return TOLUA_BCCONV_OK;

  consumer_prefix_last = (BCReg)(consumer_last - 1);
  new_consumer_base = *framesize_io;
  new_consumer_last = (BCReg)(new_consumer_base + consumer_nargs);
  if (new_consumer_last > BCMAX_A) return TOLUA_BCCONV_OK;

  new_producer_first = new_consumer_last;
  new_producer_last = (BCReg)(new_producer_first + (producer_last - producer_first));
  if (new_producer_last > BCMAX_A) return TOLUA_BCCONV_OK;

  if (tolua_find_range_hole(map, (int)new_consumer_base + 1,
                            (int)new_consumer_base + bc_c(consumer) - 1, &hole_reg)) {
    return TOLUA_BCCONV_OK;
  }
  if (tolua_find_closed_range_hole(map, new_producer_first, new_producer_last, &hole_reg)) {
    return TOLUA_BCCONV_OK;
  }

  consumer_selected = (uint8_t *)calloc((size_t)numbc, 1);
  producer_selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!consumer_selected || !producer_selected) {
    free(producer_selected);
    free(consumer_selected);
    return tolua_failbytecodeproto(ctx, consumer_pc, consumer, BC_CALL, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate adjacent cat/call repack buffers");
  }

  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, consumer_pc, consumer_base,
                                 consumer_prefix_last, targets, consumer_selected,
                                 &consumer_min_window, NULL, NULL, NULL) ||
      !tolua_select_repack_slice(buf, bc_pos, numbc, be, producer_pc, producer_first, producer_last,
                                 targets, producer_selected, &producer_min_window,
                                 NULL, NULL, NULL)) {
    free(producer_selected);
    free(consumer_selected);
    return TOLUA_BCCONV_OK;
  }

  start_window = consumer_min_window < producer_min_window ? consumer_min_window : producer_min_window;
  for (scan = start_window; scan < (int)consumer_pc; scan++) {
    BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp cur_op = bc_op(cur);

    if (consumer_selected[scan]) {
      tolua_repack_remap_reg_range(&cur, cur_op, consumer_base, consumer_prefix_last, new_consumer_base);
    }
    if (producer_selected[scan]) {
      tolua_repack_remap_reg_range(&cur, cur_op, producer_first, producer_last, new_producer_first);
    }

    tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
    if (consumer_selected[scan]) {
      tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                         consumer_base, consumer_prefix_last, new_consumer_base);
    }
    if (producer_selected[scan]) {
      tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                         producer_first, producer_last, new_producer_first);
    }
  }

  setbc_a(&consumer, new_consumer_base);
  tolua_write_ins(buf + bc_pos + (size_t)consumer_pc * 4, (uint32_t)consumer, be);
  status = tolua_update_framesize_checked(framesize_io, new_producer_last, ctx,
                                          consumer_pc, consumer, BC_CALL);
  if (status != TOLUA_BCCONV_OK) {
    free(producer_selected);
    free(consumer_selected);
    return status;
  }
  *changed = 1;
  TOLUA_REPACK_LOG(ctx, consumer_pc,
                   "adjacent cat/call success cat_old=[%u,%u] cat_new=[%u,%u] call_old=[%u,%u] call_new=[%u,%u]",
                   (unsigned int)producer_first, (unsigned int)producer_last,
                   (unsigned int)new_producer_first, (unsigned int)new_producer_last,
                   (unsigned int)consumer_base, (unsigned int)consumer_last,
                   (unsigned int)new_consumer_base, (unsigned int)new_consumer_last);

  free(producer_selected);
  free(consumer_selected);
  return TOLUA_BCCONV_OK;
}

static int tolua_try_repack_call_result_copy(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                             uint32_t producer_pc, BCIns producer,
                                             uint32_t consumer_pc, int avoid_first, int avoid_last,
                                             uint8_t *framesize_io,
                                             const uint8_t *targets, const tolua_bcshift_map *map,
                                             const tolua_bcdebug_ctx *ctx, int *changed)
{
  BCReg old_base = bc_a(producer);
  BCReg nargs = 0;
  BCReg old_last = 0;
  BCReg new_base = 0;
  BCReg new_last = 0;
  uint8_t *selected = NULL;
  uint32_t slice_interference_pc = UINT32_MAX;
  BCOp slice_interference_op = BC__MAX;
  BCReg slice_interference_reg = 0;
  int min_window = (int)producer_pc;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;
  if (tolua_pending_insert_copy.active) return TOLUA_BCCONV_OK;
  if (bc_op(producer) != BC_CALL || bc_b(producer) != 2 || bc_c(producer) == 0) {
    return TOLUA_BCCONV_OK;
  }
  if (producer_pc + 1 >= numbc) return TOLUA_BCCONV_OK;
  {
    BCIns next = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(producer_pc + 1) * 4, be);
    if (bc_op(next) == BC_MOV &&
        bc_b(next) == 0 &&
        bc_c(next) == old_base &&
        bc_a(next) != old_base) {
      return TOLUA_BCCONV_OK;
    }
  }
  if (targets[producer_pc + 1]) {
    return TOLUA_BCCONV_OK;
  }

  nargs = (BCReg)(bc_c(producer) - 1);
  old_last = (BCReg)(old_base + nargs);
  selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!selected) {
    return tolua_failbytecodeproto(ctx, producer_pc, producer, BC_CALL, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate result-copy repack slice buffer");
  }

  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, producer_pc, old_base, old_last,
                                 targets, selected, &min_window,
                                 &slice_interference_pc, &slice_interference_op,
                                 &slice_interference_reg)) {
    free(selected);
    return TOLUA_BCCONV_OK;
  }

  {
    int found = 0;
    if (avoid_first >= 0) {
      if (tolua_find_call_result_copy_base(buf, bc_pos, numbc, be, producer_pc, producer,
                                           old_base, old_last, min_window, *framesize_io,
                                           (BCReg)avoid_first, (BCReg)avoid_last,
                                           map, &new_base)) {
        found = 1;
      }
    } else if (tolua_find_call_repack_base(buf, bc_pos, numbc, be, producer_pc, producer, BC_CALL,
                                           old_base, old_last, 0, min_window, *framesize_io, NULL,
                                           map, 1, &new_base)) {
      found = 1;
    }
    if (!found) {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
  }

  {
    unsigned int cand_last = (unsigned int)new_base + (unsigned int)nargs;
    if (cand_last > BCMAX_A) {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
    new_last = (BCReg)cand_last;
  }

  for (scan = min_window; scan < (int)producer_pc; scan++) {
    BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp cur_op = bc_op(cur);
    tolua_repack_remap_reg_range(&cur, cur_op, old_base, old_last, new_base);
    tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
    tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                       old_base, old_last, new_base);
  }

  setbc_a(&producer, new_base);
  tolua_write_ins(buf + bc_pos + (size_t)producer_pc * 4, (uint32_t)producer, be);
  status = tolua_update_framesize_checked(framesize_io, new_last, ctx,
                                          producer_pc, producer, BC_CALL);
  if (status != TOLUA_BCCONV_OK) {
    free(selected);
    return status;
  }

  {
    BCReg copy_dst[1];
    BCReg copy_src[1];

    copy_dst[0] = old_base;
    copy_src[0] = new_base;
    if (!tolua_schedule_insert_copies(ctx, consumer_pc, producer_pc + 1, copy_dst, copy_src, 1)) {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
  }

  *changed = 1;
  TOLUA_REPACK_LOG(ctx, consumer_pc,
                   "schedule result-copy producer_pc=%u insert_pc=%u old_base=%u new_base=%u",
                   (unsigned int)producer_pc, (unsigned int)(producer_pc + 1),
                   (unsigned int)old_base, (unsigned int)new_base);
  free(selected);
  return TOLUA_BCCONV_INTERNAL_INSERT_COPY;
}

static int tolua_try_repack_call(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                 uint32_t pc, uint8_t *framesize_io, const uint8_t *targets,
                                 const tolua_bcshift_map *map,
                                 const tolua_bcdebug_ctx *ctx, int *changed);

static int tolua_try_repack_hole_producer_result_copy(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                      uint32_t consumer_pc, BCReg hole_reg,
                                                      uint32_t skip_pc,
                                                      uint8_t *framesize_io, const uint8_t *targets,
                                                      const tolua_bcshift_map *map,
                                                      const tolua_bcdebug_ctx *ctx, int *changed)
{
  int pass = 0;

  *changed = 0;
  if (hole_reg > BCMAX_A) return TOLUA_BCCONV_OK;

  for (pass = 0; pass < 2; pass++) {
    int scan = pass == 0 ? (int)consumer_pc - 1 : (int)consumer_pc + 1;
    int end = pass == 0 ? -1 : (int)numbc;
    int step = pass == 0 ? -1 : 1;

    while (scan != end) {
      uint32_t producer_pc = (uint32_t)scan;
      BCIns producer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)producer_pc * 4, be);
      BCOp producer_op = bc_op(producer);
      int inner_changed = 0;
      int status = TOLUA_BCCONV_OK;

      if (producer_pc == skip_pc) {
        scan += step;
        continue;
      }

      if (bc_a(producer) != hole_reg) {
        scan += step;
        continue;
      }

      if (pass == 0 &&
          producer_op == BC_CALL &&
          !targets[producer_pc] &&
          bc_b(producer) == 4 &&
          bc_c(producer) == 2) {
        TOLUA_REPACK_LOG(ctx, consumer_pc,
                         "retry via hole producer generic-for call at pc=%u reg=%u",
                         (unsigned int)producer_pc, (unsigned int)hole_reg);
        status = tolua_try_repack_call(buf, bc_pos, numbc, be, producer_pc,
                                       framesize_io, targets, map, ctx,
                                       &inner_changed);
        if (status != TOLUA_BCCONV_OK || inner_changed) {
          *changed = inner_changed;
          return status;
        }
      }

      if (producer_op == BC_CALL &&
          !targets[producer_pc] &&
          bc_b(producer) > 2 &&
          bc_c(producer) > 1 &&
          !(bc_b(producer) == 4 && bc_c(producer) == 2)) {
        TOLUA_REPACK_LOG(ctx, consumer_pc,
                         "retry via hole producer fixed-result call at pc=%u reg=%u",
                         (unsigned int)producer_pc, (unsigned int)hole_reg);
        status = tolua_try_repack_call(buf, bc_pos, numbc, be, producer_pc,
                                       framesize_io, targets, map, ctx,
                                       &inner_changed);
        if (status != TOLUA_BCCONV_OK || inner_changed) {
          *changed = inner_changed;
          return status;
        }
      }

      if (producer_op == BC_CALL &&
          !targets[producer_pc] &&
          bc_b(producer) == 1 &&
          bc_c(producer) > 1) {
        TOLUA_REPACK_LOG(ctx, consumer_pc,
                         "retry via hole producer zero-result call at pc=%u reg=%u",
                         (unsigned int)producer_pc, (unsigned int)hole_reg);
        status = tolua_try_repack_call(buf, bc_pos, numbc, be, producer_pc,
                                       framesize_io, targets, map, ctx,
                                       &inner_changed);
        if (status != TOLUA_BCCONV_OK || inner_changed) {
          *changed = inner_changed;
          return status;
        }
      }

      if (producer_op == BC_CALL &&
          !targets[producer_pc] &&
          bc_b(producer) == 2 &&
          bc_c(producer) > 1) {
        TOLUA_REPACK_LOG(ctx, consumer_pc,
                         "retry via hole producer call at pc=%u reg=%u",
                         (unsigned int)producer_pc, (unsigned int)hole_reg);
        status = tolua_try_repack_call_result_copy(buf, bc_pos, numbc, be,
                                                   producer_pc, producer, consumer_pc, (int)hole_reg, (int)hole_reg,
                                                   framesize_io, targets, map, ctx,
                                                   &inner_changed);
        if (status != TOLUA_BCCONV_OK || inner_changed) {
          *changed = inner_changed;
          return status;
        }
      }

      if (producer_op == BC_ITERC) {
        TOLUA_REPACK_LOG(ctx, consumer_pc,
                         "retry via hole producer iterc at pc=%u reg=%u",
                         (unsigned int)producer_pc, (unsigned int)hole_reg);
        status = tolua_try_repack_iterc(buf, bc_pos, numbc, be, producer_pc,
                                        framesize_io, targets, map, ctx, &inner_changed);
        if (status != TOLUA_BCCONV_OK || inner_changed) {
          *changed = inner_changed;
          return status;
        }
      }

      scan += step;
    }
  }

  return TOLUA_BCCONV_OK;
}

static int tolua_find_numeric_for_start(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                        uint32_t loop_end_pc, BCReg base, uint32_t *out_start);
static int tolua_try_repack_iterc(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                  uint32_t pc, uint8_t *framesize_io, const uint8_t *targets,
                                  const tolua_bcshift_map *map,
                                  const tolua_bcdebug_ctx *ctx, int *changed);
static int tolua_try_repack_fori(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                 uint32_t pc, uint8_t *framesize_io, const uint8_t *targets,
                                 const tolua_bcshift_map *map,
                                 const tolua_bcdebug_ctx *ctx, int *changed);
static int tolua_try_repack_future_flow_consumer(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                 uint32_t pc, BCIns ins, BCOp op,
                                                 uint8_t *framesize_io, const uint8_t *targets,
                                                 const tolua_bcshift_map *map,
                                                 const tolua_bcdebug_ctx *ctx, int *changed);
static int tolua_try_repack_call(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                 uint32_t pc, uint8_t *framesize_io, const uint8_t *targets,
                                 const tolua_bcshift_map *map,
                                 const tolua_bcdebug_ctx *ctx, int *changed);

static int tolua_find_generic_for_call_setup(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                             uint32_t call_pc, BCIns call,
                                             uint32_t *out_iter_pc, uint32_t *out_loop_end,
                                             BCReg *out_cluster_last)
{
  BCReg call_base = bc_a(call);
  uint32_t iter_pc = 0;
  uint32_t loop_end = 0;
  uint32_t body_pc = call_pc + 2;
  BCIns isnext = 0;
  BCIns iter = 0;
  BCIns iterl = 0;
  BCOp iter_op = BC__MAX;
  uint32_t target = 0;

  if (bc_op(call) != BC_CALL || bc_b(call) != 4 || bc_c(call) != 2) return 0;
  if (call_pc + 1 >= numbc || body_pc >= numbc) return 0;

  isnext = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(call_pc + 1) * 4, be);
  if (bc_op(isnext) != BC_JMP) return 0;
  if (!tolua_get_jump_target(BC_JMP, isnext, call_pc + 1, numbc, &iter_pc)) return 0;
  if (iter_pc + 1 >= numbc) return 0;

  iter = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)iter_pc * 4, be);
  iter_op = bc_op(iter);
  if (iter_op != BC_ITERC) return 0;
  if (bc_b(iter) < 2 || bc_c(iter) != 3) return 0;
  if (bc_a(iter) != (BCReg)(call_base + 3)) return 0;

  loop_end = iter_pc + 1;
  iterl = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)loop_end * 4, be);
  switch (bc_op(iterl)) {
    case BC_ITERL:
    case BC_IITERL:
    case BC_JITERL:
      break;
    default:
      return 0;
  }
  if (bc_a(iterl) != bc_a(iter)) return 0;
  if (!tolua_get_jump_target(bc_op(iterl), iterl, loop_end, numbc, &target)) return 0;
  if (target != body_pc) return 0;

  *out_iter_pc = iter_pc;
  *out_loop_end = loop_end;
  *out_cluster_last = (BCReg)(bc_a(iter) + bc_b(iter) - 2);
  if ((BCReg)(bc_a(iter) + bc_c(iter) - 2) > *out_cluster_last) {
    *out_cluster_last = (BCReg)(bc_a(iter) + bc_c(iter) - 2);
  }
  return 1;
}

static int tolua_try_repack_generic_for_call_arg_copy(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                      uint32_t pc, BCIns call,
                                                      uint32_t iter_pc, uint32_t loop_end,
                                                      BCReg old_cluster_last,
                                                      uint8_t *framesize_io, const uint8_t *targets,
                                                      const tolua_bcshift_map *map,
                                                      const tolua_bcdebug_ctx *ctx, int *changed)
{
  BCReg old_first = bc_a(call);
  BCReg old_arg = (BCReg)(old_first + 1);
  BCReg new_base = 0;
  BCReg new_last = 0;
  uint8_t *selected = NULL;
  uint32_t slice_interference_pc = UINT32_MAX;
  BCOp slice_interference_op = BC__MAX;
  BCReg slice_interference_reg = 0;
  uint32_t readonly_interference_pc = UINT32_MAX;
  BCOp readonly_interference_op = BC__MAX;
  BCReg readonly_interference_reg = 0;
  unsigned int cand_base = 0;
  int min_window = (int)pc;
  int hole_reg = -1;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;
  if (tolua_pending_insert_copy.active) return TOLUA_BCCONV_OK;
  if (targets[pc]) return TOLUA_BCCONV_OK;
  if (bc_op(call) != BC_CALL || bc_b(call) != 4 || bc_c(call) != 2) return TOLUA_BCCONV_OK;

  selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!selected) {
    return tolua_failbytecodeproto(ctx, pc, call, BC_CALL, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate generic-for arg-copy slice buffer");
  }

  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc, old_first, old_first,
                                 targets, selected, &min_window,
                                 &slice_interference_pc, &slice_interference_op,
                                 &slice_interference_reg)) {
    if (slice_interference_pc != UINT32_MAX &&
        slice_interference_pc < pc &&
        tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, pc,
                                                            old_first, old_first, targets,
                                                            selected, &min_window,
                                                            &readonly_interference_pc,
                                                            &readonly_interference_op,
                                                            &readonly_interference_reg)) {
      TOLUA_REPACK_LOG(ctx, pc,
                       "generic-for arg-copy allowed read-only interference at pc=%u op=%s reg=%u",
                       (unsigned int)slice_interference_pc,
                       tolua_bc_opname(slice_interference_op),
                       (unsigned int)slice_interference_reg);
    } else {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
  }

  for (cand_base = (unsigned int)*framesize_io; ; cand_base++) {
    unsigned int cand_last = cand_base + (unsigned int)(old_cluster_last - old_first);
    if (cand_last > BCMAX_A) {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
    new_base = (BCReg)cand_base;
    new_last = (BCReg)cand_last;
    if (tolua_find_closed_range_hole(map, (int)new_base, (int)new_last, &hole_reg)) continue;
    break;
  }

  for (scan = min_window; scan < (int)pc; scan++) {
    BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp cur_op = bc_op(cur);
    tolua_repack_remap_reg_range(&cur, cur_op, old_first, old_first, new_base);
    tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
    tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                       old_first, old_first, new_base);
  }

  for (scan = (int)pc; scan <= (int)loop_end; scan++) {
    BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp cur_op = bc_op(cur);
    tolua_repack_remap_reg_range(&cur, cur_op, old_first, old_cluster_last, new_base);
    tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
    tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                       old_first, old_cluster_last, new_base);
  }

  status = tolua_update_framesize_checked(framesize_io, new_last, ctx,
                                          pc, call, BC_CALL);
  if (status != TOLUA_BCCONV_OK) {
    free(selected);
    return status;
  }
  {
    BCReg copy_dst[1];
    BCReg copy_src[1];

    copy_dst[0] = (BCReg)(new_base + 1);
    copy_src[0] = old_arg;
    if (!tolua_schedule_insert_copies(ctx, pc, pc, copy_dst, copy_src, 1)) {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
  }

  *changed = 1;
  TOLUA_REPACK_LOG(ctx, pc,
                   "generic-for arg-copy success old=[%u,%u] new=[%u,%u] iter_pc=%u loop_end=%u insert_pc=%u copy=%u<- %u",
                   (unsigned int)old_first, (unsigned int)old_cluster_last,
                   (unsigned int)new_base, (unsigned int)new_last,
                   (unsigned int)iter_pc, (unsigned int)loop_end,
                   (unsigned int)pc,
                   (unsigned int)(new_base + 1), (unsigned int)old_arg);

  free(selected);
  return TOLUA_BCCONV_INTERNAL_INSERT_COPY;
}

static int tolua_try_repack_generic_for_call_setup(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                   uint32_t pc, BCIns call,
                                                   uint8_t *framesize_io, const uint8_t *targets,
                                                   const tolua_bcshift_map *map,
                                                   const tolua_bcdebug_ctx *ctx, int *changed)
{
  BCReg old_first = bc_a(call);
  BCReg old_args_last = (BCReg)(old_first + bc_c(call) - 1);
  BCReg old_cluster_last = 0;
  BCReg new_base = 0;
  BCReg new_last = 0;
  uint32_t iter_pc = 0;
  uint32_t loop_end = 0;
  uint8_t *selected = NULL;
  uint32_t slice_interference_pc = UINT32_MAX;
  BCOp slice_interference_op = BC__MAX;
  BCReg slice_interference_reg = 0;
  uint32_t readonly_interference_pc = UINT32_MAX;
  BCOp readonly_interference_op = BC__MAX;
  BCReg readonly_interference_reg = 0;
  unsigned int cand_base = 0;
  int min_window = (int)pc;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;
  if (!tolua_find_generic_for_call_setup(buf, bc_pos, numbc, be, pc, call,
                                         &iter_pc, &loop_end, &old_cluster_last)) {
    return TOLUA_BCCONV_OK;
  }

  selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!selected) {
    return tolua_failbytecodeproto(ctx, pc, call, BC_CALL, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate generic-for repack slice buffer");
  }

  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc, old_first, old_args_last,
                                 targets, selected, &min_window,
                                 &slice_interference_pc, &slice_interference_op,
                                 &slice_interference_reg)) {
    if (slice_interference_pc != UINT32_MAX &&
        slice_interference_pc < pc &&
        tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, pc,
                                                            old_first, old_args_last, targets,
                                                            selected, &min_window,
                                                            &readonly_interference_pc,
                                                            &readonly_interference_op,
                                                            &readonly_interference_reg)) {
      TOLUA_REPACK_LOG(ctx, pc,
                       "generic-for slice allowed read-only interference at pc=%u op=%s reg=%u",
                       (unsigned int)slice_interference_pc,
                       tolua_bc_opname(slice_interference_op),
                       (unsigned int)slice_interference_reg);
    } else {
      int inner_changed = 0;

      status = tolua_try_repack_generic_for_call_arg_copy(buf, bc_pos, numbc, be, pc, call,
                                                          iter_pc, loop_end, old_cluster_last,
                                                          framesize_io, targets, map, ctx,
                                                          &inner_changed);
      if (status != TOLUA_BCCONV_OK || inner_changed) {
        *changed = inner_changed;
        free(selected);
        return status;
      }

      if (slice_interference_pc != UINT32_MAX &&
          slice_interference_pc < pc &&
          slice_interference_op == BC_CALL) {
        BCIns interfering = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)slice_interference_pc * 4, be);

        if (bc_b(interfering) == 2 && bc_c(interfering) > 1 && !targets[slice_interference_pc]) {
          inner_changed = 0;
          TOLUA_REPACK_LOG(ctx, pc,
                           "generic-for retry via interfering single-result call at pc=%u reg=%u",
                           (unsigned int)slice_interference_pc,
                           (unsigned int)slice_interference_reg);
          status = tolua_try_repack_call(buf, bc_pos, numbc, be, slice_interference_pc,
                                         framesize_io, targets, map, ctx, &inner_changed);
          if (status != TOLUA_BCCONV_OK || inner_changed) {
            *changed = inner_changed;
            free(selected);
            return status;
          }
        }
      }
    }
    free(selected);
    return TOLUA_BCCONV_OK;
  }

  for (cand_base = (unsigned int)*framesize_io; ; cand_base++) {
    int hole_reg = -1;
    unsigned int cand_last = cand_base + (unsigned int)(old_cluster_last - old_first);

    if (cand_last > BCMAX_A) {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
    if (tolua_find_closed_range_hole(map, (int)cand_base, (int)cand_last, &hole_reg)) {
      continue;
    }
    new_base = (BCReg)cand_base;
    new_last = (BCReg)cand_last;
    break;
  }

  for (scan = min_window; scan < (int)pc; scan++) {
    BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp cur_op = bc_op(cur);
    tolua_repack_remap_reg_range(&cur, cur_op, old_first, old_args_last, new_base);
    tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
    tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                       old_first, old_args_last, new_base);
  }

  for (scan = (int)pc; scan <= (int)loop_end; scan++) {
    BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp cur_op = bc_op(cur);
    tolua_repack_remap_reg_range(&cur, cur_op, old_first, old_cluster_last, new_base);
    tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
    tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                       old_first, old_cluster_last, new_base);
  }

  status = tolua_update_framesize_checked(framesize_io, new_last, ctx,
                                          pc, call, BC_CALL);
  if (status != TOLUA_BCCONV_OK) {
    free(selected);
    return status;
  }
  *changed = 1;
  TOLUA_REPACK_LOG(ctx, pc, "generic-for success old=[%u,%u] new=[%u,%u] iter_pc=%u loop_end=%u",
                   (unsigned int)old_first, (unsigned int)old_cluster_last,
                   (unsigned int)new_base, (unsigned int)new_last,
                   (unsigned int)iter_pc, (unsigned int)loop_end);

  free(selected);
  return TOLUA_BCCONV_OK;
}

static int tolua_try_repack_call_arg_gap_copy(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                              uint32_t pc, BCIns call,
                                              uint8_t *framesize_io, const uint8_t *targets,
                                              const tolua_bcshift_map *map,
                                              const tolua_bcdebug_ctx *ctx, int *changed)
{
  BCReg old_base = bc_a(call);
  BCReg old_last = 0;
  BCReg new_base = 0;
  BCReg new_last = 0;
  BCReg copy_dst[TOLUA_MAX_INSERT_COPIES];
  BCReg copy_src[TOLUA_MAX_INSERT_COPIES];
  unsigned int cand_base = 0;
  uint8_t copy_count = 0;
  int hole_reg = -1;
  uint8_t i = 0;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;
  if (tolua_pending_insert_copy.active) return TOLUA_BCCONV_OK;
  if (targets[pc]) return TOLUA_BCCONV_OK;
  if (bc_op(call) != BC_CALL || bc_b(call) != 1 || bc_c(call) <= 1) return TOLUA_BCCONV_OK;

  copy_count = bc_c(call);
  if (copy_count == 0 || copy_count > TOLUA_MAX_INSERT_COPIES) return TOLUA_BCCONV_OK;
  old_last = (BCReg)(old_base + copy_count - 1);

  for (cand_base = (unsigned int)*framesize_io; ; cand_base++) {
    unsigned int cand_last = cand_base + (unsigned int)copy_count - 1u;
    if (cand_last > BCMAX_A) return TOLUA_BCCONV_OK;
    if (tolua_find_closed_range_hole(map, (int)cand_base, (int)cand_last, &hole_reg)) continue;
    new_base = (BCReg)cand_base;
    new_last = (BCReg)cand_last;
    break;
  }

  for (i = 0; i < copy_count; i++) {
    copy_dst[i] = (BCReg)(new_base + i);
    copy_src[i] = (BCReg)(old_base + i);
  }
  if (!tolua_schedule_insert_copies(ctx, pc, pc, copy_dst, copy_src, copy_count)) {
    return TOLUA_BCCONV_OK;
  }

  setbc_a(&call, new_base);
  tolua_write_ins(buf + bc_pos + (size_t)pc * 4, (uint32_t)call, be);
  status = tolua_update_framesize_checked(framesize_io, new_last, ctx,
                                          pc, call, BC_CALL);
  if (status != TOLUA_BCCONV_OK) {
    return status;
  }
  *changed = 1;
  TOLUA_REPACK_LOG(ctx, pc,
                   "call gap-copy success old=[%u,%u] new=[%u,%u] insert_pc=%u copies=%u",
                   (unsigned int)old_base, (unsigned int)old_last,
                   (unsigned int)new_base, (unsigned int)new_last,
                   (unsigned int)pc, (unsigned int)copy_count);
  return TOLUA_BCCONV_INTERNAL_INSERT_COPY;
}

static int tolua_try_repack_call_fixed_result_copy(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                   uint32_t pc, BCIns call,
                                                   uint8_t *framesize_io, const uint8_t *targets,
                                                   const tolua_bcshift_map *map,
                                                   const tolua_bcdebug_ctx *ctx, int *changed)
{
  BCReg old_base = bc_a(call);
  BCReg nargs = bc_c(call) > 0 ? (BCReg)(bc_c(call) - 1) : 0;
  BCReg nres = bc_b(call) > 0 ? (BCReg)(bc_b(call) - 1) : 0;
  BCReg old_args_last = 0;
  BCReg old_cluster_last = 0;
  BCReg new_base = 0;
  BCReg new_args_last = 0;
  BCReg new_cluster_last = 0;
  uint8_t *selected = NULL;
  BCReg copy_dst[TOLUA_MAX_INSERT_COPIES];
  BCReg copy_src[TOLUA_MAX_INSERT_COPIES];
  unsigned int cand_base = 0;
  int min_window = (int)pc;
  int hole_reg = -1;
  int scan = 0;
  int i = 0;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;
  if (tolua_pending_insert_copy.active) return TOLUA_BCCONV_OK;
  if (bc_op(call) != BC_CALL || bc_b(call) <= 2 || bc_c(call) <= 1) return TOLUA_BCCONV_OK;
  if (pc + 1 < numbc && targets[pc + 1]) return TOLUA_BCCONV_OK;

  old_args_last = (BCReg)(old_base + nargs);
  old_cluster_last = old_args_last;
  if (nres > 0 && (BCReg)(old_base + nres - 1) > old_cluster_last) {
    old_cluster_last = (BCReg)(old_base + nres - 1);
  }
  if (nres == 0 || nres > TOLUA_MAX_INSERT_COPIES) return TOLUA_BCCONV_OK;

  selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!selected) {
    return tolua_failbytecodeproto(ctx, pc, call, BC_CALL, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate fixed-result call repack slice buffer");
  }
  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc, old_base, old_args_last,
                                 targets, selected, &min_window,
                                 NULL, NULL, NULL)) {
    free(selected);
    return TOLUA_BCCONV_OK;
  }
  if (tolua_window_moves_call_hole(buf, bc_pos, numbc, be, pc, old_base, old_args_last,
                                   min_window, NULL, NULL, NULL)) {
    free(selected);
    return TOLUA_BCCONV_OK;
  }

  for (cand_base = (unsigned int)*framesize_io; ; cand_base++) {
    unsigned int cand_args_last = cand_base + (unsigned int)nargs;
    unsigned int cand_cluster_last = cand_base + (unsigned int)(old_cluster_last - old_base);
    int live_conflict = 0;
    BCReg live_reg = 0;

    if (cand_cluster_last > BCMAX_A) {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
    if (tolua_ranges_overlap((BCReg)cand_base, (BCReg)cand_cluster_last, old_base, old_cluster_last)) {
      continue;
    }
    if (tolua_find_range_hole(map, (int)cand_base + 1, (int)cand_base + bc_c(call) - 1, &hole_reg)) {
      continue;
    }
    if (nres > 1 &&
        tolua_find_closed_range_hole(map, (int)cand_base + 1, (int)(cand_base + nres - 1), &hole_reg)) {
      continue;
    }
    if (!map->hole[cand_base] &&
        tolua_hole_breaks_prior_insns(buf, bc_pos, numbc, be, pc, (BCReg)cand_base, pc)) {
      continue;
    }
    if (tolua_range_breaks_future_calls(buf, bc_pos, numbc, be, pc + 1,
                                        (BCReg)(cand_base + 1), (BCReg)cand_cluster_last, pc)) {
      continue;
    }
    if (cand_base < (unsigned int)*framesize_io &&
        tolua_window_touches_range(buf, bc_pos, be, min_window, pc,
                                   (BCReg)cand_base, (BCReg)cand_cluster_last)) {
      continue;
    }
    if (cand_base < (unsigned int)*framesize_io) {
      for (live_reg = (BCReg)cand_base; live_reg <= (BCReg)cand_cluster_last; live_reg++) {
        if (tolua_reg_live_after_pc(buf, bc_pos, numbc, be, pc + 1, live_reg)) {
          live_conflict = 1;
          break;
        }
      }
      if (live_conflict) continue;
    }

    new_base = (BCReg)cand_base;
    new_args_last = (BCReg)cand_args_last;
    new_cluster_last = (BCReg)cand_cluster_last;
    break;
  }

  for (scan = min_window; scan < (int)pc; scan++) {
    if (!selected[scan]) continue;
    {
      BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
      BCOp cur_op = bc_op(cur);
      tolua_repack_remap_reg_range(&cur, cur_op, old_base, old_args_last, new_base);
      tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
      tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                         old_base, old_args_last, new_base);
    }
  }

  for (i = 0; i < (int)nres; i++) {
    copy_dst[i] = (BCReg)(old_base + i);
    copy_src[i] = (BCReg)(new_base + i);
  }
  if (!tolua_schedule_insert_copies(ctx, pc, pc + 1, copy_dst, copy_src, (uint8_t)nres)) {
    free(selected);
    return TOLUA_BCCONV_OK;
  }

  setbc_a(&call, new_base);
  tolua_write_ins(buf + bc_pos + (size_t)pc * 4, (uint32_t)call, be);
  status = tolua_update_framesize_checked(framesize_io, new_cluster_last, ctx,
                                          pc, call, BC_CALL);
  if (status != TOLUA_BCCONV_OK) {
    free(selected);
    return status;
  }
  *changed = 1;
  TOLUA_REPACK_LOG(ctx, pc,
                   "fixed-result call copy success old=[%u,%u] new=[%u,%u] insert_pc=%u result_copies=%u",
                   (unsigned int)old_base, (unsigned int)old_cluster_last,
                   (unsigned int)new_base, (unsigned int)new_cluster_last,
                   (unsigned int)(pc + 1), (unsigned int)nres);
  free(selected);
  return TOLUA_BCCONV_INTERNAL_INSERT_COPY;
}

static int tolua_find_generic_for_call_from_iterc(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                  uint32_t iter_pc, BCIns iter, uint32_t *out_call_pc)
{
  uint32_t body_pc = 0;
  uint32_t call_pc = 0;
  uint32_t matched_iter_pc = 0;
  uint32_t loop_end = 0;
  BCReg cluster_last = 0;
  BCIns iterl = 0;
  BCOp iterl_op = BC__MAX;
  BCIns call = 0;

  if (bc_op(iter) != BC_ITERC || bc_b(iter) < 2 || bc_c(iter) != 3) return 0;
  if (iter_pc + 1 >= numbc) return 0;

  iterl = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(iter_pc + 1) * 4, be);
  iterl_op = bc_op(iterl);
  switch (iterl_op) {
    case BC_ITERL:
    case BC_IITERL:
    case BC_JITERL:
      break;
    default:
      return 0;
  }

  if (!tolua_get_jump_target(iterl_op, iterl, iter_pc + 1, numbc, &body_pc)) return 0;
  if (body_pc < 2) return 0;

  call_pc = body_pc - 2;
  call = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)call_pc * 4, be);
  if (!tolua_find_generic_for_call_setup(buf, bc_pos, numbc, be, call_pc, call,
                                         &matched_iter_pc, &loop_end, &cluster_last)) {
    return 0;
  }
  if (matched_iter_pc != iter_pc) return 0;

  *out_call_pc = call_pc;
  return 1;
}

static int tolua_try_repack_future_flow_consumer(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                                 uint32_t pc, BCIns ins, BCOp op,
                                                 uint8_t *framesize_io, const uint8_t *targets,
                                                 const tolua_bcshift_map *map,
                                                 const tolua_bcdebug_ctx *ctx, int *changed)
{
  uint32_t owner_pc = 0;

  *changed = 0;

  switch (op) {
    case BC_ITERC:
      TOLUA_REPACK_LOG(ctx, pc, "retry via future iterc at pc=%u", (unsigned int)pc);
      return tolua_try_repack_iterc(buf, bc_pos, numbc, be, pc, framesize_io, targets, map, ctx, changed);
    case BC_ITERL:
    case BC_IITERL:
    case BC_JITERL:
      if (pc == 0) return TOLUA_BCCONV_OK;
      owner_pc = pc - 1;
      ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)owner_pc * 4, be);
      op = bc_op(ins);
      if (op != BC_ITERC) return TOLUA_BCCONV_OK;
      TOLUA_REPACK_LOG(ctx, pc, "retry via future iter loop at pc=%u owner=%u",
                       (unsigned int)pc, (unsigned int)owner_pc);
      return tolua_try_repack_iterc(buf, bc_pos, numbc, be, owner_pc,
                                    framesize_io, targets, map, ctx, changed);
    case BC_FORI:
    case BC_JFORI:
      TOLUA_REPACK_LOG(ctx, pc, "retry via future numeric-for at pc=%u", (unsigned int)pc);
      return tolua_try_repack_fori(buf, bc_pos, numbc, be, pc, framesize_io, targets, map, ctx, changed);
    case BC_FORL:
    case BC_IFORL:
    case BC_JFORL:
      if (!tolua_find_numeric_for_start(buf, bc_pos, numbc, be, pc, bc_a(ins), &owner_pc)) {
        return TOLUA_BCCONV_OK;
      }
      TOLUA_REPACK_LOG(ctx, pc, "retry via future numeric-for loop at pc=%u owner=%u",
                       (unsigned int)pc, (unsigned int)owner_pc);
      return tolua_try_repack_fori(buf, bc_pos, numbc, be, owner_pc,
                                   framesize_io, targets, map, ctx, changed);
    default:
      return TOLUA_BCCONV_OK;
  }
}

static int tolua_try_repack_call(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                 uint32_t pc, uint8_t *framesize_io, const uint8_t *targets,
                                 const tolua_bcshift_map *map,
                                 const tolua_bcdebug_ctx *ctx, int *changed)
{
  BCIns call = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  BCOp op = bc_op(call);
  BCReg old_base = bc_a(call);
  BCReg nargs = 0;
  BCReg nres = 0;
  BCReg old_last = 0;
  BCReg new_base = 0;
  BCReg new_last = 0;
  uint8_t *selected = NULL;
  uint8_t *state_mask = NULL;
  uint8_t *rewrite = NULL;
  uint32_t *queue_pc = NULL;
  uint8_t *queue_state = NULL;
  size_t queue_cap = (size_t)numbc * 2;
  size_t head = 0, tail = 0;
  uint32_t slice_interference_pc = UINT32_MAX;
  BCOp slice_interference_op = BC__MAX;
  BCReg slice_interference_reg = 0;
  uint32_t readonly_interference_pc = UINT32_MAX;
  BCOp readonly_interference_op = BC__MAX;
  BCReg readonly_interference_reg = 0;
  uint32_t selected_hole_pc = UINT32_MAX;
  BCOp selected_hole_op = BC__MAX;
  BCReg selected_hole_reg = 0;
  BCReg candidate_min_base = 0;
  int fixed_multi_result = 0;
  int min_window = (int)pc;
  int consumer_hole_reg = -1;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;

  if (op == BC_CALL) {
    nargs = bc_c(call) > 0 ? (BCReg)(bc_c(call) - 1) : 0;
    nres = bc_b(call) > 0 ? (BCReg)(bc_b(call) - 1) : 0;
    if (bc_b(call) == 0) return TOLUA_BCCONV_OK;
    if (nres > 1) {
      status = tolua_try_repack_generic_for_call_setup(buf, bc_pos, numbc, be, pc, call,
                                                       framesize_io, targets, map, ctx, changed);
      if (status != TOLUA_BCCONV_OK || *changed) return status;
      fixed_multi_result = 1;
    }
  } else if (op == BC_CALLT) {
    nargs = bc_d(call) > 0 ? (BCReg)(bc_d(call) - 1) : 0;
    nres = 0;
  } else {
    return TOLUA_BCCONV_OK;
  }

  if (nargs == 0) return TOLUA_BCCONV_OK;

  old_last = (BCReg)(old_base + nargs);
  if (tolua_call_repack_state.active && pc < tolua_call_repack_state.numbc) {
    if (!tolua_call_repack_state.seen[pc]) {
      tolua_call_repack_state.seen[pc] = 1;
      tolua_call_repack_state.original_base[pc] = old_base;
    } else if (old_base != tolua_call_repack_state.original_base[pc]) {
      candidate_min_base = old_base;
    }
  }

  if (fixed_multi_result) {
    int hole_reg = -1;
    int inner_changed = 0;

    status = tolua_try_repack_call_fixed_result_copy(buf, bc_pos, numbc, be, pc, call,
                                                     framesize_io, targets, map, ctx,
                                                     &inner_changed);
    if (status != TOLUA_BCCONV_OK || inner_changed) {
      *changed = inner_changed;
      return status;
    }

    if (bc_b(call) > 2 &&
        tolua_find_closed_range_hole(map, (int)old_base + 1, (int)(old_base + bc_b(call) - 2),
                                     &hole_reg)) {
      status = tolua_try_repack_hole_producer_result_copy(buf, bc_pos, numbc, be,
                                                          pc, (BCReg)hole_reg, UINT32_MAX,
                                                          framesize_io, targets, map,
                                                          ctx, &inner_changed);
      if (status != TOLUA_BCCONV_OK || inner_changed) {
        *changed = inner_changed;
        return status;
      }
    }
    hole_reg = -1;
    if (tolua_find_range_hole(map, (int)old_base + 1, (int)old_base + bc_c(call) - 1,
                              &hole_reg)) {
      status = tolua_try_repack_hole_producer_result_copy(buf, bc_pos, numbc, be,
                                                          pc, (BCReg)hole_reg, UINT32_MAX,
                                                          framesize_io, targets, map,
                                                          ctx, &inner_changed);
      if (status != TOLUA_BCCONV_OK || inner_changed) {
        *changed = inner_changed;
        return status;
      }
    }
    return TOLUA_BCCONV_OK;
  }

  if (op == BC_CALL && pc > 0) {
    BCIns producer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
    status = tolua_try_repack_adjacent_cat_call_chain(buf, bc_pos, numbc, be, pc - 1, producer, pc, call,
                                                      framesize_io, targets, map, ctx, changed);
    if (status != TOLUA_BCCONV_OK || *changed) return status;
  }

  if (op == BC_CALL && bc_b(call) == 2 && pc + 1 < numbc) {
    BCIns consumer = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc + 1) * 4, be);
    BCOp consumer_op = bc_op(consumer);

    if (consumer_op == BC_CAT &&
        bc_b(consumer) <= bc_c(consumer) &&
        bc_c(consumer) == old_base) {
      status = tolua_try_repack_call_result_copy(buf, bc_pos, numbc, be,
                                                 pc, call, pc + 1, -1, -1,
                                                 framesize_io, targets, map, ctx,
                                                 changed);
      if (status != TOLUA_BCCONV_OK || *changed) return status;
    }

    if (consumer_op == BC_CALL &&
        bc_b(consumer) != 0 &&
        bc_b(consumer) <= 2 &&
        bc_c(consumer) > 1 &&
        (BCReg)(bc_a(consumer) + bc_c(consumer) - 1) == old_base) {
      TOLUA_REPACK_LOG(ctx, pc,
                       "consider adjacent call-chain consumer_pc=%u consumer_base=%u consumer_last=%u target=%u",
                       (unsigned int)(pc + 1),
                       (unsigned int)bc_a(consumer),
                       (unsigned int)(bc_a(consumer) + bc_c(consumer) - 1),
                       (unsigned int)targets[pc + 1]);
      if (!targets[pc + 1]) {
        status = tolua_try_repack_adjacent_call_chain(buf, bc_pos, numbc, be, pc, call,
                                                      pc + 1, consumer, framesize_io, targets,
                                                      map, ctx, changed);
        if (status != TOLUA_BCCONV_OK || *changed) return status;
      }
    }
  }

  selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!selected) {
    return tolua_failbytecodeproto(ctx, pc, call, op, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate call repack slice buffer");
  }

  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc, old_base, old_last,
                                 targets, selected, &min_window,
                                 &slice_interference_pc, &slice_interference_op,
                                 &slice_interference_reg)) {
    int readonly_retry_ok = 0;
    int inner_changed = 0;

    if (((op == BC_CALL &&
          tolua_find_range_hole(map, (int)old_base + 1, (int)old_base + bc_c(call) - 1,
                                &consumer_hole_reg)) ||
         (op == BC_CALLT &&
          tolua_find_range_hole(map, (int)old_base + 1, (int)old_base + bc_d(call) - 1,
                                &consumer_hole_reg)))) {
      uint32_t skip_hole_pc = UINT32_MAX;

      if (slice_interference_pc != UINT32_MAX &&
          slice_interference_pc < pc &&
          slice_interference_op == BC_CALL &&
          slice_interference_reg == (BCReg)consumer_hole_reg) {
        skip_hole_pc = slice_interference_pc;
      }
      status = tolua_try_repack_hole_producer_result_copy(buf, bc_pos, numbc, be,
                                                          pc, (BCReg)consumer_hole_reg,
                                                          skip_hole_pc,
                                                          framesize_io, targets, map,
                                                          ctx, &inner_changed);
      if (status != TOLUA_BCCONV_OK || inner_changed) {
        *changed = inner_changed;
        goto cleanup;
      }
    }

    if (slice_interference_pc != UINT32_MAX &&
        slice_interference_pc < pc &&
        slice_interference_op == BC_CALL) {
      BCIns interfering = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)slice_interference_pc * 4, be);
      if (bc_b(interfering) == 2 && bc_c(interfering) != 0 && !targets[slice_interference_pc]) {
        inner_changed = 0;
        if (slice_interference_pc + 1 == pc &&
            bc_c(interfering) == 1) {
          status = tolua_try_repack_call_result_copy(buf, bc_pos, numbc, be,
                                                     slice_interference_pc, interfering, pc,
                                                     old_base, old_last,
                                                     framesize_io, targets, map, ctx,
                                                     &inner_changed);
          if (status != TOLUA_BCCONV_OK || inner_changed) {
            *changed = inner_changed;
            goto cleanup;
          }
        }
        if (bc_c(interfering) > 1) {
          if (op == BC_CALL &&
              bc_b(call) == 2 &&
              consumer_hole_reg >= 0 &&
              bc_a(interfering) == (BCReg)consumer_hole_reg) {
            status = tolua_try_repack_call_result_copy(buf, bc_pos, numbc, be,
                                                       slice_interference_pc, interfering, pc,
                                                       old_base, old_last,
                                                       framesize_io, targets, map, ctx,
                                                       &inner_changed);
            if (status != TOLUA_BCCONV_OK || inner_changed) {
              *changed = inner_changed;
              goto cleanup;
            }
          }
          status = tolua_try_repack_first_arg_call_chain(buf, bc_pos, numbc, be,
                                                         slice_interference_pc, interfering,
                                                         pc, call, framesize_io, targets,
                                                         map, ctx, &inner_changed);
          if (status != TOLUA_BCCONV_OK || inner_changed) {
            *changed = inner_changed;
            goto cleanup;
          }
          if (op == BC_CALL &&
              bc_b(call) == 1 &&
              bc_a(interfering) >= (BCReg)(old_base + 1) &&
              bc_a(interfering) <= old_last) {
            status = tolua_try_repack_call_result_copy(buf, bc_pos, numbc, be,
                                                       slice_interference_pc, interfering, pc,
                                                       old_base, old_last,
                                                       framesize_io, targets, map, ctx,
                                                       &inner_changed);
            if (status != TOLUA_BCCONV_OK || inner_changed) {
              *changed = inner_changed;
              goto cleanup;
            }
          }
          TOLUA_REPACK_LOG(ctx, pc,
                           "retry via interfering single-result call at pc=%u reg=%u",
                           (unsigned int)slice_interference_pc,
                           (unsigned int)slice_interference_reg);
          status = tolua_try_repack_call(buf, bc_pos, numbc, be, slice_interference_pc,
                                         framesize_io, targets, map, ctx, &inner_changed);
          if (status != TOLUA_BCCONV_OK || inner_changed) {
            *changed = inner_changed;
            goto cleanup;
          }
        }
      }
    } else if (slice_interference_pc != UINT32_MAX &&
               slice_interference_pc < pc &&
               tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, pc,
                                                                   old_base, old_last, targets,
                                                                   selected, &min_window,
                                                                   &readonly_interference_pc,
                                                                   &readonly_interference_op,
                                                                   &readonly_interference_reg)) {
      readonly_retry_ok = 1;
      TOLUA_REPACK_LOG(ctx, pc,
                       "slice allowed read-only interference at pc=%u op=%s reg=%u",
                       (unsigned int)slice_interference_pc,
                       tolua_bc_opname(slice_interference_op),
                       (unsigned int)slice_interference_reg);
    } else if (readonly_interference_pc != UINT32_MAX &&
               readonly_interference_pc < pc &&
      readonly_interference_op == BC_CALL) {
      BCIns interfering = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)readonly_interference_pc * 4, be);
      if (bc_b(interfering) == 2 && bc_c(interfering) > 1 && !targets[readonly_interference_pc]) {
        inner_changed = 0;
        if (readonly_interference_pc + 1 == pc &&
            bc_c(interfering) == 1) {
          status = tolua_try_repack_call_result_copy(buf, bc_pos, numbc, be,
                                                     readonly_interference_pc, interfering, pc,
                                                     old_base, old_last,
                                                     framesize_io, targets, map, ctx,
                                                     &inner_changed);
          if (status != TOLUA_BCCONV_OK || inner_changed) {
            *changed = inner_changed;
            goto cleanup;
          }
        }
        if (bc_c(interfering) > 1) {
          if (op == BC_CALL &&
              bc_b(call) == 2 &&
              consumer_hole_reg >= 0 &&
              bc_a(interfering) == (BCReg)consumer_hole_reg) {
            status = tolua_try_repack_call_result_copy(buf, bc_pos, numbc, be,
                                                       readonly_interference_pc, interfering, pc,
                                                       old_base, old_last,
                                                       framesize_io, targets, map, ctx,
                                                       &inner_changed);
            if (status != TOLUA_BCCONV_OK || inner_changed) {
              *changed = inner_changed;
              goto cleanup;
            }
          }
          status = tolua_try_repack_first_arg_call_chain(buf, bc_pos, numbc, be,
                                                         readonly_interference_pc, interfering,
                                                         pc, call, framesize_io, targets,
                                                         map, ctx, &inner_changed);
          if (status != TOLUA_BCCONV_OK || inner_changed) {
            *changed = inner_changed;
            goto cleanup;
          }
          if (op == BC_CALL &&
              bc_b(call) == 1 &&
              bc_a(interfering) >= (BCReg)(old_base + 1) &&
              bc_a(interfering) <= old_last) {
            status = tolua_try_repack_call_result_copy(buf, bc_pos, numbc, be,
                                                       readonly_interference_pc, interfering, pc,
                                                       old_base, old_last,
                                                       framesize_io, targets, map, ctx,
                                                       &inner_changed);
            if (status != TOLUA_BCCONV_OK || inner_changed) {
              *changed = inner_changed;
              goto cleanup;
            }
          }
          TOLUA_REPACK_LOG(ctx, pc,
                           "readonly retry via interfering single-result call at pc=%u reg=%u",
                           (unsigned int)readonly_interference_pc,
                           (unsigned int)readonly_interference_reg);
          status = tolua_try_repack_call(buf, bc_pos, numbc, be, readonly_interference_pc,
                                         framesize_io, targets, map, ctx, &inner_changed);
          if (status != TOLUA_BCCONV_OK || inner_changed) {
            *changed = inner_changed;
            goto cleanup;
          }
        }
      }
    }
    if (!readonly_retry_ok) {
      if (op == BC_CALL && bc_b(call) == 1) {
        int inner_changed = 0;
        status = tolua_try_repack_call_arg_gap_copy(buf, bc_pos, numbc, be, pc, call,
                                                    framesize_io, targets, map, ctx,
                                                    &inner_changed);
        if (status != TOLUA_BCCONV_OK || inner_changed) {
          *changed = inner_changed;
          goto cleanup;
        }
      }
      TOLUA_REPACK_LOG(ctx, pc, "generic slice reject a=%u last=%u",
                       (unsigned int)old_base, (unsigned int)old_last);
      goto cleanup;
    }
  }

  if (tolua_window_moves_call_hole(buf, bc_pos, numbc, be, pc, old_base, old_last,
                                   min_window,
                                   &selected_hole_pc, &selected_hole_op,
                                   &selected_hole_reg)) {
    if (op == BC_CALL && bc_b(call) == 1) {
      int inner_changed = 0;

      status = tolua_try_repack_call_arg_gap_copy(buf, bc_pos, numbc, be, pc, call,
                                                  framesize_io, targets, map, ctx,
                                                  &inner_changed);
      if (status != TOLUA_BCCONV_OK || inner_changed) {
        *changed = inner_changed;
        goto cleanup;
      }
    }
    TOLUA_REPACK_LOG(ctx, pc,
                     "generic slice reject window-hole-at=%u op=%s reg=%u range=[%u,%u]",
                     (unsigned int)selected_hole_pc,
                     tolua_bc_opname(selected_hole_op),
                     (unsigned int)selected_hole_reg,
                     (unsigned int)old_base,
                     (unsigned int)old_last);
    goto cleanup;
  }
  if (nres == 1) {
    state_mask = (uint8_t *)calloc((size_t)numbc, 1);
    rewrite = (uint8_t *)calloc((size_t)numbc, 1);
    queue_pc = (uint32_t *)malloc(sizeof(uint32_t) * queue_cap);
    queue_state = (uint8_t *)malloc(queue_cap);
    if (!state_mask || !rewrite || !queue_pc || !queue_state) {
      status = tolua_failbytecodeproto(ctx, pc, call, op, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                       "failed to allocate call repack flow buffers");
      goto cleanup;
    }

    if (pc + 1 < numbc) {
      queue_pc[tail] = pc + 1;
      queue_state[tail++] = 0;
    }

    while (head < tail) {
      uint32_t cur_pc = queue_pc[head];
      uint8_t cur_state = queue_state[head++];
      uint8_t mask = (uint8_t)(1u << cur_state);
      BCIns ins = 0;
      BCOp ins_op = BC__MAX;
      uint32_t succ[2];
      int succ_count = 0;
      uint8_t next_state = cur_state;
      int cur_in_slice = 0;

      /* A non-target immediate successor is always reached through the current call. */
      if (cur_pc == pc + 1 && cur_pc < numbc && !targets[cur_pc]) {
        cur_state = 0;
        mask = 1u;
      }

      if ((state_mask[cur_pc] & mask) != 0) continue;
      state_mask[cur_pc] |= mask;

      ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)cur_pc * 4, be);
      ins_op = bc_op(ins);
      cur_in_slice = (cur_pc == pc) ||
                     (cur_pc < pc && cur_pc >= (uint32_t)min_window && selected[cur_pc]);

      if (!cur_in_slice &&
          (state_mask[cur_pc] & 0x3u) == 0x3u &&
          tolua_ins_reads_reg(ins_op, ins, old_base)) {
        int inner_changed = 0;
        status = tolua_try_repack_future_flow_consumer(buf, bc_pos, numbc, be, cur_pc, ins, ins_op,
                                                       framesize_io, targets, map, ctx, &inner_changed);
        if (status != TOLUA_BCCONV_OK || inner_changed) {
          *changed = inner_changed;
          goto cleanup;
        }
        TOLUA_REPACK_LOG(ctx, pc, "flow merge conflict at pc=%u old_base=%u op=%s",
                         (unsigned int)cur_pc, (unsigned int)old_base, tolua_bc_opname(ins_op));
        goto cleanup;
      }

      if (!cur_in_slice && cur_state == 0 && tolua_ins_reads_reg(ins_op, ins, old_base)) {
        rewrite[cur_pc] = 1;
      }
      if (!cur_in_slice && tolua_ins_writes_reg(ins_op, ins, old_base)) {
        next_state = 1;
      }

      succ_count = tolua_fill_successors(ins_op, ins, cur_pc, numbc, succ);
      while (succ_count-- > 0) {
        if (tail >= queue_cap) {
          TOLUA_REPACK_LOG(ctx, pc, "flow queue overflow at pc=%u", (unsigned int)cur_pc);
          goto cleanup;
        }
        queue_pc[tail] = succ[succ_count];
        queue_state[tail++] = next_state;
      }
    }

  }

  if (!tolua_find_call_repack_base(buf, bc_pos, numbc, be, pc, call, op, old_base, old_last,
                                   candidate_min_base, min_window, *framesize_io,
                                   nres == 1 ? state_mask : NULL,
                                   map, 0, &new_base)) {
    TOLUA_REPACK_LOG(ctx, pc, "candidate reject a=%u last=%u frame=%u min_window=%d",
                     (unsigned int)old_base, (unsigned int)old_last,
                     (unsigned int)*framesize_io, min_window);
    goto cleanup;
  }

  new_last = (BCReg)(new_base + nargs);

  if (nres == 1) {
    for (scan = (int)pc + 1; scan < (int)numbc; scan++) {
      if (rewrite[scan]) {
        BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
        BCOp ins_op = bc_op(ins);
        if (!tolua_rewrite_ins_source_reg(&ins, ins_op, old_base, new_base)) {
          int inner_changed = 0;
          status = tolua_try_repack_future_flow_consumer(buf, bc_pos, numbc, be, (uint32_t)scan, ins, ins_op,
                                                         framesize_io, targets, map, ctx, &inner_changed);
          if (status != TOLUA_BCCONV_OK || inner_changed) {
            *changed = inner_changed;
            goto cleanup;
          }
          TOLUA_REPACK_LOG(ctx, pc, "rewrite reject at pc=%d old_base=%u new_base=%u op=%s",
                           scan, (unsigned int)old_base, (unsigned int)new_base,
                           tolua_bc_opname(ins_op));
          goto cleanup;
        }
        tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)ins, be);
      }
    }
  }

  for (scan = min_window; scan < (int)pc; scan++) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp ins_op = bc_op(ins);
    tolua_repack_remap_reg_range(&ins, ins_op, old_base, old_last, new_base);
    tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)ins, be);
    tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, ins,
                                       old_base, old_last, new_base);
  }

  setbc_a(&call, new_base);
  tolua_write_ins(buf + bc_pos + (size_t)pc * 4, (uint32_t)call, be);
  status = tolua_update_framesize_checked(framesize_io, new_last, ctx,
                                          pc, call, op);
  if (status != TOLUA_BCCONV_OK) {
    goto cleanup;
  }
#ifdef TOLUA_REPACK_DEBUG
  tolua_debug_log_open_tsetm_pairs(buf, bc_pos, numbc, be, "after-call", pc);
#endif
  TOLUA_REPACK_LOG(ctx, pc, "generic success old=[%u,%u] new=[%u,%u]",
                   (unsigned int)old_base, (unsigned int)old_last,
                   (unsigned int)new_base, (unsigned int)new_last);
  *changed = 1;

cleanup:
  free(queue_state);
  free(queue_pc);
  free(rewrite);
  free(state_mask);
  free(selected);
  return status;
}

static int tolua_try_repack_iterc(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                  uint32_t pc, uint8_t *framesize_io, const uint8_t *targets,
                                  const tolua_bcshift_map *map,
                                  const tolua_bcdebug_ctx *ctx, int *changed)
{
  BCIns iter = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  uint32_t call_pc = 0;
  BCIns call = 0;
  BCReg a = bc_a(iter);
  int hole_reg = -1;
  int inner_changed = 0;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;
  if ((bc_b(iter) >= 2 &&
       tolua_find_closed_range_hole(map, (int)a + 1, (int)(a + bc_b(iter) - 2), &hole_reg)) ||
      tolua_find_range_hole(map, (int)a + 1, (int)a + bc_c(iter) - 1, &hole_reg)) {
    status = tolua_try_repack_hole_producer_result_copy(buf, bc_pos, numbc, be,
                                                        pc, (BCReg)hole_reg,
                                                        UINT32_MAX,
                                                        framesize_io, targets, map,
                                                        ctx, &inner_changed);
    if (status != TOLUA_BCCONV_OK || inner_changed) {
      *changed = inner_changed;
      return status;
    }
  }

  if (!tolua_find_generic_for_call_from_iterc(buf, bc_pos, numbc, be, pc, iter, &call_pc)) {
    return TOLUA_BCCONV_OK;
  }

  call = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)call_pc * 4, be);
  return tolua_try_repack_generic_for_call_setup(buf, bc_pos, numbc, be, call_pc, call,
                                                 framesize_io, targets, map, ctx, changed);
}

static int tolua_try_repack_cat_gap_copy(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                         uint32_t pc, BCIns cat, BCReg hole_reg,
                                         uint8_t *framesize_io, const uint8_t *targets,
                                         const tolua_bcshift_map *map,
                                         const tolua_bcdebug_ctx *ctx, int *changed)
{
  BCReg old_first = bc_b(cat);
  BCReg old_last = bc_c(cat);
  BCReg new_base = 0;
  BCReg new_last = 0;
  uint32_t copy_count = 0;
  BCReg copy_dst[TOLUA_MAX_INSERT_COPIES];
  BCReg copy_src[TOLUA_MAX_INSERT_COPIES];
  unsigned int cand_base = 0;
  unsigned int pass = 0;
  int hole = -1;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;
  if (tolua_pending_insert_copy.active) return TOLUA_BCCONV_OK;
  if (targets[pc]) return TOLUA_BCCONV_OK;
  if (bc_op(cat) != BC_CAT) return TOLUA_BCCONV_OK;
  if (hole_reg < old_first || hole_reg > old_last) return TOLUA_BCCONV_OK;
  copy_count = (uint32_t)(old_last - old_first + 1);
  if (copy_count == 0 || copy_count > TOLUA_MAX_INSERT_COPIES) return TOLUA_BCCONV_OK;

  for (pass = 0; pass < 2; pass++) {
    unsigned int start = pass == 0 ? 0u : (unsigned int)*framesize_io;

    for (cand_base = start; ; cand_base++) {
      unsigned int cand_last = cand_base + (unsigned int)(old_last - old_first);
      int live_conflict = 0;
      BCReg live_reg = 0;

      if (cand_last > BCMAX_A) break;
      if (pass == 0 && cand_last >= (unsigned int)*framesize_io) break;
      if (tolua_ranges_overlap((BCReg)cand_base, (BCReg)cand_last, old_first, old_last)) continue;
      new_base = (BCReg)cand_base;
      new_last = (BCReg)cand_last;
      if (tolua_find_closed_range_hole(map, (int)new_base, (int)new_last, &hole)) continue;
      if (pass == 0) {
        for (live_reg = new_base; live_reg <= new_last; live_reg++) {
          if (tolua_reg_live_after_pc(buf, bc_pos, numbc, be, pc + 1, live_reg)) {
            live_conflict = 1;
            break;
          }
        }
        if (live_conflict) continue;
      }
      goto found_cat_gap_copy_base;
    }
  }

  return TOLUA_BCCONV_OK;

found_cat_gap_copy_base:

  for (hole = 0; hole < (int)copy_count; hole++) {
    copy_dst[hole] = (BCReg)(new_base + hole);
    copy_src[hole] = (BCReg)(old_first + hole);
  }

  if (!tolua_schedule_insert_copies(ctx, pc, pc, copy_dst, copy_src, (uint8_t)copy_count)) {
    return TOLUA_BCCONV_OK;
  }

  setbc_b(&cat, new_base);
  setbc_c(&cat, new_last);
  tolua_write_ins(buf + bc_pos + (size_t)pc * 4, (uint32_t)cat, be);
  status = tolua_update_framesize_checked(framesize_io, new_last, ctx,
                                          pc, cat, BC_CAT);
  if (status != TOLUA_BCCONV_OK) {
    return status;
  }

  *changed = 1;
  TOLUA_REPACK_LOG(ctx, pc,
                   "cat gap-copy success old=[%u,%u] hole=%u new=[%u,%u] insert_pc=%u copies=%u",
                   (unsigned int)old_first, (unsigned int)old_last,
                   (unsigned int)hole_reg,
                   (unsigned int)new_base, (unsigned int)new_last,
                   (unsigned int)pc,
                   (unsigned int)copy_count);
  return TOLUA_BCCONV_INTERNAL_INSERT_COPY;
}

static int tolua_try_repack_cat(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                uint32_t pc, uint8_t *framesize_io, const uint8_t *targets,
                                const tolua_bcshift_map *map, const tolua_bcdebug_ctx *ctx,
                                int *changed)
{
  BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  BCReg old_first = bc_b(ins);
  BCReg old_last = bc_c(ins);
  BCReg new_base = 0;
  BCReg new_last = 0;
  uint8_t *selected = NULL;
  uint32_t slice_interference_pc = UINT32_MAX;
  BCOp slice_interference_op = BC__MAX;
  BCReg slice_interference_reg = 0;
  uint32_t readonly_interference_pc = UINT32_MAX;
  BCOp readonly_interference_op = BC__MAX;
  BCReg readonly_interference_reg = 0;
  unsigned int pass = 0;
  unsigned int cand_base = 0;
  int min_window = (int)pc;
  int cat_hole_reg = -1;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;
  if (old_last < old_first) return TOLUA_BCCONV_OK;

  selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!selected) return TOLUA_BCCONV_ERR_OUT_OF_MEMORY;

  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc, old_first, old_last,
                                 targets, selected, &min_window,
                                 &slice_interference_pc, &slice_interference_op,
                                 &slice_interference_reg)) {
    int inner_changed = 0;

    if (tolua_find_closed_range_hole(map, old_first, old_last, &cat_hole_reg)) {
      uint32_t skip_hole_pc = UINT32_MAX;

      if (slice_interference_pc != UINT32_MAX &&
          slice_interference_pc < pc &&
          slice_interference_op == BC_CALL &&
          slice_interference_reg == (BCReg)cat_hole_reg) {
        skip_hole_pc = slice_interference_pc;
      }
      status = tolua_try_repack_hole_producer_result_copy(buf, bc_pos, numbc, be,
                                                          pc, (BCReg)cat_hole_reg,
                                                          skip_hole_pc,
                                                          framesize_io, targets, map,
                                                          ctx, &inner_changed);
      if (status != TOLUA_BCCONV_OK || inner_changed) {
        *changed = inner_changed;
        free(selected);
        return status;
      }

      inner_changed = 0;
      status = tolua_try_repack_cat_gap_copy(buf, bc_pos, numbc, be, pc, ins,
                                             (BCReg)cat_hole_reg,
                                             framesize_io, targets, map, ctx, &inner_changed);
      if (status != TOLUA_BCCONV_OK || inner_changed) {
        *changed = inner_changed;
        free(selected);
        return status;
      }
    }

    if (slice_interference_pc != UINT32_MAX &&
        slice_interference_pc < pc &&
        slice_interference_op == BC_CALL) {
      BCIns interfering = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)slice_interference_pc * 4, be);
      if (bc_b(interfering) == 2 && bc_c(interfering) != 0 && !targets[slice_interference_pc]) {
        inner_changed = 0;
        TOLUA_REPACK_LOG(ctx, pc,
                         "cat retry via interfering single-result call at pc=%u reg=%u",
                         (unsigned int)slice_interference_pc,
                         (unsigned int)slice_interference_reg);
        status = tolua_try_repack_call(buf, bc_pos, numbc, be, slice_interference_pc,
                                       framesize_io, targets, map, ctx, &inner_changed);
        if (status != TOLUA_BCCONV_OK || inner_changed) {
          *changed = inner_changed;
          free(selected);
          return status;
        }
      }
    } else if (slice_interference_pc != UINT32_MAX &&
               slice_interference_pc < pc &&
               tolua_retry_repack_slice_with_readonly_interference(buf, bc_pos, numbc, be, pc,
                                                                   old_first, old_last, targets,
                                                                   selected, &min_window,
                                                                   &readonly_interference_pc,
                                                                   &readonly_interference_op,
                                                                   &readonly_interference_reg)) {
      TOLUA_REPACK_LOG(ctx, pc,
                       "cat slice allowed read-only interference at pc=%u op=%s reg=%u",
                       (unsigned int)slice_interference_pc,
                       tolua_bc_opname(slice_interference_op),
                       (unsigned int)slice_interference_reg);
    } else if (readonly_interference_pc != UINT32_MAX &&
               readonly_interference_pc < pc &&
               readonly_interference_op == BC_CALL) {
      BCIns interfering = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)readonly_interference_pc * 4, be);
      if (bc_b(interfering) == 2 && bc_c(interfering) != 0 && !targets[readonly_interference_pc]) {
        inner_changed = 0;
        TOLUA_REPACK_LOG(ctx, pc,
                         "cat readonly retry via interfering single-result call at pc=%u reg=%u",
                         (unsigned int)readonly_interference_pc,
                         (unsigned int)readonly_interference_reg);
        status = tolua_try_repack_call(buf, bc_pos, numbc, be, readonly_interference_pc,
                                       framesize_io, targets, map, ctx, &inner_changed);
        if (status != TOLUA_BCCONV_OK || inner_changed) {
          *changed = inner_changed;
          free(selected);
          return status;
        }
      }
    } else {
      free(selected);
      return TOLUA_BCCONV_OK;
    }
  }

  for (pass = 0; pass < 2; pass++) {
    unsigned int start = pass == 0 ? 0u : (unsigned int)*framesize_io;

    for (cand_base = start; ; cand_base++) {
      unsigned int cand_last = cand_base + (unsigned int)(old_last - old_first);
      int hole_reg = -1;
      int live_conflict = 0;
      BCReg live_reg = 0;

      if (cand_last > BCMAX_A) break;
      if (pass == 0 && cand_last >= (unsigned int)*framesize_io) break;
      if (tolua_ranges_overlap((BCReg)cand_base, (BCReg)cand_last, old_first, old_last)) continue;
      if (tolua_find_closed_range_hole(map, (int)cand_base, (int)cand_last, &hole_reg)) continue;
      if (pass == 0) {
        if (tolua_window_touches_range(buf, bc_pos, be, min_window, pc,
                                       (BCReg)cand_base, (BCReg)cand_last)) {
          continue;
        }
        for (live_reg = (BCReg)cand_base; live_reg <= (BCReg)cand_last; live_reg++) {
          if (tolua_reg_live_after_pc(buf, bc_pos, numbc, be, pc + 1, live_reg)) {
            live_conflict = 1;
            break;
          }
        }
        if (live_conflict) continue;
      }
      new_base = (BCReg)cand_base;
      new_last = (BCReg)cand_last;
      goto found_cat_repack_base;
    }
  }

  free(selected);
  return TOLUA_BCCONV_OK;

found_cat_repack_base:

  for (scan = min_window; scan < (int)pc; scan++) {
    BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp cur_op = bc_op(cur);
    tolua_repack_remap_reg_range(&cur, cur_op, old_first, old_last, new_base);
    tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
    tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                       old_first, old_last, new_base);
  }

  setbc_b(&ins, (BCReg)(new_base + (bc_b(ins) - old_first)));
  setbc_c(&ins, (BCReg)(new_base + (bc_c(ins) - old_first)));
  tolua_write_ins(buf + bc_pos + (size_t)pc * 4, (uint32_t)ins, be);
  status = tolua_update_framesize_checked(framesize_io, new_last, ctx,
                                          pc, ins, BC_CAT);
  if (status != TOLUA_BCCONV_OK) {
    free(selected);
    return status;
  }
#ifdef TOLUA_REPACK_DEBUG
  tolua_debug_log_open_tsetm_pairs(buf, bc_pos, numbc, be, "after-fori", pc);
#endif
  *changed = 1;

  free(selected);
  return TOLUA_BCCONV_OK;
}

static int tolua_try_repack_ret(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                uint32_t pc, uint8_t *framesize_io, const uint8_t *targets,
                                const tolua_bcdebug_ctx *ctx, int *changed)
{
  BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  BCReg old_first = bc_a(ins);
  BCReg old_last = 0;
  BCReg new_base = 0;
  BCReg new_last = 0;
  uint8_t *selected = NULL;
  int min_window = (int)pc;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;
  if (bc_d(ins) <= 1) return TOLUA_BCCONV_OK;

  old_last = (BCReg)(old_first + bc_d(ins) - 2);
  new_base = *framesize_io;
  new_last = (BCReg)(new_base + (old_last - old_first));
  if (new_last > BCMAX_A) return TOLUA_BCCONV_OK;

  selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!selected) return TOLUA_BCCONV_ERR_OUT_OF_MEMORY;

  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc, old_first, old_last,
                                 targets, selected, &min_window,
                                 NULL, NULL, NULL)) {
    free(selected);
    return TOLUA_BCCONV_OK;
  }

  for (scan = min_window; scan < (int)pc; scan++) {
    BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp cur_op = bc_op(cur);
    tolua_repack_remap_reg_range(&cur, cur_op, old_first, old_last, new_base);
    tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
    tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                       old_first, old_last, new_base);
  }

  setbc_a(&ins, new_base);
  tolua_write_ins(buf + bc_pos + (size_t)pc * 4, (uint32_t)ins, be);
  status = tolua_update_framesize_checked(framesize_io, new_last, ctx,
                                          pc, ins, BC_RET);
  if (status != TOLUA_BCCONV_OK) {
    free(selected);
    return status;
  }
  *changed = 1;

  free(selected);
  return TOLUA_BCCONV_OK;
}

static int tolua_find_numeric_for_end(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                      uint32_t pc, BCReg base, uint32_t *out_end)
{
  uint32_t body_pc = pc + 1;
  uint32_t scan = 0;

  for (scan = pc + 1; scan < numbc; scan++) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp op = bc_op(ins);
    uint32_t target = 0;

    switch (op) {
      case BC_FORL:
      case BC_IFORL:
      case BC_JFORL:
        if (bc_a(ins) != base) break;
        if (!tolua_get_jump_target(op, ins, scan, numbc, &target)) break;
        if (target != body_pc) break;
        *out_end = scan;
        return 1;
      default:
        break;
    }
  }

  return 0;
}

static int tolua_find_numeric_for_start(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                        uint32_t loop_end_pc, BCReg base, uint32_t *out_start)
{
  BCIns loop_end = 0;
  BCOp loop_end_op = BC__MAX;
  uint32_t body_pc = 0;
  uint32_t scan = 0;

  loop_end = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)loop_end_pc * 4, be);
  loop_end_op = bc_op(loop_end);
  switch (loop_end_op) {
    case BC_FORL:
    case BC_IFORL:
    case BC_JFORL:
      break;
    default:
      return 0;
  }

  if (!tolua_get_jump_target(loop_end_op, loop_end, loop_end_pc, numbc, &body_pc) || body_pc == 0) {
    return 0;
  }

  for (scan = loop_end_pc; scan-- > 0;) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp op = bc_op(ins);
    uint32_t matched_end = 0;

    switch (op) {
      case BC_FORI:
      case BC_JFORI:
        break;
      default:
        continue;
    }

    if (bc_a(ins) != base || scan + 1 != body_pc) continue;
    if (!tolua_find_numeric_for_end(buf, bc_pos, numbc, be, scan, base, &matched_end) ||
        matched_end != loop_end_pc) {
      continue;
    }

    *out_start = scan;
    return 1;
  }

  return 0;
}

static int tolua_try_repack_fori_copy_setup(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                            uint32_t pc, uint8_t *framesize_io,
                                            const uint8_t *targets, const tolua_bcshift_map *map,
                                            const tolua_bcdebug_ctx *ctx, int *changed)
{
  BCIns fori = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  BCReg old_base = bc_a(fori);
  BCReg new_base = 0;
  BCReg new_last = 0;
  uint32_t loop_end = 0;
  BCReg copy_dst[3];
  BCReg copy_src[3];
  unsigned int cand_base = 0;
  int hole_reg = -1;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;
  if (tolua_pending_insert_copy.active) return TOLUA_BCCONV_OK;
  if (targets[pc]) return TOLUA_BCCONV_OK;
  if (!tolua_find_numeric_for_end(buf, bc_pos, numbc, be, pc, old_base, &loop_end)) {
    return TOLUA_BCCONV_OK;
  }

  for (cand_base = (unsigned int)*framesize_io; ; cand_base++) {
    unsigned int cand_last = cand_base + 3u;
    if (cand_last > BCMAX_A) return TOLUA_BCCONV_OK;
    if (tolua_find_closed_range_hole(map, (int)cand_base, (int)cand_last, &hole_reg)) continue;
    new_base = (BCReg)cand_base;
    new_last = (BCReg)cand_last;
    break;
  }

  for (scan = (int)pc; scan <= (int)loop_end; scan++) {
    BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp cur_op = bc_op(cur);
    tolua_repack_remap_reg_range(&cur, cur_op, old_base, (BCReg)(old_base + 3), new_base);
    tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
    tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                       old_base, (BCReg)(old_base + 3), new_base);
  }

  copy_dst[0] = new_base;
  copy_src[0] = old_base;
  copy_dst[1] = (BCReg)(new_base + 1);
  copy_src[1] = (BCReg)(old_base + 1);
  copy_dst[2] = (BCReg)(new_base + 2);
  copy_src[2] = (BCReg)(old_base + 2);
  if (!tolua_schedule_insert_copies(ctx, pc, pc, copy_dst, copy_src, 3)) {
    return TOLUA_BCCONV_OK;
  }

  status = tolua_update_framesize_checked(framesize_io, new_last, ctx,
                                          pc, fori, BC_FORI);
  if (status != TOLUA_BCCONV_OK) {
    return status;
  }
  *changed = 1;
  TOLUA_REPACK_LOG(ctx, pc,
                   "fori copy-setup success old=[%u,%u] new=[%u,%u] loop_end=%u copies=3",
                   (unsigned int)old_base, (unsigned int)(old_base + 3),
                   (unsigned int)new_base, (unsigned int)new_last,
                   (unsigned int)loop_end);
  return TOLUA_BCCONV_INTERNAL_INSERT_COPY;
}

static int tolua_try_repack_fori(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                 uint32_t pc, uint8_t *framesize_io, const uint8_t *targets,
                                 const tolua_bcshift_map *map,
                                 const tolua_bcdebug_ctx *ctx, int *changed)
{
  BCIns fori = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
  BCReg old_base = bc_a(fori);
  BCReg new_base = 0;
  BCReg new_last = 0;
  uint32_t loop_end = 0;
  uint8_t *selected = NULL;
  unsigned int cand_base = 0;
  int hole_reg = -1;
  int min_window = (int)pc;
  int scan = 0;
  int status = TOLUA_BCCONV_OK;

  *changed = 0;
  if (!tolua_find_numeric_for_end(buf, bc_pos, numbc, be, pc, old_base, &loop_end)) {
    return TOLUA_BCCONV_OK;
  }

  for (cand_base = (unsigned int)*framesize_io; ; cand_base++) {
    unsigned int cand_last = cand_base + 3u;
    if (cand_last > BCMAX_A) return TOLUA_BCCONV_OK;
    if (tolua_find_closed_range_hole(map, (int)cand_base, (int)cand_last, &hole_reg)) continue;
    new_base = (BCReg)cand_base;
    new_last = (BCReg)cand_last;
    break;
  }

  selected = (uint8_t *)calloc((size_t)numbc, 1);
  if (!selected) return TOLUA_BCCONV_ERR_OUT_OF_MEMORY;

  if (!tolua_select_repack_slice(buf, bc_pos, numbc, be, pc, old_base, (BCReg)(old_base + 2),
                                 targets, selected, &min_window,
                                 NULL, NULL, NULL)) {
    int inner_changed = 0;
    status = tolua_try_repack_fori_copy_setup(buf, bc_pos, numbc, be, pc,
                                              framesize_io, targets, map, ctx, &inner_changed);
    if (status != TOLUA_BCCONV_OK || inner_changed) {
      *changed = inner_changed;
      free(selected);
      return status;
    }
    free(selected);
    return TOLUA_BCCONV_OK;
  }

  for (scan = min_window; scan < (int)pc; scan++) {
    if (!selected[scan]) continue;
    {
      BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
      BCOp cur_op = bc_op(cur);
      tolua_repack_remap_reg_range(&cur, cur_op, old_base, (BCReg)(old_base + 2), new_base);
      tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
      if (cur_op == BC_CALL && bc_b(cur) == 0) {
#ifdef TOLUA_REPACK_DEBUG
        fprintf(stderr, "[repack] pc=%d fori-preheader remapped open call a=%u c=%u\n",
                scan, (unsigned int)bc_a(cur), (unsigned int)bc_c(cur));
#endif
      }
      tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                         old_base, (BCReg)(old_base + 2), new_base);
    }
  }

  for (scan = (int)pc; scan <= (int)loop_end; scan++) {
    BCIns cur = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)scan * 4, be);
    BCOp cur_op = bc_op(cur);
    tolua_repack_remap_reg_range(&cur, cur_op, old_base, (BCReg)(old_base + 3), new_base);
    tolua_write_ins(buf + bc_pos + (size_t)scan * 4, (uint32_t)cur, be);
    if (cur_op == BC_CALL && bc_b(cur) == 0) {
#ifdef TOLUA_REPACK_DEBUG
      fprintf(stderr, "[repack] pc=%d fori-body remapped open call a=%u c=%u\n",
              scan, (unsigned int)bc_a(cur), (unsigned int)bc_c(cur));
#endif
    }
    tolua_sync_open_tsetm_after_repack(buf, bc_pos, numbc, be, (uint32_t)scan, cur,
                                       old_base, (BCReg)(old_base + 3), new_base);
  }

  status = tolua_update_framesize_checked(framesize_io, new_last, ctx,
                                          pc, fori, BC_FORI);
  if (status != TOLUA_BCCONV_OK) {
    free(selected);
    return status;
  }
  *changed = 1;

  free(selected);
  return TOLUA_BCCONV_OK;
}

static int tolua_collect_proto_holes(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                     int remap_v1, int target_fr2, tolua_bcshift_map *map,
                                     const tolua_bcdebug_ctx *ctx);

static int tolua_try_repack_proto_calls(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                        uint8_t *framesize_io, const tolua_bcdebug_ctx *ctx)
{
  uint8_t *targets = NULL;
  int status = TOLUA_BCCONV_OK;

  if (*framesize_io > BCMAX_A) {
    return tolua_failbytecodeproto(ctx, 0, 0, BC__MAX, TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                   "proto frame size %u exceeds register limit before repack",
                                   (unsigned int)*framesize_io);
  }

  targets = (uint8_t *)calloc((size_t)numbc, 1);
  if (!targets) {
    return tolua_failbytecodeproto(ctx, 0, 0, BC__MAX, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate jump target buffer");
  }
  if (!tolua_init_call_repack_state(numbc)) {
    free(targets);
    return tolua_failbytecodeproto(ctx, 0, 0, BC__MAX, TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                                   "failed to allocate call repack tracker");
  }

  tolua_mark_proto_targets(buf, bc_pos, numbc, be, targets);

  for (;;) {
    tolua_bcshift_map map;
    uint32_t pc = 0;
    int changed = 0;

    status = tolua_collect_proto_holes(buf, bc_pos, numbc, be, 0, 1, &map, ctx);
    if (status != TOLUA_BCCONV_OK) break;

    for (pc = 0; pc < numbc; pc++) {
      BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)pc * 4, be);
      BCOp op = bc_op(ins);
      BCReg a = bc_a(ins);
      int hole_reg = -1;

      if (op == BC_CALL && bc_b(ins) != 0 && bc_c(ins) > 1 &&
          (((bc_b(ins) > 2) &&
            tolua_find_closed_range_hole(&map, (int)a + 1, (int)(a + bc_b(ins) - 2), &hole_reg)) ||
           tolua_find_range_hole(&map, (int)a + 1, (int)a + bc_c(ins) - 1, &hole_reg))) {
        status = tolua_try_repack_call(buf, bc_pos, numbc, be, pc, framesize_io, targets, &map,
                                       ctx, &changed);
      } else if (op == BC_CALLT && bc_d(ins) > 1 &&
                 tolua_find_range_hole(&map, (int)a + 1, (int)a + bc_d(ins) - 1, &hole_reg)) {
        status = tolua_try_repack_call(buf, bc_pos, numbc, be, pc, framesize_io, targets, &map,
                                       ctx, &changed);
      } else if (op == BC_CAT &&
                 tolua_find_closed_range_hole(&map, bc_b(ins), bc_c(ins), &hole_reg)) {
        status = tolua_try_repack_cat(buf, bc_pos, numbc, be, pc, framesize_io, targets, &map,
                                      ctx, &changed);
      } else if (op == BC_RET && bc_d(ins) > 1 &&
                 tolua_find_range_hole(&map, a, (int)a + bc_d(ins) - 2, &hole_reg)) {
        status = tolua_try_repack_ret(buf, bc_pos, numbc, be, pc, framesize_io, targets, ctx, &changed);
      } else if (op == BC_FORI &&
                 tolua_find_closed_range_hole(&map, a, (int)a + 3, &hole_reg)) {
        status = tolua_try_repack_fori(buf, bc_pos, numbc, be, pc, framesize_io, targets, &map,
                                       ctx, &changed);
      } else if (op == BC_ITERC &&
                 ((bc_b(ins) >= 2 &&
                   tolua_find_closed_range_hole(&map, (int)a + 1, (int)(a + bc_b(ins) - 2), &hole_reg)) ||
                  tolua_find_range_hole(&map, (int)a + 1, (int)a + bc_c(ins) - 1, &hole_reg))) {
        status = tolua_try_repack_iterc(buf, bc_pos, numbc, be, pc, framesize_io, targets, &map,
                                        ctx, &changed);
      }

      if (changed && status == TOLUA_BCCONV_OK && op == BC_CAT) {
        TOLUA_REPACK_LOG(ctx, pc, "cat success%s", "");
      } else if (changed && status == TOLUA_BCCONV_OK && op == BC_RET) {
        TOLUA_REPACK_LOG(ctx, pc, "ret success%s", "");
      } else if (changed && status == TOLUA_BCCONV_OK && op == BC_FORI) {
        TOLUA_REPACK_LOG(ctx, pc, "fori success%s", "");
      }

      if (status != TOLUA_BCCONV_OK || changed) break;
    }

    if (status != TOLUA_BCCONV_OK || !changed) break;
  }

  tolua_clear_call_repack_state();
  free(targets);
  return status;
}

static int tolua_collect_proto_holes(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                     int remap_v1, int target_fr2, tolua_bcshift_map *map,
                                     const tolua_bcdebug_ctx *ctx)
{
  uint32_t i = 0;

  memset(map, 0, sizeof(*map));

  for (i = 0; i < numbc; i++) {
    BCIns ins = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)i * 4, be);
    BCOp op = BC__MAX;
    int status = tolua_resolve_proto_op(buf, bc_pos, numbc, i, be, remap_v1, target_fr2, ins, &op, ctx);
    if (status != TOLUA_BCCONV_OK) return status;

    switch (op) {
      case BC_CALL:
      case BC_CALLT:
      case BC_ITERC:
        map->hole[bc_a(ins)] = 1;
        break;
      case BC_CALLM:
      case BC_CALLMT:
      case BC_RETM:
        return tolua_failbytecodeproto(ctx, i, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_OPCODE,
                                       "opcode %s needs open-result conversion that is not implemented",
                                       tolua_bc_opname(op));
      default:
        break;
    }
  }

  tolua_build_shift_map(map);
  return TOLUA_BCCONV_OK;
}

static int tolua_validate_open_tsetm_pair(const uint8_t *buf, size_t bc_pos, uint32_t numbc,
                                          uint32_t pc, int be, BCIns ins, BCOp op,
                                          const tolua_bcdebug_ctx *ctx, int producer_side)
{
  BCReg a = bc_a(ins);

  if (producer_side) {
    BCIns next = 0;

    if (pc + 1 >= numbc) {
      return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                     "%s with open results must be followed by TSETM",
                                     tolua_bc_opname(op));
    }

    next = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc + 1) * 4, be);
    if (bc_op(next) != BC_TSETM) {
      return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                     "%s with open results is only supported when immediately consumed by TSETM",
                                     tolua_bc_opname(op));
    }
    if (bc_a(next) != a) {
      return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                     "%s open-result base A=%u does not match following TSETM base A=%u",
                                     tolua_bc_opname(op), (unsigned int)a,
                                     (unsigned int)bc_a(next));
    }

    return TOLUA_BCCONV_OK;
  } else {
    BCIns prev = 0;
    BCOp prev_op = BC__MAX;

    if (pc == 0) {
      return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                     "TSETM has no preceding open-result producer");
    }

    prev = (BCIns)tolua_read_ins(buf + bc_pos + (size_t)(pc - 1) * 4, be);
    prev_op = bc_op(prev);
    if (!((prev_op == BC_CALL || prev_op == BC_VARG) && bc_b(prev) == 0)) {
      return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                     "TSETM must follow CALL/VARG with open results");
    }
    if (bc_a(prev) != a) {
      TOLUA_REPACK_LOG(ctx, pc, "tsetm mismatch prev_pc=%u prev_op=%s raw=0x%08x a=%u b=%u c=%u d=%u",
                       (unsigned int)(pc - 1), tolua_bc_opname(prev_op), (unsigned int)prev,
                       (unsigned int)bc_a(prev), (unsigned int)bc_b(prev),
                       (unsigned int)bc_c(prev), (unsigned int)bc_d(prev));
      return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                     "TSETM base A=%u does not match preceding %s base A=%u",
                                     (unsigned int)a, tolua_bc_opname(prev_op),
                                     (unsigned int)bc_a(prev));
    }

    return TOLUA_BCCONV_OK;
  }
}

static int tolua_validate_proto_ins(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                    const tolua_bcshift_map *map, BCOp op, BCIns ins,
                                    const tolua_bcdebug_ctx *ctx, uint32_t pc)
{
  BCReg a = bc_a(ins);
  int hole_reg = -1;

  switch (op) {
    case BC_CALL: {
      BCReg b = bc_b(ins);
      BCReg c = bc_c(ins);
      if (b == 0) {
        int pair_status = TOLUA_BCCONV_OK;
        if (c == 0) {
          return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                         "CALL must have at least one encoded argument slot");
        }
        pair_status = tolua_validate_open_tsetm_pair(buf, bc_pos, numbc, pc, be, ins, op, ctx, 1);
        if (pair_status != TOLUA_BCCONV_OK) return pair_status;
        if (tolua_find_range_hole(map, (int)a + 1, (int)a + c - 1, &hole_reg)) {
          return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                         "CALL open-result arguments in [%d,%d) cross FR2 hole at register %d",
                                         (int)a + 1, (int)a + c - 1, hole_reg);
        }
        return TOLUA_BCCONV_OK;
      }
      if (c == 0) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "CALL must have at least one encoded argument slot");
      }
      if (tolua_find_closed_range_hole(map, (int)a + 1, (int)(a + b - 2), &hole_reg)) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "CALL fixed results in [%d,%d) cross FR2 hole at register %d",
                                       (int)a + 1, (int)a + b - 2, hole_reg);
      }
      if (tolua_find_range_hole(map, (int)a + 1, (int)a + c - 1, &hole_reg)) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "CALL argument range [%d,%d) crosses FR2 hole at register %d",
                                       (int)a + 1, (int)a + c - 1, hole_reg);
      }
      return TOLUA_BCCONV_OK;
    }
    case BC_CALLT: {
      BCReg d = bc_d(ins);
      if (d == 0) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "CALLT must have at least one encoded argument slot");
      }
      if (tolua_find_range_hole(map, (int)a + 1, (int)a + d - 1, &hole_reg)) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "CALLT argument range [%d,%d) crosses FR2 hole at register %d",
                                       (int)a + 1, (int)a + d - 1, hole_reg);
      }
      return TOLUA_BCCONV_OK;
    }
    case BC_ITERC: {
      BCReg b = bc_b(ins);
      BCReg c = bc_c(ins);
      if (b == 0) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "ITERC with open results (B=0) is not supported");
      }
      if (c != 3) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "ITERC expects the standard two-argument iterator layout (C=3), got C=%u",
                                       (unsigned int)c);
      }
      if (tolua_find_closed_range_hole(map, (int)a + 1, (int)(a + b - 2), &hole_reg)) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "ITERC fixed results in [%d,%d) cross FR2 hole at register %d",
                                       (int)a + 1, (int)a + b - 2, hole_reg);
      }
      if (tolua_find_range_hole(map, (int)a + 1, (int)a + c - 1, &hole_reg)) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "ITERC iterator arguments in [%d,%d) cross FR2 hole at register %d",
                                       (int)a + 1, (int)a + c - 1, hole_reg);
      }
      return TOLUA_BCCONV_OK;
    }
    case BC_VARG: {
      BCReg b = bc_b(ins);
      if (b == 0) {
        return tolua_validate_open_tsetm_pair(buf, bc_pos, numbc, pc, be, ins, op, ctx, 1);
      }
      if (tolua_find_range_hole(map, a, (int)a + b - 2, &hole_reg)) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "VARG destination range [%d,%d) crosses FR2 hole at register %d",
                                       (int)a, (int)a + b - 2, hole_reg);
      }
      return TOLUA_BCCONV_OK;
    }
    case BC_TSETM:
      return tolua_validate_open_tsetm_pair(buf, bc_pos, numbc, pc, be, ins, op, ctx, 0);
    case BC_RET:
      if (tolua_find_range_hole(map, a, (int)a + bc_d(ins) - 2, &hole_reg)) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "RET result range [%d,%d) crosses FR2 hole at register %d",
                                       (int)a, (int)a + bc_d(ins) - 2, hole_reg);
      }
      return TOLUA_BCCONV_OK;
    case BC_CAT:
      if (tolua_find_closed_range_hole(map, bc_b(ins), bc_c(ins), &hole_reg)) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "CAT operand range [%d,%d] crosses FR2 hole at register %d",
                                       (int)bc_b(ins), (int)bc_c(ins), hole_reg);
      }
      return TOLUA_BCCONV_OK;
    case BC_FORI:
    case BC_JFORI:
    case BC_FORL:
    case BC_IFORL:
    case BC_JFORL:
      if (tolua_find_closed_range_hole(map, a, (int)a + 3, &hole_reg)) {
        return tolua_failbytecodeproto(ctx, pc, ins, op, TOLUA_BCCONV_ERR_UNSUPPORTED_LAYOUT,
                                       "numeric for-loop control slots [%d,%d] cross FR2 hole at register %d",
                                       (int)a, (int)a + 3, hole_reg);
      }
      return TOLUA_BCCONV_OK;
    default:
      return TOLUA_BCCONV_OK;
  }
}

static int tolua_rewrite_proto_ins(const uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                   const tolua_bcshift_map *map, BCIns *ins,
                                   const tolua_bcdebug_ctx *ctx, uint32_t pc)
{
  BCOp op = bc_op(*ins);
  BCMode mode = BCMnone;
  BCReg reg = 0;
  int mapped_tsetm_a = 0;
  int status = TOLUA_BCCONV_OK;

  if (op == BC_TSETM) {
    reg = bc_a(*ins);
    if (!tolua_map_reg(map, reg, &reg)) {
      return tolua_failbytecodeproto(ctx, pc, *ins, op, TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                     "failed to remap TSETM base register %u",
                                     (unsigned int)bc_a(*ins));
    }
    setbc_a(ins, reg);
    mapped_tsetm_a = 1;
  }

  status = tolua_validate_proto_ins(buf, bc_pos, numbc, be, map, op, *ins, ctx, pc);

  if (status != TOLUA_BCCONV_OK) return status;

  mode = bcmode_a(op);
  if (!mapped_tsetm_a && tolua_op_a_is_reg(op, mode)) {
    reg = bc_a(*ins);
    if (!tolua_map_reg(map, reg, &reg)) {
      return tolua_failbytecodeproto(ctx, pc, *ins, op, TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                     "failed to remap A register %u", (unsigned int)bc_a(*ins));
    }
    setbc_a(ins, reg);
  }

  if (bcmode_hasd(op)) {
    mode = bcmode_d(op);
    if (tolua_is_reg_mode(mode)) {
      reg = bc_d(*ins);
      if (!tolua_map_reg(map, reg, &reg)) {
        return tolua_failbytecodeproto(ctx, pc, *ins, op, TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                       "failed to remap D register %u", (unsigned int)bc_d(*ins));
      }
      setbc_d(ins, reg);
    }
  } else {
    mode = bcmode_b(op);
    if (tolua_is_reg_mode(mode)) {
      reg = bc_b(*ins);
      if (!tolua_map_reg(map, reg, &reg)) {
        return tolua_failbytecodeproto(ctx, pc, *ins, op, TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                       "failed to remap B register %u", (unsigned int)bc_b(*ins));
      }
      setbc_b(ins, reg);
    }

    mode = bcmode_c(op);
    if (tolua_is_reg_mode(mode)) {
      reg = bc_c(*ins);
      if (!tolua_map_reg(map, reg, &reg)) {
        return tolua_failbytecodeproto(ctx, pc, *ins, op, TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                       "failed to remap C register %u", (unsigned int)bc_c(*ins));
      }
      setbc_c(ins, reg);
    }
  }

  return TOLUA_BCCONV_OK;
}

static int tolua_patch_proto_bytecode(uint8_t *buf, size_t bc_pos, uint32_t numbc, int be,
                                      int remap_v1, int target_fr2, int prepared_already,
                                      uint8_t *framesize_io,
                                      const tolua_bcdebug_ctx *ctx)
{
  tolua_bcshift_map map;
  uint32_t i = 0;
  int status = TOLUA_BCCONV_OK;
  int patch_remap_v1 = prepared_already ? 0 : remap_v1;

  if (!prepared_already && (target_fr2 || remap_v1)) {
    status = tolua_prepare_proto_bytecode(buf, bc_pos, numbc, be, remap_v1, target_fr2, ctx);
    if (status != TOLUA_BCCONV_OK) return status;
    patch_remap_v1 = 0;
  }

  if (target_fr2) {
    status = tolua_try_repack_proto_calls(buf, bc_pos, numbc, be, framesize_io, ctx);
    if (status != TOLUA_BCCONV_OK) return status;
  }

  if (target_fr2) {
    status = tolua_collect_proto_holes(buf, bc_pos, numbc, be, patch_remap_v1, target_fr2, &map, ctx);
    if (status != TOLUA_BCCONV_OK) return status;
  }

  for (i = 0; i < numbc; i++) {
    uint8_t *slot = buf + bc_pos + (size_t)i * 4;
    BCIns ins = (BCIns)tolua_read_ins(slot, be);
    BCOp op = BC__MAX;

    status = tolua_resolve_proto_op(buf, bc_pos, numbc, i, be, patch_remap_v1, target_fr2, ins, &op, ctx);
    if (status != TOLUA_BCCONV_OK) return status;

    setbc_op(&ins, op);

    if (target_fr2) {
      status = tolua_rewrite_proto_ins(buf, bc_pos, numbc, be, &map, &ins, ctx, i);
      if (status != TOLUA_BCCONV_OK) return status;
    }

    tolua_write_ins(slot, (uint32_t)ins, be);
  }

  return TOLUA_BCCONV_OK;
}

static int tolua_convert_bytecode_inplace(uint8_t **buf_io, size_t *len_io, int target_fr2)
{
  uint8_t *buf = *buf_io;
  size_t len = *len_io;
  size_t pos = 0;
  size_t flag_pos = 0;
  size_t chunk_name_pos = 0;
  size_t chunk_name_len = 0;
  uint32_t flags = 0;
  uint32_t version = 0;
  int be = 0;
  int strip = 0;
  int source_fr2 = 0;
  int status = TOLUA_BCCONV_OK;
  uint32_t retry_proto_index = UINT32_MAX;
  int retry_proto_prepared = 0;

  if (len < 5) {
    return tolua_failbytecode(TOLUA_BCCONV_ERR_NOT_BYTECODE,
                              "input is too short to be a LuaJIT bytecode chunk (size=%u)",
                              (unsigned int)len);
  }
  if (buf[0] != TOLUA_BCDUMP_HEAD1 || buf[1] != TOLUA_BCDUMP_HEAD2 || buf[2] != TOLUA_BCDUMP_HEAD3) {
    return tolua_failbytecode(TOLUA_BCCONV_ERR_NOT_BYTECODE,
                              "bytecode header mismatch: got %02x %02x %02x",
                              (unsigned int)buf[0], (unsigned int)buf[1], (unsigned int)buf[2]);
  }
  version = buf[3];
  if (version != 1 && version != TOLUA_BCDUMP_VERSION) {
    return tolua_failbytecode(TOLUA_BCCONV_ERR_UNSUPPORTED_VERSION,
                              "unsupported bytecode version %u", (unsigned int)version);
  }

  pos = 4;
  flag_pos = pos;
  if (!tolua_read_uleb128(buf, len, &pos, &flags)) {
    return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                              "failed to read bytecode flags");
  }
  if ((flags & ~(TOLUA_BCDUMP_F_FR2 * 2 - 1)) != 0) {
    return tolua_failbytecode(TOLUA_BCCONV_ERR_INVALID_FLAGS,
                              "unsupported flags value 0x%x", (unsigned int)flags);
  }

  be = (flags & TOLUA_BCDUMP_F_BE) ? 1 : 0;
  strip = (flags & TOLUA_BCDUMP_F_STRIP) ? 1 : 0;
  source_fr2 = (flags & TOLUA_BCDUMP_F_FR2) ? 1 : 0;

  if (source_fr2) {
    if (target_fr2 && version == TOLUA_BCDUMP_VERSION) {
      return TOLUA_BCCONV_OK;
    }

    return tolua_failbytecode(TOLUA_BCCONV_ERR_SOURCE_FR2,
                              "source chunk already has FR2 flag set");
  }

  if (version == 1 && target_fr2) {
    if (!strip) {
      uint32_t name_len = 0;
      if (!tolua_read_uleb128(buf, len, &pos, &name_len)) {
        return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                  "failed to read chunk name length");
      }
      if ((size_t)name_len > len - pos) {
        return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                  "chunk name length %u exceeds remaining size %u",
                                  (unsigned int)name_len, (unsigned int)(len - pos));
      }
      chunk_name_pos = pos;
      chunk_name_len = (size_t)name_len;
      pos += (size_t)name_len;
    }

    for (uint32_t proto_index = 0;; proto_index++) {
      tolua_bcdebug_ctx ctx;
      size_t proto_len_pos = pos;
      uint32_t proto_len = 0;
      size_t p = 0;
      size_t proto_end = 0;
      size_t bc_pos = 0;
      size_t framesize_pos = 0;
      uint32_t numkgc = 0, numkn = 0, numbc = 0;
      uint32_t sizedbg = 0;

      ctx.chunk_name = (!strip && chunk_name_len != 0) ? (const char *)(buf + chunk_name_pos) : NULL;
      ctx.chunk_name_len = chunk_name_len;
      ctx.proto_index = proto_index;
      ctx.proto_flags = 0;

      if (!tolua_read_uleb128(buf, len, &pos, &proto_len)) {
        return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                  "failed to read proto length for proto %u",
                                  (unsigned int)proto_index);
      }
      if (proto_len == 0) break;
      if ((size_t)proto_len > len - pos) {
        return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                  "proto %u length %u exceeds remaining chunk size %u",
                                  (unsigned int)proto_index, (unsigned int)proto_len,
                                  (unsigned int)(len - pos));
      }

      p = pos;
      proto_end = pos + (size_t)proto_len;
      if (p + 4 > proto_end) {
        return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                  "proto %u header is truncated", (unsigned int)proto_index);
      }
      ctx.proto_flags = buf[p];
      framesize_pos = p + 2;
      p += 4;

      if (!tolua_read_uleb128(buf, len, &p, &numkgc) ||
          !tolua_read_uleb128(buf, len, &p, &numkn) ||
          !tolua_read_uleb128(buf, len, &p, &numbc)) {
        return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                  "proto %u has malformed KGC/KNUM/BC counts",
                                  (unsigned int)proto_index);
      }
      if (p > proto_end) {
        return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                  "proto %u count section exceeds proto bounds",
                                  (unsigned int)proto_index);
      }
      (void)numkgc;
      (void)numkn;

      if (!strip) {
        if (!tolua_read_uleb128(buf, len, &p, &sizedbg)) {
          return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                    "proto %u has malformed debug-size field",
                                    (unsigned int)proto_index);
        }
        if (sizedbg) {
          uint32_t firstline = 0, numline = 0;
          if (!tolua_read_uleb128(buf, len, &p, &firstline) ||
              !tolua_read_uleb128(buf, len, &p, &numline)) {
            return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                      "proto %u has malformed debug line info",
                                      (unsigned int)proto_index);
          }
        }
        if (p > proto_end) {
          return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                    "proto %u debug header exceeds proto bounds",
                                    (unsigned int)proto_index);
        }
      }

      bc_pos = p;
      if ((size_t)numbc > (proto_end - bc_pos) / 4) {
        return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                  "proto %u bytecode count %u exceeds proto body",
                                  (unsigned int)proto_index, (unsigned int)numbc);
      }

      {
        int patch_remap_v1 = !(retry_proto_index == proto_index && retry_proto_prepared);
        status = tolua_patch_proto_v1_fr2(buf, bc_pos, numbc, be, patch_remap_v1,
                                          &buf[framesize_pos], &ctx);
      }
      if (status == TOLUA_BCCONV_INTERNAL_INSERT_COPY) {
        uint8_t *rebuilt = NULL;
        size_t rebuilt_size = 0;
        int rebuild_status = TOLUA_BCCONV_OK;

        rebuilt = tolua_rebuild_chunk_with_insert_copy(buf, len, &rebuilt_size, &rebuild_status);
        if (rebuilt == NULL) return rebuild_status;

        free(buf);
        buf = rebuilt;
        len = rebuilt_size;
        *buf_io = buf;
        *len_io = len;
        tolua_clear_pending_insert_copy();
        retry_proto_index = proto_index;
        retry_proto_prepared = 1;
        pos = proto_len_pos;
        proto_index--;
        continue;
      }
      if (status != TOLUA_BCCONV_OK) {
        return status;
      }
      if (retry_proto_index == proto_index) {
        retry_proto_index = UINT32_MAX;
        retry_proto_prepared = 0;
      }

      pos = proto_end;
    }

    buf[3] = TOLUA_BCDUMP_VERSION;
    buf[flag_pos] = (uint8_t)(flags | TOLUA_BCDUMP_F_FR2);
    *buf_io = buf;
    *len_io = len;
    return TOLUA_BCCONV_OK;
  }

  if (!strip) {
    uint32_t name_len = 0;
    if (!tolua_read_uleb128(buf, len, &pos, &name_len)) {
      return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                "failed to read chunk name length");
    }
    if ((size_t)name_len > len - pos) {
      return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                "chunk name length %u exceeds remaining size %u",
                                (unsigned int)name_len, (unsigned int)(len - pos));
    }
    chunk_name_pos = pos;
    chunk_name_len = (size_t)name_len;
    pos += (size_t)name_len;
  }

  tolua_clear_pending_insert_copy();
  for (uint32_t proto_index = 0;; proto_index++) {
    tolua_bcdebug_ctx ctx;
    size_t proto_len_pos = pos;
    uint32_t proto_len = 0;
    size_t p = 0;
    size_t proto_end = 0;
    size_t bc_pos = 0;
    size_t framesize_pos = 0;
    uint32_t numkgc = 0, numkn = 0, numbc = 0;
    uint32_t sizedbg = 0;
    ctx.chunk_name = (!strip && chunk_name_len != 0) ? (const char *)(buf + chunk_name_pos) : NULL;
    ctx.chunk_name_len = chunk_name_len;
    ctx.proto_index = proto_index;
    ctx.proto_flags = 0;

    if (!tolua_read_uleb128(buf, len, &pos, &proto_len)) {
      return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                "failed to read proto length for proto %u",
                                (unsigned int)proto_index);
    }
    if (proto_len == 0) break;
    if ((size_t)proto_len > len - pos) {
      return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                "proto %u length %u exceeds remaining chunk size %u",
                                (unsigned int)proto_index, (unsigned int)proto_len,
                                (unsigned int)(len - pos));
    }

    p = pos;
    proto_end = pos + (size_t)proto_len;
    if (p + 4 > proto_end) {
      return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                "proto %u header is truncated", (unsigned int)proto_index);
    }
    ctx.proto_flags = buf[p];
    framesize_pos = p + 2;
    p += 4; /* pflags, params, framesize, uv */

    if (!tolua_read_uleb128(buf, len, &p, &numkgc) ||
        !tolua_read_uleb128(buf, len, &p, &numkn) ||
        !tolua_read_uleb128(buf, len, &p, &numbc)) {
      return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                "proto %u has malformed KGC/KNUM/BC counts",
                                (unsigned int)proto_index);
    }
    if (p > proto_end) {
      return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                "proto %u count section exceeds proto bounds",
                                (unsigned int)proto_index);
    }
    (void)numkgc;
    (void)numkn;

    if (!strip) {
      if (!tolua_read_uleb128(buf, len, &p, &sizedbg)) {
        return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                  "proto %u has malformed debug-size field",
                                  (unsigned int)proto_index);
      }
      if (sizedbg) {
        uint32_t firstline = 0, numline = 0;
        if (!tolua_read_uleb128(buf, len, &p, &firstline) ||
            !tolua_read_uleb128(buf, len, &p, &numline)) {
          return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                    "proto %u has malformed debug line info",
                                    (unsigned int)proto_index);
        }
      }
      if (p > proto_end) {
        return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                  "proto %u debug header exceeds proto bounds",
                                  (unsigned int)proto_index);
      }
    }

    bc_pos = p;
    if ((size_t)numbc > (proto_end - bc_pos) / 4) {
      return tolua_failbytecode(TOLUA_BCCONV_ERR_MALFORMED_CHUNK,
                                "proto %u bytecode count %u exceeds proto body",
                                (unsigned int)proto_index, (unsigned int)numbc);
    }

    status = tolua_patch_proto_bytecode(buf, bc_pos, numbc, be, version == 1, target_fr2,
                                        retry_proto_index == proto_index && retry_proto_prepared,
                                        &buf[framesize_pos], &ctx);
    if (status == TOLUA_BCCONV_INTERNAL_INSERT_COPY) {
      uint8_t *rebuilt = NULL;
      size_t rebuilt_size = 0;
      int rebuild_status = TOLUA_BCCONV_OK;

      rebuilt = tolua_rebuild_chunk_with_insert_copy(buf, len, &rebuilt_size, &rebuild_status);
      if (rebuilt == NULL) return rebuild_status;

      free(buf);
      buf = rebuilt;
      len = rebuilt_size;
      *buf_io = buf;
      *len_io = len;
      tolua_clear_pending_insert_copy();
      retry_proto_index = proto_index;
      retry_proto_prepared = 1;
      pos = proto_len_pos;
      proto_index--;
      continue;
    }
    if (status != TOLUA_BCCONV_OK) {
      return status;
    }
    if (retry_proto_index == proto_index) {
      retry_proto_index = UINT32_MAX;
      retry_proto_prepared = 0;
    }

    if (target_fr2) {
      uint8_t framesize = buf[framesize_pos];
      tolua_bcshift_map map;
      BCReg last = 0;
      BCReg mapped = 0;

      status = tolua_collect_proto_holes(buf, bc_pos, numbc, be, 0, target_fr2, &map, &ctx);
      if (status != TOLUA_BCCONV_OK) {
        return status;
      }

      last = (BCReg)(framesize - 1);
      if (!tolua_map_reg(&map, last, &mapped)) {
        return tolua_failbytecode(TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                  "proto %u frame size %u overflows after FR2 remap",
                                  (unsigned int)proto_index, (unsigned int)framesize);
      }
      if ((uint32_t)mapped + 1 > 0xff) {
        return tolua_failbytecode(TOLUA_BCCONV_ERR_REGISTER_OVERFLOW,
                                  "proto %u remapped frame size %u exceeds register limit",
                                  (unsigned int)proto_index, (unsigned int)mapped + 1);
      }
      buf[framesize_pos] = (uint8_t)(mapped + 1);
    }

    pos = proto_end;
  }

  buf[3] = TOLUA_BCDUMP_VERSION;
  buf[flag_pos] = (uint8_t)((flags & ~TOLUA_BCDUMP_F_FR2) | (target_fr2 ? TOLUA_BCDUMP_F_FR2 : 0));
  *buf_io = buf;
  *len_io = len;
  return TOLUA_BCCONV_OK;
}

LUALIB_API char* tolua_convertbytecode(const char *buff, int sz, int target_fr2, int *out_sz)
{
  return tolua_convertbytecodeex(buff, sz, target_fr2, out_sz, NULL);
}

LUALIB_API char* tolua_convertbytecodeex(const char *buff, int sz, int target_fr2, int *out_sz, int *error_code)
{
  uint8_t *patched = NULL;
  size_t patched_size = 0;
  int status = TOLUA_BCCONV_OK;

  tolua_clearbytecodedebug();
  if (out_sz != NULL) *out_sz = 0;
  if (error_code != NULL) *error_code = TOLUA_BCCONV_OK;
  if (buff == NULL || sz <= 0) {
    tolua_failbytecode(TOLUA_BCCONV_ERR_INVALID_ARGS,
                       "invalid arguments: buff=%p, size=%d", buff, sz);
    if (error_code != NULL) *error_code = TOLUA_BCCONV_ERR_INVALID_ARGS;
    return NULL;
  }
  if (target_fr2 != 0) target_fr2 = 1;

  patched = (uint8_t *)malloc((size_t)sz);
  if (patched == NULL) {
    tolua_failbytecode(TOLUA_BCCONV_ERR_OUT_OF_MEMORY,
                       "failed to allocate %d bytes for converted bytecode", sz);
    if (error_code != NULL) *error_code = TOLUA_BCCONV_ERR_OUT_OF_MEMORY;
    return NULL;
  }

  memcpy(patched, buff, (size_t)sz);
  patched_size = (size_t)sz;
  status = tolua_convert_bytecode_inplace(&patched, &patched_size, target_fr2);

  if (status != TOLUA_BCCONV_OK) {
    if (tolua_last_bytecode_debug[0] == '\0') {
      tolua_failbytecode(status, "conversion aborted without detailed context");
    }
    if (error_code != NULL) *error_code = status;
    free(patched);
    return NULL;
  }

  tolua_clearbytecodedebug();
  if (out_sz != NULL) *out_sz = (int)patched_size;
  return (char *)patched;
}
#else
LUALIB_API char* tolua_convertbytecode(const char *buff, int sz, int target_fr2, int *out_sz)
{
  return tolua_convertbytecodeex(buff, sz, target_fr2, out_sz, NULL);
}

LUALIB_API char* tolua_convertbytecodeex(const char *buff, int sz, int target_fr2, int *out_sz, int *error_code)
{
  (void)buff;
  (void)sz;
  (void)target_fr2;
  tolua_clearbytecodedebug();
  tolua_setbytecodedebug("bytecode conversion failed (%s): bytecode conversion requires a LuaJIT build",
                         tolua_getbytecodeerrorstr(TOLUA_BCCONV_ERR_UNSUPPORTED_RUNTIME));
  if (out_sz != NULL) *out_sz = 0;
  if (error_code != NULL) *error_code = TOLUA_BCCONV_ERR_UNSUPPORTED_RUNTIME;
  return NULL;
}
#endif

LUALIB_API int tolua_loadbuffer(lua_State *L, const char *buff, int sz, const char *name)
{
    int status = luaL_loadbuffer(L, buff, (size_t)sz, name);
#if defined(LUAJIT_VERSION)
    if (status == LUA_ERRSYNTAX && buff != NULL && sz > 4 &&
        (uint8_t)buff[0] == TOLUA_BCDUMP_HEAD1 &&
        (uint8_t)buff[1] == TOLUA_BCDUMP_HEAD2 &&
        (uint8_t)buff[2] == TOLUA_BCDUMP_HEAD3) {
      int target_fr2 = LJ_FR2 ? 1 : 0;
      int conv_status = TOLUA_BCCONV_OK;
      int patched_sz = 0;
      const char *orig_error = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
      int trace_chunk = (name != NULL) &&
                        (strstr(name, "battle.lua") != NULL ||
                         strstr(name, "main.lua") != NULL ||
                         strstr(name, "migong.lua") != NULL);
      char *patched = NULL;

      if (!tolua_bytecode_build_logged) {
        tolua_emitlog("[tolua-bytecode] build=%s runtime_fr2=%d", tolua_bytecode_build_tag, target_fr2);
        tolua_bytecode_build_logged = 1;
      }
      patched = tolua_convertbytecodeex(buff, sz, target_fr2, &patched_sz, &conv_status);
      if (patched != NULL) {
        lua_pop(L, 1); /* Drop previous incompatible-bytecode error. */
        status = luaL_loadbuffer(L, patched, (size_t)patched_sz, name);
        if (trace_chunk) {
          tolua_emitlog("[tolua-bytecode] build=%s conv_ok name=%s src=%d out=%d",
                        tolua_bytecode_build_tag, name != NULL ? name : "<null>", sz, patched_sz);
        }
        free(patched);
      } else {
        const char *conv_detail = tolua_getlastbytecodedebug();
        const char *conv_name = tolua_getbytecodeerrorstr(conv_status);
        if (trace_chunk) {
          tolua_emitlog("[tolua-bytecode] build=%s conv_fail name=%s err=%s detail=%s",
                        tolua_bytecode_build_tag, name != NULL ? name : "<null>",
                        conv_name != NULL ? conv_name : "unknown",
                        (conv_detail != NULL && conv_detail[0] != '\0') ? conv_detail : "conversion failed");
        }
        if ((conv_detail != NULL && conv_detail[0] != '\0') || conv_status != TOLUA_BCCONV_OK) {
          const char *detail = (conv_detail != NULL && conv_detail[0] != '\0') ? conv_detail : "conversion failed";
          const char *err_name = (conv_name != NULL && conv_name[0] != '\0') ? conv_name : "unknown";
          if (orig_error != NULL && orig_error[0] != '\0') {
            lua_pushfstring(L, "%s\n[tolua-bytecode] %s (%s)", orig_error, detail, err_name);
          } else {
            lua_pushfstring(L, "[tolua-bytecode] %s (%s)", detail, err_name);
          }
          if (lua_gettop(L) >= 2) {
            lua_replace(L, -2); /* Replace original syntax error with richer context. */
          }
        }
      }
    }
#endif
    return status;
}

static int _lua_getfield(lua_State *L)
{
    const char *name = lua_tostring(L, 2);    
    lua_getfield(L, 1, name);    
    return 1;
}
        
LUA_API int tolua_getfield(lua_State *L, int idx, const char *field)
{
    idx = abs_index(L, idx);    
    lua_pushcfunction(L, _lua_getfield);
    lua_pushvalue(L, idx);
    lua_pushstring(L, field);
    return lua_pcall(L, 2, 1, 0);
}

static int _lua_setfield(lua_State *L)
{
    const char *name = lua_tostring(L, 2);
    lua_setfield(L, 1, name);
    return 0;
}

LUA_API int tolua_setfield(lua_State *L, int idx, const char *key)        
{
    int top = lua_gettop(L);
    idx = abs_index(L, idx);
    lua_pushcfunction(L, _lua_setfield);
    lua_pushvalue(L, idx);
    lua_pushstring(L, key);
    lua_pushvalue(L, top);
    lua_remove(L, top);
    return lua_pcall(L, 3, -1, 0);
}

static int _lua_gettable(lua_State *L)
{    
    lua_gettable(L, 1);    
    return 1;
}

LUA_API int tolua_gettable(lua_State *L, int idx)
{
    int top = lua_gettop(L);
    idx = abs_index(L, idx);
    lua_pushcfunction(L, _lua_gettable);
    lua_pushvalue(L, idx);
    lua_pushvalue(L, top);
    lua_remove(L, top);
    return lua_pcall(L, 2, -1, 0);
}

static int _lua_settable(lua_State *L)
{
    lua_settable(L, 1);
    return 0;
}

LUA_API int tolua_settable(lua_State *L, int idx)
{
    int top = lua_gettop(L);
    idx = abs_index(L, idx);
    lua_pushcfunction(L, _lua_settable);
    lua_pushvalue(L, idx);
    lua_pushvalue(L, top - 1);
    lua_pushvalue(L, top);
    lua_remove(L, top);
    lua_remove(L, top - 1);
    return lua_pcall(L, 3, -1, 0);
}

static int tolua_closure(lua_State *L)
{
    lua_CFunction fn = (lua_CFunction)lua_tocfunction(L, lua_upvalueindex(2));
    int r = fn(L);    
    
    if (lua_toboolean(L, lua_upvalueindex(1)))
    {
        lua_pushboolean(L, 0);
        lua_replace(L, lua_upvalueindex(1));
        return lua_error(L);
    }
    
    return r;
}

//hack for luac, 避免luac error破坏包裹c#函数的异常块(luajit采用的是类似c++异常)
LUA_API int tolua_pushcfunction(lua_State *L, lua_CFunction fn)
{        
    lua_pushboolean(L, 0);
    lua_pushcfunction(L, fn);
    lua_pushcclosure(L, tolua_closure, 2);
    return 0;
}

static int tolua_pusherror(lua_State *L, const char *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    luaL_where(L, 1);
    lua_pushvfstring(L, fmt, argp);
    va_end(argp);
    lua_concat(L, 2);
    return 1;
}

LUALIB_API int tolua_argerror(lua_State *L, int narg, const char *extramsg)
{
    lua_Debug ar;
    
    if (!lua_getstack(L, 0, &ar))  /* no stack frame? */
    {
        return tolua_pusherror(L, "bad argument #%d (%s)", narg, extramsg);        
    }

    lua_getinfo(L, "n", &ar);

    if (strcmp(ar.namewhat, "method") == 0) 
    {
        narg--;  /* do not count `self' */

        if (narg == 0)  /* error is in the self argument itself? */
        {
            return tolua_pusherror(L, "calling " LUA_QS " on bad self (%s)", ar.name, extramsg);
        }
    }

    if (ar.name == NULL)
    {
        ar.name = "?";
    }    

    return tolua_pusherror(L, "bad argument #%d to " LUA_QS " (%s)", narg, ar.name, extramsg);
}

LUALIB_API int tolua_error(lua_State *L, const char *msg)
{
    lua_pushboolean(L, 1);
    lua_replace(L, lua_upvalueindex(1));
    lua_pushstring(L, msg);
    return 1;
}

LUALIB_API int tolua_getn(lua_State *L, int i)
{
    return tolua_objlen(L, i);
}

LUALIB_API int tolua_strlen(const char *str)
{
    if (str == NULL)
    {
        return 0;
    }

    int len = (int)strlen(str);
    return len;
}

static bool _preload(lua_State *L)
{    
    lua_settop(L, 2); 
    lua_getmetatable(L, 1);
    lua_pushstring(L, ".name");             //stack: t key mt ".name"
    lua_rawget(L, -2);                      //stack: t key mt space

    if (!lua_isnil(L, -1))                  //stack: t key mt space
    {                      
        lua_getref(L, LUA_RIDX_PRELOAD);    //stack: t key mt space preload
        lua_pushvalue(L, -2);               //stack: t key mt space preload space
        lua_pushstring(L, ".");             //stack: t key mt space preload space.
        lua_pushvalue(L, 2);                //stack: t key mt space preload space.key
        lua_concat(L, 3);                   //stack: t key mt space preload key1
        lua_pushvalue(L, -1);               //stack: t key mt space preload key1 key1
        lua_rawget(L, -3);                  //stack: t key mt space preload key1 value

        if (!lua_isnil(L, -1)) 
        {      
            lua_pop(L, 1);                      //stack: t key mt space preload key1
            lua_getref(L, LUA_RIDX_REQUIRE);    //stack: t key mt space preload key1 require
            lua_pushvalue(L, -2);
            lua_call(L, 1, 1);                         
            return true;
        }        
    }

    lua_settop(L, 2); 
    return false;
}

static int class_index_event(lua_State *L)
{
	int t = lua_type(L, 1);

    if (t == LUA_TUSERDATA)
    {    	
        lua_getfenv(L,1);

        if (!lua_rawequal(L, -1, TOLUA_NOPEER))     // stack: t k env
        {
            while (lua_istable(L, -1))                       // stack: t k v mt 
            {      
                lua_pushvalue(L, 2); 
                lua_rawget(L, -2);
            
                if (!lua_isnil(L, -1))
                {                    
                    return 1;
                }

                lua_pop(L, 1);
                lua_pushlightuserdata(L, &gettag);          
                lua_rawget(L, -2);                      //stack: obj key env tget
            
                if (lua_istable(L, -1))
                {                    
                    lua_pushvalue(L, 2);                //stack: obj key env tget key
                    lua_rawget(L, -2);                  //stack: obj key env tget func 

                    if (lua_isfunction(L, -1))
                    {                        
                        lua_pushvalue(L, 1);
                        lua_call(L, 1, 1);
                        return 1;
                    }    

                    lua_pop(L, 1);                 
                }

                lua_pop(L, 1); 

                if (lua_getmetatable(L, -1) == 0)               // stack: t k v mt mt
                {
                    lua_pushnil(L);
                }

                lua_remove(L, -2);                              // stack: t k v mt
            }        
        };

        lua_settop(L,2);                                        				
    	lua_pushvalue(L, 1);						// stack: obj key obj	

    	while (lua_getmetatable(L, -1) != 0)
    	{        	
        	lua_remove(L, -2);						// stack: obj key mt

			if (lua_isnumber(L,2))                 	// check if key is a numeric value
			{		    
		    	lua_pushstring(L,".geti");
		    	lua_rawget(L,-2);                   // stack: obj key mt func

		    	if (lua_isfunction(L,-1))
		    	{
		        	lua_pushvalue(L,1);
		        	lua_pushvalue(L,2);
		        	lua_call(L,2,1);
		        	return 1;
		    	}
			}
			else
        	{
        		lua_pushvalue(L, 2);			    // stack: obj key mt key
        		lua_rawget(L, -2);					// stack: obj key mt value        

        		if (!lua_isnil(L, -1))
        		{
        	    	return 1;
        		}
                
                lua_pop(L, 1);
				lua_pushlightuserdata(L, &gettag);        	
        		lua_rawget(L, -2);					//stack: obj key mt tget

        		if (lua_istable(L, -1))
        		{
        	    	lua_pushvalue(L, 2);			//stack: obj key mt tget key
        	    	lua_rawget(L, -2);           	//stack: obj key mt tget value 

        	    	if (lua_isfunction(L, -1))
        	    	{
        	        	lua_pushvalue(L, 1);
        	        	lua_call(L, 1, 1);
        	        	return 1;
        	    	}                    
        		}
    		}

            lua_settop(L, 3);
        }

        lua_settop(L, 2);
        int *udata = (int*)lua_touserdata(L, 1);

        if (*udata == LUA_NULL_USERDATA)
        {
            return luaL_error(L, "attemp to index %s on a nil value", lua_tostring(L, 2));   
        }
        
        if (toluaflags & FLAG_INDEX_ERROR)
        {
            return luaL_error(L, "field or property %s does not exist", lua_tostring(L, 2));
        }        
    }
    else if(t == LUA_TTABLE)
    {
    	lua_pushvalue(L,1);                          //stack: obj key obj

		while (lua_getmetatable(L, -1) != 0)         //stack: obj key obj mt
    	{
        	lua_remove(L, -2);						// stack: obj key mt

        	lua_pushvalue(L, 2);			    	// stack: obj key mt key
        	lua_rawget(L, -2);						// stack: obj key mt value        

        	if (!lua_isnil(L, -1))
        	{
        		if (lua_isfunction(L, -1))			//cache static function
        		{
        			lua_pushvalue(L, 2);           // stack: obj key mt value key
        			lua_pushvalue(L, -2);          // stack: obj key mt value key value
        			lua_rawset(L, 1);
        		}

        	    return 1;
        	}        	
        	
            lua_pop(L, 1);        
			lua_pushlightuserdata(L, &gettag);        	
        	lua_rawget(L, -2);						//stack: obj key mt tget

        	if (lua_istable(L, -1))
        	{
        	    lua_pushvalue(L, 2);				//stack: obj key mt tget key
        	    lua_rawget(L, -2);           		//stack: obj key mt tget value 

        	    if (lua_isfunction(L, -1))
        	    {        	        	
                    lua_pushvalue(L, 1);
        	        lua_call(L, 1, 1);
        	        return 1;
        	    }
        	}
    		
        	lua_settop(L, 3);
    	}        
        
        if (_preload(L))
        {
            return 1;
        }          
        
        if (toluaflags & FLAG_INDEX_ERROR)
        {
            return luaL_error(L, "field or property %s does not exist", lua_tostring(L, 2));               
        }      
    }

    lua_pushnil(L);
    return 1;
}

static void storeatubox (lua_State *L, int lo)
{
    lua_getfenv(L, lo);                         // stack: t, k, v, _env

    if (lua_rawequal(L, -1, TOLUA_NOPEER)) 
    {
        lua_pop(L, 1);
        return;
        //lua_newtable(L);                        // stack: t, k, v, t
        //lua_pushvalue(L, -1);                   // stack: t, k, v, t, t        
        //lua_setfenv(L, lo);                     // stack: t, k, v, t
    };

    lua_insert(L, -3);
    lua_settable(L, -3);
    lua_pop(L, 1);
}

static int class_newindex_event(lua_State *L)
{
	int t = lua_type(L, 1);

	if (t == LUA_TUSERDATA)
    {
        bool useEnv = false;
        lua_getfenv(L, 1);

        if (!lua_rawequal(L, -1, TOLUA_NOPEER)) 
        {             
            useEnv = true;

            while (lua_istable(L, -1))                       // stack: t k v mt 
            {       
                lua_pushvalue(L, 2);                        // stack: t k v mt k
                lua_rawget(L, -2);                          // stack: t k v mt value        

                if (!lua_isnil(L, -1))
                {                    
                    lua_pop(L, 1);
                    lua_insert(L, -3);
                    lua_rawset(L, -3);
                    return 0;
                }

                lua_pop(L, 1);                
                lua_pushlightuserdata(L, &settag);              // stack: t k v mt tset
                lua_rawget(L, -2);                   

                if (lua_istable(L, -1)) 
                {
                    lua_pushvalue(L, 2);                         // stack: t k v mt tset k
                    lua_rawget(L, -2);                    

                    if (lua_isfunction(L, -1)) 
                    {                          
                        lua_pushvalue(L, 1); 
                        lua_pushvalue(L, 3); 
                        lua_call(L, 2, 0);
                        return 0;
                    }

                    lua_pop(L, 1);                              // stack: t k v mt tset 
                }  

                lua_pop(L, 1);                                  // stack: t k v mt

                if (lua_getmetatable(L, -1) == 0)               // stack: t k v mt mt
                {
                    lua_pushnil(L);
                }

                lua_remove(L, -2);                              // stack: t k v mt
            }              
        }

        lua_settop(L, 3);
    	lua_getmetatable(L,1);

    	while (lua_istable(L, -1))                			// stack: t k v mt
    	{        	
    		if (lua_isnumber(L, 2))
    		{
				lua_pushstring(L,".seti");
                lua_rawget(L,-2);                      		// stack: obj key mt func

                if (lua_isfunction(L,-1))
                {
                    lua_pushvalue(L,1);
                    lua_pushvalue(L,2);
                    lua_pushvalue(L,3);
                    lua_call(L,3,0);                    
                }

                return 0;
    		}
    		else
        	{
        		lua_pushlightuserdata(L, &settag);
        		lua_rawget(L, -2);                      	// stack: t k v mt tset

        		if (lua_istable(L, -1))
        		{
            		lua_pushvalue(L, 2);
            		lua_rawget(L, -2);                     	// stack: t k v mt tset func

            		if (lua_isfunction(L, -1))
            		{
                		lua_pushvalue(L, 1);
                		lua_pushvalue(L, 3);
                		lua_call(L, 2, 0);
                		return 0;
            		}

            		lua_pop(L, 1);                          // stack: t k v mt tset 
        		}

        		lua_pop(L, 1);                              // stack: t k v mt 

        		if (lua_getmetatable(L, -1) == 0)           // stack: t k v mt mt
        		{
            		lua_pushnil(L);
        		}

        		lua_remove(L, -2);                          // stack: t k v mt 
        	}
    	}

        lua_settop(L, 3);                                       // stack: t k v
        int* udata = (int*)lua_touserdata(L, 1);

        if (*udata == LUA_NULL_USERDATA)
        {
            return luaL_error(L, "attemp to index %s on a nil value", lua_tostring(L, 2));   
        }        

        if (useEnv) 
        {
            lua_getfenv(L, 1);                         // stack: t, k, v, _env
            lua_insert(L, -3);
            lua_settable(L, -3);
            lua_pop(L, 1);
            return 0;
        }                       
	}
	else if (t == LUA_TTABLE)
	{
		lua_getmetatable(L, 1);								// stack: t k v mt 

		while (lua_istable(L, -1))                			// stack: t k v mt 
    	{  		
			lua_pushlightuserdata(L, &settag);				// stack: t k v mt tset
        	lua_rawget(L, -2);       

        	if (lua_istable(L,-1)) 
        	{
            	lua_pushvalue(L,2);  						// stack: t k v mt tset k
            	lua_rawget(L,-2);

            	if (lua_isfunction(L,-1)) 
            	{  
                	lua_pushvalue(L,1); 
                	lua_pushvalue(L,3); 
                	lua_call(L,2,0);
                	return 0;
            	}

            	lua_pop(L, 1);                          	// stack: t k v mt tset 
        	}  

			lua_pop(L, 1);                              	// stack: t k v mt

        	if (lua_getmetatable(L, -1) == 0)           	// stack: t k v mt mt
        	{
            	lua_pushnil(L);
        	}

        	lua_remove(L, -2);                          	// stack: t k v mt
        }      
	}

    lua_settop(L, 3); 
    return luaL_error(L, "field or property %s does not exist", lua_tostring(L, 2));      
}

static int enum_index_event (lua_State *L)
{
	lua_getmetatable(L, 1);									//stack: t, k, mt

	if (lua_istable(L, -1))
	{
		lua_pushvalue(L, 2);								//stack: t, k, mt, k
		lua_rawget(L, -2);									//stack: t, k, mt, v

		if (!lua_isnil(L, -1))
		{		
			return 1;
		}

		lua_pop(L, 1);										//stack: t, k, mt
		lua_pushlightuserdata(L, &gettag);		
		lua_rawget(L, -2); 									//stack: t, k, mt, tget

		if (lua_istable(L,-1)) 
		{
            lua_pushvalue(L,2);  							//stack: t k mt tget k
            lua_rawget(L,-2);								//stack: t k mt tget v

           	if (lua_isfunction(L,-1)) 
			{  
				lua_call(L, 0, 1);					
				lua_pushvalue(L,2); 				
				lua_pushvalue(L,-2); 				
				lua_rawset(L, 3);		
				return 1;			
			}

			lua_pop(L, 1);
        }			
	}

	lua_settop(L, 2);
	lua_pushnil(L);
	return 1;	
}

static int enum_newindex_event(lua_State *L)
{	
	luaL_error(L, "the left-hand side of an assignment must be a variable, a property or an indexer");
    return 1;
}

static int static_index_event(lua_State *L)
{    
    lua_pushvalue(L, 2);                    //stack: t key key
    lua_rawget(L, 1);                       //stack: t key value        

    if (!lua_isnil(L, -1))
    {
        return 1;
    }

    lua_pop(L, 1);            
    lua_pushlightuserdata(L, &gettag);      //stack: t key tag    
    lua_rawget(L, 1);                       //stack: t key tget

    if (lua_istable(L, -1))
    {
        lua_pushvalue(L, 2);                //stack: obj key tget key
        lua_rawget(L, -2);                  //stack: obj key tget func 

        if (lua_isfunction(L, -1))
        {                       
            lua_call(L, 0, 1);
            return 1;
        }        
    }
    
    lua_settop(L, 2);

    if (_preload(L))
    {
        return 1;
    }
    
    if (toluaflags & FLAG_INDEX_ERROR)
    {
        luaL_error(L, "field or property %s does not exist", lua_tostring(L, 2));    
    }

    return 1;
}

static int static_newindex_event(lua_State *L)
{
    lua_pushlightuserdata(L, &settag);              //stack: t k v tag
    lua_rawget(L, 1);                               //stack: t k v tset

    if (lua_istable(L,-1)) 
    {
        lua_pushvalue(L, 2);                         //stack: t k v tset k
        lua_rawget(L, -2);                           //stack: t k v tset func

        if (lua_isfunction(L, -1))
        {    
            lua_pushvalue(L,1); 
            lua_pushvalue(L,3); 
            lua_call(L,2,0);
            return 0;
        }
    }

    lua_settop(L, 3); 
    luaL_error(L, "field or property %s does not exist", lua_tostring(L, 2));
    return 1;
}

static int vptr_index_event(lua_State *L)
{    
    lua_pushlightuserdata(L, &vptr);
    lua_rawget(L, 1);                                   // stack: t key u
    lua_replace(L, 1);                                  // stack: u key
    lua_pushvalue(L, 1);                                // stack: u key u

    while (lua_getmetatable(L, -1) != 0)
    {
        lua_remove(L, -2);                              // stack: u key mt
        lua_pushvalue(L, 2);                            // stack: u key mt key
        lua_rawget(L, -2);                              // stack: u key mt value        

        if (!lua_isnil(L, -1))
        {
            return 1;
        }
        
        lua_pop(L, 1);        
        lua_pushlightuserdata(L, &gettag);          
        lua_rawget(L, -2);                              //stack: u key mt tget

        if (lua_istable(L, -1))
        {
            lua_pushvalue(L, 2);                        //stack: obj key mt tget key
            lua_rawget(L, -2);                          //stack: obj key mt tget value 

            if (lua_isfunction(L, -1))
            {
                lua_pushvalue(L, 1);
                lua_call(L, 1, 1);
                return 1;
            }
        }

        lua_settop(L, 3);
    }

    lua_settop(L, 2);
    lua_pushnil(L);
    return 1;
}

static int vptr_newindex_event(lua_State *L)
{    
    lua_pushlightuserdata(L, &vptr);
    lua_rawget(L, 1);                                   // stack: t key v u        
    lua_getmetatable(L, -1);

    while (lua_istable(L, -1))                          // stack: u k v mt
    {           
        lua_pushlightuserdata(L, &settag);
        lua_rawget(L, -2);                              // stack: u k v mt tset

        if (lua_istable(L, -1))
        {
            lua_pushvalue(L, 2);
            lua_rawget(L, -2);                          // stack: u k v mt tset func

            if (lua_isfunction(L, -1))
            {
                lua_pushvalue(L, 4);
                lua_pushvalue(L, 3);
                lua_call(L, 2, 0);
                return 0;
            }

            lua_pop(L, 1);                              // stack: t k v mt tset 
        }

        lua_pop(L, 1);                                  // stack: t k v mt 

        if (lua_getmetatable(L, -1) == 0)               // stack: t k v mt mt
        {
            lua_pushnil(L);
        }

        lua_remove(L, -2);                              // stack: t k v mt             
    }

    lua_settop(L, 3);    
    return 1;
}

LUALIB_API bool tolua_isvptrtable(lua_State *L, int index)
{    
    lua_pushlightuserdata(L, &vptr);
    lua_rawget(L, index);
    bool flag = lua_isnil(L, -1) ? false : true;
    lua_pop(L, 1);
    return flag;
}

LUALIB_API void tolua_setindex(lua_State *L)
{
	lua_pushstring(L, "__index");
	lua_pushcfunction(L, class_index_event);
	lua_rawset(L, -3);
}

LUALIB_API void tolua_setnewindex(lua_State *L)
{
	lua_pushstring(L, "__newindex");
	lua_pushcfunction(L, class_newindex_event);
	lua_rawset(L, -3);
}

LUALIB_API bool tolua_pushudata(lua_State *L, int index)
{
	lua_getref(L, LUA_RIDX_UBOX);			// stack: ubox
	lua_rawgeti(L, -1, index); 				// stack: ubox, obj

	if (!lua_isnil(L, -1))
	{
		lua_remove(L, -2); 					// stack: obj
		return true;
	}

	lua_pop(L, 2);
	return false;
}

LUALIB_API void tolua_pushnewudata(lua_State *L, int metaRef, int index)
{
	lua_getref(L, LUA_RIDX_UBOX);
	tolua_newudata(L, index);
	lua_getref(L, metaRef);
	lua_setmetatable(L, -2);
	lua_pushvalue(L, -1);
	lua_rawseti(L, -3, index);
	lua_remove(L, -2);	
}

static int module_index_event(lua_State *L)
{    
    lua_pushvalue(L, 2);                    //stack: t key key
    lua_rawget(L, 1);                       //stack: t key value            

    if (!lua_isnil(L, -1))
    {
        return 1;
    }

    lua_pop(L, 1);                          //stack: t key 
    lua_pushstring(L, ".name");             //stack: t key ".name"
    lua_rawget(L, 1);        

    if (!lua_isnil(L, -1))                  //stack: t key space
    {                      
        lua_getref(L, LUA_RIDX_PRELOAD);    //stack: t key space preload
        lua_pushvalue(L, -2);
        lua_pushstring(L, ".");
        lua_pushvalue(L, 2);
        lua_concat(L, 3);                   //stack: t key space preload key
        lua_pushvalue(L, -1);               //stack: t key space preload key1 key1
        lua_rawget(L, -3);                  //stack: t key space preload key1 value        

        if (!lua_isnil(L, -1)) 
        {      
            lua_pop(L, 1);                      //stack: t key space preload key1
            lua_getref(L, LUA_RIDX_REQUIRE);
            lua_pushvalue(L, -2);
            lua_call(L, 1, 1);                    
        }
        else
        {
            lua_pushnil(L);                            
        }
    }
    
    return 1;
}

typedef struct stringbuffer 
{        
  const char *buffer;
  size_t len;
} stringbuffer;

static stringbuffer sb;

void initmodulebuffer()
{
    sb.len = 0;
    sb.buffer = NULL;
}

void pushmodule(lua_State *L, const char *str)
{    
    luaL_Buffer b;
    luaL_buffinit(L, &b);

    if (sb.len > 0)
    {
        luaL_addlstring(&b, sb.buffer, sb.len);
        luaL_addchar(&b, '.');
    }

    luaL_addstring(&b, str);
    luaL_pushresult(&b);    
    sb.buffer = lua_tolstring(L, -1, &sb.len);    
}

LUALIB_API bool tolua_beginmodule(lua_State *L, const char *name)
{
    if (name != NULL)
    {                
        lua_pushstring(L, name);			//stack key
        lua_rawget(L, -2);					//stack value

        if (lua_isnil(L, -1))  
        {
            lua_pop(L, 1);
            lua_newtable(L);				//stack table

            lua_pushstring(L, "__index");
            lua_pushcfunction(L, module_index_event);
            lua_rawset(L, -3);

            lua_pushstring(L, name);        //stack table name         
            lua_pushstring(L, ".name");     //stack table name ".name"            
            pushmodule(L, name);            //stack table name ".name" module            
            lua_rawset(L, -4);              //stack table name            
            lua_pushvalue(L, -2);			//stack table name table
            lua_rawset(L, -4);   			//stack table

            lua_pushvalue(L, -1);
            lua_setmetatable(L, -2);
            return true;
        }
        else if (lua_istable(L, -1))
        {
            if (lua_getmetatable(L, -1) == 0)
            {
                lua_pushstring(L, "__index");
                lua_pushcfunction(L, module_index_event);
                lua_rawset(L, -3);

                lua_pushstring(L, name);        //stack table name         
                lua_pushstring(L, ".name");     //stack table name ".name"            
                pushmodule(L, name);            //stack table name ".name" module            
                lua_rawset(L, -4);              //stack table name            
                lua_pushvalue(L, -2);           //stack table name table
                lua_rawset(L, -4);              //stack table

                lua_pushvalue(L, -1);
                lua_setmetatable(L, -2);                    
            }
            else
            {
                lua_pushstring(L, ".name");
                lua_gettable(L, -3);      
                sb.buffer = lua_tolstring(L, -1, &sb.len);                    
                lua_pop(L, 2);
            }

            return true;
        }

        return false;
    }
    else
    {                
        lua_pushvalue(L, LUA_GLOBALSINDEX);
        return true;
    }                
}

LUALIB_API void tolua_endmodule(lua_State *L)
{
    lua_pop(L, 1);
    int len = (int)sb.len;

    while(len-- >= 0)
    {
        if (sb.buffer[len] == '.')
        {
            sb.len = len;
            return;
        }
    }

    sb.len = 0;
}

static int class_new_event(lua_State *L)
{         
    if (!lua_istable(L, 1))
    {
        return luaL_typerror(L, 1, "table");        
    }

    int count = lua_gettop(L); 
    lua_pushvalue(L,1);  

    if (lua_getmetatable(L,-1))
    {   
        lua_remove(L,-2);                      
        lua_pushstring(L, "New");               
        lua_rawget(L,-2);    

        if (lua_isfunction(L,-1))
        {            
            for (int i = 2; i <= count; i++)
            {
                lua_pushvalue(L, i);                    
            }

            lua_call(L, count - 1, 1);
            return 1;
        }

        lua_settop(L,3);
    }    

    return luaL_error(L,"attempt to perform ctor operation failed");    
}

static void _pushfullname(lua_State *L, int pos)
{
    if (sb.len > 0)
    {
        lua_pushlstring(L, sb.buffer, sb.len);
        lua_pushstring(L, ".");
        lua_pushvalue(L,  pos < 0 ? pos - 2 : pos + 2);
        lua_concat(L, 3);
    }
    else
    {
        lua_pushvalue(L, pos);
    }
}

static void _addtoloaded(lua_State *L)
{
    lua_getref(L, LUA_RIDX_LOADED);
    _pushfullname(L, -3);
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

LUALIB_API int tolua_beginclass(lua_State *L, const char *name, int baseType, int ref)
{
    int reference = ref;
    lua_pushstring(L, name);                
    lua_newtable(L);      
    _addtoloaded(L);

    if (ref == LUA_REFNIL)        
    {
        lua_newtable(L);
        lua_pushvalue(L, -1);
        reference = luaL_ref(L, LUA_REGISTRYINDEX); 
    }
    else
    {
        lua_getref(L, reference);    
    }

    if (baseType != 0)
    {
        lua_getref(L, baseType);        
        lua_setmetatable(L, -2);
    }
           
    lua_pushlightuserdata(L, &tag);
    lua_pushnumber(L, 1);
    lua_rawset(L, -3);

    lua_pushstring(L, ".name");
    _pushfullname(L, -4);
    lua_rawset(L, -3);

    lua_pushstring(L, ".ref");
    lua_pushinteger(L, reference);
    lua_rawset(L, -3);

    lua_pushstring(L, "__call");
    lua_pushcfunction(L, class_new_event);
    lua_rawset(L, -3);

    tolua_setindex(L);
    tolua_setnewindex(L); 
    return reference;
}


LUALIB_API void tolua_endclass(lua_State *L)
{
	lua_setmetatable(L, -2);
    lua_rawset(L, -3);            
}

/*void settablename(lua_State *L, const char *label, const char *name)
{
    lua_pushstring(L, "name");
    char cname[128];
    int l1 = strlen(label);
    int l2 = strlen(name)    ;
    strncat(cname, label, 128 - l1);
    strncat(cname, name, 128 - l1 - l2);
    lua_pushstring(L, cname);
    lua_rawset(L, -3);
}*/

LUALIB_API int tolua_beginenum(lua_State *L, const char *name)
{
	lua_pushstring(L, name);                               
    lua_newtable(L);                                       
    _addtoloaded(L);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    int reference = luaL_ref(L, LUA_REGISTRYINDEX);            
    lua_pushlightuserdata(L, &tag);
    lua_pushnumber(L, 1);
    lua_rawset(L, -3);

    lua_pushstring(L, ".name");
    _pushfullname(L, -4);  
    lua_rawset(L, -3);

	lua_pushstring(L, "__index");
	lua_pushcfunction(L, enum_index_event);
	lua_rawset(L, -3);

	lua_pushstring(L, "__newindex");
	lua_pushcfunction(L, enum_newindex_event);
	lua_rawset(L, -3);	

	return reference;
}

LUALIB_API void tolua_endenum(lua_State *L)
{
	lua_setmetatable(L, -2);
    lua_rawset(L, -3);    
}

LUALIB_API void tolua_beginstaticclass(lua_State *L, const char *name)
{    
    lua_pushstring(L, name);  
    lua_newtable(L);
    _addtoloaded(L);
    lua_pushvalue(L, -1);

    lua_pushlightuserdata(L, &tag);
    lua_pushnumber(L, 1);
    lua_rawset(L, -3);

    lua_pushstring(L, ".name");
    _pushfullname(L, -4);
    lua_rawset(L, -3);

    lua_pushstring(L, "__index");
    lua_pushcfunction(L, static_index_event);
    lua_rawset(L, -3);

    lua_pushstring(L, "__newindex");
    lua_pushcfunction(L, static_newindex_event);
    lua_rawset(L, -3);      
}

LUALIB_API void tolua_endstaticclass(lua_State *L)
{
    lua_setmetatable(L, -2);
    lua_rawset(L, -3);    
}

LUALIB_API void tolua_constant(lua_State *L, const char *name, double value)
{
    lua_pushstring(L, name);
    lua_pushnumber(L, value);
    lua_rawset(L,-3);
}

LUALIB_API void tolua_function(lua_State *L, const char *name, lua_CFunction fn)
{
  	lua_pushstring(L, name);
    tolua_pushcfunction(L, fn);
  	lua_rawset(L, -3);

    /*lua_pushstring(L, name);
    lua_pushcfunction(L, fn);
    lua_rawset(L, -3);*/
}

LUALIB_API void tolua_variable(lua_State *L, const char *name, lua_CFunction get, lua_CFunction set)
{                
    lua_pushlightuserdata(L, &gettag);
    lua_rawget(L, -2);

    if (!lua_istable(L, -1))
    {
        /* create .get table, leaving it at the top */
        lua_pop(L, 1);
        lua_newtable(L);        
        lua_pushlightuserdata(L, &gettag);
        lua_pushvalue(L, -2);
        lua_rawset(L, -4);
    }

    lua_pushstring(L, name);
    //lua_pushcfunction(L, get);
    tolua_pushcfunction(L, get);
    lua_rawset(L, -3);                  /* store variable */
    lua_pop(L, 1);                      /* pop .get table */

    /* set func */
    if (set != NULL)
    {        
        lua_pushlightuserdata(L, &settag);
        lua_rawget(L, -2);

        if (!lua_istable(L, -1))
        {
            /* create .set table, leaving it at the top */
            lua_pop(L, 1);
            lua_newtable(L);            
            lua_pushlightuserdata(L, &settag);
            lua_pushvalue(L, -2);
            lua_rawset(L, -4);
        }

        lua_pushstring(L, name);
        //lua_pushcfunction(L, set);
        tolua_pushcfunction(L, set);
        lua_rawset(L, -3);                  /* store variable */
        lua_pop(L, 1);                      /* pop .set table */
    }
}

LUALIB_API int toluaL_ref(lua_State *L)
{
	int stackPos = abs_index(L, -1);	
	lua_getref(L, LUA_RIDX_FIXEDMAP);
	lua_pushvalue(L, stackPos);
	lua_rawget(L, -2);

	if (!lua_isnil(L, -1))
	{
		int ref = (int)lua_tointeger(L, -1);
		lua_pop(L, 3);
		return ref;
	}
	else
	{
		lua_pushvalue(L, stackPos);
		int ref = luaL_ref(L, LUA_REGISTRYINDEX);
		lua_pushvalue(L, stackPos);
		lua_pushinteger(L, ref);
		lua_rawset(L, -4);
		lua_pop(L, 3);
		return ref;
	}
}

LUALIB_API void toluaL_unref(lua_State *L, int reference)
{
	lua_getref(L, LUA_RIDX_FIXEDMAP);
	lua_getref(L, reference);
	lua_pushnil(L);
	lua_rawset(L, -3);
	luaL_unref(L, LUA_REGISTRYINDEX, reference);
	lua_pop(L, 1);
}

LUA_API lua_State* tolua_getmainstate(lua_State *L1)
{
	lua_rawgeti(L1, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
	lua_State *L = lua_tothread(L1, -1);
	lua_pop(L1, 1);
	return L;
}

LUA_API int tolua_getvaluetype(lua_State *L, int stackPos)
{
	stackPos = abs_index(L, stackPos);
	lua_getref(L, LUA_RIDX_CHECKVALUE);
	lua_pushvalue(L, stackPos);
	lua_call(L, 1, 1);
	int ret = (int)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return ret;
}

LUALIB_API bool tolua_createtable(lua_State *L, const char *path, int szhint)
{
	const char *e = NULL;
	lua_pushvalue(L, LUA_GLOBALSINDEX);						//stack _G

	do 
	{
	  e = strchr(path, '.');
	  if (e == NULL) e = path + strlen(path);
	  lua_pushlstring(L, path, e - path);					//stack _G key
	  lua_rawget(L, -2);									//stack _G value
	  int type = lua_type(L, -1);

	  if (type == LUA_TNIL) 
	  {  
	    lua_pop(L, 1); 										//stack _G
	    lua_createtable(L, 0, (*e == '.' ? 1 : szhint));	//stack _G table
	    lua_pushlstring(L, path, e - path);					//stack _G table name
	    lua_pushvalue(L, -2);								//stack _G table name table
	    lua_settable(L, -4);  								//stack _G table
	  }
	  else if (type != LUA_TTABLE) 
	  {  
	    lua_pop(L, 2);  
	    return false;  
	  }

	  lua_remove(L, -2);  									//stack table
	  path = e + 1;
	} while (*e == '.');

	return true;
}

LUALIB_API bool tolua_beginpremodule(lua_State *L, const char *path, int szhint)
{
    const char *e = NULL;
    const char *name = path;
    lua_pushvalue(L, LUA_GLOBALSINDEX);                     //stack _G

    do 
    {
        e = strchr(path, '.');
        if (e == NULL) e = path + strlen(path);
        lua_pushlstring(L, path, e - path);                   //stack t key
        lua_rawget(L, -2);                                    //stack t value
        int type = lua_type(L, -1);

        if (type == LUA_TNIL) 
        {
            lua_pop(L, 1);                                      //stack t
            lua_createtable(L, 0, (*e == '.' ? 1 : szhint));    //stack t table
            lua_pushlstring(L, path, e - path);                 //stack t table name
            lua_pushvalue(L, -2);                               //stack t table name table
            lua_settable(L, -4);                                //stack t table

            lua_pushstring(L, ".name");        
            pushmodule(L, name);
            lua_rawset(L, -3);               

            lua_pushstring(L, "__index");
            lua_pushcfunction(L, module_index_event);
            lua_rawset(L, -3);    
        }
        else if (type != LUA_TTABLE) 
        {  
            lua_pop(L, 1);  
            return false;  
        }

        lua_remove(L, -2);                                    //stack table
        path = e + 1;
    } while (*e == '.');

    lua_pushstring(L, ".name");
    lua_gettable(L, -2);      
    sb.buffer = lua_tolstring(L, -1, &sb.len);    
    lua_pop(L, 1);
    return true;
}

LUALIB_API bool tolua_endpremodule(lua_State *L, int ref)
{    
    lua_getref(L, ref);
    lua_pushstring(L, ".name");
    lua_rawget(L, -2);                

    if (!tolua_createtable(L, lua_tostring(L, -1), 0))
    {
        lua_pushnil(L);
    }
    
    sb.len = 0;
    return true;
}

LUALIB_API bool tolua_addpreload(lua_State *L, const char *path)
{    
    const char *e = NULL;
    const char *name = path;
    int top = lua_gettop(L);
    lua_pushvalue(L, LUA_GLOBALSINDEX);                         //stack _G

    do 
    {
        e = strchr(path, '.');
        if (e == NULL) e = path + strlen(path);
        lua_pushlstring(L, path, e - path);                     //stack t key
        lua_rawget(L, -2);                                      //stack t value
        int type = lua_type(L, -1);

        if (type == LUA_TNIL) 
        {
            lua_pop(L, 1);                                      //stack t
            lua_createtable(L, 0, 0);                           //stack t table
            lua_pushlstring(L, path, e - path);                 //stack t table name
            lua_pushvalue(L, -2);                               //stack t table name table
            lua_settable(L, -4);                                //stack t table

            lua_pushstring(L, ".name");        
            lua_pushstring(L, name);
            lua_rawset(L, -3);               

            lua_pushstring(L, "__index");
            lua_pushcfunction(L, module_index_event);
            lua_rawset(L, -3);    
        }
        else if (type != LUA_TTABLE) 
        {  
            lua_settop(L, top);
            return false;  
        }

        lua_remove(L, -2);                                      //stack table
        path = e + 1;
    } while (*e == '.');

    lua_settop(L, top);
    return true;
}

LUALIB_API int tolua_getclassref(lua_State *L, int pos)
{    
    lua_getmetatable(L, pos);           //mt
    lua_pushstring(L, ".ref");          //mt .ref
    lua_rawget(L, -2);                  //mt ref
    int ref = (int)lua_tointeger(L, -1);         
    return ref;
}

LUALIB_API bool tolua_pushluatable(lua_State *L, const char *path)
{
	const char *e = NULL;
	lua_pushvalue(L, LUA_GLOBALSINDEX);	

	do 
	{
	  e = strchr(path, '.');
	  if (e == NULL) e = path + strlen(path);
	  lua_pushlstring(L, path, e - path);
	  lua_rawget(L, -2);

	  if (!lua_istable(L, -1))
	  {  
	    lua_pop(L, 2); 
	    return false;
	  }

	  lua_remove(L, -2);  
	  path = e + 1;
	} while (*e == '.');

	return true;
}

LUALIB_API const char* tolua_typename(lua_State *L, int lo)
{
    int tag = lua_type(L,lo);
    
    if (tag == LUA_TNONE)
    {
		lua_pushstring(L,"[no object]");
    }
    else if (tag != LUA_TUSERDATA && tag != LUA_TTABLE)
    {
        lua_pushstring(L, lua_typename(L,tag));
    }
    else if (tag == LUA_TUSERDATA)
    {
        if (!lua_getmetatable(L,lo))
        {
            lua_pushstring(L, lua_typename(L,tag));
        }
        else
        {
            lua_pushstring(L, ".name");
            lua_rawget(L, -2);

            if (!lua_isstring(L,-1))
            {
                lua_pop(L,1);
                lua_pushstring(L,"[undefined]");
            }
        }
    }
    else  //is table
    {
        lua_pushvalue(L,lo);        

        if (!lua_getmetatable(L,lo))
        {
            lua_pop(L,1);
            lua_pushstring(L,"table");
        }
        else
        {
            lua_pushstring(L, ".name");
            lua_rawget(L, -2);

            lua_pushstring(L,"class ");
            lua_insert(L,-2);
            lua_concat(L,2);
        }
    }

    return lua_tostring(L, -1);
}

LUALIB_API int tolua_getmetatableref(lua_State *L, int pos)
{
	int ref = LUA_REFNIL;	

    if (lua_getmetatable(L, pos) != 0)
    {
        lua_pushstring(L, ".ref");
        lua_rawget(L, -2);	

        if (lua_isnumber(L, -1))
        {
            ref = (int)lua_tointeger(L, -1);
        }

        lua_pop(L, 2);
    }

    return ref;
}

static lua_State* getthread(lua_State *L, int *arg) 
{
    if (lua_isthread(L, 1)) 
    {
        *arg = 1;
        return lua_tothread(L, 1);
    }
    else 
    {
        *arg = 0;
        return L;  
    }
}

static int traceback(lua_State *L) 
{
    int arg;
    lua_State *L1 = getthread(L, &arg);
    const char *msg = lua_tostring(L, arg + 1);

    if (msg == NULL && !lua_isnoneornil(L, arg + 1))  
    {
        lua_pushvalue(L, arg + 1);  
    }
    else 
    {
        if (NULL != strstr(msg, "stack traceback:"))
        {
            lua_pushvalue(L, arg + 1);
            return 1;
        }

        int level = (int)luaL_optinteger(L, arg + 2, (L == L1) ? 1 : 0);
#ifdef LUAJIT_VERSION            
        luaL_traceback(L, L1, msg, level);
#else        
        lua_getref(L, LUA_RIDX_TRACEBACK);
        lua_pushthread(L1);
        lua_pushvalue(L, arg + 1);
        lua_pushnumber(L, level + 1);
        lua_call(L, 3, 1);   
#endif            
    }     

    return 1;
}

LUALIB_API int tolua_beginpcall(lua_State *L, int reference)
{	
    lua_getref(L, LUA_RIDX_CUSTOMTRACEBACK);
	int top = lua_gettop(L);
	lua_getref(L, reference);
	return top;
}

LUALIB_API void tolua_pushtraceback(lua_State *L)
{
	lua_getref(L, LUA_RIDX_CUSTOMTRACEBACK);
}

/*static const int sentinel_ = 0;
#define sentinel ((void *)&sentinel_)

static int _require(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);    
    lua_settop(L, 1);  
    const char *key = luaL_gsub(L, name, "/", ".");            
    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    lua_getfield(L, 3, key);

    if (lua_toboolean(L, -1)) 
    {  
        if (lua_touserdata(L, -1) == sentinel)  
        {
            luaL_error(L, "loop or previous error loading module " LUA_QS, name);
        }

        return 1;  //package is already loaded
    }

    // else must load it; iterate over available loaders
    lua_getfield(L, LUA_ENVIRONINDEX, "loaders");

    if (!lua_istable(L, -1))
    {
        luaL_error(L, LUA_QL("package.loaders") " must be a table");
    }

    lua_pushliteral(L, "");  //error message accumulator

    for(int i = 1; ; i++) 
    {
        lua_rawgeti(L, -2, i);  // get a loader

        if (lua_isnil(L, -1))
        {
            luaL_error(L, "module " LUA_QS " not found:%s", name, lua_tostring(L, -2));
        }

        lua_pushstring(L, name);
        lua_call(L, 1, 1);  

        if (lua_isfunction(L, -1))  // did it find module?
        {
            break;  // module loaded successfully
        }
        else if (lua_isstring(L, -1))  // loader returned error message?
        {
            lua_concat(L, 2);  // accumulate it
        }
        else
        {
            lua_pop(L, 1);            
        }    
    }

    lua_pushlightuserdata(L, sentinel);
    lua_setfield(L, 3, key);   // _LOADED[name] = sentinel
    lua_pushstring(L, name);    // pass name as argument to module
    lua_call(L, 1, 1);          // run loaded module

    if (!lua_isnil(L, -1))  // non-nil return? 
    {
        lua_setfield(L, 3, key);  // _LOADED[name] = returned value
    }

    lua_getfield(L, 3, key);

    if (lua_touserdata(L, -1) == sentinel) 
    {   // module did not set a value? 
        lua_pushboolean(L, 1);          // use true as result
        lua_pushvalue(L, -1);           // extra copy to be returned
        lua_setfield(L, 3, key);        // _LOADED[name] = true 
    }

    return 1;
}

void tolua_openrequire(lua_State *L)
{    
    lua_getglobal(L, "require");
    lua_pushcfunction(L, _require);
    lua_getfenv(L, -2);
    lua_setfenv(L, -2);
    lua_setglobal(L, "require");
    lua_pop(L, 1);
}*/

LUALIB_API int tolua_require(lua_State *L, const char *fileName)
{
    int top = lua_gettop(L);
    lua_getref(L, LUA_RIDX_CUSTOMTRACEBACK);
    lua_getref(L, LUA_RIDX_REQUIRE);
    lua_pushstring(L, fileName);        
    int ret = lua_pcall(L, 1, -1, top + 1);
    lua_remove(L, top + 1);    
    return ret;
}


/*LUALIB_API bool tolua_checkluaslot(lua_State *L, int stackPos, int *func, int *table)
{
    lua_pushstring(L, "__call");      
    lua_rawget(L, stackPos);

    if (lua_isfunction(L, -1))
    {
        *func = toluaL_ref(L);        
    }
    else
    {
        lua_pop(L, 1);
        return false;
    }

    lua_pushvalue(L, stackPos);
    *table = toluaL_ref(L);    
    return true;
}*/

/*static int do_operator (lua_State *L, const char *op)
{
    if (lua_isuserdata(L,1))
    {        
        lua_pushvalue(L,1);  

        while (lua_getmetatable(L,-1))
        {   
            lua_remove(L,-2);                      
            lua_pushstring(L,op);               
            lua_rawget(L,-2);    

            if (lua_isfunction(L,-1))
            {
                lua_pushvalue(L,1);
                lua_pushvalue(L,2);
                lua_call(L,2,1);
                return 1;
            }

            lua_settop(L,3);
        }
    }

    luaL_error(L,"attempt to perform operation %s failed", op);
    return 0;
}

static int class_add_event (lua_State *L)
{
    return do_operator(L,"op_Addition");
}

static int class_sub_event (lua_State *L)
{
    return do_operator(L,"op_Subtraction");
}

static int class_mul_event (lua_State *L)
{
    return do_operator(L,"op_Multiply");
}

static int class_div_event (lua_State *L)
{
    return do_operator(L,"op_Division");
}

static int class_equals_event (lua_State *L)
{
    return do_operator(L,"op_Equality");
}*/


#ifdef _WIN32
double tolua_timegettime()
{
	FILETIME ft;
	double t;
	GetSystemTimeAsFileTime(&ft);
	/* Windows file time (time since January 1, 1601 (UTC)) */
	t = ft.dwLowDateTime / 1.0e7 + ft.dwHighDateTime*(4294967296.0 / 1.0e7);
	/* convert to Unix Epoch time (time since January 1, 1970 (UTC)) */
	return (t - 11644473600.0);
}
#else
double tolua_timegettime()
{
	struct timeval v;
	gettimeofday(&v, (struct timezone *) NULL);
	/* Unix Epoch time (time since January 1, 1970 (UTC)) */
	return v.tv_sec + v.tv_usec / 1.0e6;
}
#endif

static int tolua_gettime(lua_State *L)
{
	lua_pushnumber(L, tolua_timegettime());
	return 1;
}

static int tolua_bnd_setpeer(lua_State *L) 
{
    // stack: userdata, table
    if (!lua_isuserdata(L, -2)) 
    {
        return luaL_error(L, "Invalid argument #1 to setpeer: userdata expected.");        
    }

    if (lua_isnil(L, 2)) 
    {
        lua_pop(L, 1);
        lua_pushvalue(L, TOLUA_NOPEER);
        lua_setfenv(L, -2);
    }        
    else
    {
        lua_pushvalue(L, 2);                //stack: u p p
        lua_setfenv(L, -3);                 //stack: u p
        lua_newtable(L);                    //stack: u p vt        
        
        lua_pushlightuserdata(L, &vptr);    
        lua_pushvalue(L, 1);
        lua_rawset(L, -3);
        //lua_pushvalue(L, 1);
        //lua_rawseti(L, -2, 1);

        lua_getref(L, LUA_RIDX_VPTR);       //stack: u p vt mt
        lua_setmetatable(L, -2);            //stack: u p vt

        lua_pushstring(L, "base");          //stack: u p vt "base"
        lua_pushvalue(L, -2);               //stack: u p vt "base" vt
        lua_rawset(L, 2);                   //stack: u p vt    
        lua_pop(L, 1);
    }

    return 0;
};

static int tolua_bnd_getpeer(lua_State *L) 
{    
    lua_getfenv(L, -1);

    if (lua_rawequal(L, -1, TOLUA_NOPEER)) 
    {
        lua_pop(L, 1);
        lua_pushnil(L);
    };

    return 1;
};

static int tolua_bnd_getfunction(lua_State *L)
{
    lua_pushvalue(L, 1);                            // stack: obj key obj

    while (lua_getmetatable(L, -1) != 0)            // stack: obj key mt
    {            
        lua_remove(L, -2);                          // stack: obj key mt

        lua_pushvalue(L, 2);                        // stack: obj key mt key
        lua_rawget(L, -2);                          // stack: obj key mt value        

        if (!lua_isfunction(L, -1))
        {
            return 1;
        }
        else
        {
            lua_pop(L, 1);
        }
        
        lua_pushlightuserdata(L, &gettag);          
        lua_rawget(L, -2);                          //stack: obj key mt tget

        if (lua_istable(L, -1))
        {
            lua_pushvalue(L, 2);                    //stack: obj key mt tget key
            lua_rawget(L, -2);                      //stack: obj key mt tget value 

            if (lua_isfunction(L, -1))
            {                                           
                return 1;
            }
        }
            
        lua_settop(L, 3);                           //stack: obj key mt
    }

    lua_settop(L, 2);
    lua_pushnil(L);
    return 1;
}

static int tolua_bnd_type (lua_State *L)
{
    tolua_typename(L,lua_gettop(L));
    return 1;
}

static int tolua_initgettable(lua_State *L)
{
    if (!lua_istable(L, 1))
    {        
        return luaL_typerror(L, 1, "table");         
    }

    lua_newtable(L);
    lua_pushlightuserdata(L, &gettag);
    lua_pushvalue(L, -2);    
    lua_rawset(L, 1);
    return 1;
}

static int tolua_initsettable(lua_State *L)
{
    if (!lua_istable(L, 1))
    {        
        return luaL_typerror(L, 1, "table");         
    }

    lua_newtable(L);
    lua_pushlightuserdata(L, &settag);    
    lua_pushvalue(L, -2);    
    lua_rawset(L, 1);
    return 1;
}

void tolua_openvptr(lua_State *L)
{
    lua_newtable(L);        

    lua_pushstring(L, "__index");
    lua_pushcfunction(L, vptr_index_event);
    lua_rawset(L, -3);  

    lua_pushstring(L, "__newindex");
    lua_pushcfunction(L, vptr_newindex_event);
    lua_rawset(L, -3);  

    lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_VPTR); 
}

static const struct luaL_Reg tolua_funcs[] = 
{
	{ "gettime", tolua_gettime },
	{ "typename", tolua_bnd_type },
	{ "setpeer", tolua_bnd_setpeer},
	{ "getpeer", tolua_bnd_getpeer},
    { "getfunction", tolua_bnd_getfunction},
    { "initset", tolua_initsettable},
    { "initget", tolua_initgettable},
    { "int64", tolua_newint64},        
    { "uint64", tolua_newuint64},
    { "traceback", traceback},
	{ NULL, NULL }
};

void tolua_setluabaseridx(lua_State *L)
{    
	for (int i = 1; i <= 64; i++)
	{
		lua_pushinteger(L, i);
		lua_rawseti(L, LUA_REGISTRYINDEX, i);
	}

    //同lua5.1.5之后版本放入mainstate和_G
	lua_pushthread(L);
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);

	lua_pushvalue(L, LUA_GLOBALSINDEX);
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);

    //cache require函数
    lua_getglobal(L, "require");
    lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_REQUIRE);      
}

void tolua_openpreload(lua_State *L)
{
    lua_getglobal(L, "package");
    lua_pushstring(L, "preload");
    lua_rawget(L, -2);
    lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_PRELOAD);    
    lua_pushstring(L, "loaded");
    lua_rawget(L, -2);
    lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_LOADED);    
    lua_pop(L, 1);
}

void tolua_opentraceback(lua_State *L)
{
    lua_getglobal(L, "debug");
    lua_pushstring(L, "traceback");
    lua_rawget(L, -2);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_GLOBALSINDEX, "traceback");            
    lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_TRACEBACK);
    lua_pop(L, 1);    

    lua_pushcfunction(L, traceback);
    lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_CUSTOMTRACEBACK);    
}

void tolua_openubox(lua_State *L)
{
	lua_newtable(L);
	lua_newtable(L);            
	lua_pushstring(L, "__mode");
	lua_pushstring(L, "v");
	lua_rawset(L, -3);
	lua_setmetatable(L, -2);            
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_UBOX);	
}

void tolua_openfixedmap(lua_State *L)
{
	lua_newtable(L);
	//lua_newtable(L);
	//lua_pushstring(L, "__mode");
	//lua_pushstring(L, "k");
	//lua_rawset(L, -3);
	//lua_setmetatable(L, -2);    	
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_FIXEDMAP);		
}

/* Cache optional Lua-side value type checker in the registry. */
void tolua_openvaluetype(lua_State *L)
{
	lua_getglobal(L, "GetLuaValueType");
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_CHECKVALUE);	
}

void tolua_openluavec3(lua_State *L)
{    
	lua_getglobal(L, "Vector3");

    if (!lua_istable(L, 1))
    {        
        luaL_error(L, "Vector3 does not exist or not be loaded");
        return;
    }

	lua_pushstring(L, "New");
	lua_rawget(L, -2);
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_PACKVEC3);	
	lua_pushstring(L, "Get");
	lua_rawget(L, -2);
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_UNPACKVEC3);	
	lua_pop(L, 1);
}

void tolua_openluavec2(lua_State *L)
{    
	lua_getglobal(L, "Vector2");

    if (!lua_istable(L, 1))
    {        
        luaL_error(L, "Vector2 does not exist or not be loaded");
        return;
    }

	lua_pushstring(L, "New");
	lua_rawget(L, -2);
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_PACKVEC2);	
	lua_pushstring(L, "Get");
	lua_rawget(L, -2);
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_UNPACKVEC2);	
	lua_pop(L, 1);
}

void tolua_openluavec4(lua_State *L)
{    
	lua_getglobal(L, "Vector4");

    if (!lua_istable(L, 1))
    {        
        luaL_error(L, "Vector4 does not exist or not be loaded");
        return;
    }

	lua_pushstring(L, "New");
	lua_rawget(L, -2);
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_PACKVEC4);	
	lua_pushstring(L, "Get");
	lua_rawget(L, -2);
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_UNPACKVEC4);	
	lua_pop(L, 1);
}

void tolua_openluaclr(lua_State *L)
{    
	lua_getglobal(L, "Color");

    if (!lua_istable(L, 1))
    {        
        luaL_error(L, "Color does not exist or not be loaded");
        return;
    }

	lua_pushstring(L, "New");
	lua_rawget(L, -2);
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_PACKCLR);	
	lua_pushstring(L, "Get");
	lua_rawget(L, -2);
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_UNPACKCLR);	
	lua_pop(L, 1);
}

void tolua_openluaquat(lua_State *L)
{    
	lua_getglobal(L, "Quaternion");

    if (!lua_istable(L, 1))
    {        
        luaL_error(L, "Quaternion does not exist or not be loaded");
        return;
    }

	lua_pushstring(L, "New");
	lua_rawget(L, -2);
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_PACKQUAT);	
	lua_pushstring(L, "Get");
	lua_rawget(L, -2);
	lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_UNPACKQUAT);		
	lua_pop(L, 1);
}

void tolua_openlualayermask(lua_State *L)
{    
    lua_getglobal(L, "LayerMask");   

    if (!lua_istable(L, 1))
    {        
        luaL_error(L, "LayerMask does not exist or not be loaded");
        return;
    }

    lua_pushstring(L, "New");
    lua_rawget(L, -2);
    lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_PACKLAYERMASK);   
    lua_pushstring(L, "Get");
    lua_rawget(L, -2);
    lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_UNPACKLAYERMASK); 
    lua_pop(L, 1);
}

void tolua_openupdate(lua_State *L)
{
    lua_getglobal(L, "Update");

    if (!lua_isfunction(L, 1))
    {
        luaL_error(L, "Update function does not exist or not be loaded");
        return;
    }

    lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_UPDATE);    
    lua_getglobal(L, "LateUpdate");

    if (!lua_isfunction(L, 1))
    {
        luaL_error(L, "LateUpdate function does not exist or not be loaded");
        return;
    }

    lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_LATEUPDATE);  
    lua_getglobal(L, "FixedUpdate");

    if (!lua_isfunction(L, 1))
    {
        luaL_error(L, "FixedUpdate function does not exist or not be loaded");
        return;
    }

    lua_rawseti(L, LUA_REGISTRYINDEX, LUA_RIDX_FIXEDUPDATE);      
}


static int _openlualibs(lua_State *L)
{
    tolua_openvaluetype(L);
    tolua_openluavec3(L);
    tolua_openluavec2(L);
    tolua_openluavec4(L);
    tolua_openluaclr(L);
    tolua_openluaquat(L);   
    tolua_openlualayermask(L);
    tolua_openupdate(L);
    return 0;
}

LUALIB_API int tolua_openlualibs(lua_State *L)
{    
    lua_pushcfunction(L, _openlualibs);
    return lua_pcall(L, 0, -1, 0);    
}

static int mathf_ispoweroftwo(lua_State *L)
{
    lua_Integer mask = luaL_checkinteger(L, 1);
    bool flag = (mask & (mask-1)) == 0 ? true : false;
    lua_pushboolean(L, flag);
    return 1;
}

lua_Integer NextPowerOfTwo(lua_Integer v)
{
    v -= 1;
    v |= v >> 16;
    v |= v >> 8;
    v |= v >> 4;
    v |= v >> 2;
    v |= v >> 1;
    return v + 1;
}

static int mathf_nextpoweroftwo(lua_State *L)
{
    lua_Integer v = luaL_checkinteger(L, 1);
    v = NextPowerOfTwo(v);
    lua_pushnumber(L, v);
    return 1;
}

static int mathf_closestpoweroftwo(lua_State *L)
{
    lua_Integer v = luaL_checkinteger(L, 1);
    lua_Integer nextPower = NextPowerOfTwo (v);
    lua_Integer prevPower = nextPower >> 1;    

    if (v - prevPower < nextPower - v)
    {
        lua_pushnumber(L, prevPower);        
    }
    else
    {    
        lua_pushnumber(L, nextPower);
    }

    return 1;
}

static int mathf_gammatolinearspace(lua_State *L)
{
    lua_Number value = luaL_checknumber(L, 1);

    if (value <= 0.04045f)
    {
        value /= 12.92f;
    }
    else if (value < 1.0f)
    {    
        value = pow((value + 0.055f)/1.055f, 2.4f);
    }
    else
    {
        value = pow(value, 2.4f);
    }

    lua_pushnumber(L, value);
    return 1;    
}

static int mathf_lineartogammaspace (lua_State *L)
{
    lua_Number value = luaL_checknumber(L, 1);

    if (value <= 0.0f)
    {    
        value = 0;
    }
    else if (value <= 0.0031308f)
    {
        value *= 12.92f;
    }
    else if (value <= 1.0f)
    {
        value = 1.055f * powf(value, 0.41666f) - 0.055f;
    }
    else
    {
        value = powf(value, 0.41666f);
    }

    lua_pushnumber(L, value);
    return 1;
}

static int mathf_normalize(lua_State *L)
{
    float x = (float)lua_tonumber(L, 1);
    float y = (float)lua_tonumber(L, 2);
    float z = (float)lua_tonumber(L, 3);

    float len = sqrt(x * x + y * y + z * z);
    
    if (len == 1)
    {
        lua_pushnumber(L, x);
        lua_pushnumber(L, y);
        lua_pushnumber(L, z);
    }
    else if (len > 1e-5)
    {
        lua_pushnumber(L, x);
        lua_pushnumber(L, y);
        lua_pushnumber(L, z);
    }
    else    
    {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
    }

    return 3;
}

static const struct luaL_Reg tolua_mathf[] = 
{
    { "NextPowerOfTwo", mathf_nextpoweroftwo },
    { "ClosestPowerOfTwo", mathf_closestpoweroftwo },
    { "IsPowerOfTwo", mathf_ispoweroftwo},
    { "GammaToLinearSpace", mathf_gammatolinearspace},
    { "LinearToGammaSpace", mathf_lineartogammaspace},
    { "Normalize", mathf_normalize},
    { NULL, NULL }
};

LUALIB_API void tolua_openlibs(lua_State *L)
{   
    initmodulebuffer();
    luaL_openlibs(L);   
    int top = lua_gettop(L);    

    tolua_setluabaseridx(L);    
    tolua_opentraceback(L);
    tolua_openpreload(L);
    tolua_openubox(L);
    tolua_openfixedmap(L);    
    tolua_openint64(L);
    tolua_openuint64(L);
    tolua_openvptr(L);    
    //tolua_openrequire(L);

    luaL_register(L, "Mathf", tolua_mathf);     
    luaL_register(L, "tolua", tolua_funcs);    

    lua_getglobal(L, "tolua");

    lua_pushstring(L, "gettag");
    lua_pushlightuserdata(L, &gettag);
    lua_rawset(L, -3);

    lua_pushstring(L, "settag");
    lua_pushlightuserdata(L, &settag);
    lua_rawset(L, -3);

    lua_pushstring(L, "version");
    lua_pushstring(L, "1.0.7");
    lua_rawset(L, -3);

    lua_settop(L,top);
}

LUALIB_API void tolua_setflag(int bit, bool flag)
{
    if (flag)
    {
        toluaflags |= bit;    
    }
    else
    {
        toluaflags &= ~bit;    
    }
}

LUALIB_API bool tolua_getflag(int bit)
{
    return toluaflags & bit ? true : false;
}

static luaL_Buffer lua_buffer[3];
static int _bufferIndex = 0;

LUALIB_API luaL_Buffer* tolua_buffinit(lua_State *L)
{
    luaL_Buffer* buffer = &lua_buffer[_bufferIndex & 3];
    luaL_buffinit(L, buffer);
    ++_bufferIndex;
    return buffer;
}

LUALIB_API void tolua_addlstring(luaL_Buffer *b, const char *s, int l)
{
    luaL_addlstring(b, s, (size_t)l);
}

LUALIB_API void tolua_addstring(luaL_Buffer *b, const char *s)
{
    luaL_addstring(b, s);
}

LUALIB_API void tolua_pushresult(luaL_Buffer *b)
{
    luaL_pushresult(b);
}

LUALIB_API void tolua_addchar(luaL_Buffer *b, char c)
{
    luaL_addchar(b, c);
}

LUALIB_API int tolua_update(lua_State *L, float deltaTime, float unscaledTime)
{
    int top = tolua_beginpcall(L, LUA_RIDX_UPDATE);
    lua_pushnumber(L, deltaTime);
    lua_pushnumber(L, unscaledTime);
    return lua_pcall(L, 2, -1, top);
}

LUALIB_API int tolua_lateupdate(lua_State *L)
{
    int top = tolua_beginpcall(L, LUA_RIDX_LATEUPDATE);
    return lua_pcall(L, 0, -1, top);
}

LUALIB_API int tolua_fixedupdate(lua_State *L, float fixedTime)
{
    int top = tolua_beginpcall(L, LUA_RIDX_FIXEDUPDATE);
    lua_pushnumber(L, fixedTime);
    return lua_pcall(L, 1, -1, top);
}

static int index_op_this(lua_State *L)
{
    lua_pushvalue(L, 2);                                //stack: t, k, k
    lua_rawget(L, -2);                                  //stack: t, k, v

    if (!lua_isnil(L, -1))
    {       
        lua_pushlightuserdata(L, &vptr);               // stack: t, k, v, vptr
        lua_rawget(L, 1);                              // stack: t, k, v, u
        lua_replace(L, 1);                             // stack: u, k, v
    }    

    return 1;
}

static int newindex_op_this(lua_State *L)
{
    lua_pushvalue(L, 2);                                //stack: t, k, v, k
    lua_rawget(L, 1);                                   //stack: t, k, v, f

    if (!lua_isnil(L, -1))
    {       
        lua_pushlightuserdata(L, &vptr);               // stack: t, k, v, f, vptr
        lua_rawget(L, 1);                              // stack: t, k, v, f, u
        lua_replace(L, 1);                             // stack: u, k, v, f
    }    

    return 1;
}

LUALIB_API void tolua_regthis(lua_State *L, lua_CFunction get, lua_CFunction set)
{
    lua_newtable(L);                        //u t
    
    lua_pushlightuserdata(L, &vptr);    
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);

    if (get != NULL)
    {
        lua_pushstring(L, "get");    
        tolua_pushcfunction(L, get);
        lua_rawset(L, -3);                  
    }

    if (set != NULL)
    {
        lua_pushstring(L, "set");    
        tolua_pushcfunction(L, set);
        lua_rawset(L, -3);          
    }    

    lua_pushstring(L, "__index");
    lua_pushcfunction(L, index_op_this);
    lua_rawset(L, -3);  

    lua_pushstring(L, "__newindex");
    lua_pushcfunction(L, newindex_op_this);
    lua_rawset(L, -3);          
}

LUALIB_API int tolua_where (lua_State *L, int level) 
{
    lua_Debug ar;

    if (lua_getstack(L, level, &ar)) 
    {  
        lua_getinfo(L, "Sl", &ar);  

        if (ar.currentline > 0) 
        {              
            lua_pushstring(L, ar.source);
            return ar.currentline;
        }
    }

    lua_pushliteral(L, "");
    return -1;
}

/*---------------------------luanet compatibility exports--------------------------------*/
LUALIB_API void* luanet_gettag() { return tolua_tag(); }
LUALIB_API int luanet_tonetobject(lua_State *L, int index) { return tolua_rawnetobj(L, index); }
LUALIB_API void luanet_newudata(lua_State *L, int val) { tolua_newudata(L, val); }
LUALIB_API int luanet_rawnetobj(lua_State *L, int index) { return tolua_rawnetobj(L, index); }

LUALIB_API int luanet_checkudata(lua_State *L, int index, const char *type) { 
    int* udata = (int*)lua_touserdata(L, index);
    if (udata != NULL && lua_getmetatable(L, index)) {
        lua_getfield(L, LUA_REGISTRYINDEX, type);
        if (lua_rawequal(L, -1, -2)) {
            lua_pop(L, 2);
            return *udata;
        }
        lua_pop(L, 2);
    }
    return -1;
}
