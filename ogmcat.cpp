/*
  ogmcat -- utility for concatenating OGG media files

  Written by Moritz Bunkus <moritz@bunkus.org>

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#define OGMSPLIT
#include "ogmmerge.h"
#include "ogmstreams.h"
#include "queue.h"
#include "vorbis_header_utils.h"

#define BLOCK_SIZE  4096
#define MAXSYNCMODE    4
#define DEFSYNCMODE    4

class cat_packetizer_c: public q_c {
  private:
    ogg_int64_t old_granulepos;
    char        stype;
    double      sample_rate;
  public:
    cat_packetizer_c(double nsample_rate, int nserial, char nstype);
    virtual ~cat_packetizer_c();
    
    virtual stamp_t make_timestamp(ogg_int64_t granulepos);
    virtual void    process(ogg_packet *op);
    virtual void    produce_eos_packet() {};
    virtual void    produce_header_packets() {};
    virtual void    reset() {};
};

typedef struct stream_t {
  int                 serial;
  int                 eos, comment;
  char                stype;
  ogg_stream_state    instate;
  ogg_packet          header_packet, header_packet2, header_packet3;

  stream_header       sth;
  vorbis_info         vi;
  
  ogg_int64_t         last_granulepos, this_granulepos;
  ogg_int64_t         granulepos, granuleposadd;
  ogg_int64_t         packetno;
  ogg_int64_t         num_frames, cur_frame;
  double              sample_rate;
  vorbis_comment      vc;
  int                 vc_unpacked;
  ogmmerge_page_t    *mpage;
  cat_packetizer_c   *pzr;
  struct stream_t    *next;
} stream_t;

typedef struct source_t {
  char     *name;
  int       fd;
  off_t     size;
  double    manual_sync;
  stream_t *streams;
  source_t *next;
} source_t;

source_t       *sources = NULL;
int             numstreams = 0;
stream_t       *vstream = NULL;

ogg_int64_t     bread = 0;
ogg_int64_t     bwritten = 0, bwritten_all = 0;

FILE           *fout;
char           *fout_name;
int             fout_num = 1;

int             verbose = 0;
int             frontend_mode = 0;
int             safety = 1;
int             sync_mode = DEFSYNCMODE;

int             last_percent = 0;

/*
 * cat_packetizer class functions
 */

cat_packetizer_c::cat_packetizer_c(double nsample_rate, int nserial,
                                   char nstype) {
  old_granulepos = 0;
  stype = nstype;
  sample_rate = nsample_rate;
  serialno = nserial;
  ogg_stream_init(&os, serialno);
}

cat_packetizer_c::~cat_packetizer_c() {
}

stamp_t cat_packetizer_c::make_timestamp(ogg_int64_t granulepos) {
  stamp_t stamp;
  
#ifndef XIPHCORRECTINTERLEAVING  
  if (stype == 't')
    stamp = (stamp_t)((double)granulepos * (double)1000000.0 /
                      sample_rate);
  else
#endif
    stamp = (stamp_t)((double)old_granulepos * (double)1000000.0 /
                      sample_rate);
  old_granulepos = granulepos;
  
  return stamp;
}

void cat_packetizer_c::process(ogg_packet *op) {
  ogg_stream_packetin(&os, op);
  queue_pages();
}

/*
 * General helper functions, usage
 */

void usage(char *fname) {
  fprintf(stdout,
    "Usage: %s [options] -o out in1 [in2 [in3 ...]]\n\n"
    " options:\n"
    "  in1, in2 ...         Sources that will be concatenated.\n"
    "  -m, --manualsync <n> Additional sync in ms for the next input file.\n"
    "  -o, --output out     Use 'out' as the base name. This is mandatory.\n"
    "  -s, --sync <nr>      Uses sync mode <nr>. Valid values are 0..%d.\n"
    "                       Default is %d.\n"
    "  -n, --nosafetychecks Disable all safety checks. This might produce\n"
    "                       broken and unplayable files.\n"
/*    "  --frontend           Enable frontend mode. Progress output will be\n"
    "                       terminated by \\n instead of \\r.\n"*/
    "  -v, --verbose        Be verbose and show each OGG packet.\n"
    "                       Can be used twice to increase verbosity.\n"
    "  -h, --help           Show this help.\n"
    "  -V, --version        Show version information.\n", fname, MAXSYNCMODE,
    DEFSYNCMODE);
}

