/*
  ogmmerge -- utility for splicing together ogg bitstreams
      from component media subtypes

  p_mp3.cpp
  MP3 output module

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
#include "mp3_common.h"
#include "p_mp3.h"
#include "vorbis_header_utils.h"

mp3_packetizer_c::mp3_packetizer_c(unsigned long nsamples_per_sec,
                                   int nchannels, int nmp3rate,
                                   audio_sync_t *nasync, range_t *nrange,
                                   char **ncomments) throw (error_c) : q_c() {
  serialno = create_unique_serial();
  ogg_stream_init(&os, serialno);
  packetno = 0;
  samples_per_sec = nsamples_per_sec;
  channels = nchannels;
  mp3rate = nmp3rate;
  bytes_output = 0;
  memcpy(&async, nasync, sizeof(audio_sync_t));
  memcpy(&range, nrange, sizeof(range_t));
  comments = generate_vorbis_comment(ncomments);
  old_granulepos = 0;
  packet_buffer = NULL;
  buffer_size = 0;
  eos = 0;
}

mp3_packetizer_c::~mp3_packetizer_c() {
  ogg_stream_clear(&os);
  if (comments != NULL) {
    vorbis_comment_clear(comments);
    free(comments);
  }
  if (packet_buffer != NULL)
    free(packet_buffer);
}

void mp3_packetizer_c::add_to_buffer(char *buf, int size) {
  char *new_buffer;
  
  new_buffer = (char *)realloc(packet_buffer, buffer_size + size);
  if (new_buffer == NULL)
    die("realloc");
  
  memcpy(new_buffer + buffer_size, buf, size);
  packet_buffer = new_buffer;
  buffer_size += size;
}

int mp3_packetizer_c::mp3_packet_available() {
  unsigned long header;
  int           pos;
  mp3_header_t  mp3header;
  
  if (packet_buffer == NULL)
    return 0;
  pos = find_mp3_header(packet_buffer, buffer_size, &header);
  if (pos < 0)
    return 0;
  decode_mp3_header(header, &mp3header);
  if ((pos + mp3header.framesize + 4) > buffer_size)
    return 0;
  
  return 1;
}

void mp3_packetizer_c::remove_mp3_packet(int pos, int framesize) {
  int   new_size;
  char *temp_buf;
  
  new_size = buffer_size - (pos + framesize + 4) + 1;
  temp_buf = (char *)malloc(new_size);
  if (temp_buf == NULL)
    die("malloc");
  if (new_size != 0)
    memcpy(temp_buf, &packet_buffer[pos + framesize + 4 - 1], new_size);
  free(packet_buffer);
  packet_buffer = temp_buf;
  buffer_size = new_size;
}

char *mp3_packetizer_c::get_mp3_packet(unsigned long *header,
                                       mp3_header_t *mp3header) {
  int     pos;
  char   *buf;
  double  pims;
  
  if (packet_buffer == NULL)
    return 0;
  pos = find_mp3_header(packet_buffer, buffer_size, header);
  if (pos < 0)
    return 0;
  decode_mp3_header(*header, mp3header);
  if ((pos + mp3header->framesize + 4) > buffer_size)
    return 0;

  pims = 1000.0 * 1152.0 / mp3_freqs[mp3header->sampling_frequency];

  if (async.displacement < 0) {
    /*
     * MP3 audio synchronization. displacement < 0 means skipping an
     * appropriate number of packets at the beginning.
     */
    async.displacement += (int)pims;
    if (async.displacement > -(pims / 2))
      async.displacement = 0;
    
    remove_mp3_packet(pos, mp3header->framesize);
    
    return 0;
  }

  if ((verbose > 1) && (pos > 1))
    fprintf(stdout, "mp3_packetizer: skipping %d bytes (no valid MP3 header "
            "found).\n", pos);
  buf = (char *)malloc(mp3header->framesize + 4);
  if (buf == NULL)
    die("malloc");
  memcpy(buf, packet_buffer + pos, mp3header->framesize + 4);
  
  if (async.displacement > 0) {
    /*
     * MP3 audio synchronization. displacement > 0 is solved by creating
     * silent MP3 packets and repeating it over and over again (well only as
     * often as necessary of course. Wouldn't want to spoil your movie by
     * providing a silent MP3 stream ;)).
     */
    async.displacement -= (int)pims;
    if (async.displacement < (pims / 2))
      async.displacement = 0;
    memset(buf + 4, 0, mp3header->framesize);
    
    return buf;
  }

  remove_mp3_packet(pos, mp3header->framesize);

  return buf;
}

