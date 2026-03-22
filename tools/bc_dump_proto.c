#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lj_obj.h"
#include "lj_bc.h"

#define TOLUA_BCDUMP_F_BE 0x01
#define TOLUA_BCDUMP_F_STRIP 0x02

#define BCNAME(name, ma, mb, mc, mt) #name,
static const char *kBcOpNames[] = { BCDEF(BCNAME) };
#undef BCNAME

/* uLua (LuaJIT 2.0 / bytecode version 1) -> current BC enum mapping. */
static const uint8_t kUluaBcMap[] = {
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

static uint32_t read_u32(const uint8_t *p, int be)
{
  if (be) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
  }
  return ((uint32_t)p[3] << 24) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[1] << 8) |
         (uint32_t)p[0];
}

static uint32_t read_u16(const uint8_t *p, int be)
{
  if (be) {
    return ((uint32_t)p[0] << 8) | (uint32_t)p[1];
  }
  return ((uint32_t)p[1] << 8) | (uint32_t)p[0];
}

static int read_uleb128(const uint8_t *buf, size_t len, size_t *pos, uint32_t *out)
{
  uint32_t v = 0;
  uint32_t shift = 0;
  size_t p = *pos;
  while (1) {
    uint8_t b;
    if (p >= len) return 0;
    b = buf[p++];
    v |= (uint32_t)(b & 0x7f) << shift;
    if ((b & 0x80) == 0) break;
    shift += 7;
    if (shift > 28) return 0;
  }
  *pos = p;
  *out = v;
  return 1;
}

static uint8_t *read_file(const char *path, size_t *out_len)
{
  FILE *fp = NULL;
  long size = 0;
  uint8_t *buf = NULL;

  *out_len = 0;
  fp = fopen(path, "rb");
  if (!fp) return NULL;
  if (fseek(fp, 0, SEEK_END) != 0) goto fail;
  size = ftell(fp);
  if (size < 0) goto fail;
  if (fseek(fp, 0, SEEK_SET) != 0) goto fail;

  buf = (uint8_t *)malloc((size_t)size);
  if (!buf) goto fail;
  if ((size_t)size > 0 && fread(buf, 1, (size_t)size, fp) != (size_t)size) goto fail;
  fclose(fp);
  *out_len = (size_t)size;
  return buf;

fail:
  if (fp) fclose(fp);
  free(buf);
  return NULL;
}

static void print_usage(const char *argv0)
{
  fprintf(stderr, "usage: %s <bytecode_file> <proto_index|-1> [pc_from] [pc_to]\n", argv0);
  fprintf(stderr, "  proto_index=-1 prints proto summaries only.\n");
}

