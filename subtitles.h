/*
  ogmmerge -- utility for splicing together ogg bitstreams
  from component media subtypes

  subtitles.h
  subtitle queueing and checking helper class definition

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

#ifndef __SUBTITLES_H__
#define __SUBTITLES_H__

#include <ogg/ogg.h>

#include "ogmmerge.h"
#include "p_textsubs.h"

typedef struct sub_t {
  ogg_int64_t  start, end;
  char        *subs;
  sub_t       *next;
} sub_t;

class subtitles_c {
  private:
    sub_t *first, *last;
  public:
    subtitles_c();
    ~subtitles_c();
    
    void   add(ogg_int64_t, ogg_int64_t, char *);
    int    check();
    void   process(textsubs_packetizer_c *);
    sub_t *get_next();
};

#endif
