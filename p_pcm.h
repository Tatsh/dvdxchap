/*
  ogmmerge -- utility for splicing together ogg bitstreams
      from component media subtypes

  p_pcm.h
  class definition for the PCM output module

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/
 
#ifndef __P_PCM_H
#define __P_PCM_H

#include "ogmmerge.h"
#include "ogmstreams.h"
#include "queue.h"

class pcm_packetizer_c: public q_c {
  private:
    int                 packetno;
    int                 bps;
    u_int64_t           bytes_output;
    unsigned long       samples_per_sec;
    int                 channels;
    int                 bits_per_sample;
    char               *tempbuf;
    audio_sync_t        async;
    range_t             range;
    ogg_int64_t         old_granulepos;

  public:
    pcm_packetizer_c(unsigned long nsamples_per_sec, int nchannels,
                     int nbits_per_sample, audio_sync_t *nasync,
                     range_t *nrange, char **ncomments) throw (error_c);
    virtual ~pcm_packetizer_c();
    
    virtual int     process(char *buf, int size, int last_frame);
    virtual stamp_t make_timestamp(ogg_int64_t granulepos);
    virtual void    produce_eos_packet();
    virtual void    produce_header_packets();
    virtual void    reset();
};

const int pcm_interleave = 16;

#endif
