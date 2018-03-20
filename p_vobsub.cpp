/*
  ogmmerge -- utility for splicing together ogg bitstreams
      from component media subtypes

  p_textsubs.cpp
  text subtitle output module

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <ogg/ogg.h>

#include "ogmmerge.h"
#include "queue.h"
#include "ogmstreams.h"
#include "p_vobsub.h"
#include "vorbis_header_utils.h"

#ifdef ENABLE_VOBSUB

vobsub_packetizer_c::vobsub_packetizer_c(int nwidth, int nheight,
                                         char *npalette, int nlangidx,
                                         char *nid, int nindex,
                                         audio_sync_t *nasync,
                                         range_t *nrange, char **ncomments)
                                         throw (error_c) : q_c() {
  char buffer[50];
  serialno = create_unique_serial();
  ogg_stream_init(&os, serialno);
  packetno = 0;
  memcpy(&async, nasync, sizeof(audio_sync_t));
  memcpy(&range, nrange, sizeof(range_t));
  range.start *= 1000;
  range.end *= 1000;
  eos_packet_created = 0;
  params.width = nwidth;
  params.height = nheight;
  params.palette = strdup(npalette);
  if (params.palette == NULL)
    die("strdup");
  params.langidx = nlangidx;
  params.id = strdup(nid);
  if (params.id == NULL)
    die("strdup");
  params.index = nindex;
  comments = generate_vorbis_comment(ncomments);
  vorbis_comment_remove_tag(comments, "ID");
  vorbis_comment_remove_tag(comments, "INDEX");
  vorbis_comment_remove_tag(comments, "LANGIDX");
  vorbis_comment_remove_tag(comments, "PALETTE");
  vorbis_comment_add_tag(comments, "ID", params.id);
  buffer[49] = 0;
  snprintf(buffer, 49, "%d", params.index);
  vorbis_comment_add_tag(comments, "INDEX", buffer);
  snprintf(buffer, 49, "%d", params.langidx);
  vorbis_comment_add_tag(comments, "LANGIDX", buffer);
  vorbis_comment_add_tag(comments, "PALETTE", params.palette);
  old_granulepos = 0;
  last_granulepos = 0;
}

vobsub_packetizer_c::~vobsub_packetizer_c() {
  ogg_stream_clear(&os);
  if (comments != NULL) {
    vorbis_comment_clear(comments);
    free(comments);
  }
}

void vobsub_packetizer_c::produce_header_packets() {
  ogg_packet     op;
  stream_header  sh;
  int            clen, res;
  char          *tempbuf;
  
  memset(&sh, 0, sizeof(sh));
  strcpy(sh.streamtype, "image");
  strncpy(sh.subtype, "VBSB", 4);
  put_uint32(&sh.size, sizeof(sh));
  put_uint64(&sh.time_unit, 10000);
  put_uint64(&sh.samples_per_unit, 1);
  put_uint32(&sh.default_len, 1);
  put_uint32(&sh.buffersize, 16384);

  tempbuf = (char *)malloc(sizeof(sh) + 1);
  if (tempbuf == NULL)
    die("malloc");
  *tempbuf = PACKET_TYPE_HEADER;
  memcpy(&tempbuf[1], &sh, sizeof(sh));
  op.packet = (unsigned char *)tempbuf;
  op.bytes = 1 + get_uint32(&sh.size);
  op.b_o_s = 1;
  op.e_o_s = 0;
  op.packetno = 0;
  op.granulepos = 0;
  /* submit it */
  ogg_stream_packetin(&os, &op);
  packetno++;
  flush_pages(PACKET_TYPE_HEADER);
  free(tempbuf);

  /* Create the comments packet */
  clen = -1 * comments_to_buffer(comments, NULL, 0);
  tempbuf = (char *)malloc(clen);
  if (tempbuf == NULL)
    die("malloc");
  op.packet = (unsigned char *)tempbuf;
  op.bytes = clen;
  op.b_o_s = 0;
  op.e_o_s = 0;
  op.granulepos = 0;
  op.packetno = 1;
  if ((res = comments_to_buffer(comments, (char *)op.packet, clen)) < 0) {
    fprintf(stderr, "FATAL: p_vobsub: comments_to_buffer returned %d, " \
            "clen is %d\n", res, clen);
    exit(1);
  }
  ogg_stream_packetin(&os, &op);
  flush_pages(PACKET_TYPE_COMMENT);
  packetno++;
  free(tempbuf);

  last_granulepos = 0;
}

