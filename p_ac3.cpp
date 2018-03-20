/*
  ogmmerge -- utility for splicing together ogg bitstreams
  from component media subtypes

  p_ac3.cpp
  AC3 output module

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
#include "ac3_common.h"
#include "p_ac3.h"
#include "vorbis_header_utils.h"

ac3_packetizer_c::ac3_packetizer_c(unsigned long nsamples_per_sec,
                                   int nchannels, int nbitrate,
                                   audio_sync_t *nasync, range_t *nrange,
                                   char **ncomments) throw (error_c) : q_c() {
  serialno = create_unique_serial();
  ogg_stream_init(&os, serialno);
  packetno = 0;
  bytes_output = 0;
  memcpy(&async, nasync, sizeof(audio_sync_t));
  memcpy(&range, nrange, sizeof(range_t));
  comments = generate_vorbis_comment(ncomments);
  old_granulepos = 0;
  packet_buffer = NULL;
  buffer_size = 0;
  eos = 0;
  set_params(nsamples_per_sec, nchannels, nbitrate);
}

ac3_packetizer_c::~ac3_packetizer_c() {
  ogg_stream_clear(&os);
  if (comments != NULL) {
    vorbis_comment_clear(comments);
    free(comments);
  }
  if (packet_buffer != NULL)
    free(packet_buffer);
}

void ac3_packetizer_c::set_params(unsigned long nsamples_per_sec,
                                  int nchannels, int nbitrate) {
  samples_per_sec = nsamples_per_sec;
  channels = nchannels;
  bitrate = nbitrate;
}

void ac3_packetizer_c::add_to_buffer(char *buf, int size) {
  char *new_buffer;
  
  new_buffer = (char *)realloc(packet_buffer, buffer_size + size);
  if (new_buffer == NULL)
    die("realloc");
  
  memcpy(new_buffer + buffer_size, buf, size);
  packet_buffer = new_buffer;
  buffer_size += size;
}

int ac3_packetizer_c::ac3_packet_available() {
  int           pos;
  ac3_header_t  ac3header;
  
  if (packet_buffer == NULL)
    return 0;
  pos = find_ac3_header((unsigned char *)packet_buffer, buffer_size,
                        &ac3header);
  if (pos < 0)
    return 0;
  
  return 1;
}

void ac3_packetizer_c::remove_ac3_packet(int pos, int framesize) {
  int   new_size;
  char *temp_buf;
  
  new_size = buffer_size - (pos + framesize);
  if (new_size != 0) {
    temp_buf = (char *)malloc(new_size);
    if (temp_buf == NULL)
      die("malloc");
    memcpy(temp_buf, &packet_buffer[pos + framesize],
           new_size);
  } else
    temp_buf = NULL;
  free(packet_buffer);
  packet_buffer = temp_buf;
  buffer_size = new_size;
}

char *ac3_packetizer_c::get_ac3_packet(unsigned long *header,
                                       ac3_header_t *ac3header) {
  int     pos;
  char   *buf;
  double  pims;
  
  if (packet_buffer == NULL)
    return 0;
  pos = find_ac3_header((unsigned char *)packet_buffer, buffer_size, ac3header);
  if (pos < 0)
    return 0;
  if ((pos + ac3header->bytes) > buffer_size)
    return 0;

  pims = ((double)ac3header->bytes) * 1000.0 /
         ((double)ac3header->bit_rate / 8.0);

  if (async.displacement < 0) {
    /*
     * AC3 audio synchronization. displacement < 0 means skipping an
     * appropriate number of packets at the beginning.
     */
    async.displacement += (int)pims;
    if (async.displacement > -(pims / 2))
      async.displacement = 0;
    
    remove_ac3_packet(pos, ac3header->bytes);
    
    return 0;
  }

  if (verbose && (pos > 1))
    fprintf(stdout, "ac3_packetizer: skipping %d bytes (no valid AC3 header "
            "found). This might make audio/video go out of sync, but this "
            "stream is damaged.\n", pos);
  buf = (char *)malloc(ac3header->bytes);
  if (buf == NULL)
    die("malloc");
  memcpy(buf, packet_buffer + pos, ac3header->bytes);
  
  if (async.displacement > 0) {
    /*
     * AC3 audio synchronization. displacement > 0 is solved by duplicating
     * the very first AC3 packet as often as necessary. I cannot create
     * a packet with total silence because I don't know how, and simply
     * settings the packet's values to 0 does not work as the AC3 header
     * contains a CRC of its data.
     */
    async.displacement -= (int)pims;
    if (async.displacement < (pims / 2))
      async.displacement = 0;
    
    return buf;
  }

  remove_ac3_packet(pos, ac3header->bytes);
  
  return buf;
}

