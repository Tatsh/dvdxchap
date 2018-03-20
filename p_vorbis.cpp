/*
  ogmmerge -- utility for splicing together ogg bitstreams
      from component media subtypes

  p_vorbis.cpp
  Vorbis audio output module

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>

#include "ogmmerge.h"
#include "queue.h"
#include "r_ogm.h"
#include "p_vorbis.h"
#include "vorbis_header_utils.h"

vorbis_packetizer_c::vorbis_packetizer_c(audio_sync_t *nasync, range_t *nrange,
                                         char **ncomments) throw (error_c)
                                         : q_c() {
  packetno = 0;
  old_granulepos = 0;
  serialno = create_unique_serial();
  ogg_stream_init(&os, serialno);
  memcpy(&async, nasync, sizeof(audio_sync_t));
  memcpy(&range, nrange, sizeof(range_t));
  skip_packets = 0;
  last_granulepos = 0;
  last_granulepos_seen = 0;
  range_converted = 0;
  header_packet = NULL;
  comments = generate_vorbis_comment(ncomments);
}

vorbis_packetizer_c::~vorbis_packetizer_c() {
  ogg_stream_clear(&os);
  vorbis_comment_clear(&vc);
  vorbis_info_clear(&vi);
  if (comments != NULL) {
    vorbis_comment_clear(comments);
    free(comments);
  }
  if (header_packet != NULL) {
    if (header_packet->packet != NULL)
      free(header_packet->packet);
    free(header_packet);
  }
}

int vorbis_packetizer_c::encode_silence(int fd) {
  vorbis_dsp_state   lvds;
  vorbis_block       lvb;
  vorbis_info        lvi;
  vorbis_comment     lvc;
  ogg_stream_state   loss;
  ogg_page           log;
  ogg_packet         lop;
  ogg_packet         h_main, h_comments, h_codebook;
  int                samples, i, j, k, eos;
  float            **buffer;
  
  samples = vi.rate * async.displacement / 1000;
  vorbis_info_init(&lvi);
  if (vorbis_encode_setup_vbr(&lvi, vi.channels, vi.rate, 1))
    return EOTHER;
  vorbis_encode_setup_init(&lvi);
  vorbis_analysis_init(&lvds, &lvi);
  vorbis_block_init(&lvds, &lvb);
  vorbis_comment_init(&lvc);
  lvc.vendor = strdup(VERSIONINFO);
  lvc.user_comments = (char **)mmalloc(4);
  lvc.comment_lengths = (int *)mmalloc(4);
  lvc.comments = 0;
  ogg_stream_init(&loss, serialno);
  vorbis_analysis_headerout(&lvds, &lvc, &h_main, &h_comments, &h_codebook);
  ogg_stream_packetin(&loss, &h_main);
  ogg_stream_packetin(&loss, &h_comments);
  ogg_stream_packetin(&loss, &h_codebook);
  while (ogg_stream_flush(&loss, &log)) {
    write(fd, log.header, log.header_len);
    write(fd, log.body, log.body_len);
  }
  eos = 0;
  for (i = 0; i <= 1; i++) {
    if (i == 0) {
      buffer = vorbis_analysis_buffer(&lvds, samples);
      for (j = 0; j < samples; j++)
        for (k = 0; k < vi.channels; k++)
          buffer[k][j] = 0.0f;
      vorbis_analysis_wrote(&lvds, samples);
    } else
      vorbis_analysis_wrote(&lvds, 0);
    while (vorbis_analysis_blockout(&lvds, &lvb) == 1) {
      vorbis_analysis(&lvb, NULL);
      vorbis_bitrate_addblock(&lvb);
      while (vorbis_bitrate_flushpacket(&lvds, &lop)) {
        ogg_stream_packetin(&loss, &lop);
        while (!eos) {
          if (!ogg_stream_pageout(&loss, &log))
            break;
          write(fd, log.header, log.header_len);
          write(fd, log.body, log.body_len);
          if (ogg_page_eos(&log))
            eos = 1;
        }
      }
    }
  }

  ogg_stream_clear(&loss);
  vorbis_block_clear(&lvb);
  vorbis_dsp_clear(&lvds);
  vorbis_info_clear(&lvi);
  vorbis_comment_clear(&lvc);
  
  return 0;
}

void vorbis_packetizer_c::setup_displacement() {
  char tmpname[30];
  int fd, old_verbose, status;
  ogm_reader_c *ogm_reader;
  audio_sync_t nosync;
  range_t norange;
  generic_packetizer_c *old_packetizer;
  ogmmerge_page_t *mpage;
  
  if (async.displacement <= 0)
    return;
    
  strcpy(tmpname, "/tmp/ogmmerge-XXXXXX");
  if ((fd = mkstemp(tmpname)) == -1) {
    fprintf(stderr, "FATAL: vorbis_packetizer: mkstemp() failed.\n");
    exit(1);
  }
  if (encode_silence(fd) < 0) {
    fprintf(stderr, "FATAL: Could not encode silence.\n");
    exit(1);
  }
  close(fd);  

  old_verbose = verbose;
  memset(&norange, 0, sizeof(range_t));
  verbose = 0;
  nosync.displacement = 0;
  nosync.linear = 1.0;
  try {
    ogm_reader = new ogm_reader_c(tmpname, NULL, NULL, NULL, &nosync,
                                  &norange, NULL, NULL);
  } catch (error_c error) {
    fprintf(stderr, "FATAL: vorbis_packetizer: Could not create an " \
            "ogm_reader for the temporary file.\n%s\n", error.get_error());
    exit(1);
  }
  ogm_reader->overwrite_eos(1);
  memcpy(&nosync, &async, sizeof(audio_sync_t));
  async.displacement = 0;
  async.linear = 1.0;
  status = ogm_reader->read();
  mpage = ogm_reader->get_header_page();
  free(mpage->og->header);
  free(mpage->og->body);
  free(mpage->og);
  free(mpage);
  skip_packets = 2;
  old_packetizer = ogm_reader->set_packetizer(this);
  while (status == EMOREDATA)
    status = ogm_reader->read();
  ogm_reader->set_packetizer(old_packetizer);
  delete ogm_reader;
  verbose = old_verbose;
  unlink(tmpname);
  memcpy(&async, &nosync, sizeof(audio_sync_t));
}

void vorbis_packetizer_c::reset() {
}

void vorbis_packetizer_c::produce_header_packets() {
  vorbis_info_init(&vi);
  vorbis_comment_init(&vc);
  vorbis_synthesis_headerin(&vi, &vc, header_packet);
  packetno = 1;
  ogran = header_packet->granulepos;
  ogg_stream_packetin(&os, header_packet);
  flush_pages(PACKET_TYPE_HEADER);
  /*
   * Convert the range parameters from seconds to samples for easier handling
   * later on.
   */
  if (!range_converted) {
    range.start *= vi.rate;
    range.end *= vi.rate;
    range_converted = 1;
  }
}