int main(int argc, char **argv)
{
  const char *path = NULL;
  int want_proto = 0;
  int pc_from = -1;
  int pc_to = -1;
  uint8_t *buf = NULL;
  size_t len = 0;
  size_t pos = 0;
  uint32_t flags = 0;
  uint8_t version = 0;
  int be = 0;
  int strip = 0;
  int remap_v1 = 0;
  int proto_index = 0;
  int found = 0;

  if (argc < 3 || argc > 5) {
    print_usage(argv[0]);
    return 2;
  }

  path = argv[1];
  want_proto = atoi(argv[2]);
  if (argc >= 4) pc_from = atoi(argv[3]);
  if (argc >= 5) pc_to = atoi(argv[4]);

  buf = read_file(path, &len);
  if (!buf) {
    fprintf(stderr, "failed to read file: %s\n", path);
    return 1;
  }

  if (len < 4 || buf[0] != 0x1b || buf[1] != 'L' || buf[2] != 'J') {
    fprintf(stderr, "not a LuaJIT bytecode file: %s\n", path);
    free(buf);
    return 1;
  }

  version = buf[3];
  remap_v1 = (version == 1) ? 1 : 0;
  pos = 4; /* skip ESC LJ + version */
  if (!read_uleb128(buf, len, &pos, &flags)) {
    fprintf(stderr, "failed to read flags\n");
    free(buf);
    return 1;
  }
  be = (flags & TOLUA_BCDUMP_F_BE) ? 1 : 0;
  strip = (flags & TOLUA_BCDUMP_F_STRIP) ? 1 : 0;

  if (!strip) {
    uint32_t name_len = 0;
    if (!read_uleb128(buf, len, &pos, &name_len)) {
      fprintf(stderr, "failed to read chunk name length\n");
      free(buf);
      return 1;
    }
    if (pos + (size_t)name_len > len) {
      fprintf(stderr, "chunk name out of range\n");
      free(buf);
      return 1;
    }
    pos += (size_t)name_len;
  }

  while (1) {
    uint32_t proto_len = 0;
    size_t proto_start = 0;
    size_t proto_end = 0;
    uint8_t pflags = 0;
    uint8_t numparams = 0;
    uint8_t framesize = 0;
    uint8_t numuv = 0;
    uint32_t numkgc = 0;
    uint32_t numkn = 0;
    uint32_t numbc = 0;
    uint32_t sizedbg = 0;
    uint32_t firstline = 0;
    uint32_t numline = 0;
    size_t bc_pos = 0;

    if (!read_uleb128(buf, len, &pos, &proto_len)) {
      fprintf(stderr, "failed to read proto length at index %d\n", proto_index);
      free(buf);
      return 1;
    }
    if (proto_len == 0) break;

    proto_start = pos;
    proto_end = proto_start + (size_t)proto_len;
    if (proto_end > len || proto_end < proto_start) {
      fprintf(stderr, "proto %d out of range\n", proto_index);
      free(buf);
      return 1;
    }
    if (pos + 4 > proto_end) {
      fprintf(stderr, "proto %d header truncated\n", proto_index);
      free(buf);
      return 1;
    }

    pflags = buf[pos++];
    numparams = buf[pos++];
    framesize = buf[pos++];
    numuv = buf[pos++];
    if (!read_uleb128(buf, proto_end, &pos, &numkgc) ||
        !read_uleb128(buf, proto_end, &pos, &numkn) ||
        !read_uleb128(buf, proto_end, &pos, &numbc)) {
      fprintf(stderr, "proto %d layout decode failed\n", proto_index);
      free(buf);
      return 1;
    }
    if (!strip) {
      if (!read_uleb128(buf, proto_end, &pos, &sizedbg)) {
        fprintf(stderr, "proto %d debug size decode failed\n", proto_index);
        free(buf);
        return 1;
      }
      if (sizedbg > 0 &&
          (!read_uleb128(buf, proto_end, &pos, &firstline) ||
           !read_uleb128(buf, proto_end, &pos, &numline))) {
        fprintf(stderr, "proto %d debug line decode failed\n", proto_index);
        free(buf);
        return 1;
      }
    }
    bc_pos = pos;
    if (bc_pos + (size_t)numbc * 4 > proto_end) {
      fprintf(stderr, "proto %d bytecode range out of proto bounds\n", proto_index);
      free(buf);
      return 1;
    }

    if (want_proto == -1 || want_proto == proto_index) {
      uint32_t i = 0;
      printf("proto=%d numbc=%u framesize=%u params=%u uv=%u pflags=0x%02x",
             proto_index, (unsigned int)numbc, (unsigned int)framesize,
             (unsigned int)numparams, (unsigned int)numuv, (unsigned int)pflags);
      if (!strip) {
        printf(" dbg=%u lines=%u-%u",
               (unsigned int)sizedbg,
               (unsigned int)firstline,
               (unsigned int)(firstline + numline));
      }
      printf("\n");

      if (want_proto == proto_index) {
        int start = 0;
        int end = (int)numbc - 1;
        const uint8_t *lineinfo = NULL;
        size_t lineinfo_len = 0;
        uint32_t lineinfo_unit = 0;
        found = 1;
        if (pc_from >= 0) start = pc_from;
        if (pc_to >= 0) end = pc_to;
        if (start < 0) start = 0;
        if (end >= (int)numbc) end = (int)numbc - 1;

        if (!strip && sizedbg > 0 && numline > 0 && (size_t)sizedbg <= proto_end - proto_start) {
          size_t dbg_pos = proto_end - (size_t)sizedbg;
          if (numline < 256) lineinfo_unit = 1;
          else if (numline < 65536) lineinfo_unit = 2;
          else lineinfo_unit = 4;
          if (lineinfo_unit > 0 &&
              dbg_pos <= proto_end &&
              (size_t)numbc <= (proto_end - dbg_pos) / lineinfo_unit) {
            lineinfo = buf + dbg_pos;
            lineinfo_len = (size_t)numbc * lineinfo_unit;
          }
        }

        if (start <= end) {
          for (i = (uint32_t)start; i <= (uint32_t)end; i++) {
            uint32_t ins = read_u32(buf + bc_pos + (size_t)i * 4, be);
            BCOp op_raw = bc_op(ins);
            BCOp op = op_raw;
            const char *name = "INVALID";
            int has_line = 0;
            uint32_t line_no = 0;
            if (remap_v1) {
              if ((size_t)op_raw < sizeof(kUluaBcMap)) op = (BCOp)kUluaBcMap[(int)op_raw];
              else op = BC__MAX;
            }
            if (op < BC__MAX) name = kBcOpNames[(int)op];
            if (lineinfo != NULL && (size_t)(i + 1) * lineinfo_unit <= lineinfo_len) {
              uint32_t line_ofs = 0;
              const uint8_t *lp = lineinfo + (size_t)i * lineinfo_unit;
              if (lineinfo_unit == 1) line_ofs = (uint32_t)lp[0];
              else if (lineinfo_unit == 2) line_ofs = read_u16(lp, be);
              else line_ofs = read_u32(lp, be);
              line_no = firstline + line_ofs;
              has_line = 1;
            }
            if (has_line) {
              printf("%04u line=%u %-6s A=%u B=%u C=%u D=%u raw=0x%08x op_raw=%u op=%u\n",
                     (unsigned int)i, (unsigned int)line_no, name,
                     (unsigned int)bc_a(ins), (unsigned int)bc_b(ins),
                     (unsigned int)bc_c(ins), (unsigned int)bc_d(ins),
                     (unsigned int)ins,
                     (unsigned int)op_raw,
                     (unsigned int)op);
            } else {
              printf("%04u %-6s A=%u B=%u C=%u D=%u raw=0x%08x op_raw=%u op=%u\n",
                     (unsigned int)i, name,
                     (unsigned int)bc_a(ins), (unsigned int)bc_b(ins),
                     (unsigned int)bc_c(ins), (unsigned int)bc_d(ins),
                     (unsigned int)ins,
                     (unsigned int)op_raw,
                     (unsigned int)op);
            }
          }
        }
      }
    }

    pos = proto_end;
    proto_index++;
  }

  if (want_proto >= 0 && !found) {
    fprintf(stderr, "proto index %d not found\n", want_proto);
    free(buf);
    return 1;
  }

  free(buf);
  return 0;
}