void print_progress(ogg_int64_t current, ogg_int64_t num, char *s) {
  if (frontend_mode)
    fprintf(stdout, "Processing %s %lld/%lld (%lld%%)\n", s,
            current, num, current * 100 / num);
  else
    fprintf(stdout, "Processing %s %lld/%lld (%lld%%)\r", s,
            current, num, current * 100 / num);
  fflush(stdout);
}

void copy_ogg_packet(ogg_packet *dst, ogg_packet *src) {
  memcpy(dst, src, sizeof(ogg_packet));
  dst->packet = (unsigned char *)malloc(src->bytes);
  if (dst->packet == NULL)
    die("malloc");
  memcpy(dst->packet, src->packet, src->bytes);
}

/*
 * File handling
 */

void write_page(ogg_page *page) {
  int ih, ib;
  
  if (fout == NULL)
    return;

  ih = fwrite(page->header, 1, page->header_len, fout);
  ib = fwrite(page->body, 1, page->body_len, fout);

  bwritten += ih + ib;
  bwritten_all += ih + ib;

  if (verbose > 1)
    fprintf(stdout, "(%s) %d + %d bytes written\n", __FILE__, ih, ib);
}

void flush_pages(stream_t *stream) {
  ogmmerge_page_t *mpage;
  ogg_page        *page;

  stream->pzr->flush_pages();
  if (stream->mpage == NULL)
    stream->mpage = stream->pzr->get_page();
  while (stream->mpage != NULL) {
    mpage = stream->mpage;
    page = mpage->og;
    write_page(page);
    free(page->header);
    free(page->body);
    free(page);
    free(mpage);
    stream->mpage = stream->pzr->get_page();
  }
}

int pages_available() {
  stream_t *stream;
  int       eos_only;
  
  eos_only = 1;
  stream = sources->streams;
  while (stream != NULL) {
    if (stream->mpage != NULL) {
      stream = stream->next;
      eos_only = 0;
      continue;
    }
    if (stream->pzr->page_available()) {
      stream->mpage = stream->pzr->get_page();
      stream = stream->next;
      eos_only = 0;
      continue;
    }
    if (!stream->eos)
      return 0;
    stream = stream->next;
  }
  
  return 1 - eos_only;
}

void write_winner_page() {
  stream_t *winner, *cur;
  
  winner = sources->streams;
  cur = winner->next;
  while (cur != NULL) {
    if (cur->mpage != NULL) {
      if (winner->mpage == NULL)
        winner = cur;
      else if (cur->mpage &&
               (cur->mpage->timestamp < winner->mpage->timestamp))
        winner = cur;
    }
    cur = cur->next;
  }
  if (winner->mpage != NULL) {
    write_page(winner->mpage->og);
    free(winner->mpage->og->header);
    free(winner->mpage->og->body);
    free(winner->mpage->og);
    free(winner->mpage);
    winner->mpage = NULL;
  }
}

void write_all_winner_pages() {
  while (pages_available())
    write_winner_page();
}
  
void flush_all_streams() {
  stream_t *s;
  
  s = sources->streams;
  while (s != NULL) {
    flush_pages(s);
    s = s->next;
  }
}

void produce_eos_packets() {
  stream_t   *s;
  ogg_packet  p;

  s = sources->streams;
  while (s != NULL) {
    if (!s->eos) {
      p.packet = (unsigned char *)"";
      p.bytes = 1;
      p.e_o_s = 1;
      p.b_o_s = 0;
      p.packetno = s->packetno++;
      p.granulepos = s->this_granulepos + (ogg_int64_t)s->granuleposadd;
      s->pzr->process(&p);
      s->eos = 1;
    }
    s = s->next;
  }
  write_all_winner_pages();
}

