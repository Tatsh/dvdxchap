/*
  ogmmerge -- utility for splicing together ogg bitstreams
  from component media subtypes

  generic.cpp
  generic base classes, chapter file support

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <vorbis/codec.h>

#include "common.h"
#include "ogmmerge.h"
#include "vorbis_header_utils.h"

generic_packetizer_c::generic_packetizer_c() {
  comments = NULL;
}

int generic_packetizer_c::serial_in_use(int serial) {
  return (serial == serialno);
}

void generic_packetizer_c::set_comments(vorbis_comment *ncomments) {
  if (comments != NULL) {
    vorbis_comment_clear(comments);
    free(comments);
  }
  if (ncomments == NULL) {
    comments = NULL;
    return;
  }
  comments = (vorbis_comment *)malloc(sizeof(vorbis_comment));
  if (comments == NULL)
    die("malloc");
  memcpy(comments, ncomments, sizeof(vorbis_comment));
}
 
generic_reader_c::generic_reader_c() {
  chapter_info = NULL;
}

generic_reader_c::~generic_reader_c() {
  if (chapter_info != NULL) {
    vorbis_comment_clear(chapter_info);
    free(chapter_info);
  }
}
 
void generic_reader_c::set_chapter_info(vorbis_comment *info) {
  if (chapter_info != NULL) {
    vorbis_comment_clear(chapter_info);
    free(chapter_info);
  }
  chapter_info = vorbis_comment_dup(info);
}

#define isequal(s) (*(s) == '=')
#define iscolon(s) (*(s) == ':')
#define isfullstop(s) (*(s) == '.')
#define istwodigits(s) (isdigit(*(s)) && isdigit(*(s + 1)))
#define isthreedigits(s) (isdigit(*(s)) && isdigit(*(s + 1)) && \
                          isdigit(*(s + 2)))
#define isarrow(s) (!strncmp((s), " --> ", 5))
#define istimestamp(s) (istwodigits(s) && iscolon(s + 2) && \
                        istwodigits(s + 3) && iscolon(s + 5) && \
                        istwodigits(s + 6) && isfullstop(s + 8) && \
                        isthreedigits(s + 9))
#define ischapter(s) ((strlen(s) == 22) && \
                      !strncmp(s, "CHAPTER", 7) && \
                      istwodigits(s + 7) && isequal(s + 9) && \
                      istimestamp(s + 10))
#define ischaptername(s) ((strlen(s) > 14) && \
                          !strncmp(s, "CHAPTER", 7) && \
                          istwodigits(s + 7) && \
                          !strncmp(s + 9, "NAME", 4) && isequal(s + 13))
                        
int chapter_information_probe(FILE *file, off_t size) {
  char buf[201];
  int  len;

  if (size < 37)
    return 0;
  if (fseeko(file, 0, SEEK_SET) != 0)
    return 0;
  if (fgets(buf, 200, file) == NULL)
    return 0;
  len = strlen(buf);
  if (len == 0)
    return 0;
  if (buf[len - 1] != '\n')
    return 0;
  if (strncmp(buf, "CHAPTER", 7))
    return 0;
  if (strncmp(&buf[9], "=", 1))
    return 0;
  if (!istimestamp(&buf[10]))
    return 0;
  if (fgets(buf, 200, file) == NULL)
    return 0;
  len = strlen(buf);
  if (len == 0)
    return 0;
  if (buf[len - 1] != '\n')
    return 0;
  if (strncmp(buf, "CHAPTER", 7))
    return 0;
  if (strncmp(&buf[9], "NAME=", 5))
    return 0;

  return 1;
}

vorbis_comment *chapter_information_read(char *name) {
  vorbis_comment *vc;
  char            buf[201];
  char           *s;
  int             len;
  FILE           *file;
  
  if ((file = fopen(name, "r")) == NULL)
    return NULL;
  
  if (fseeko(file, 0, SEEK_SET) != 0)
    return NULL;
  
  if (verbose)
    fprintf(stdout, "Using chapter information reader for %s.\n", name);
  
  vc = (vorbis_comment *)malloc(sizeof(vorbis_comment));
  if (vc == NULL)
    die("malloc");

  vc->vendor = strdup(VERSIONINFO);
  vc->user_comments = (char **)mmalloc(4);
  vc->comment_lengths = (int *)mmalloc(4);
  vc->comments = 0;

  while (!feof(file)) {
    if (fgets(buf, 200, file) != NULL) {
      len = strlen(buf);
      if (len > 0) {
        s = &buf[len - 1];
        while ((s != buf) && ((*s == '\n') || (*s == '\r'))) {
          *s = 0;
          s--;
        }
        len = strlen(buf);
        if (len > 0)
          vorbis_comment_add(vc, buf);
      }
    }
  }
  
  return vc;
}

vorbis_comment *chapter_information_adjust(vorbis_comment *vc, double start,
                                           double end) {
  vorbis_comment *nvc;
  int             i, chapter_sub, chapter, hour, min, sec, msec;
  char           *copy;
  char           *last;
  char            copy_chapters[100], new_chapter[24];
  double          cstart;

  if (vc == NULL)
    return NULL;

  memset(copy_chapters, 0, 100);
  nvc = (vorbis_comment *)malloc(sizeof(vorbis_comment));
  if (nvc == NULL)
    die("malloc");

  nvc->vendor = strdup(VERSIONINFO);
  nvc->user_comments = (char **)mmalloc(4);
  nvc->comment_lengths = (int *)mmalloc(4);
  nvc->comments = 0;
  chapter_sub = -1;
  last = NULL;

  for (i = 0; i < vc->comments; i++) {
    if (ischapter(vc->user_comments[i])) {
      copy = strdup(vc->user_comments[i]);
      if (copy == NULL)
        die("malloc");
/*
 * CHAPTER01=01:23:45.678
 * 0123456789012345678901
 *           1         2
 */
      copy[9] = 0;    // =
      copy[12] = 0;   // :
      copy[15] = 0;   // :
      copy[18] = 0;   // .
      chapter = strtol(&copy[7], NULL, 10);
      hour= strtol(&copy[10], NULL, 10);
      min = strtol(&copy[13], NULL, 10);
      sec = strtol(&copy[16], NULL, 10);
      msec = strtol(&copy[19], NULL, 10);
      cstart = msec + (1000.0 * sec) + (60000.0 * min) + (3600000.0 * hour);
      if ((cstart >= start) && (cstart < end)) {
        copy_chapters[chapter] = 1;
        if (chapter_sub == -1) {
          chapter_sub = chapter - 1;
          if (last && (cstart > start)) {
            int last_length = strlen(last);
            sprintf(new_chapter, "CHAPTER01=00:00:00.000");
            vorbis_comment_add(nvc, new_chapter);
            last  = (char *)realloc(last, last_length + 12 + 1);
            sprintf(&last[7], "01");
            last[9] = 'N';
            sprintf(&last[last_length], " (continued)");
            last[last_length+12] = 0;
            vorbis_comment_add(nvc, last);
            free(last);
            chapter_sub--;
          }
        }
        chapter -= chapter_sub;
        sprintf(new_chapter, "CHAPTER%02d=%02d:%02d:%02d.%03d", chapter,
                (((int)(cstart - start)) / 1000 / 60 / 60),
                (((int)(cstart - start)) / 1000 / 60) % 60,
                (((int)(cstart - start)) / 1000) % 60,
                ((int)(cstart - start)) % 1000);
        vorbis_comment_add(nvc, new_chapter);
      }
      free(copy);
    } else if (ischaptername(vc->user_comments[i])) {
      memcpy(new_chapter, &vc->user_comments[i][7], 2);
      new_chapter[2] = 0;
      chapter = strtol(new_chapter, NULL, 10);
      if (copy_chapters[chapter]) {
        copy = strdup(vc->user_comments[i]);
        if (copy == NULL)
          die("malloc");
        sprintf(&copy[7], "%02d", chapter - chapter_sub);
        copy[9] = 'N';
        vorbis_comment_add(nvc, copy);
        free(copy);
      } else if (chapter_sub == -1) {
        if (last != NULL)
          free(last);
        last = strdup(vc->user_comments[i]);
        if (last == NULL)
          die("malloc");
      }
    } else
      vorbis_comment_add(nvc, vc->user_comments[i]);
  }
  
  return nvc;
}
