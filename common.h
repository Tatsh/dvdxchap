/*
  ogmmerge -- utility for splicing together ogg bitstreams
      from component media subtypes

  ogmmerge.h
  general class and type definitions

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/
#ifndef __COMMON_H
#define __COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NEED_FSEEKO
#include <stdio.h>
#endif
#include <ogg/ogg.h>
#include "ogmstreams.h"

#define VERSIONINFO "ogmtools v" VERSION

extern int verbose;

/* errors */
#define EMOREDATA    -1
#define EMALLOC      -2
#define EBADHEADER   -3
#define EBADEVENT    -4
#define EOTHER       -5

/* types */
#define TYPEUNKNOWN   0
#define TYPEOGM       1
#define TYPEAVI       2
#define TYPEWAV       3 
#define TYPESRT       4
#define TYPEMP3       5
#define TYPEAC3       6
#define TYPECHAPTERS  7
#define TYPEMICRODVD  8
#define TYPEVOBSUB    9

#define die(a) _die(a, __FILE__, __LINE__)
void _die(const char *, const char *, int);

#define FOURCC(a, b, c, d) (unsigned long)((((unsigned char)a) << 24) + \
                           (((unsigned char)b) << 16) + \
                           (((unsigned char)c) << 8) + \
                           ((unsigned char)d))

char       **dup_comments(char **comments);
void         free_comments(char **comments);
ogg_packet  *duplicate_ogg_packet(ogg_packet *src);

u_int16_t get_uint16(const void *buf);
u_int32_t get_uint32(const void *buf);
u_int64_t get_uint64(const void *buf);
void put_uint16(void *buf, u_int16_t);
void put_uint32(void *buf, u_int32_t);
void put_uint64(void *buf, u_int64_t);
void copy_headers(stream_header *dst, old_stream_header *src, int size);

#ifdef NEED_FSEEKO
int fseeko(FILE *, off_t, int);
off_t ftello(FILE *);
#endif /* NEED_FSEEKO */

#ifdef __cplusplus
}
#endif

#endif