void vobsub_packetizer_c::reset() {
}

int vobsub_packetizer_c::process(ogg_int64_t start, ogg_int64_t end,
                                 char *subs, int slen, int last_sub) {
  ogg_packet     op;
  char          *tempbuf;
  int            clen, idx, i;
  unsigned char *bptr;

  if (packetno == 0)
    produce_header_packets();
  
  if (eos_packet_created)
    return 0;

  // Adjust the start and end values according to the audio adjustment.
  start += async.displacement;
  start = (ogg_int64_t)(async.linear * start);
  end += async.displacement;
  end = (ogg_int64_t)(async.linear * end);
  /*
   * Now adjust and check the range. If the end is < 0 then it is definitely
   * too early.
   */
  start -= (ogg_int64_t)range.start;
  end -= (ogg_int64_t)range.start;
  if (end < 0) {
    if (last_sub) {
      produce_eos_packet();
      return 0;
    } else
      return EMOREDATA;
  }
  // If the start is > the range end the packet is too late.
  if ((range.end > 0) && (start > (range.end - range.start))) {
    if (last_sub || !eos_packet_created) {
      produce_eos_packet();
      return 0;
    } else
      return EMOREDATA;
  }
  // At least part of the subtitle packet must be shown.
  if (start < 0)
    start = 0;
  if ((range.end > 0) && (end > (range.end - range.start)))
    end = (ogg_int64_t)(range.end - range.start);

  clen = end - start;
  for (i = 3; i >= 0; i--)
    if (clen > (1 << (8 * i)))
      break;
  i++;
  idx = strlen(subs) - 1;
  while ((idx >= 0) && ((subs[idx] == '\n') || (subs[idx] == '\r'))) {
    subs[idx] = 0;
    idx--;
  }
  subs[idx + 1] = 0x0d;
  subs[idx + 2] = 0;
  tempbuf = (char *)malloc(slen + i + 1);
  memcpy((char *)&tempbuf[i + 1], subs, slen);
  tempbuf[0] = (((i & 3) << 6) + ((i & 4) >> 1)) |
               PACKET_IS_SYNCPOINT;
  bptr = (unsigned char *)&tempbuf[1];
  for (idx = 0; idx < i; idx++) {
    *(bptr + idx) = (unsigned char)(clen & 0xFF);
    clen >>= 8;
  }
  op.packet = (unsigned char *)tempbuf;
  op.bytes = slen + i + 1;
  op.b_o_s = 0;
  op.e_o_s = last_sub;
  op.granulepos = start;
  op.packetno = packetno++;
  ogg_stream_packetin(&os, &op);
  flush_pages();
  free(tempbuf);

  last_granulepos = end;

  free(subs);

  return EMOREDATA;
}

void vobsub_packetizer_c::produce_eos_packet() {
  ogg_packet op;
  char tempbuf[4];
  
  tempbuf[0] = (1 << 6) | PACKET_IS_SYNCPOINT;
  tempbuf[1] = (char)1;
  tempbuf[2] = ' ';
  tempbuf[3] = 0;
  op.packet = (unsigned char *)tempbuf;
  op.bytes = 4;
  op.b_o_s = 0;
  op.e_o_s = 1;
  op.granulepos = last_granulepos;
  op.packetno = packetno++;
  ogg_stream_packetin(&os, &op);
  flush_pages();
  eos_packet_created = 1;
}
  
stamp_t vobsub_packetizer_c::make_timestamp(ogg_int64_t granulepos) {
  stamp_t stamp;

#ifndef XIPHCORRECTINTERLEAVING  
  stamp = granulepos * 1000;
#else
  stamp = old_granulepos * 1000;
#endif
  old_granulepos = granulepos;
  
  return stamp;
}

#endif // ENABLE_VOBSUB
