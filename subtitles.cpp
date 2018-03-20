/*
  ogmmerge -- utility for splicing together ogg bitstreams
  from component media subtypes

  subtitles.cpp
  subtitle queueing and checking helper class

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ogmmerge.h"
#include "subtitles.h"

subtitles_c::subtitles_c() {
  first = NULL;
  last = NULL;
}

subtitles_c::~subtitles_c() {
  sub_t *current = first;
  
  while (current != NULL) {
    if (current->subs != NULL)
      free(current->subs);
    last = current;
    current = current->next;
    free(last);
  }
}
    
void subtitles_c::add(ogg_int64_t nstart, ogg_int64_t nend, char *nsubs) {
  sub_t *s;
  
  s = (sub_t *)malloc(sizeof(sub_t));
  if (s == NULL)
    die("malloc");
  s->subs = strdup(nsubs);
  s->start = nstart;
  s->end = nend;
  s->next = NULL;
  
  if (last == NULL) {
    first = s;
    last = s;
  } else {
    last->next = s;
    last = s;
  }
}

int subtitles_c::check() {
  sub_t *current;
  int error = 0;
  char *c;
  
  current = first;
  while ((current != NULL) && (current->next != NULL)) {
    if (current->end > current->next->start) {
      if (verbose) {
        char short_subs[21];
        
        memset(short_subs, 0, 21);
        strncpy(short_subs, current->subs, 20);
        for (c = short_subs; *c != 0; c++)
          if (*c == '\n')
            *c = ' ';
        fprintf(stdout, "subtitles: Warning: current entry ends after "
                "the next one starts. This end: %02lld:%02lld:%02lld,%03lld"
                "  next start: %02lld:%02lld:%02lld,%03lld  (\"%s\"...)\n",
                current->end / (60 * 60 * 1000),
                (current->end / (60 * 1000)) % 60,
                (current->end / 1000) % 60,
                current->end % 1000,
                current->next->start / (60 * 60 * 1000),
                (current->next->start / (60 * 1000)) % 60,
                (current->next->start / 1000) % 60,
                current->next->start % 1000,
                short_subs);
      }
      current->end = current->next->start - 1;
    }
    current = current->next;
  }
  
  current = first;
  while (current != NULL) {
    if (current->start > current->end) {
      error = 1;
      if (verbose) {
        char short_subs[21];
        
        memset(short_subs, 0, 21);
        strncpy(short_subs, current->subs, 20);
        for (c = short_subs; *c != 0; c++)
          if (*c == '\n')
            *c = ' ';
        fprintf(stdout, "subtitles: Warning: after fixing the time the "
                "current entry begins after it ends. This start: "
                "%02lld:%02lld:%02lld,%03lld  this end: %02lld:%02lld:"
                "%02lld,%03lld  (\"%s\"...)\n",
                current->start / (60 * 60 * 1000),
                (current->start / (60 * 1000)) % 60,
                (current->start / 1000) % 60,
                current->start % 1000,
                current->end / (60 * 60 * 1000),
                (current->end / (60 * 1000)) % 60,
                (current->end / 1000) % 60,
                current->end % 1000,
                short_subs);
       }
    }
    current = current->next;
  }
  
  return error;
}

void subtitles_c::process(textsubs_packetizer_c *p) {
  sub_t *current;
  
  while ((current = get_next()) != NULL) {
    p->process(current->start, current->end, current->subs,
               (first == NULL ? 1 : 0));
    free(current->subs);
    free(current);
  }
}

sub_t *subtitles_c::get_next() {
  sub_t *current;
  
  if (first == NULL)
    return NULL;
  
  current = first;
  if (first == last) {
    first = NULL;
    last = NULL;
  } else
    first = first->next;
  
  return current;
}
