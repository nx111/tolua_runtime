#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "tolua.h"

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

static int write_file(const char *path, const void *buf, size_t size)
{
  FILE *fp = fopen(path, "wb");
  if (!fp) return 0;
  if (size != 0 && fwrite(buf, 1, size, fp) != size) {
    fclose(fp);
    return 0;
  }
  fclose(fp);
  return 1;
}

int main(int argc, char **argv)
{
  const char *input = NULL;
  const char *output = NULL;
  size_t input_size = 0;
  int output_size = 0;
  int error_code = 0;
  int target_fr2 = 1;
  uint8_t *input_buf = NULL;
  char *output_buf = NULL;

  if (argc < 3 || argc > 4) {
    fprintf(stderr, "usage: %s <input> <output> [target_fr2]\n", argv[0]);
    return 2;
  }

  input = argv[1];
  output = argv[2];
  if (argc == 4) target_fr2 = atoi(argv[3]) != 0;

  input_buf = read_file(input, &input_size);
  if (!input_buf) {
    fprintf(stderr, "read failed: %s (%s)\n", input, strerror(errno));
    return 1;
  }

  output_buf = tolua_convertbytecodeex((const char *)input_buf, (int)input_size, target_fr2,
                                       &output_size, &error_code);
  if (!output_buf) {
    fprintf(stderr, "convert failed: err=%d debug=%s\n",
            error_code, tolua_getlastbytecodedebug());
    free(input_buf);
    return 1;
  }

  if (!write_file(output, output_buf, (size_t)output_size)) {
    fprintf(stderr, "write failed: %s (%s)\n", output, strerror(errno));
    free(output_buf);
    free(input_buf);
    return 1;
  }

  printf("converted %s -> %s (%zu -> %d bytes, target_fr2=%d)\n",
         input, output, input_size, output_size, target_fr2);

  free(output_buf);
  free(input_buf);
  return 0;
}