/* 
 * Some notes - processing is straight-forward if no AV synchronization
 * is needed - the packet is simply handed over to ogg_stream_packetin.
 * Unfortunately things are not that easy if AV sync is done. For a
 * negative displacement packets are simply discarded if their granulepos
 * is set before the displacement. For positive displacements the packetizer
 * has to generate silence and insert this silence just after the first
 * three stream packets - the Vorbis header, the comment and the
 * setup packets.
 * The creation of the silence is done by encoding 0 samples with 
 * libvorbis. This produces an OGG file that is read by a separate
 * ogm_reader_c. We set this very packetizer as the reader's packetizer
 * so that we get the packets we want from the 'silenced' file. This means
 * skipping the very first three packets, hence the 'skip_packets' variable.
 */
int vorbis_packetizer_c::process(ogg_packet *op, ogg_int64_t gran) {
  ogg_int64_t this_granulepos;
  
  /*
   * We might want to skip a certain amount of packets if this packetizer
   * is used by a second ogm_reader_c, one that reads from a 'silence'
   * file. skip_packets is then intially set to 3 in order to skip all
   * header packets.
   */
  if (skip_packets > 0) {
    skip_packets--;
    return EMOREDATA;
  }
  // The initial packet has the headers that we get our informations from.
  if (packetno == 0) {
    header_packet = duplicate_ogg_packet(op);
    produce_header_packets();
  } else if (packetno == 1) {
    /*
     * This is the comment packet. If the user supplied a comment string
     * via the command line then the current comments are replaced by them.
     * Otherwise simply copy the comments.
     */
    if ((comments == NULL) || (comments->comments == 0)) {
      ogg_stream_packetin(&os, op);
    } else {
      int clen, res;
      ogg_packet cop;
      
      clen = -1 * comments_to_buffer(comments, NULL, 0);
      cop.packet = (unsigned char *)mmalloc(clen);
      cop.bytes = clen;
      cop.b_o_s = 0;
      cop.e_o_s = 0;
      cop.granulepos = 0;
      cop.packetno = 1;
      res = comments_to_buffer(comments, (char *)cop.packet, clen);
      if (res < 0) {
        fprintf(stderr, "FATAL: p_vorbis: comments_to_buffer returned %d, " \
                "clen is %d\n", res, clen);
        exit(1);
      }
      ogg_stream_packetin(&os, &cop);
      free(cop.packet);
    }
    flush_pages(PACKET_TYPE_COMMENT);
    packetno++;
  } else {
    // Recalculate the granulepos if needed.
    if (op->granulepos == -1)
      op->granulepos = gran;
    /* 
     * Handle the displacements - but only if the packet itself is not
     * a header packet.
     */
    if ((async.displacement > 0) && (packetno > 2))
      op->granulepos += vi.rate * async.displacement / 1000;
    else if ((packetno > 2) && (async.displacement < 0))
      op->granulepos += vi.rate * async.displacement / 1000;
    // Handle the linear sync - simply multiply with the given factor.
    op->granulepos = (u_int64_t)((double)op->granulepos * async.linear);

    this_granulepos = op->granulepos;

    if (packetno == 2) {
    /*
     * The first three packets have to be copied as they are all header/
     * comments/setup packets.
     */
      ogg_stream_packetin(&os, op);
      flush_pages();
      packetno++;
      setup_displacement();
    }
    // range checking
    else if ((op->granulepos >= range.start) &&
             (last_granulepos_seen >= range.start) &&
             ((range.end == 0) || (op->granulepos <= range.end))) {
      // Adjust the granulepos
      op->granulepos = (u_int64_t)(op->granulepos - range.start);
      // If no or a positive displacement is set the packet has to be output.
      if (async.displacement >= 0) {
        ogg_stream_packetin(&os, op);
        packetno++;
      }
      /*
       * Only output the current packet if its granulepos ( = position in 
       * samples) is bigger than the displacement.
       */
      else if (op->granulepos > 0) {
        ogg_stream_packetin(&os, op);
        packetno++;
      }
      queue_pages();
      last_granulepos = op->granulepos;
    } else if (op->e_o_s) {
      ogg_packet p;
      unsigned char c = 0;
      
      flush_pages();
      memcpy(&p, op, sizeof(ogg_packet));
      p.granulepos = last_granulepos;
      p.packet = &c;
      p.bytes = 1;
      ogg_stream_packetin(&os, &p);
      flush_pages();
      packetno++;
    }
    
    last_granulepos_seen = this_granulepos;
  }

  return EMOREDATA;
}

void vorbis_packetizer_c::produce_eos_packet() {
  ogg_packet op;
  
  op.packet = (unsigned char *)"";
  op.bytes = 1;
  op.b_o_s = 0;
  op.e_o_s = 1;
  op.granulepos = ogran;
  op.packetno = 99999999;
  process(&op, ogran);
}
  
stamp_t vorbis_packetizer_c::make_timestamp(ogg_int64_t granulepos) {
  stamp_t stamp;
  
  if (vi.rate == 0)
    die("vi.rate == 0!");

#ifndef INCORRECT_INTERLEAVING
  if (granulepos == -1)
    stamp = (u_int64_t)((double)old_granulepos * (double)1000000 /
                        (double)vi.rate);
  else {
    stamp = (u_int64_t)((double)old_granulepos * (double)1000000 /
                        (double)vi.rate);
    old_granulepos = granulepos;
  }
#else
  if (granulepos == -1)
    stamp = (u_int64_t)((double)granulepos * (double)1000000 /
                        (double)vi.rate);
  else
    stamp = (u_int64_t)((double)granulepos * (double)1000000 /
                        (double)vi.rate);
#endif
  
  return stamp;
}

