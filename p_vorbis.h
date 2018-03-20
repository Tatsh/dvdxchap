/*
  ogmmerge -- utility for splicing together ogg bitstreams
      from component media subtypes

  p_vorbis.h
  class definition for the Vorbis audio output module

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

#ifndef __P_VORBIS_H__
#define __P_VORBIS_H__

#include "ogmmerge.h"
#include "queue.h"

class vorbis_packetizer_c: public q_c {
  private:
    ogg_int64_t     old_granulepos;
    ogg_int64_t     ogran;
    ogg_int64_t     last_granulepos;
    ogg_int64_t     last_granulepos_seen;
    int             packetno;
    int             skip_packets;
    audio_sync_t    async;
    range_t         range;
    vorbis_info     vi;
    vorbis_comment  vc;
    int             range_converted;
    ogg_packet     *header_packet;

  public:
    vorbis_packetizer_c(audio_sync_t *nasync, range_t *nrange, char **ncomments)
                        throw (error_c);
    virtual ~vorbis_packetizer_c();
    
    virtual int     process(ogg_packet *op, ogg_int64_t gran);
    virtual stamp_t make_timestamp(ogg_int64_t granulepos);
    virtual void    produce_eos_packet();
    virtual void    produce_header_packets();
    virtual void    reset();
  
  private:
    virtual void    setup_displacement();
    virtual int     encode_silence(int fd);
};


#endif  /* __P_VORBIS_H__*/