void write_stream_headers() {
  stream_t *stream;
  
  stream = sources->streams;
  while (stream != NULL) {
    stream->pzr = new cat_packetizer_c(stream->sample_rate, stream->serial,
                                       stream->stype);
    stream->eos = 0;
    stream->granulepos = 0;
    stream->pzr->process(&stream->header_packet);
    flush_pages(stream);
    stream = stream->next;
  }
}

long get_queued_bytes() {
  long      bytes;
  stream_t *stream;
  
  stream = sources->streams;
  bytes = 0;
  while (stream != NULL) {
    bytes += stream->pzr->get_queued_bytes();
    stream = stream->next;
  }
  
  return bytes;
}

/*
 * stream and source adding and finding
 */

void add_stream(source_t *source, stream_t *ndmx) {
  stream_t *cur = source->streams;
  
  if (cur == NULL) {
    source->streams = ndmx;
    ndmx->next = NULL;
  } else {
    while (cur->next != NULL)
      cur = cur->next;
    cur->next = ndmx;
    ndmx->next = NULL;
  }
}

stream_t *find_stream(int fserial) {
  stream_t *cur = sources->streams;
  
  if ((cur != NULL) && (cur->next == NULL))
    // Only one track. Let's just assume that the user wants to concatenate
    // Ogg audio files whose serial numbers are random.
    return cur;

  while (cur != NULL) {
    if (cur->serial == fserial)
      break;
    cur = cur->next;
  }
  
  return cur;
}

void add_source(char *name, int fd, off_t size, double manual_sync) {
  source_t *new_src, *cur;  
  
  new_src = (source_t *)malloc(sizeof(source_t));
  if (new_src == NULL)
    die("malloc");
  memset(new_src, 0, sizeof(source_t));
  
  new_src->name = strdup(name);
  if (new_src->name == NULL)
    die("strdup");
  new_src->fd = fd;
  new_src->size = size;
  new_src->manual_sync = manual_sync;
  
  if (sources == NULL)
    sources = new_src;
  else {
    cur = sources;
    while (cur->next != NULL)
      cur = cur->next;
    cur->next = new_src;
  }
}

/*
 * all-mighty processing functions
 */

