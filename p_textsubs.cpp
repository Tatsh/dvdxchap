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
#include "p_textsubs.h"
#include "vorbis_header_utils.h"

textsubs_packetizer_c::textsubs_packetizer_c(audio_sync_t *nasync,
                                             range_t *nrange, char **ncomments)
                                             throw (error_c) : q_c() {
  serialno = create_unique_serial();
  ogg_stream_init(&os, serialno);
  packetno = 0;
  memcpy(&async, nasync, sizeof(audio_sync_t));
  memcpy(&range, nrange, sizeof(range_t));
  range.start *= 1000;
  range.end *= 1000;
  eos_packet_created = 0;
  comments = generate_vorbis_comment(ncomments);
  old_granulepos = 0;
  last_granulepos = 0;
}

textsubs_packetizer_c::~textsubs_packetizer_c() {
  ogg_stream_clear(&os);
  if (comments != NULL) {
    vorbis_comment_clear(comments);
    free(comments);
  }
}

void textsubs_packetizer_c::produce_header_packets() {
  ogg_packet     op;
  stream_header  sh;
  int            clen, res;
  char          *tempbuf;
  
  memset(&sh, 0, sizeof(sh));
  strcpy(sh.streamtype, "text");
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
    fprintf(stderr, "FATAL: p_textsubs: comments_to_buffer returned %d, " \
            "clen is %d\n", res, clen);
    exit(1);
  }
  ogg_stream_packetin(&os, &op);
  flush_pages(PACKET_TYPE_COMMENT);
  packetno++;
  free(tempbuf);

  last_granulepos = 0;
}

void textsubs_packetizer_c::reset() {
}

int textsubs_packetizer_c::process(ogg_int64_t start, ogg_int64_t end,
                                   char *_subs, int last_sub) {
  ogg_packet     op;
  char          *tempbuf;
  int            clen, i, idx, num_newlines;
  unsigned char *bptr;
  char          *subs, *idx1, *idx2;

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

  /*
   * First create the 'empty' subtitle packet. Take the difference between
   * the current start and its old position as its duration. 
   */
  if (!omit_empty_packets) {
    clen = start - last_granulepos;
    for (i = 3; i >= 0; i--)
      if (clen > (1 << (8 * i)))
        break;
    i++;
    tempbuf = (char *)malloc(1 + i + 1);
    tempbuf[i + 1] = 0;
    tempbuf[0] = (((i & 3) << 6) + ((i & 4) >> 1)) |
      PACKET_IS_SYNCPOINT;
    bptr = (unsigned char *)&tempbuf[1];
    for (idx = 0; idx < i; idx++) {
      *(bptr + idx) = (unsigned char)(clen & 0xFF);
      clen >>= 8;
    }
    op.packet = (unsigned char *)tempbuf;
    op.bytes = i + 2;
    op.b_o_s = 0;
    op.e_o_s = 0;
    op.granulepos = last_granulepos;
    op.packetno = packetno++;
    ogg_stream_packetin(&os, &op);
    flush_pages();
    free(tempbuf);
  }
  
  clen = end - start;
  for (i = 3; i >= 0; i--)
    if (clen > (1 << (8 * i)))
      break;
  i++;

  idx1 = _subs;
  subs = NULL;
  num_newlines = 0;
  while (*idx1 != 0) {
    if (*idx1 == '\n')
      num_newlines++;
    idx1++;
  }
  subs = (char *)malloc(strlen(_subs) + num_newlines * 2 + 2);
  if (subs == NULL)
    die("malloc");

  idx1 = _subs;
  idx2 = subs;
  while (*idx1 != 0) {
    if (*idx1 == '\n') {
      *idx2 = '\r';
      idx2++;
      *idx2 = '\n';
      idx2++;
    } else if (*idx1 != '\r') {
      *idx2 = *idx1;
      idx2++;
    }
    idx1++;
  }
  *idx2 = 0;
  if (idx2 != subs) {
    idx2--;
    while ((idx2 != subs) && ((*idx2 == '\n') || (*idx2 == '\r'))) {
      *idx2 = 0;
      idx2--;
    }
  }

  tempbuf = (char *)malloc(strlen(subs) + i + 2);
  memcpy((char *)&tempbuf[i + 1], subs, strlen(subs) + 1);
  tempbuf[0] = (((i & 3) << 6) + ((i & 4) >> 1)) |
               PACKET_IS_SYNCPOINT;
  bptr = (unsigned char *)&tempbuf[1];
  for (idx = 0; idx < i; idx++) {
    *(bptr + idx) = (unsigned char)(clen & 0xFF);
    clen >>= 8;
  }
  op.packet = (unsigned char *)tempbuf;
  op.bytes = strlen(subs) + i + 2;
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

void textsubs_packetizer_c::produce_eos_packet() {
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
  
stamp_t textsubs_packetizer_c::make_timestamp(ogg_int64_t granulepos) {
  stamp_t stamp;

#ifndef XIPHCORRECTINTERLEAVING  
  stamp = granulepos * 1000;
#else
  stamp = old_granulepos * 1000;
#endif
  old_granulepos = granulepos;
  
  return stamp;
}
