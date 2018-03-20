/*
  ogmmerge -- utility for splicing together ogg bitstreams
      from component media subtypes

  p_pcm.cpp
  raw & uncompressed PCM output module

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
#include "p_pcm.h"
#include "vorbis_header_utils.h"

pcm_packetizer_c::pcm_packetizer_c(unsigned long nsamples_per_sec,
                                   int nchannels, int nbits_per_sample,
                                   audio_sync_t *nasync, range_t *nrange,
                                   char **ncomments) throw (error_c) : q_c() {
  serialno = create_unique_serial();
  ogg_stream_init(&os, serialno);
  packetno = 0;
  bps = nchannels * nbits_per_sample * nsamples_per_sec / 8;
  tempbuf = (char *)malloc(bps + 128);
  if (tempbuf == NULL)
    die("malloc");
  samples_per_sec = nsamples_per_sec;
  channels = nchannels;
  bits_per_sample = nbits_per_sample;
  bytes_output = 0;
  memcpy(&async, nasync, sizeof(audio_sync_t));
  memcpy(&range, nrange, sizeof(range_t));
  comments = generate_vorbis_comment(ncomments);
  old_granulepos = 0;
}

pcm_packetizer_c::~pcm_packetizer_c() {
  ogg_stream_clear(&os);
  if (tempbuf != NULL)
    free(tempbuf);
  if (comments != NULL) {
    vorbis_comment_clear(comments);
    free(comments);
  }
}

void pcm_packetizer_c::reset() {
}

void pcm_packetizer_c::produce_header_packets() {
  ogg_packet    op;
  stream_header sh;
  int           clen, res;

  memset(&sh, 0, sizeof(sh));
  strcpy(sh.streamtype, "audio");
  memcpy(sh.subtype, "0001", 4);
  put_uint32(&sh.size, sizeof(sh));
  put_uint64(&sh.time_unit, 10000000);
  put_uint64(&sh.samples_per_unit, samples_per_sec);
  put_uint32(&sh.default_len, 1);
  put_uint32(&sh.buffersize, bps);
  put_uint16(&sh.bits_per_sample, bits_per_sample);
  put_uint16(&sh.sh.audio.channels, channels);
  put_uint16(&sh.sh.audio.blockalign, channels * bits_per_sample / 8);
  put_uint32(&sh.sh.audio.avgbytespersec, sh.buffersize);

  *((unsigned char *)tempbuf) = PACKET_TYPE_HEADER;
  memcpy((char *)&tempbuf[1], &sh, sizeof(sh));
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

  /* Create the comments packet */
  clen = -1 * comments_to_buffer(comments, NULL, 0);
  op.packet = (unsigned char *)tempbuf;
  op.bytes = clen;
  op.b_o_s = 0;
  op.e_o_s = 0;
  op.granulepos = 0;
  op.packetno = 1;
  if ((res = comments_to_buffer(comments, (char *)op.packet, clen)) < 0) {
    fprintf(stderr, "FATAL: p_pcm: comments_to_buffer returned %d, " \
            "clen is %d\n", res, clen);
    exit(1);
  }
  ogg_stream_packetin(&os, &op);
  flush_pages(PACKET_TYPE_COMMENT);
  packetno++;
}

int pcm_packetizer_c::process(char *buf, int size, int last_frame) {
  ogg_packet    op;
  int           i;

  int start, j;
  u_int16_t samp_in_subpacket;
  unsigned char *bptr;
  int bytes_per_subpacket;
  int remaining_bytes;
  int complete_subpackets;

  if (size > bps) { 
    fprintf(stderr, "FATAL: pcm_packetizer: size (%d) > bps (%d)\n", size,
            bps);
    exit(1);
  }

  if (packetno == 0)
    produce_header_packets();

  bytes_per_subpacket = bps / pcm_interleave;
  complete_subpackets = size / bytes_per_subpacket;
  remaining_bytes = size % bytes_per_subpacket;
  
  memcpy(&tempbuf[1 + sizeof(samp_in_subpacket)], buf, size);
  for (i = 0; i < complete_subpackets; i++) {
    int last_packet = 0;
    if (last_frame && (i == (complete_subpackets - 1)) &&
        (remaining_bytes == 0))
      last_packet = 1;
    start = i * bytes_per_subpacket;
    samp_in_subpacket = bytes_per_subpacket * 8 / bits_per_sample / channels;
    tempbuf[start] = ((sizeof(samp_in_subpacket) & 3) << 6) +
                     ((sizeof(samp_in_subpacket) & 4) >> 1);
    tempbuf[start] |= (i == 0 ? PACKET_IS_SYNCPOINT : 0);
    op.bytes = bytes_per_subpacket + 1 + sizeof(samp_in_subpacket);
    bptr = (unsigned char *)&tempbuf[start + 1];
    for (j = 0; j < sizeof(samp_in_subpacket); j++) {
      *(bptr + j) = (unsigned char)(samp_in_subpacket & 0xFF);
      samp_in_subpacket = samp_in_subpacket >> 8;
    }
    op.packet = (unsigned char *)&tempbuf[start];
    op.b_o_s = 0;
    op.e_o_s = last_packet;
    op.granulepos = (u_int64_t)(bytes_output * 8 / bits_per_sample /
                                channels);
    op.packetno = packetno++;
    ogg_stream_packetin(&os, &op);
    bytes_output += bytes_per_subpacket;
  }
  if (remaining_bytes) {
    int last_packet = last_frame;
    start = complete_subpackets * bytes_per_subpacket;
    samp_in_subpacket = remaining_bytes * 8 / bits_per_sample / channels;
    tempbuf[start] = ((sizeof(samp_in_subpacket) & 3) << 6) +
                     ((sizeof(samp_in_subpacket) & 4) >> 1);
    tempbuf[start] |= (i == 0 ? PACKET_IS_SYNCPOINT : 0);
    op.bytes = remaining_bytes + 1 + sizeof(samp_in_subpacket);
    bptr = (unsigned char *)&tempbuf[start + 1];
    for (j = 0; j < sizeof(samp_in_subpacket); j++) {
      *(bptr + j) = (unsigned char)(samp_in_subpacket & 0xFF);
      samp_in_subpacket = samp_in_subpacket >> 8;
    }
    op.packet = (unsigned char *)&tempbuf[start];
    op.b_o_s = 0;
    op.e_o_s = last_packet;
    op.granulepos = (u_int64_t)(bytes_output * 8 / bits_per_sample /
                                channels);
    op.packetno = packetno++;
    ogg_stream_packetin(&os, &op);
    bytes_output += remaining_bytes;
  }
  
  if (last_frame)
    flush_pages();
  else
    queue_pages();

  return EMOREDATA;
}

void pcm_packetizer_c::produce_eos_packet() {
  char buf = 0;
  process(&buf, 1, 1);
}
  
stamp_t pcm_packetizer_c::make_timestamp(ogg_int64_t granulepos) {
  stamp_t stamp;

#ifndef INCORRECT_INTERLEAVING
  stamp = (stamp_t)((double)old_granulepos * 1000000.0 /
           (double)samples_per_sec);
  old_granulepos = granulepos;
#else
  stamp = (stamp_t)((double)granulepos * 1000000.0 /
           (double)samples_per_sec);
#endif
  
  return stamp;
}