void probe_all_sources() {
  ogg_sync_state  sync;
  ogg_page        page;
  ogg_packet      pack;
  vorbis_comment  vc;
  source_t       *src;
  stream_t       *stream;
  char           *buf;
  int             np, res;
  long            nread;
  
  src = sources;
  while (src != NULL) {
    fprintf(stdout, "(%s) Probing file '%s'...\n", __FILE__, src->name);
    ogg_sync_init(&sync);
    while (1) {
      np = ogg_sync_pageseek(&sync, &page);
      if (np < 0) {
        fprintf(stderr, "(%s) ogg_sync_pageseek failed for '%s'.\n", __FILE__,
                src->name);
        exit(1);
      }
      if (np == 0) {
        buf = ogg_sync_buffer(&sync, BLOCK_SIZE);
        if (!buf) {
          fprintf(stderr, "(%s) ogg_sync_buffer failed for '%s'.\n", __FILE__,
                  src->name);
          exit(1);
        }
        if ((nread = read(src->fd, buf, BLOCK_SIZE)) <= 0) {
          fprintf(stderr, "(%s) File '%s' ended before the header packet "
                  "was found. This file is broken.\n", __FILE__, src->name);
          exit(1);
        }
        ogg_sync_wrote(&sync, nread);
        bread += nread;
        continue;
      }

      if (!ogg_page_bos(&page))
        break;
      stream = (stream_t *)malloc(sizeof(stream_t));
      if (stream == NULL)
        die("malloc");
      memset(stream, 0, sizeof(stream_t));
      stream->serial = ogg_page_serialno(&page);
      if (ogg_stream_init(&stream->instate, stream->serial)) {
        fprintf(stderr, "(%s) ogg_stream_init failed\n", __FILE__);
        exit(1);
      }
      add_stream(src, stream);
      ogg_stream_pagein(&stream->instate, &page);
      ogg_stream_packetout(&stream->instate, &pack);
      copy_ogg_packet(&stream->header_packet, &pack);
      if ((pack.bytes >= 7) &&
          !strncmp((char *)&pack.packet[1], "vorbis", 6)) {
        stream->stype = 'V';
        vorbis_info_init(&stream->vi);
        vorbis_comment_init(&vc);
        if ((res = vorbis_synthesis_headerin(&stream->vi, &vc, &pack)) >= 0)
          stream->sample_rate = stream->vi.rate;
        else {
          fprintf(stderr, "(%s) Vorbis audio stream indicated " \
                  "but no Vorbis stream header found in '%s'. Error code was "
                  "%d.\n", __FILE__, src->name, res);
          exit(1);
        }
      } else if (((*pack.packet & PACKET_TYPE_BITS ) == PACKET_TYPE_HEADER) &&
	         (pack.bytes >=
                   (int)(sizeof(stream_header) + 1))) {
        stream_header *sth = (stream_header *)(pack.packet + 1);
        if (!strncmp(sth->streamtype, "video", 5)) {
          stream->sample_rate = (double)10000000 /
            (double)get_uint64(&sth->time_unit);
          stream->stype = 'v';
          if (vstream == NULL)
            vstream = stream;
        } else if (!strncmp(sth->streamtype, "audio", 5)) {
          stream->sample_rate = get_uint64(&sth->samples_per_unit);
          stream->stype = 'a';
        } else if (!strncmp(sth->streamtype, "text", 4)) {
          stream->stype = 't';
          stream->sample_rate = (double)10000000 /
            (double)get_uint64(&sth->time_unit);
        } else {
          fprintf(stderr, "(%s) Found new header of unknown/" \
                  "unsupported type in '%s'.\n", __FILE__, src->name);
          exit(1);
        }
        memcpy(&stream->sth, sth, sizeof(stream_header));
      } else {
        fprintf(stderr, "(%s) Found unknown header in '%s'.\n", __FILE__,
                src->name);
        exit(1);
      }
    }
    
    ogg_sync_clear(&sync);
    lseek(src->fd, 0, SEEK_SET);
    src = src->next;
  }
}

