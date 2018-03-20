#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ogg/ogg.h>
#ifdef NEED_FSEEKO
#include <sys/stat.h>
#include <errno.h>
#endif

#include "common.h"

void _die(const char *s, const char *file, int line) {
  fprintf(stderr, "die @ %s/%d : %s\n", file, line, s);
  exit(1);
}

ogg_packet *duplicate_ogg_packet(ogg_packet *src) {
  ogg_packet *dst;
  
  dst = (ogg_packet *)malloc(sizeof(ogg_packet));
  if (dst == NULL)
    die("malloc");
  memcpy(dst, src, sizeof(ogg_packet));
  dst->packet = (unsigned char *)malloc(src->bytes);
  if (dst->packet == NULL)
    die("malloc");
  memcpy(dst->packet, src->packet, src->bytes);
  
  return dst;
}

char **dup_comments(char **comments) {
  char **new_comments;
  int    nc;
  
  if (comments == NULL)
    return NULL;

  for (nc = 0; comments[nc] != NULL; nc++)
    ;
  
  new_comments = (char **)malloc(sizeof(char *) * nc + 1);
  if (new_comments == NULL)
    die("malloc");
  
  for (nc = 0; comments[nc] != NULL; nc++) {
    new_comments[nc] = strdup(comments[nc]);
    if (new_comments[nc] == NULL)
      die("strdup");
  }
  new_comments[nc] = NULL;
  
  return new_comments;
}

void free_comments(char **comments) {
  int i;
  
  if (comments != NULL) {
    for (i = 0; comments[i] != NULL; i++)
      free(comments[i]);
    free(comments);
  }
}

u_int16_t get_uint16(const void *buf) {
  u_int16_t      ret;
  unsigned char *tmp;

  tmp = (unsigned char *)buf;

  ret = tmp[1] & 0xff;
  ret = (ret << 8) + (tmp[0] & 0xff);

  return ret;
}

u_int32_t get_uint32(const void *buf) {
  u_int32_t      ret;
  unsigned char *tmp;

  tmp = (unsigned char *)buf;

  ret = tmp[3] & 0xff;
  ret = (ret << 8) + (tmp[2] & 0xff);
  ret = (ret << 8) + (tmp[1] & 0xff);
  ret = (ret << 8) + (tmp[0] & 0xff);

  return ret;
}

u_int64_t get_uint64(const void *buf) {
  u_int64_t      ret;
  unsigned char *tmp;

  tmp = (unsigned char *) buf;

  ret = tmp[7] & 0xff;
  ret = (ret << 8) + (tmp[6] & 0xff);
  ret = (ret << 8) + (tmp[5] & 0xff);
  ret = (ret << 8) + (tmp[4] & 0xff);
  ret = (ret << 8) + (tmp[3] & 0xff);
  ret = (ret << 8) + (tmp[2] & 0xff);
  ret = (ret << 8) + (tmp[1] & 0xff);
  ret = (ret << 8) + (tmp[0] & 0xff);

  return ret;
}

void put_uint16(void *buf, u_int16_t val) {
  unsigned char *tmp;

  tmp = (unsigned char *) buf;

  tmp[0] = val & 0xff;
  tmp[1] = (val >>= 8) & 0xff;
}

void put_uint32(void *buf, u_int32_t val) {
  unsigned char *tmp;

  tmp = (unsigned char *) buf;

  tmp[0] = val & 0xff;
  tmp[1] = (val >>= 8) & 0xff;
  tmp[2] = (val >>= 8) & 0xff;
  tmp[3] = (val >>= 8) & 0xff;
}

void put_uint64(void *buf, u_int64_t val) {
  unsigned char *tmp;

  tmp = (unsigned char *) buf;

  tmp[0] = val & 0xff;
  tmp[1] = (val >>= 8) & 0xff;
  tmp[2] = (val >>= 8) & 0xff;
  tmp[3] = (val >>= 8) & 0xff;
  tmp[4] = (val >>= 8) & 0xff;
  tmp[5] = (val >>= 8) & 0xff;
  tmp[6] = (val >>= 8) & 0xff;
  tmp[7] = (val >>= 8) & 0xff;
}

#ifdef NEED_FSEEKO
/*
 *	On BSD/OS and NetBSD, off_t and fpos_t are the same.  Standards
 *	say off_t is an arithmetic type, but not necessarily integral,
 *	while fpos_t might be neither.
 */
int fseeko(FILE *stream, off_t offset, int whence) {
  off_t floc;
  struct stat filestat;

  switch (whence) {
    case SEEK_CUR:
      flockfile(stream);
      if (fgetpos(stream, &floc) != 0)
        goto failure;
      floc += offset;
      if (fsetpos(stream, &floc) != 0)
        goto failure;
      funlockfile(stream);
      return 0;
      break;
    case SEEK_SET:
      if (fsetpos(stream, &offset) != 0)
        return -1;
      return 0;
      break;
    case SEEK_END:
      flockfile(stream);
      if (fstat(fileno(stream), &filestat) != 0)
        goto failure;
      floc = filestat.st_size;
      if (fsetpos(stream, &floc) != 0)
        goto failure;
      funlockfile(stream);
      return 0;
      break;
    default:
      errno =	EINVAL;
      return -1;
  }

 failure:
  funlockfile(stream);
  return -1;
}

off_t ftello(FILE *stream) {
  off_t floc;

  if (fgetpos(stream, &floc) != 0)
    return -1;
  return floc;
}

#endif /* NEED_FSEEKO */

#define COPY(m, s) memcpy(&dst->m, &src->m, s)
void copy_headers(stream_header *dst, old_stream_header *src, int size) {
  if (size == sizeof(old_stream_header)) {
    COPY(streamtype[0], 8);
    COPY(subtype[0], 4);
    COPY(size, 4);
    COPY(time_unit, 8);
    COPY(samples_per_unit, 8);
    COPY(default_len, 4);
    COPY(buffersize, 4);
    COPY(bits_per_sample, 2);
    COPY(sh, sizeof(stream_header_video));
  } else
    memcpy(dst, src, size);
}
