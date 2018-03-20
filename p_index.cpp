/*
  ogmmerge -- utility for splicing together ogg bitstreams
      from component media subtypes

  p_index.cpp
  video index

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

#ifdef ENABLE_INDEX

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <ogg/ogg.h>

#include "ogmmerge.h"
#include "queue.h"
#include "p_index.h"
#include "vorbis_header_utils.h"

index_packetizer_c::index_packetizer_c(int nserial) throw (error_c) : q_c() {
  serialno = create_unique_serial();
  ogg_stream_init(&os, serialno);
  granulepos = 0;
  serial = nserial;
  packetno = 0;
  produce_header_packets();
}

index_packetizer_c::~index_packetizer_c() {
}

void index_packetizer_c::reset() {
}

void index_packetizer_c::produce_header_packets() {
  ogg_packet      op;
  stream_header   sh;
  int             clen, res;
  char           *tempbuf;
  vorbis_comment *vc;

  tempbuf = (char *)malloc(sizeof(sh) + 64);
  if (tempbuf == NULL)
    die("malloc");
  memset(&sh, 0, sizeof(sh));
  strcpy(sh.streamtype, "index");
  put_uint32(&sh.subtype[0], serial);
  put_uint32(&sh.size, sizeof(sh));
  put_uint64(&sh.time_unit, 10000000);

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
  vc = generate_vorbis_comment(NULL);
  clen = -1 * comments_to_buffer(vc, NULL, 0);
  op.packet = (unsigned char *)tempbuf;
  op.bytes = clen;
  op.b_o_s = 0;
  op.e_o_s = 0;
  op.granulepos = 0;
  op.packetno = 1;
  if ((res = comments_to_buffer(vc, (char *)op.packet, clen)) < 0) {
    fprintf(stderr, "FATAL: p_index: comments_to_buffer returned %d, " \
            "clen is %d\n", res, clen);
    exit(1);
  }
  ogg_stream_packetin(&os, &op);
  flush_pages(PACKET_TYPE_COMMENT);
  packetno++;
  vorbis_comment_clear(vc);
  free(vc);
}

int index_packetizer_c::process(idx_entry *entries, int num) {
  ogg_packet op;
  char *tempbuf;

  tempbuf = (char *)malloc(num * sizeof(idx_entry) + 64);
  if (tempbuf == NULL)
    die("malloc");

  this->serial = serial;

  memcpy(&tempbuf[1], entries, num * sizeof(idx_entry));
  tempbuf[0] = PACKET_IS_SYNCPOINT;
  op.bytes = num * sizeof(idx_entry) + 1;
  op.packet = (unsigned char *)tempbuf;
  op.b_o_s = 0;
  op.e_o_s = 1;
  op.granulepos = 0;
  op.packetno = packetno++;
  ogg_stream_packetin(&os, &op);
  
  flush_pages();

  free(tempbuf);

  return EMOREDATA;
}

void index_packetizer_c::produce_eos_packet() {
}
  
stamp_t index_packetizer_c::make_timestamp(ogg_int64_t granulepos) {
  return 0;
}

#endif // ENABLE_INDEX
