/*
  ogmmerge -- utility for splicing together ogg bitstreams
      from component media subtypes

  queue.h
  class definitions for the OGG page queueing class

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

#ifndef __UTIL_H__
#define __UTIL_H__

#include <ogg/ogg.h>
#include "ogmmerge.h"
#include "ogmstreams.h"

// q_page_t is used internally only
typedef struct q_page {
  ogmmerge_page_t *mpage;
  struct q_page   *next;
} q_page_t;

class q_c: public generic_packetizer_c {
  private:
    struct q_page    *first;
    struct q_page    *current;
    int               next_is_key;
  protected:
    ogg_stream_state  os;
    
  public:
    q_c() throw (error_c);
    virtual ~q_c();
    
    virtual int              add_ogg_page(ogg_page *, int header_page,
                                          int index_serial);
    virtual int              flush_pages(int header_page = 0);
    virtual int              queue_pages(int header_page = 0);
    virtual void             next_page_contains_keyframe(int serial);
    virtual ogmmerge_page_t *get_page();
    virtual ogmmerge_page_t *get_header_page(int header_type =
                                             PACKET_TYPE_HEADER);
    virtual int              page_available();
    virtual int              header_page_available();
    virtual stamp_t          make_timestamp(ogg_int64_t granulepos) = 0;
    virtual stamp_t          get_smallest_timestamp();
    
    virtual long             get_queued_bytes();

  private:
    static ogg_page         *copy_ogg_page(ogg_page *);
};

#endif  /* __UTIL_H__ */