void check_all_sources() {
  source_t *src;
  stream_t *stream, *str;
  char      codec1[5], codec2[5];
  double    fps1, fps2;
  int       num_not_matching, not_present;
  
  src = sources->next;
  while (src != NULL) {
    str = src->streams;
    if (str== NULL) {
      fprintf(stderr, "(%s) File '%s' does not contain any stream.\n",
              __FILE__, src->name);
      if (safety)
        exit(1);
    }
    num_not_matching = 0;
    while (str != NULL) {
      stream = find_stream(str->serial);
      not_present = 0;
      if (stream == NULL)
        not_present = 1;
      else if (str->serial != stream->serial) {
        num_not_matching++;
        if (num_not_matching > 1)
          not_present = 0;
      }
      if (not_present) {
        fprintf(stderr, "(%s) File '%s' contains a%s %s stream (serial %d) "
                "that is not present in '%s'.\n", __FILE__, src->name,
                str->stype == 'a' ? "n" : "",
                str->stype == 'V' ? "Vorbis" :
                  str->stype == 'v' ? "video" :
                  str->stype == 'a' ? "audio" :
                  str->stype == 't' ? "text" : "unknown??",
                str->serial, sources->name);
        exit(1);
      }
      if (str->stype != stream->stype) {
        fprintf(stderr, "(%s) File '%s' contains a%s %s stream (serial %d) "
                "that is of another type (%s) in '%s'.\n", __FILE__, src->name,
                str->stype == 'a' ? "n" : "",
                str->stype == 'V' ? "Vorbis" :
                  str->stype == 'v' ? "video" :
                  str->stype == 'a' ? "audio" :
                  str->stype == 't' ? "text" : "unknown??",
                str->serial,
                stream->stype == 'V' ? "Vorbis" :
                  stream->stype == 'v' ? "video" :
                  stream->stype == 'a' ? "audio" :
                  stream->stype == 't' ? "text" : "unknown??",
                sources->name);
        if (safety)
          exit(1);
      }
      switch (str->stype) {
        case 'V':
          if (str->vi.rate != stream->vi.rate) {
            fprintf(stderr, "(%s) Stream parameter mismatch for '%s' and '%s':"
                    " serial %d, type Vorbis, rate %ld != %ld\n", __FILE__,
                    sources->name, src->name, str->serial,
                    str->vi.rate, stream->vi.rate);
            if (safety)
              exit(1);
          }
          if (str->vi.channels != stream->vi.channels) {
            fprintf(stderr, "(%s) Stream parameter mismatch for '%s' and '%s':"
                    " serial %d, type Vorbis, channels %d != %d\n", __FILE__,
                    sources->name, src->name, str->serial,
                    str->vi.channels, stream->vi.channels);
            if (safety)
              exit(1);
          }
          break;
        case 'v':
          codec1[4] = 0;
          codec2[4] = 0;
          memcpy(codec1, str->sth.subtype, 4);
          memcpy(codec2, stream->sth.subtype, 4);
          if (strcmp(codec1, codec2)) {
            fprintf(stderr, "(%s) Stream parameter mismatch for '%s' and '%s':"
                    " serial %d, type video, codec %s != %s\n", __FILE__,
                    sources->name, src->name, str->serial, codec1, codec2);
            if (safety)
              exit(1);
          }
          
          fps1 = (double)10000000 / (double)get_uint64(&str->sth.time_unit);
          fps2 = (double)10000000 / (double)get_uint64(&stream->sth.time_unit);
          if (fps1 != fps2) {
            fprintf(stderr, "(%s) Stream parameter mismatch for '%s' and '%s':"
                    " serial %d, type video, fps %.3f != %.3f\n", __FILE__,
                    sources->name, src->name, str->serial, fps1, fps2);
            if (safety)
              exit(1);
          }
          
          if (get_uint32(&str->sth.sh.video.width) !=
              get_uint32(&stream->sth.sh.video.width)) {
            fprintf(stderr, "(%s) Stream parameter mismatch for '%s' and '%s':"
                    " serial %d, type video, width %d != %d\n", __FILE__,
                    sources->name, src->name, str->serial,
                    get_uint32(&str->sth.sh.video.width),
                    get_uint32(&stream->sth.sh.video.width));
            if (safety)
              exit(1);
          }
          
          if (get_uint32(&str->sth.sh.video.height) !=
              get_uint32(&stream->sth.sh.video.height)) {
            fprintf(stderr, "(%s) Stream parameter mismatch for '%s' and '%s':"
                    " serial %d, type video, height %d != %d\n", __FILE__,
                    sources->name, src->name, str->serial,
                    get_uint32(&str->sth.sh.video.height),
                    get_uint32(&stream->sth.sh.video.height));
            if (safety)
              exit(1);
          }
          
          break;
        case 'a':
          codec1[4] = 0;
          codec2[4] = 0;
          memcpy(codec1, str->sth.subtype, 4);
          memcpy(codec2, stream->sth.subtype, 4);
          if (strcmp(codec1, codec2)) {
            fprintf(stderr, "(%s) Stream parameter mismatch for '%s' and '%s':"
                    " serial %d, type audio, codec %s != %s\n", __FILE__,
                    sources->name, src->name, str->serial, codec1, codec2);
            if (safety)
              exit(1);
          }
          
          if (get_uint64(&str->sth.samples_per_unit) !=
              get_uint64(&stream->sth.samples_per_unit)) {
            fprintf(stderr, "(%s) Stream parameter mismatch for '%s' and '%s':"
                    " serial %d, type audio, samples per second %lld != "
                    "%lld\n", __FILE__, sources->name, src->name, str->serial,
                    get_uint64(&str->sth.samples_per_unit),
                    get_uint64(&stream->sth.samples_per_unit));
            if (safety)
              exit(1);
          }

          if (get_uint16(&str->sth.bits_per_sample) !=
              get_uint16(&stream->sth.bits_per_sample)) {
            fprintf(stderr, "(%s) Stream parameter mismatch for '%s' and '%s':"
                    " serial %d, type audio, bits per sample %d != %d\n",
                    __FILE__, sources->name, src->name, str->serial,
                    get_uint16(&str->sth.bits_per_sample),
                    get_uint16(&stream->sth.bits_per_sample));
            if (safety)
              exit(1);
          }

          if (get_uint16(&str->sth.sh.audio.channels) !=
              get_uint16(&stream->sth.sh.audio.channels)) {
            fprintf(stderr, "(%s) Stream parameter mismatch for '%s' and '%s':"
                    " serial %d, type audio, channels %d != %d\n", __FILE__,
                    sources->name, src->name, str->serial,
                    get_uint16(&str->sth.sh.audio.channels),
                    get_uint16(&stream->sth.sh.audio.channels));
            if (safety)
              exit(1);
          }

          break;
        case 't':
          if (get_uint64(&str->sth.time_unit) !=
              get_uint64(&stream->sth.time_unit)) {
            fprintf(stderr, "(%s) Stream parameter mismatch for '%s' and '%s':"
                    " serial %d, type text, time unit %lld != %lld\n",
                    __FILE__, sources->name, src->name, str->serial,
                    get_uint64(&str->sth.time_unit),
                    get_uint64(&stream->sth.time_unit));
            if (safety)
              exit(1);
          }

          break;
        default:
          fprintf(stderr, "(%s) Unsupported stream type in '%s'. This is a "
                  "bug in ogmcat. Please report it to moritz@bunkus.org.\n",
                  __FILE__, src->name);
      }
      str = str->next;
    }
  
    src = src->next;
  }
}

