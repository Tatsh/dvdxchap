/*
  ogmmerge -- utility for splicing together ogg bitstreams
  from component media subtypes

  p_textsubs.h
  class definition for the text subtitle output module

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

#ifndef __P_TEXTSUBS_H__
#define __P_TEXTSUBS_H__

#include "ogmmerge.h"
#include "queue.h"

class textsubs_packetizer_c: public q_c {
  private:
    ogg_int64_t     old_granulepos, last_granulepos;
    int             packetno;
    audio_sync_t    async;
    range_t         range;
    int             eos_packet_created;

  public:
    textsubs_packetizer_c(audio_sync_t *nasync, range_t *nrange,
                          char **ncomments) throw (error_c);
    virtual ~textsubs_packetizer_c();
    
    virtual int     process(ogg_int64_t start, ogg_int64_t end, char *_subs,
                            int last_sub);
    virtual stamp_t make_timestamp(ogg_int64_t granulepos);
    virtual void    produce_eos_packet();
    virtual void    produce_header_packets();
    virtual void    reset();
};


#endif  /* __P_TEXTSUBS_H__*/