void ac3_packetizer_c::produce_header_packets() {
  stream_header  sh;
  unsigned char *tempbuf;
  ogg_packet     op;
  int            clen, res;

  memset(&sh, 0, sizeof(sh));
  strcpy(sh.streamtype, "audio");
  memcpy(sh.subtype, "2000", 4);
  put_uint32(&sh.size, sizeof(sh));
  put_uint64(&sh.time_unit, 10000000);
  put_uint64(&sh.samples_per_unit, samples_per_sec);
  put_uint32(&sh.default_len, 1);
  put_uint32(&sh.buffersize, samples_per_sec);
  put_uint16(&sh.bits_per_sample, 2);
  put_uint16(&sh.sh.audio.channels, channels);
  put_uint16(&sh.sh.audio.blockalign, 1536);
  put_uint32(&sh.sh.audio.avgbytespersec, bitrate * 1000 / 8);

  tempbuf = (unsigned char *)malloc(sizeof(sh) + 1);
  if (tempbuf == NULL)
    die("malloc");
  *tempbuf = PACKET_TYPE_HEADER;
  memcpy((char *)&tempbuf[1], &sh, sizeof(sh));
  op.packet = tempbuf;
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
  tempbuf = (unsigned char *)malloc(clen);
  if (tempbuf == NULL)
    die("malloc");
  op.packet = tempbuf;
  op.bytes = clen;
  op.b_o_s = 0;
  op.e_o_s = 0;
  op.granulepos = 0;
  op.packetno = 1;
  if ((res = comments_to_buffer(comments, (char *)op.packet, clen)) < 0) {
    fprintf(stderr, "FATAL: p_ac3: comments_to_buffer returned %d, " \
            "clen is %d\n", res, clen);
    exit(1);
  }
  ogg_stream_packetin(&os, &op);
  flush_pages(PACKET_TYPE_COMMENT);
  packetno++;
  free(tempbuf);
}

int ac3_packetizer_c::process(char *buf, int size, int last_frame) {
  ogg_packet     op;
  int            i, tmpsize;
  unsigned char *bptr, *tempbuf;
  char          *packet;
  unsigned long  header;
  ac3_header_t   ac3header;

  add_to_buffer(buf, size);
  while ((packet = get_ac3_packet(&header, &ac3header)) != NULL) {
    if (packetno == 0) {
      set_params(ac3header.sample_rate, ac3header.channels,
                 ac3header.bit_rate / 1000);
      produce_header_packets();
    }
    
    tempbuf = (unsigned char *)malloc(ac3header.bytes + 1 + sizeof(u_int16_t));
    if (tempbuf == NULL)
      die("malloc");
    tempbuf[0] = (((sizeof(u_int16_t) & 3) << 6) +
                  ((sizeof(u_int16_t) & 4) >> 1)) | PACKET_IS_SYNCPOINT;
    op.bytes = ac3header.bytes + 1 + sizeof(u_int16_t);
    bptr = (unsigned char *)&tempbuf[1];
    for (i = 0, tmpsize = 1536; i < sizeof(u_int16_t); i++) {
      *(bptr + i) = (unsigned char)(tmpsize & 0xFF);
      tmpsize = tmpsize >> 8;
    }
    memcpy(&tempbuf[1 + sizeof(u_int16_t)], packet, ac3header.bytes);
    op.packet = (unsigned char *)&tempbuf[0];
    op.b_o_s = 0;
    if (last_frame && !ac3_packet_available()) {
      op.e_o_s = 1;
      eos = 1;
    } else
      op.e_o_s = 0;
    op.granulepos = (u_int64_t)((packetno - 2) * 1536 * async.linear);
    op.packetno = packetno++;
    ogg_stream_packetin(&os, &op);
    if (force_flushing)
      flush_pages();
    bytes_output += ac3header.bytes;
    free(tempbuf);
    free(packet);
  }

  if (last_frame) {
    flush_pages();
    return 0;
  } else {
    if (!force_flushing)
      queue_pages();
    return EMOREDATA;
  }
}

void ac3_packetizer_c::reset() {
}

void ac3_packetizer_c::produce_eos_packet() {
  ogg_packet op;

  if (eos)
    return;  
  op.packet = (unsigned char *)"";
  op.bytes = 1;
  op.b_o_s = 0;
  op.e_o_s = 1;
  op.granulepos = (u_int64_t)((packetno - 2) * 1536);
  op.packetno = packetno++;
  ogg_stream_packetin(&os, &op);
  flush_pages();
  eos = 1;
}
  
stamp_t ac3_packetizer_c::make_timestamp(ogg_int64_t granulepos) {
  stamp_t stamp;

#ifndef INCORRECT_INTERLEAVING
  stamp = (stamp_t)((double)old_granulepos * (double)1000000.0 /
                    (double)samples_per_sec);
  old_granulepos = granulepos;
#else
  stamp = (stamp_t)((double)granulepos * 1000000.0 /
                    (double)samples_per_sec);
#endif

  return stamp;
}