void process_ogms() {
  ogg_sync_state    sync;
  ogg_page          page;
  ogg_packet        pack;
  int               nread, np, sno, hdrlen, lenbytes, i;
  stream_t         *stream;
  char             *buf;
  source_t         *src;
  double            vtime2, vtime3, vtime4;
  ogg_int64_t       manual_sync_granule;

  fout = fopen(fout_name, "w");
  if (fout == NULL) {
    fprintf(stderr, "(%s) Could not open '%s' for writing.\n", __FILE__,
            fout_name);
    exit(1);
  }
  write_stream_headers();
  src = sources;
  while (src != NULL) {
    fprintf(stdout, "(%s) Processing input file '%s'...\n", __FILE__,
            src->name);
    ogg_sync_init(&sync);

    while (1) {
      np = ogg_sync_pageseek(&sync, &page);
      if (np < 0) {
        fprintf(stderr, "(%s) ogg_sync_pageseek failed for '%s'.\n", __FILE__,
                src->name);
        exit(1);
      }
      if (np == 0) {
        buf = ogg_sync_buffer(&sync, BLOCK_SIZE);
        if (!buf) {
          fprintf(stderr, "(%s) ogg_sync_buffer failed for '%s'.\n", __FILE__,
                  src->name);
          exit(1);
        }
        if ((nread = read(src->fd, buf, BLOCK_SIZE)) <= 0)
          break;
        ogg_sync_wrote(&sync, nread);
        bread += nread;
        continue;
      }

      sno = ogg_page_serialno(&page);
      stream = find_stream(sno);
      if (stream == NULL)
        fprintf(stdout, "(%s) Encountered page for the unknown serial " \
                "%d. Skipping this page.\n", __FILE__, sno);
      else {
        ogg_stream_pagein(&stream->instate, &page);
        stream->last_granulepos = stream->this_granulepos;
        stream->this_granulepos = ogg_page_granulepos(&page);
        if (ogg_page_bos(&page)) {
          if (ogg_stream_init(&stream->instate, sno)) {
            fprintf(stderr, "(%s) ogg_stream_init failed\n", __FILE__);
            exit(1);
          }
        } else {
          while (ogg_stream_packetout(&stream->instate, &pack) == 1) {
            if (src == sources) {
              if (stream->stype != 'V') {
                if (stream->comment == 0) {
                  copy_ogg_packet(&stream->header_packet2, &pack);
                  if (stream->stype == 'v') {
                    vorbis_unpack_comment(&stream->vc, (char *)pack.packet,
                                          pack.bytes);
                    stream->vc_unpacked = 1;
                  }
                  stream->comment = 1;
                  stream->packetno = 2;
                  pack.packetno = 1;
                  pack.granulepos = 0;
                  stream->pzr->process(&pack);
                  flush_pages(stream);
                  continue;
                }
              } else {
                if (stream->comment == 0) {
                  copy_ogg_packet(&stream->header_packet2, &pack);
                  stream->comment = 1;
                  pack.packetno = 1;
                  pack.granulepos = 0;
                  stream->pzr->process(&pack);
                  continue;
                } else if (stream->comment == 1) {
                  copy_ogg_packet(&stream->header_packet3, &pack);
                  stream->comment = 2;
                  stream->packetno = 3;
                  pack.packetno = 2;
                  pack.granulepos = 0;
                  stream->pzr->process(&pack);
                  flush_pages(stream);
                  continue;
                }
              }
            } else if ((pack.packet[0] & PACKET_TYPE_HEADER )
                        == PACKET_TYPE_HEADER)
              continue;
            if (pack.e_o_s && (src->next == NULL))
              stream->eos = 1;
            else if (pack.e_o_s && (src->next != NULL) &&
                     (pack.bytes <= 1))
              continue;
            else
              pack.e_o_s = 0;
            if (pack.granulepos == -1)
              pack.granulepos = stream->this_granulepos;

            if (stream->stype == 'v') {
              if (pack.packet[0] & PACKET_IS_SYNCPOINT)
                stream->pzr->flush_pages();
              hdrlen = (*pack.packet & PACKET_LEN_BITS01) >> 6;
              hdrlen |= (*pack.packet & PACKET_LEN_BITS2) << 1;
              lenbytes = 1;
              if ((hdrlen > 0) && (pack.bytes >= (hdrlen + 1)))
                for (i = 0, lenbytes = 0; i < hdrlen; i++) {
                  lenbytes = lenbytes << 8;
                  lenbytes += *((unsigned char *)pack.packet + hdrlen - i);
                }
              pack.granulepos = stream->granulepos + lenbytes - 1;
              if (lenbytes != 1)
                fprintf(stdout, "len: %d at pno %lld p0 %d\n", lenbytes,
                        stream->packetno, (int)pack.packet[0]);
              stream->granulepos += lenbytes;
            } else
              pack.granulepos += (ogg_int64_t)stream->granuleposadd;
            pack.packetno = stream->packetno++;
            stream->pzr->process(&pack);
            if (stream->stype == 't')
              stream->pzr->flush_pages();
          }
        }
      }
      write_all_winner_pages();
    }

    if (src->next != NULL) {    
      stream = sources->streams;
      if (vstream != NULL) {
        vtime2 = (double)(vstream->granulepos + 1) * 1000.0 /
          vstream->sample_rate;
        vtime3 = (double)(vstream->granulepos) * 1000.0 / vstream->sample_rate;
        vtime4 = (double)(vstream->granulepos - 1) * 1000.0 /
          vstream->sample_rate;
      }
      while (stream != NULL) {
        if ((sync_mode == 0) || (vstream == NULL))
          stream->granuleposadd += stream->this_granulepos;
        else if (sync_mode == 1)
          stream->granuleposadd += stream->last_granulepos;
        else if (stream->stype != 'v') {
          if (sync_mode == 2)
            stream->granuleposadd = (ogg_int64_t)(vtime2 * stream->sample_rate
                                                 / 1000.0);
          else if (sync_mode == 3)
            stream->granuleposadd = (ogg_int64_t)(vtime3 * stream->sample_rate
                                                 / 1000.0);
          else if (sync_mode == 4)
            stream->granuleposadd = (ogg_int64_t)(vtime4 * stream->sample_rate
                                                 / 1000.0);
          else {
            fprintf(stderr, "(%s) Internal error: unknown sync_mode %d. "
                    "Please contact me at moritz@bunkus.org please.\n",
                    __FILE__, sync_mode);
            exit(1);
          }
        }
        manual_sync_granule = (ogg_int64_t)(src->next->manual_sync * 1000.0 /
                                            stream->sample_rate);
        stream->granuleposadd += manual_sync_granule;
        if (verbose) {
          fprintf(stdout, "Stream %d, type %s, current granulepos %lld",
                  stream->serial,
                  stream->stype == 'v' ? "video" :
                    stream->stype == 'V' ? "Vorbis" :
                    stream->stype == 'a' ? "audio" :
                    stream->stype == 't' ? "text" : "unknown",
                  stream->this_granulepos);
          if (stream->stype != 'v')
            fprintf(stdout, ", granulepos offset %lld (including manual sync "
                    "%lld)", stream->granuleposadd, manual_sync_granule);
          fprintf(stdout, "\n");
        }
        stream = stream->next;
      }
    }
    
    src = src->next;
  }

  produce_eos_packets();
  stream = sources->streams;
  while (stream != NULL) {
    delete(stream->pzr);
    stream->mpage = NULL;
    stream = stream->next;
  }
  ogg_sync_clear(&sync);
  fclose(fout);
}

