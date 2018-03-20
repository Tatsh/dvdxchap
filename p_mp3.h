/*
  ogmmerge -- utility for splicing together ogg bitstreams
      from component media subtypes

  p_mp3.h
  class definition for the MP3 output module

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/
 
#ifndef __P_MP3_H
#define __P_MP3_H

#include "ogmmerge.h"
#include "ogmstreams.h"
#include "queue.h"
#include "mp3_common.h"

class mp3_packetizer_c: public q_c {
  private:
    int                 bps, eos;
    u_int64_t           bytes_output, packetno;
    unsigned long       samples_per_sec;
    int                 channels;
    int                 mp3rate;
    char               *tempbuf;
    audio_sync_t        async;
    range_t             range;
    ogg_int64_t         old_granulepos;
    char               *packet_buffer;
    int                 buffer_size;
    ogg_int64_t         disk_fileno;

  public:
    mp3_packetizer_c(unsigned long nsamples_per_sec, int nchannels,
                     int nmp3rate, audio_sync_t *nasync,
                     range_t *nrange, char **ncomments) throw (error_c);
    virtual ~mp3_packetizer_c();
    
    virtual int     process(char *buf, int size, int last_frame);
    virtual stamp_t make_timestamp(ogg_int64_t granulepos);
    virtual void    produce_eos_packet();
    virtual void    produce_header_packets();
    virtual void    reset();
  private:
    virtual void    add_to_buffer(char *buf, int size);
    virtual char   *get_mp3_packet(unsigned long *header,
                                   mp3_header_t *mp3header);
    virtual int     mp3_packet_available();
    virtual void    remove_mp3_packet(int pos, int framesize);
};

#endif
