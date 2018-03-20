/*
  ogminfo -- utility for getting informations about an OGG media file

  Written by Moritz Bunkus <moritz@bunkus.org>

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include "common.h"
#include "ogmstreams.h"
#include "vorbis_header_utils.h"

#define BLOCK_SIZE 4096
//#define BLOCK_SIZE 1

typedef struct stream_t {
  int               serial;
  int               fd;
  int               vorbis;
  double            sample_rate;
  int               eos, comment;
  int               sno;
  char              stype;
  int               header_packets;
  ogg_stream_state  state;
  ogg_int64_t       last_granulepos, this_granulepos, biggest_granulepos;
  ogg_int64_t       num_packets;
  u_int64_t         size;
  struct stream_t  *next;
} stream_t;

stream_t    *first;
int          nastreams = 0, nvstreams = 0, ntstreams = 0, nistreams = 0;
int          numstreams = 0;
int          headers_read = 0;
ogg_int64_t  pagebytesread = 0;

void add_stream(stream_t *ndmx) {
  stream_t *cur = first;
  
  if (first == NULL) {
    first = ndmx;
    first->next = NULL;
  } else {
    cur = first;
    while (cur->next != NULL)
      cur = cur->next;
    cur->next = ndmx;
    ndmx->next = NULL;
  }
}

stream_t *find_stream(int fserial) {
  stream_t *cur = first;
  
  while (cur != NULL) {
    if (cur->serial == fserial)
      break;
    cur = cur->next;
  }
  
  return cur;
}

int verbose = 0;
int check_timing = 0;
int summary = 0;

void usage(char *fname) {
  fprintf(stdout,
    "Usage: %s [options] inname\n\n"
    " options:\n"
    "  inname         Use 'inname' as the source.\n"
    "  -v, --verbose  Increase verbosity. See the man page for a detailed\n"
    "                 description of what ogminfo outputs.\n"
    "  -s, --summary  Print a summary for each track.\n"
    "  -h, --help     Show this help.\n"
    "  -V, --version  Show version information.\n", fname);
}

#ifndef XIPHCORRECTINTERLEAVING  
#define OUTOFSYNC   ((((stream->stype == 't' ? end_pts : start_pts) \
                       - last_pts) < 0.0 ) ? "OUT_OF_SYNC " : "sync_ok ")
#else
#define OUTOFSYNC   (((start_pts - last_pts) < 0.0 ) ? \
                     "OUT_OF_SYNC " : "sync_ok ")
#endif
#define ISSYNCPOINT ((pack->packet[0] & PACKET_IS_SYNCPOINT) ? \
                     "IS_SYNCPOINT " : "")
#define E_O_S       (pack->e_o_s ? "EOS " : "")

double start_pts, end_pts, last_pts;

void dump_streamheader(stream_header *sth, char stype, int sno, int snum) {
  char streamtype[9], subtype[5];
  memcpy(streamtype, sth->streamtype, 8);
  streamtype[8] = 0;
  memcpy(subtype, sth->subtype, 4);
  subtype[4] = 0;
  fprintf(stdout, "(%s) (%c%d/serial %d) Full stream_header dump: {"
          "streamtype = \"%s\", subtype = \"%s\", size = %d, "
          "time_unit = %lld, samples_per_unit = %lld, default_len = %d, "
          "buffersize = %d, bits_per_sample = %d, "
          "sh = { video = { width = %d, height = %d} / audio = { "
          "channels = %d, blockalign = %d, avgbytespersec = %d } }\n",
          __FILE__, stype, sno, snum,
          streamtype, subtype, get_uint32(&sth->size),
          get_uint64(&sth->time_unit),
          get_uint64(&sth->samples_per_unit), get_uint32(&sth->default_len),
          get_uint32(&sth->buffersize),
          get_uint16(&sth->bits_per_sample),
          get_uint32(&sth->sh.video.width), get_uint32(&sth->sh.video.height),
          get_uint16(&sth->sh.audio.channels),
          get_uint16(&sth->sh.audio.blockalign),
          get_uint32(&sth->sh.audio.avgbytespersec));
}

int all_header_packets_dumped() {
  stream_t *stream = first;
  
  while (stream != NULL) {
    if ((stream->stype == 'V') && (stream->header_packets < 3))
      return 0;
    if (stream->header_packets < 2)
      return 0;
    stream = stream->next;
  }
  
  return 1;
}

void handle_packet(stream_t *stream, ogg_packet *pack, ogg_page *page) {
  if ((verbose == 1) && headers_read && all_header_packets_dumped() &&
      !summary)
    exit(0);
  if (pack->e_o_s)
    stream->eos = 1;
  if (ogg_page_granulepos(page) > stream->biggest_granulepos)
    stream->biggest_granulepos = ogg_page_granulepos(page);
  stream->num_packets++;
  stream->size += (u_int64_t)pack->bytes;
  if (verbose == 0)
    return;
  if ((*pack->packet & 3) == PACKET_TYPE_HEADER) {
    stream->header_packets++;
    if (verbose < 2)
      return;
    fprintf(stdout, "(%s) %c%d: header packet, length %ld\n", __FILE__,
            stream->stype, stream->sno, pack->bytes);
  } else if ((*pack->packet & 3) == PACKET_TYPE_COMMENT) {
    int            i;
    vorbis_comment vc;

    stream->header_packets++;
    stream->comment = 1;
    if (verbose < 1)
      return;
    vorbis_comment_init(&vc);
    if (vorbis_unpack_comment(&vc, pack->packet, pack->bytes) != 0)
      fprintf(stdout, "(%s) %c%d: comment packet, length %ld. This packet "
              "does NOT contain a valid Vorbis comment packet!\n", 
              __FILE__, stream->stype, stream->sno, pack->bytes);
    else {
      fprintf(stdout, "(%s) %c%d: comment packet, length %ld,", __FILE__,
              stream->stype, stream->sno, pack->bytes);
      if (vc.comments > 0) {
        fprintf(stdout, " %d user comment field%s:\n", vc.comments,
                vc.comments != 1 ? "s" : "");
        for (i = 0; i < vc.comments; i++)
          fprintf(stdout, "(%s) %c%d:   %s\n", __FILE__, stream->stype,
                  stream->sno, vc.user_comments[i]);
      } else
        fprintf(stdout, " no user comment fields available.\n");
    }
    vorbis_comment_clear(&vc);
  } else if ((stream->stype == 'a') && stream->vorbis) {
    if (verbose < 1)
      return;
    fprintf(stdout, "(%s) a%d: % 7ld bytes granulepos: % 10lld pno: % 10lld ",
             __FILE__, stream->sno, pack->bytes, ogg_page_granulepos(page),
             pack->packetno);
    if (check_timing && (stream->sample_rate != -1)) {
      end_pts = (double)ogg_page_granulepos(page) * 
                (double)1000.0 / (double)stream->sample_rate;
      start_pts = (double)stream->last_granulepos * 
                  (double)1000.0 / (double)stream->sample_rate;
      fprintf(stdout, " start: % 13.2fms  end: % 13.2fms %s",
              start_pts, end_pts, OUTOFSYNC);
      last_pts = start_pts;
    }
    fprintf(stdout, "%s%s\n", ISSYNCPOINT, E_O_S);
  } else {
    int hdrlen, i;
    long lenbytes = 0, n;

    if (verbose < 2)
      return;

    hdrlen = (*pack->packet & PACKET_LEN_BITS01) >> 6;
    hdrlen |= (*pack->packet & PACKET_LEN_BITS2) << 1;
    n = pack->bytes - 1 - hdrlen;
    for (i = 0; i < hdrlen; i++) {
      lenbytes = lenbytes << 8;
      lenbytes += *((unsigned char *)pack->packet + hdrlen - i);
    }
    if (stream->stype == 'i')
      fprintf(stdout, "(%s) i%d: % 7ld bytes", __FILE__, stream->sno, n);
    else {
      fprintf(stdout, "(%s) %c%d: % 7ld bytes granulepos: % 10lld pno: % "
              "10lld ", __FILE__, stream->stype, stream->sno, n,
              ogg_page_granulepos(page), pack->packetno);
      if (check_timing && (stream->sample_rate != -1)) {
        end_pts = (double)ogg_page_granulepos(page) * 
                  (double)1000.0 / (double)stream->sample_rate;
        start_pts = (double)stream->last_granulepos * 
                    (double)1000.0 / (double)stream->sample_rate;
        fprintf(stdout, " start: % 13.2fms  end: % 13.2fms %s",
                start_pts, end_pts, OUTOFSYNC);
        last_pts = start_pts;
      }
      fprintf(stdout, " hdrlen: %d", hdrlen);
      if (hdrlen > 0)
        fprintf(stdout, " duration: %ld", lenbytes);
    }
    fprintf(stdout, " %s%s\n", ISSYNCPOINT, E_O_S);
  }
}

void process_ogm(int fdin)
{
  ogg_sync_state    sync;
  ogg_page          page;
  ogg_packet        pack;
  ogg_stream_state  sstate;
  vorbis_info       vi;
  vorbis_comment    vc;
  char             *buf;
  int               nread, serialnumber;
  int               endofstream = 0;
  stream_t         *stream;
  long              pageseek;

  last_pts = 0.0;
  start_pts = 0.0;
  end_pts = 0.0;

  ogg_sync_init(&sync);
  while (1) {
    pageseek = ogg_sync_pageseek(&sync, &page);
    if (pageseek < 0) {
      fprintf(stderr, "(%s) ogg_sync_pageseek failed\n", __FILE__);
      return;
    } else if (pageseek == 0) {
      buf = ogg_sync_buffer(&sync, BLOCK_SIZE);
      if (!buf) {
        fprintf(stderr, "(%s) ogg_sync_buffer failed\n", __FILE__);
        return;
      }
      if ((nread = read(fdin, buf, BLOCK_SIZE)) <= 0) {
        if (verbose > 1)
          fprintf(stdout, "(%s) end of stream\n", __FILE__);
        return;
      }
      ogg_sync_wrote(&sync, nread);
      continue;
    }
    pagebytesread += pageseek;

    if (!ogg_page_bos(&page))
      break;

    if (verbose > 2)
      fprintf(stdout, "(%s) NEW PAGE pos: % 12lld, len: % 12ld, body_len: "
              "% 12ld\n", __FILE__, pagebytesread - pageseek, pageseek,
              page.body_len);

    serialnumber = ogg_page_serialno(&page);
    if (ogg_stream_init(&sstate, serialnumber)) {
      fprintf(stderr, "(%s) ogg_stream_init failed\n", __FILE__);
      return;
    }
    ogg_stream_pagein(&sstate, &page);
    ogg_stream_packetout(&sstate, &pack);

    if ((pack.bytes >= 7) && ! strncmp(&pack.packet[1], "vorbis", 6)) {
      stream = (stream_t *)mmalloc(sizeof(stream_t));
      stream->serial = serialnumber;
      stream->vorbis = 1;
      stream->sample_rate = -1;
      stream->sno = nastreams + 1;
      stream->stype = 'a';
      memcpy(&stream->state, &sstate, sizeof(sstate));
      vorbis_info_init(&vi);
      vorbis_comment_init(&vc);
      if (vorbis_synthesis_headerin(&vi, &vc, &pack) >= 0) {
        fprintf(stdout, "(%s) (a%d/serial %d) Vorbis audio (channels %d "
                "rate %ld)\n", __FILE__, nastreams + 1, serialnumber,
                vi.channels, vi.rate);
        stream->sample_rate = vi.rate;
      } else
        fprintf(stdout, "(%s) (a%d/serial %d) Vorbis audio stream indicated "
                "but no Vorbis stream header found.\n", __FILE__,
                nastreams + 1, serialnumber);
      do
        handle_packet(stream, &pack, &page);
      while (ogg_stream_packetout(&stream->state, &pack) == 1);
      add_stream(stream);
      nastreams++;
      numstreams++;
    } else if ((pack.bytes >= 142) &&
               !strncmp(&pack.packet[1],"Direct Show Samples embedded in Ogg",
                        35) ) {
      if ((get_uint32(pack.packet+96) == 0x05589f80) &&
          (pack.bytes >= 184)) {
         fprintf(stdout, "(%s) (v%d/serial %d) Found old video header. Not "
                 "supported.\n", __FILE__, nvstreams + 1, serialnumber);
      } else if (get_uint32(pack.packet+96) == 0x05589F81) {
        fprintf(stdout, "(%s) (a%d/serial %d) Found old audio header. Not "
                "supported.\n", __FILE__, nastreams + 1, serialnumber);
      } else {
        if (verbose > 0)
          fprintf(stdout, "(%s) OGG stream %d, serial %d, has an old header "
                  "with an unknown type.", __FILE__, numstreams + 1,
                  serialnumber);
        numstreams++;
      }
    } else if (((*pack.packet & PACKET_TYPE_BITS ) == PACKET_TYPE_HEADER) &&
	       (pack.bytes >= (int)(sizeof(old_stream_header) + 1))) {
      stream_header sth;
      copy_headers(&sth, (old_stream_header *)&pack.packet[1], pack.bytes);
      if (!strncmp(sth.streamtype, "video", 5)) {
        unsigned long codec;
        codec = (sth.subtype[0] << 24) + 
          (sth.subtype[1] << 16) + (sth.subtype[2] << 8) + sth.subtype[3]; 
        fprintf(stdout, "(%s) (v%d/serial %d) fps: %.3f width height: %dx%d "
                "codec: %p (%c%c%c%c)\n", __FILE__, nvstreams + 1,
                serialnumber, (double)10000000 /
                (double)get_uint64(&sth.time_unit),
                get_uint32(&sth.sh.video.width),
                get_uint32(&sth.sh.video.height), (void *)codec,
                sth.subtype[0], sth.subtype[1], sth.subtype[2],
                sth.subtype[3]);
        if (verbose > 3)
          dump_streamheader(&sth, 'v', nvstreams + 1, serialnumber);
        stream = (stream_t *)mmalloc(sizeof(stream_t));
        stream->stype = 'v';
        stream->serial = serialnumber;
        stream->sample_rate = (double)10000000 /
          (double)get_uint64(&sth.time_unit);
        stream->sno = nvstreams + 1;
        memcpy(&stream->state, &sstate, sizeof(sstate));
        do
          handle_packet(stream, &pack, &page);
        while (ogg_stream_packetout(&stream->state, &pack) == 1);
        add_stream(stream);
        nvstreams++;
        numstreams++;
      } else if (!strncmp(sth.streamtype, "audio", 5)) {
        int codec;
        char buf[5];
        memcpy(buf, sth.subtype, 4);
        buf[4] = 0;
        codec = strtoul(buf, NULL, 16);
        fprintf(stdout, "(%s) (a%d/serial %d) codec: %d (0x%04x) (%s) bits "
                "per sample: %d channels: %d  samples per second: %lld"
                " avgbytespersec: %d blockalign: %hd\n",
                __FILE__, nastreams + 1, serialnumber, codec, codec,
                codec == 0x1 ? "PCM" : codec == 55 ? "MP3" :
                codec == 0x55 ? "MP3" :
                codec == 0x2000 ? "AC3" : "unknown",
                get_uint16(&sth.bits_per_sample),
                get_uint16(&sth.sh.audio.channels),
                get_uint64(&sth.samples_per_unit),
                get_uint32(&sth.sh.audio.avgbytespersec),
                get_uint16(&sth.sh.audio.blockalign));
        if (verbose > 3)
          dump_streamheader(&sth, 'a', nastreams + 1, serialnumber);
        stream = (stream_t *)mmalloc(sizeof(stream_t));
        stream->sno = nastreams + 1;
        stream->stype = 'a';
        stream->sample_rate = get_uint64(&sth.samples_per_unit);
        stream->serial = serialnumber;
        memcpy(&stream->state, &sstate, sizeof(sstate));
        do
          handle_packet(stream, &pack, &page);
        while (ogg_stream_packetout(&stream->state, &pack) == 1);
        add_stream(stream);
        nastreams++;
        numstreams++;
      } else if (!strncmp(sth.streamtype, "text", 4)) {
        fprintf(stdout, "(%s) (t%d/serial %d) text/subtitle stream\n",
                __FILE__, ntstreams + 1, serialnumber);
        if (verbose > 3)
          dump_streamheader(&sth, 't', ntstreams + 1, serialnumber);
        stream = (stream_t *)mmalloc(sizeof(stream_t));
        stream->sno = ntstreams + 1;
        stream->stype = 't';
        stream->sample_rate = (double)10000000 /
          (double)get_uint64(&sth.time_unit);
        stream->serial = serialnumber;
        memcpy(&stream->state, &sstate, sizeof(sstate));
        do
          handle_packet(stream, &pack, &page);
        while (ogg_stream_packetout(&stream->state, &pack) == 1);
        add_stream(stream);
        ntstreams++;
        numstreams++;
      } else if (!strncmp(sth.streamtype, "index", 5)) {
        int idx_serial;

        idx_serial = *((int *)&sth.subtype[0]);
        fprintf(stdout, "(%s) (i%d/serial %d) index stream for video stream "
                "with the serial %d\n", __FILE__, nistreams + 1, serialnumber,
                idx_serial);
        stream = (stream_t *)mmalloc(sizeof(stream_t));
        stream->sno = nistreams + 1;
        stream->stype = 'i';
        stream->sample_rate = 0;
        stream->serial = serialnumber;
        memcpy(&stream->state, &sstate, sizeof(sstate));
        do
          handle_packet(stream, &pack, &page);
        while (ogg_stream_packetout(&stream->state, &pack) == 1);
        add_stream(stream);
        nistreams++;
        numstreams++;
      } else {
        fprintf(stdout, "(%s) (%d) found new header of unknown/"
                "unsupported type\n", __FILE__, numstreams + 1);
        numstreams++;
      }

    } else {
      fprintf(stdout, "(%s) OGG stream %d is of an unknown type "
              "(bad header?)\n", __FILE__, numstreams + 1);
      numstreams++;
    }
  }
  
  if ((verbose == 0) && !summary)
    exit(0);

  headers_read = 1;
  endofstream = 0;
  while (!endofstream) {
    serialnumber = ogg_page_serialno(&page);
    stream = find_stream(serialnumber);
    if (stream == NULL) {
      if (verbose > 2)
        fprintf(stdout, "(%s) Encountered packet for an unknown serial "
                "%d !?\n", __FILE__, serialnumber);
    } else {
      if (verbose > 2)
        fprintf(stdout, "(%s) %c%d: NEW PAGE pos: % 12lld, len: % 12ld, "
                "body_len: % 12ld\n",
                __FILE__, stream->stype, stream->sno, pagebytesread - pageseek,
                pageseek, page.body_len);
      ogg_stream_pagein(&stream->state, &page);
      stream->last_granulepos = stream->this_granulepos;
      stream->this_granulepos = ogg_page_granulepos(&page);
      while (ogg_stream_packetout(&stream->state, &pack) == 1)
        handle_packet(stream, &pack, &page);
    }

    while ((pageseek = ogg_sync_pageseek(&sync, &page)) <= 0) {
      buf = ogg_sync_buffer(&sync, BLOCK_SIZE);
      nread = read(fdin, buf, BLOCK_SIZE);
      if (nread <= 0) {
        stream = first;
        while (stream != NULL) {
          if (stream->eos == 0)
            fprintf(stdout, "(%s) (%c%d/serial %d) end-of-stream marker "
                    "missing\n", __FILE__, stream->stype, stream->sno,
                    stream->serial);
          if ((stream->comment != 1) && (verbose >= 1))
            fprintf(stdout, "(%s) (%c%d/serial %d) comment packet missing\n",
                    __FILE__, stream->stype, stream->sno, stream->serial);
          stream = stream->next;
        }
        if (verbose > 1)
          fprintf(stdout, "(%s) end of stream\n", __FILE__);
        endofstream = 1;
        break;
      } else
        ogg_sync_wrote(&sync, nread);
    }
    pagebytesread += pageseek;
  }
  
  if (summary) {
    stream = first;
    while (stream != NULL) {
      double now;

      now = stream->biggest_granulepos / (double)stream->sample_rate;
      fprintf(stdout, "(%s) (%c%d/serial %d) stream size: %llu bytes (%.3f "
              "kbit/s, %.3f KB/s), number of packets: %lld, length in "
              "seconds: %.3f\n", __FILE__, stream->stype, stream->sno,
              stream->serial, stream->size,
              (stream->size * 8.0 / 1000.0) / now,
              (stream->size / 1024.0) / now,
              stream->num_packets, now * 1000.0);
      stream = stream->next;
    }
  }
}

int main(int argc, char *argv[]) {
  int i, fdin = -1;
  
  nice(2);

  for (i = 1; i < argc; i++)
    if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version")) {
      fprintf(stdout, "ogminfo v" VERSION "\n");
      exit(0);
    }
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v")) {
      check_timing = 1;
      verbose++;
    } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      usage(argv[0]);
      return 0;
    } else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--summary"))
      summary = 1;
    else {
      fdin = open(argv[i], O_RDONLY);
      if (fdin == -1) {
        fprintf(stderr, "(%s) Could not open \"%s\".\n", __FILE__,
                argv[i]);
        return 1;
      }
    }
  }

  if (fdin == -1) {
    usage(argv[0]);
    exit(1);
  }
  
  if (verbose >= 2)
    summary = 1;
  process_ogm(fdin);  
  
  close(fdin);
  return 0;
}