int main(int argc, char *argv[]) {
  int    i, fdin = -1;
  off_t  file_size;
  double manual_sync;

  fprintf(stdout, "This program is not finished. It will probably not "
          "work for your files. It might work for files created with "
          "ogmsplit. It might not. Please do not ask me when it will "
          "be ready - I simply don't know. If you want to experiment "
          "with it then go ahead.\n");

  for (i = 1; i < argc; i++)
    if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version")) {
      fprintf(stdout, "ogmsplit v" VERSION "\n");
      exit(0);
    }
  manual_sync = 0.0;
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
      verbose++;
    else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      usage(argv[0]);
      return 0;
    } else if (!strcmp(argv[i], "--frontend"))
      frontend_mode = 1;
    else if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--nosafetychecks"))
      safety = 0;
    else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
      if ((i + 1) > argc) {
        fprintf(stderr, "(%s) -o requires an argument.\n", __FILE__);
        exit(1);
      }
      fout_name = argv[i + 1];
      i++;
    } else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--sync")) {
      if ((i + 1) > argc) {
        fprintf(stderr, "(%s) -s requires an argument.\n", __FILE__);
        exit(1);
      }
      sync_mode = strtol(argv[i + 1], NULL, 10);
      if ((sync_mode < 0) || (sync_mode > MAXSYNCMODE)) {
        fprintf(stderr, "(%s) Invalid sync mode %d. Valid values are 0..%d\n.",
                __FILE__, sync_mode, MAXSYNCMODE);
        exit(1);
      }
      i++;
    } else if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--manualsync")) {
      if ((i + 1) > argc) {
        fprintf(stderr, "(%s) -m requires an argument.\n", __FILE__);
        exit(1);
      }
      manual_sync = strtod(argv[i + 1], NULL);
      i++;
    } else {
      fdin = open(argv[i], O_RDONLY);
      if (fdin == -1) {
        fprintf(stderr, "(%s) Could not open \"%s\".\n", __FILE__,
                argv[i]);
        return 1;
      }
      file_size = lseek(fdin, 0, SEEK_END);
      if (file_size == (off_t)-1) {
        fprintf(stderr, "(%s) Could not seek to end of file.\n", __FILE__);
        return 1;
      }
      lseek(fdin, 0, SEEK_SET);
      add_source(argv[i], fdin, file_size, manual_sync);
      manual_sync = 0.0;
    }
  }

  if ((sources == NULL) || (sources->next == NULL)) {
    fprintf(stdout, "(%s) Not enough source files given.\n", __FILE__);
    usage(argv[0]);
    exit(1);
  }
  if (fout_name == NULL) {
    fprintf(stdout, "(%s) No output file name given.\n", __FILE__);
    usage(argv[0]);
    exit(1);
  }
  
  probe_all_sources();
  check_all_sources();
  if ((sync_mode == 1) && (vstream == 0)) {
    fprintf(stdout, "(%s) Sync mode 1 only works if the source files contain "
            "at least one video stream.\n", __FILE__);
    exit(1);
  }
  process_ogms();

  return 0;
}