void mp3_packetizer_c::produce_header_packets() {
  stream_header  sh;
  unsigned char *tempbuf;
  ogg_packet     op;
  int            clen, res;

  memset(&sh, 0, sizeof(sh));
  strcpy(sh.streamtype, "audio");
  memcpy(sh.subtype, "0055", 4);
  put_uint32(&sh.size, sizeof(sh));
  put_uint64(&sh.time_unit, 10000000);
  put_uint64(&sh.samples_per_unit, samples_per_sec);
  put_uint32(&sh.default_len, 1);
  put_uint32(&sh.buffersize, samples_per_sec);
  put_uint16(&sh.bits_per_sample, 0);
  put_uint16(&sh.sh.audio.channels, channels);
  put_uint16(&sh.sh.audio.blockalign, 1152);
  put_uint32(&sh.sh.audio.avgbytespersec, mp3rate * 1000 / 8);

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
    fprintf(stderr, "FATAL: p_mp3: comments_to_buffer returned %d, " \
            "clen is %d\n", res, clen);
    exit(1);
  }
  ogg_stream_packetin(&os, &op);
  flush_pages(PACKET_TYPE_COMMENT);
  packetno++;
  free(tempbuf);
}

int mp3_packetizer_c::process(char *buf, int size, int last_frame) {
  ogg_packet     op;
  int            i, tmpsize;
  unsigned char *bptr, *tempbuf;
  char          *packet;
  unsigned long  header;
  mp3_header_t   mp3header;

  if (packetno == 0)
    produce_header_packets();

  add_to_buffer(buf, size);
  while ((packet = get_mp3_packet(&header, &mp3header)) != NULL) {
    if ((4 - ((header >> 17) & 3)) != 3) {
      fprintf(stdout, "Warning: p_mp3: packet is not a valid MP3 packet (" \
              "packet number %lld)\n", packetno - 2);
      return EMOREDATA;
    }  

    tempbuf = (unsigned char *)malloc(mp3header.framesize + 5 +
                                      sizeof(u_int16_t));
    if (tempbuf == NULL)
      die("malloc");
    tempbuf[0] = (((sizeof(u_int16_t) & 3) << 6) +
                  ((sizeof(u_int16_t) & 4) >> 1)) | PACKET_IS_SYNCPOINT;
    op.bytes = mp3header.framesize + 5 + sizeof(u_int16_t);
    bptr = (unsigned char *)&tempbuf[1];
    for (i = 0, tmpsize = 1152; i < sizeof(u_int16_t); i++) {
      *(bptr + i) = (unsigned char)(tmpsize & 0xFF);
      tmpsize = tmpsize >> 8;
    }
    memcpy(&tempbuf[1 + sizeof(u_int16_t)], packet, mp3header.framesize + 4);
    op.packet = (unsigned char *)&tempbuf[0];
    op.b_o_s = 0;
    if (last_frame && !mp3_packet_available()) {
      op.e_o_s = 1;
      eos = 1;
    } else
      op.e_o_s = 0;
    op.granulepos = (u_int64_t)((packetno - 2) * 1152 * async.linear);
    op.packetno = packetno++;
    ogg_stream_packetin(&os, &op);
    bytes_output += mp3header.framesize + 4;
    free(tempbuf);
    free(packet);
  }

  if (last_frame) {
    flush_pages();
    return 0;
  } else {
    queue_pages();
    return EMOREDATA;
  }
}

void mp3_packetizer_c::reset() {
}

void mp3_packetizer_c::produce_eos_packet() {
  ogg_packet op;

  if (eos)
    return;  
  op.packet = (unsigned char *)"";
  op.bytes = 1;
  op.b_o_s = 0;
  op.e_o_s = 1;
  op.granulepos = (u_int64_t)((packetno - 2) * 1152);
  op.packetno = packetno++;
  ogg_stream_packetin(&os, &op);
  flush_pages();
  eos = 1;
}
  
stamp_t mp3_packetizer_c::make_timestamp(ogg_int64_t granulepos) {
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
