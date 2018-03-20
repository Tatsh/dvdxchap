/*
  ogmmerge -- utility for splicing together ogg bitstreams
      from component media subtypes

  p_video.h
  class definition for the video output module

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/
 
#ifndef __P_VIDEO_H
#define __P_VIDEO_H

#include "ogmmerge.h"
#include "ogmstreams.h"
#include "queue.h"

class video_packetizer_c: public q_c {
  private:
    char            codec[5];
    double          fps, sample_rate;
    int             width, height;
    int             bpp;
    int             max_frame_size;
    int             packetno;
    ogg_int64_t     last_granulepos, old_granulepos;
    char           *tempbuf;
    vorbis_comment *chapter_info;
    range_t         range;
  public:
    video_packetizer_c(char *, double, int, int, int, int, audio_sync_t *,
                       range_t *nrange, char **ncomments) throw (error_c);
    virtual ~video_packetizer_c();
    
    virtual int            process(char *buf, int size, int num_frames, int key,
                                   int last_frame);
    virtual stamp_t        make_timestamp(ogg_int64_t granulepos);
    virtual void           produce_eos_packet();
    virtual void           produce_header_packets();
    virtual void           reset();
    virtual void           set_chapter_info(vorbis_comment *info);
  private:
    virtual vorbis_comment *strip_chapters();
};

#endif
