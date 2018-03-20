/*
  ogmmerge -- utility for splicing together ogg bitstreams
  from component media subtypes

  p_video.cpp
  video output module (DivX, XviD, not MPEG1/2)

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include "ogmmerge.h"
#include "queue.h"
#include "p_video.h"
#include "vorbis_header_utils.h"

video_packetizer_c::video_packetizer_c(char *ncodec, double nfps, int nwidth,
                                       int nheight, int nbpp,
                                       int nmax_frame_size, audio_sync_t *as,
                                       range_t *nrange, char **ncomments)
                                       throw (error_c) : q_c() {
  serialno = create_unique_serial();
  ogg_stream_init(&os, serialno);
  packetno = 0;
  memcpy(codec, ncodec, 4);
  codec[4] = 0;
  fps = nfps;
  width = nwidth;
  height = nheight;
  bpp = nbpp;
  max_frame_size = nmax_frame_size;
  last_granulepos = 0;
  tempbuf = (char *)malloc(max_frame_size + 1);
  memcpy(&range, nrange, sizeof(range_t));
  if (tempbuf == NULL)
    die("malloc");
  range.start *= fps;
  range.end *= fps;
  old_granulepos = 0;
  chapter_info = NULL;
  comments = generate_vorbis_comment(ncomments);
#ifdef ENABLE_INDEX
  add_index(serialno);
#endif
}

void video_packetizer_c::reset() {
}

#define isequal(s) (*(s) == '=')
#define istwodigits(s) (isdigit(*(s)) && isdigit(*(s + 1)))

vorbis_comment *video_packetizer_c::strip_chapters() {
  int             i, done;
  vorbis_comment *new_vc;
  
  if (comments == NULL)
    return NULL;
  new_vc = vorbis_comment_dup(comments);
  do {
    for (i = 0; i < new_vc->comments; i++) {
      done = 1;
      if ((new_vc->comment_lengths[i] >= 14) &&
          !strncmp(new_vc->user_comments[i], "CHAPTER", 7) &&
          ( (istwodigits(&new_vc->user_comments[i][7]) &&
             isequal(&new_vc->user_comments[i][9]))
           ||
            (istwodigits(&new_vc->user_comments[i][7]) &&
             !strncmp(&new_vc->user_comments[i][9], "NAME", 4) &&
             isequal(&new_vc->user_comments[i][13]))
          )) {
        vorbis_comment_remove_number(new_vc, i);
        done = 0;
        break;
      }
    }
  } while (!done && (new_vc->comments > 0));
  
  return new_vc;
}

void video_packetizer_c::produce_header_packets() {
  stream_header   sh;
  int             vclen;
  ogg_packet      op;
  vorbis_comment *new_comments, *new_comments2;

  *((unsigned char *)tempbuf) = PACKET_TYPE_HEADER;
  memset(&sh, 0, sizeof(stream_header));
  strcpy(sh.streamtype, "video");
  memcpy(sh.subtype, codec, 4);
  put_uint32(&sh.size, sizeof(sh));
  put_uint64(&sh.time_unit, (ogg_int64_t)((double)10000000.0 / (double)fps));
  sample_rate = (double)10000000 / (double)get_uint64(&sh.time_unit);
  put_uint64(&sh.samples_per_unit, 1);
  put_uint32(&sh.default_len, 1);
  put_uint32(&sh.buffersize, max_frame_size);
  put_uint16(&sh.bits_per_sample, bpp);
  put_uint32(&sh.sh.video.width, width);
  put_uint32(&sh.sh.video.height, height);
  memcpy(&tempbuf[1], &sh, sizeof(stream_header));
  op.packet = (unsigned char *)tempbuf;
  op.bytes = 1 + get_uint32(&sh.size);
  op.b_o_s = 1;
  op.e_o_s = 0;
  op.packetno = 0;
  op.granulepos = 0;
  ogg_stream_packetin(&os, &op);
  packetno++;
  flush_pages(PACKET_TYPE_HEADER);

  if (chapter_info != NULL) {
    new_comments = strip_chapters();
    new_comments = vorbis_comment_cat(new_comments, chapter_info);
  } else
    new_comments = vorbis_comment_dup(comments);
  new_comments2 = chapter_information_adjust(new_comments,
                                             (range.start / fps) * 1000.0,
                                             (range.end == 0.0) ?
                                               99999999999.0 :
                                               (range.end / fps) * 1000.0);
  if (new_comments2 != NULL) {
    vorbis_comment_clear(new_comments);
    free(new_comments);
    new_comments = new_comments2;
  }
  vclen = comments_to_buffer(new_comments, tempbuf, max_frame_size + 1);
  if (vclen < 0) {
    fprintf(stderr, "FATAL: p_video: buffer too small for %d bytes " \
            "(can hold %d bytes).\n", vclen, max_frame_size + 1);
    exit(1);
  }
  if (new_comments != NULL) {
    vorbis_comment_clear(new_comments);
    free(new_comments);
  }
  op.packet = (unsigned char *)tempbuf;
  op.bytes = vclen;
  op.b_o_s = 0;
  op.e_o_s = 0;
  op.packetno = 1;
  op.granulepos = 0;
  ogg_stream_packetin(&os, &op);
  flush_pages(PACKET_TYPE_COMMENT);
  packetno++;
}


int video_packetizer_c::process(char *buf, int size, int num_frames,
                                int key, int last_frame) {
  ogg_packet op;
  int        offset;

  if (size > max_frame_size) {
    fprintf(stderr, "FATAL: p_video: size (%d) > max_frame_size (%d)\n",
            size, max_frame_size);
    exit(1);
  }

  if (packetno == 0)
    produce_header_packets();

  if ((packetno >= range.start) &&
      ((range.end == 0) || (packetno < range.end))) {
    if (key) {
      flush_pages();
      next_page_contains_keyframe(serialno);
    }
    *((unsigned char *)tempbuf) = key ? PACKET_IS_SYNCPOINT : 0;
    if (num_frames == 1)
      offset = 0;
    else {
      unsigned char *bptr;
      int nf, idx;
      
      for (offset = 3; offset >= 0; offset--)
        if (num_frames > (1 << (8 * offset)))
          break;
      offset++;
      tempbuf[0] |= (((offset & 3) << 6) + ((offset & 4) >> 1));
      bptr = (unsigned char *)&tempbuf[1];
      nf = num_frames;
      for (idx = 0; idx < offset; idx++) {
        *(bptr + idx) = (unsigned char)(nf & 0xFF);
        nf >>= 8;
      }
    }
    memcpy(&tempbuf[1 + offset], buf, size);
    op.bytes = size + 1 + offset;
    op.packet = (unsigned char *)tempbuf;
    op.b_o_s = 0;
    op.e_o_s = last_frame;
    op.granulepos = last_granulepos + num_frames - 1;
    op.packetno = packetno;
    ogg_stream_packetin(&os, &op);
    queue_pages();
    last_granulepos += num_frames;
  } else if (last_frame) {
    *((unsigned char *)tempbuf) = 0;
    op.bytes = 1;
    op.packet = (unsigned char *)tempbuf;
    op.b_o_s = 0;
    op.e_o_s = 1;
    op.granulepos = last_granulepos;
    op.packetno = packetno;
    ogg_stream_packetin(&os, &op);
    flush_pages();
    last_granulepos++;
  }
  packetno++;
    
  return EMOREDATA;
}

void video_packetizer_c::produce_eos_packet() {
  char buf = 0;
  process(&buf, 1, 1, 0, 1);
}
  
video_packetizer_c::~video_packetizer_c() {
  if (tempbuf != NULL)
    free(tempbuf);
  ogg_stream_clear(&os);
  if (comments != NULL) {
    vorbis_comment_clear(comments);
    free(comments);
  }
  if (chapter_info != NULL) {
    vorbis_comment_clear(chapter_info);
    free(chapter_info);
  }
}

void video_packetizer_c::set_chapter_info(vorbis_comment *vc) {
  if (chapter_info != NULL) {
    vorbis_comment_clear(chapter_info);
    free(chapter_info);
  }
  chapter_info = vorbis_comment_dup(vc);
}

stamp_t video_packetizer_c::make_timestamp(ogg_int64_t granulepos) {
  stamp_t stamp;

#ifndef INCORRECT_INTERLEAVING
  stamp = (stamp_t)((double)old_granulepos * (double)1000000.0 / sample_rate);
  old_granulepos = granulepos;
#else
  stamp = (stamp_t)((double)granulepos * (double)1000000.0 / sample_rate);
#endif

  return stamp;
}
