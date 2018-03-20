/*
  ogmmerge -- utility for splicing together ogg bitstreams
      from component media subtypes

  p_index.h
  class definition for video index

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

#ifdef ENABLE_INDEX
 
#ifndef __P_INDEX_H
#define __P_INDEX_H

#include "ogmmerge.h"
#include "ogmstreams.h"
#include "queue.h"

typedef struct idx_entry {
  ogg_int64_t granulepos;
  off_t       filepos;
};

class index_packetizer_c: public q_c {
  private:
    ogg_int64_t granulepos, packetno;
    int         serial;
  public:
    index_packetizer_c(int nserial) throw (error_c);
    virtual ~index_packetizer_c();
    
    virtual int     process(idx_entry *entries, int num);
    virtual stamp_t make_timestamp(ogg_int64_t granulepos);
    virtual void    produce_eos_packet();
    virtual void    produce_header_packets();
    virtual void    reset();
};

#endif

#endif // ENABLE_INDEX
