/*
  ogmsplit -- utility for splitting an OGG media file

  Written by Moritz Bunkus <moritz@bunkus.org>

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

#include <ctype.h>
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

#define BLOCK_SIZE 4096

#define PM_FINDSPLITPOINTS  1
#define PM_PRINTSPLITPOINTS 2

class split_packetizer_c: public q_c {
  private:
    ogg_int64_t old_granulepos;
    char        stype;
    double      sample_rate;
  public:
    split_packetizer_c(double nsample_rate, ogg_stream_state *ostate,
                       char nstype);
    virtual ~split_packetizer_c();
    
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
  ogg_stream_state    outstate;
  ogg_packet          header_packet1;
  ogg_packet          header_packet2;
  ogg_packet          header_packet3;
  ogg_int64_t         last_granulepos, this_granulepos;
  ogg_int64_t         granulepos;
  ogg_int64_t         packetno;
  ogg_int64_t         num_frames, cur_frame;
  double              sample_rate, granulepossub;
  vorbis_comment      vc;
  int                 vc_unpacked;
  ogmmerge_page_t    *mpage;
  split_packetizer_c *pzr;
  struct stream_t    *next;
} stream_t;

typedef struct splitpoint_t {
  ogg_int64_t pos_bytes;
  double      pos_time;
  ogg_int64_t frameno;
} splitpoint_t;

typedef struct cut_t {
  double        start;
  double        end;
  struct cut_t *next;
} cut_t;

stream_t       *first = NULL, *vstream = NULL;
int             numstreams = 0;

ogg_int64_t     bread = 0;
ogg_int64_t     bwritten = 0, bwritten_all = 0;

ogg_int64_t     split_bytes = -1;
double          last_split = 0.0, current_time = 0.0;
double          split_time = -1.0;
splitpoint_t   *splitpoints = NULL;
int             num_splitpoints = 0, next_sp = -1;
int             cut_mode = 0;
cut_t          *cuts = NULL, *current_cut = NULL;

int             print_mode = 0;
int             count_only = 0;
int             frontend_mode = 0;

FILE           *fout;
char           *fout_base, *fout_ext;
int             fout_num = 1;
int             max_num_files = INT_MAX;

int             verbose = 0;

off_t           file_size;
int             last_percent = 0;

/*
 * split_packetizer class functions
 */

split_packetizer_c::split_packetizer_c(double nsample_rate,
                                       ogg_stream_state *ostate,
                                       char nstype) {
  old_granulepos = 0;
  stype = nstype;
  sample_rate = nsample_rate;
  memcpy(&os, ostate, sizeof(ogg_stream_state));
}

split_packetizer_c::~split_packetizer_c() {
}

stamp_t split_packetizer_c::make_timestamp(ogg_int64_t granulepos) {
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

void split_packetizer_c::process(ogg_packet *op) {
  ogg_stream_packetin(&os, op);
  queue_pages();
}

/*
 * General helper functions, usage
 */

void usage(char *fname) {
  fprintf(stdout,
    "Usage: %s [options] inname\n\n"
    " options:\n"
    "  inname           Use 'inname' as the source.\n"
    "  -o, --output out Use 'out' as the base name. Ascending part numbers\n"
    "                   will be appended to it. Default is 'inname'.\n"
    "  The operation mode can be set with exactly one of -s, -t, -c or -p.\n"
    "  The default mode is to split by size (-s).\n"
    "  -s, --size size  Size in MiB ( = 1024 * 1024 bytes) after which a new\n"
    "                   file will be opened (approximately). Default is 700MiB."
  "\n                   Size can end in 'B' or 'b' to indicate 'bytes'\n"
    "                   instead of 'MiB'.\n"
    "  -t, --time time  Split after the given elapsed time (approximately).\n"
    "                  'time' takes the form HH:MM:SS.sss or simply SS(.sss),\n"
    "                   e.g. 00:05:00.000 or 300.000 or simply 300.\n"
    "  -c, --cuts cuts  Produce output files as specified by cuts, a list of\n"
    "                   slices of the form \"start-end\" or \"start+length\",\n"
    "                   separated by commas. If start is omitted, it defaults\n"
    "                   to the end of the previous cut. start and end take\n"
    "                   the same format as the arguments to \"-t\".\n"
    "  -p, --print-splitpoints Only print the key frames and the number of\n"
    "                   bytes encountered before each. Useful to find the\n"
    "                   exact splitting point.\n"
    "  -n, --num num    Don't create more than num separate files. The last one"
  "\n                   may be bigger than the desired size. Default is an\n"
    "                   unlimited number. Can only be used with -s or -t.\n"
    "  --frontend       Frontend mode. Progress output will be terminated by\n"
    "                   \\n instead of \\r.\n"
    "  -v, --verbose    Be verbose and show each OGG packet.\n"
    "                   Can be used twice to increase verbosity.\n"
    "  -h, --help       Show this help.\n"
    "  -V, --version    Show version information.\n", fname);
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

double parse_time(char *s) {
  char *c, *a, *dot;
  int num_colons;
  double seconds;
  
  dot = strchr(s, '.');
  if (dot != NULL) {
    *dot = 0;
    dot++;
  }
  for (c = s, num_colons = 0; *c; c++) {
    if (*c == ':')
      num_colons++;
    else if ((*c < '0') || (*c > '9')) {
      fprintf(stderr, "ERROR: illegal character '%c' in time range.\n", *c);
      exit(1);
    }
  }
  if (num_colons > 2) {
    fprintf(stderr, "ERROR: illegal time range: %s.\n", s);
    exit(1);
  }
  if (num_colons == 0) {
    seconds = strtod(s, NULL);
    if (dot != NULL)
      seconds += strtod(dot, NULL) / 1000.0;
  }
  for (a = s, c = s, seconds = 0; *c; c++) {
    if (*c == ':') {
      *c = 0;
      seconds *= 60;
      seconds += atoi(a);
      a = c + 1;
    }
  }
  seconds *= 60;
  seconds += atoi(a);
  
  if (dot != NULL)
    seconds += strtod(dot, NULL) / 1000.0;
  
  return seconds;
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

void open_out_file() {
  char *buf;
  
  buf = (char *)malloc(strlen(fout_base) + strlen(fout_ext) + 16);
  sprintf(buf, "%s-%06d.%s", fout_base, fout_num++, fout_ext);
  fout = fopen(buf, "w");
  if (fout == NULL) {
    fprintf(stderr, "(%s) Could not open '%s' for writing (%s).\n", __FILE__,
            buf, strerror(errno));
    exit(1);
  }
  if (verbose)
    fprintf(stdout, "(%s) Starting new file '%s'.\n", __FILE__, buf);
  free(buf);
  bwritten = 0;
}

void write_page(ogg_page *page) {
  int ih, ib;
  
  if (!print_mode && !count_only) {
    if (fout == NULL)
      return;
    ih = fwrite(page->header, 1, page->header_len, fout);
    ib = fwrite(page->body, 1, page->body_len, fout);
  } else {
    ih = page->header_len;
    ib = page->body_len;
  }
  bwritten += ih + ib;
  bwritten_all += ih + ib;
  if ((verbose > 1) && !print_mode && !count_only)
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
  stream = first;
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
  
  winner = first;
  cur = first->next;
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
  
  s = first;
  while (s != NULL) {
    flush_pages(s);
    s = s->next;
  }
}

void produce_eos_packets() {
  stream_t   *s;
  ogg_packet  p;

  s = first;
  while (s != NULL) {
    if (!s->eos) {
      p.packet = (unsigned char *)"";
      p.bytes = 1;
      p.e_o_s = 1;
      p.b_o_s = 0;
      p.packetno = s->packetno++;
      p.granulepos = s->this_granulepos - (ogg_int64_t)s->granulepossub;
      s->pzr->process(&p);
      s->eos = 1;
    }
    s = s->next;
  }
  write_all_winner_pages();
}

void write_stream_headers() {
  stream_t *stream;
  
  stream = first;
  while (stream != NULL) {
    delete(stream->pzr);
    if (stream->mpage != NULL) {
      free(stream->mpage->og->header);
      free(stream->mpage->og->body);
      free(stream->mpage->og);
      free(stream->mpage);
      stream->mpage = NULL;
    }
    ogg_stream_init(&stream->outstate, stream->serial);
    stream->pzr = new split_packetizer_c(stream->sample_rate,
                                         &stream->outstate,
                                         stream->stype);
    stream->eos = 0;
    stream->granulepos = 0;
    stream->pzr->process(&stream->header_packet1);
    flush_pages(stream);
    stream = stream->next;
  }
  stream = first;
  while (stream != NULL) {
    if ((stream->stype == 'v') && stream->vc_unpacked && !print_mode) {
      vorbis_comment *new_vc;
      int             clen, res;
      unsigned char  *buf;
      ogg_packet      op;
      
      if (cut_mode && (current_cut != NULL))
        new_vc = chapter_information_adjust(&stream->vc, current_cut->start,
                                            current_cut->end);
      else if (!cut_mode) {
        if ((next_sp != -1) && (fout_num <= max_num_files))
          new_vc = chapter_information_adjust(&stream->vc, current_time,
                                              splitpoints[next_sp].pos_time);
        else
          new_vc = chapter_information_adjust(&stream->vc, current_time,
                                              9999999999.0);
      } else
        new_vc = vorbis_comment_dup(&stream->vc);
      clen = -1 * comments_to_buffer(new_vc, NULL, 0);
      buf = (unsigned char *)malloc(clen);
      if (buf == NULL)
        die("malloc");
      if ((res = comments_to_buffer(new_vc, (char *)buf, clen)) < 0) {
        fprintf(stderr, "FATAL: ogmsplit: comments_to_buffer failed. clen "
                "is %d, result was %d.\n", clen, res);
        exit(1);
      }
      op.packet = buf;
      op.bytes = clen;
      op.b_o_s = 0;
      op.e_o_s = 0;
      op.granulepos = 0;
      op.packetno = 1;
      stream->pzr->process(&op);
      free(buf);
      vorbis_comment_clear(new_vc);
      free(new_vc);
    } else
      stream->pzr->process(&stream->header_packet2);
    if (stream->stype != 'V')
      flush_pages(stream);
    stream->packetno = 2;
    stream = stream->next;
  }
  stream = first;
  while (stream != NULL) {
    if (stream->stype == 'V') {
      stream->pzr->process(&stream->header_packet3);
      flush_pages(stream);
      stream->packetno = 3;
    }
    stream = stream->next;
  }
}

long get_queued_bytes() {
  long      bytes;
  stream_t *stream;
  
  stream = first;
  bytes = 0;
  while (stream != NULL) {
    bytes += stream->pzr->get_queued_bytes();
    stream = stream->next;
  }
  
  return bytes;
}

/*
 * stream and splitpoint adding and finding
 */

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

void add_splitpoint(splitpoint_t *sp) {
  splitpoints = (splitpoint_t *)realloc(splitpoints, (num_splitpoints + 1) *
                                        sizeof(splitpoint_t));
  if (splitpoints == NULL)
    die("realloc");
  memcpy(&splitpoints[num_splitpoints], sp, sizeof(splitpoint_t));
  num_splitpoints++;
}

int find_next_splitpoint(int start) {
  int i;

  if (start == -1)
    return -1;  
  for (i = start + 1; i < num_splitpoints; i++) {
    if ((split_bytes > 0) &&
        ((bwritten_all + split_bytes) < splitpoints[i].pos_bytes))
      return i - 1;
    if ((split_time > 0) &&
        ((current_time + split_time + 1) < splitpoints[i].pos_time))
      return i - 1;
  }
  
  return -1;
}

/*
 * all-mighty processing function
 */

void process_ogm(int fdin) {
  ogg_sync_state    sync;
  ogg_page          page;
  ogg_packet        pack;
  int               nread, np, sno, endofstream;
  stream_t         *stream;
  char             *buf;
  vorbis_info       vi;
  vorbis_comment    vc;
  int               hdrlen, i;
  long              lenbytes;
  splitpoint_t      sp;
  int               do_close = 0, do_open = 0;

  ogg_sync_init(&sync);
  while (1) {
    np = ogg_sync_pageseek(&sync, &page);
    if (np < 0) {
      fprintf(stderr, "(%s) ogg_sync_pageseek failed\n", __FILE__);
      return;
    }
    if (np == 0) {
      buf = ogg_sync_buffer(&sync, BLOCK_SIZE);
      if (!buf) {
        fprintf(stderr, "(%s) ogg_sync_buffer failed\n", __FILE__);
        return;
      }
      if ((nread = read(fdin, buf, BLOCK_SIZE)) <= 0)
        return;
      ogg_sync_wrote(&sync, nread);
      bread += nread;
      continue;
    }

    if (!ogg_page_bos(&page)) {
      break;
    } else {
      if (print_mode) {
        stream = (stream_t *)malloc(sizeof(stream_t));
        if (stream == NULL)
          die("malloc");
        memset(stream, 0, sizeof(stream_t));
        stream->serial = ogg_page_serialno(&page);
        if (ogg_stream_init(&stream->instate, stream->serial)) {
          fprintf(stderr, "(%s) ogg_stream_init failed\n", __FILE__);
          return;
        }
        add_stream(stream);
        ogg_stream_pagein(&stream->instate, &page);
        ogg_stream_packetout(&stream->instate, &pack);
        copy_ogg_packet(&stream->header_packet1, &pack);
        ogg_stream_init(&stream->outstate, stream->serial);
        if ((pack.bytes >= 7) &&
            !strncmp((char *)&pack.packet[1], "vorbis", 6)) {
          stream->stype = 'V';
          vorbis_info_init(&vi);
          vorbis_comment_init(&vc);
          if (vorbis_synthesis_headerin(&vi, &vc, &pack) >= 0)
            stream->sample_rate = vi.rate;
          else {
            fprintf(stderr, "(%s) Vorbis audio stream indicated " \
                    "but no Vorbis stream header found.\n", __FILE__);
            exit(1);
          }
        } else if (((*pack.packet & PACKET_TYPE_BITS ) ==
                    PACKET_TYPE_HEADER) &&
                   (pack.bytes >= (int)(sizeof(old_stream_header) + 1))) {
          stream_header sth;
          copy_headers(&sth, (old_stream_header *)&pack.packet[1], pack.bytes);
          if (!strncmp(sth.streamtype, "video", 5)) {
            stream->sample_rate = (double)10000000 /
              (double)get_uint64(&sth.time_unit);
            stream->stype = 'v';
            if (vstream == NULL)
              vstream = stream;
          } else if (!strncmp(sth.streamtype, "audio", 5)) {
            stream->sample_rate = get_uint64(&sth.samples_per_unit);
            stream->stype = 'a';
          } else if (!strncmp(sth.streamtype, "text", 4)) {
            stream->stype = 't';
            stream->sample_rate = (double)10000000 /
              (double)get_uint64(&sth.time_unit);
          } else {
            fprintf(stderr, "(%s) Found new header of unknown/" \
                    "unsupported type\n", __FILE__);
            exit(1);
          }
        } else {
          fprintf(stderr, "(%s) Found unknown header.\n", __FILE__);
          exit(1);
        }
        stream->pzr = new split_packetizer_c(stream->sample_rate,
                                             &stream->outstate, stream->stype);
        stream->pzr->process(&pack);
        flush_pages(stream);
        stream->packetno++;
      } else {
        stream = find_stream(ogg_page_serialno(&page));
        if (stream == NULL) {
          fprintf(stderr, "(%s) Stream info has changed since the first pass."
                  " First pass did not record information about stream number "
                  "%d.\n", __FILE__, ogg_page_serialno(&page));
          exit(1);
        }
        ogg_stream_init(&stream->instate, stream->serial);
        ogg_stream_pagein(&stream->instate, &page);
        ogg_stream_packetout(&stream->instate, &pack);
        ogg_stream_init(&stream->outstate, stream->serial);
        stream->pzr = new split_packetizer_c(stream->sample_rate,
                                             &stream->outstate, stream->stype);
        stream->eos = 0;
        stream->comment = 0;
      }
    }
  }
  
  if (vstream == NULL) {
    fprintf(stderr, "(%s) Found no video stream. Not splitting.\n", __FILE__);
    exit(1);
  }

  if (verbose <= 1) {
    if (print_mode & PM_FINDSPLITPOINTS)
      print_progress(0, file_size, "bytes");
    else if (!print_mode)
      print_progress(0, vstream->num_frames, "frame");
  }

  endofstream = 0;
  if (!print_mode && !cut_mode)
    next_sp = find_next_splitpoint(0);
  while (!endofstream) {
    sno = ogg_page_serialno(&page);
    stream = find_stream(sno);
    if (stream == NULL) {
      if (verbose > 1)
        fprintf(stdout, "(%s) Encountered packet for an unknown serial " \
                "%d !?\n", __FILE__, sno);
    } else {
      ogg_stream_pagein(&stream->instate, &page);
      stream->last_granulepos = stream->this_granulepos;
      stream->this_granulepos = ogg_page_granulepos(&page);
      while (ogg_stream_packetout(&stream->instate, &pack) == 1) {
        if (stream->stype != 'V') {
          if (stream->comment == 0) {
            if (print_mode) {
              copy_ogg_packet(&stream->header_packet2, &pack);
              if (stream->stype == 'v') {
                vorbis_unpack_comment(&stream->vc, (char *)pack.packet,
                                      pack.bytes);
                stream->vc_unpacked = 1;
              }
            }
            stream->comment = 1;
            stream->packetno = 2;
            continue;
          }
        } else {
          if (stream->comment == 0) {
            if (print_mode)
              copy_ogg_packet(&stream->header_packet2, &pack);
            stream->comment = 1;
            continue;
          } else if (stream->comment == 1) {
            if (print_mode)
              copy_ogg_packet(&stream->header_packet3, &pack);
            stream->comment = 2;
            stream->packetno = 3;
            continue;
          }
        }
        
        if (pack.granulepos == -1)
          pack.granulepos = stream->this_granulepos;
        current_time = (double)stream->last_granulepos * 1000.0 /
                       stream->sample_rate;
        if (cut_mode) {
          if ((current_cut != NULL) && (fout == NULL) &&
              (current_time == 0) && (current_cut->start == 0.0) &&
            !print_mode) {
            open_out_file();
            write_stream_headers();
          }
          if ((current_cut != NULL) && (current_time >= current_cut->end) &&
              (fout != NULL))
            do_close = 1;
        } else if ((fout == NULL) && !print_mode) {
          open_out_file();
          write_stream_headers();
          do_open = 0;
        }
        if ((stream->stype == 'v') && (pack.packet[0] & PACKET_IS_SYNCPOINT)) {
          stream->pzr->flush_pages();
          if (print_mode && (vstream == stream)) {
            ogg_int64_t pts = (ogg_int64_t)current_time;
            sp.pos_time = current_time;
            sp.pos_bytes = bwritten + get_queued_bytes();
            sp.frameno = stream->granulepos;
            add_splitpoint(&sp);
            if (print_mode & PM_PRINTSPLITPOINTS)
              fprintf(stdout, "(%s) Split point: %d, frameno: %lld, "
                      "bytes: %lld, start: %02d:%02d:%02d.%03d\n", __FILE__,
                      num_splitpoints - 1, stream->granulepos,
                      bwritten + get_queued_bytes(),
                      (int)(pts / 3600000),
                      (int)(pts / 60000) % 60,
                      (int)(pts / 1000) % 60,
                      (int)(pts % 1000));
          } else if (cut_mode) {
            if ((current_cut != NULL) && (fout == NULL) &&
                (current_time >= current_cut->start))
              do_open = 1;
          } else if (!print_mode &&
              (fout_num <= max_num_files) &&
              (next_sp != -1) &&
              (
               ((split_bytes > 0) &&
                ((bwritten_all + get_queued_bytes()) >=
                    splitpoints[next_sp].pos_bytes)
               ) ||
               (
                (split_time > 0) &&
                (current_time >= splitpoints[next_sp].pos_time)
               )
              )) {
            do_open = 1;
            do_close = 1;
          }
        }
        if (stream->stype == 'v') {
          if (do_close && !print_mode) {
            produce_eos_packets();
            if (fout != NULL) {
              fclose(fout);
              fout = NULL;
            }
            if (verbose) {
              ogg_int64_t sst = (ogg_int64_t)current_time;
              ogg_int64_t tf = (ogg_int64_t)(current_time - last_split);

              fprintf(stdout, "\n(%s) Closing file after writing %lld bytes "
                      "(%lld bytes written in all files), %02d:%02d:%02d.%03d "
                      "in this file (%02d:%02d:%02d.%03d elapsed).\n", __FILE__,
                      bwritten, bwritten_all,
                      (int)(tf / 3600000),
                      (int)(tf / 60000) % 60,
                      (int)(tf / 1000) % 60,
                      (int)(tf % 1000),
                      (int)(sst / 3600000),
                      (int)(sst / 60000) % 60,
                      (int)(sst / 1000) % 60,
                      (int)(sst % 1000));
            }
            if (cut_mode) {
              if (current_cut != NULL)
                current_cut = current_cut->next;
              if (current_cut == NULL)
                return;
            }
          }
          if (do_open && !print_mode) {
            stream_t *cur;

            if (!cut_mode)
              next_sp = find_next_splitpoint(next_sp);
            open_out_file();
            write_stream_headers();
            last_split = current_time;
            cur = first;
            while (cur != NULL) {
              cur->granulepossub = current_time * cur->sample_rate / 1000.0;
              cur = cur->next;
            }
          }
          do_open = 0;
          do_close = 0;


          hdrlen = (*pack.packet & PACKET_LEN_BITS01) >> 6;
          hdrlen |= (*pack.packet & PACKET_LEN_BITS2) << 1;
          lenbytes = 1;
          if ((hdrlen > 0) && (pack.bytes >= (hdrlen + 1)))
            for (i = 0, lenbytes = 0; i < hdrlen; i++) {
              lenbytes = lenbytes << 8;
              lenbytes += *((unsigned char *)pack.packet + hdrlen - i);
            }
          pack.granulepos = stream->granulepos + lenbytes - 1;
          stream->granulepos += lenbytes;

          if (print_mode)
            stream->num_frames += lenbytes;
          else {
            stream->cur_frame += lenbytes;
            if ((verbose <= 1) &&
                (stream->cur_frame % (stream->num_frames / 100) == 0))
              print_progress(stream->cur_frame, stream->num_frames, "frame");
          }
        } else
          pack.granulepos -= (ogg_int64_t)stream->granulepossub;
        pack.packetno = stream->packetno++;
        if (pack.e_o_s)
          stream->eos = 1;
        if (!cut_mode ||
            ((current_cut != NULL) && (current_time >= current_cut->start))) {
          stream->pzr->process(&pack);
          if (stream->stype == 't')
            stream->pzr->flush_pages();
        } else {
          count_only = 1;
          stream->pzr->process(&pack);
          if (stream->stype == 't')
            stream->pzr->flush_pages();
        }
        write_all_winner_pages();
        count_only = 0;
      }
    }

    while (ogg_sync_pageseek(&sync, &page) <= 0) {
      buf = ogg_sync_buffer(&sync, BLOCK_SIZE);
      nread = read(fdin, buf, BLOCK_SIZE);
      if (nread <= 0) {
        endofstream = 1;
        break;
      } else {
        ogg_sync_wrote(&sync, nread);
        bread += nread;
        if ((print_mode & PM_FINDSPLITPOINTS) && (verbose <= 1) &&
            (last_percent !=
              (int)((double)bread * 100.0 / (double)file_size))) {
          print_progress(bread, file_size, "bytes");
          last_percent = (int)((double)bread * 100.0 / (double)file_size);
        }
      }
    }
  }

  produce_eos_packets();  
  if (!print_mode) {
    fclose(fout);
    if (verbose) {
      ogg_int64_t sst = (ogg_int64_t)current_time;
      ogg_int64_t tf = (ogg_int64_t)(current_time - last_split);

      fprintf(stdout, "(%s) Closing file after writing %lld bytes "
              "(%lld bytes written in all files), %02d:%02d:%02d.%03d "
              "in this file (%02d:%02d:%02d.%03d elapsed).\n", __FILE__,
              bwritten, bwritten_all,
              (int)(tf / 3600000),
              (int)(tf / 60000) % 60,
              (int)(tf / 1000) % 60,
              (int)(tf % 1000),
              (int)(sst / 3600000),
              (int)(sst / 60000) % 60,
              (int)(sst / 1000) % 60,
              (int)(sst % 1000));
    }
  }
  
  stream = first;
  while (stream != NULL) {
    delete(stream->pzr);
    stream->mpage = NULL;
    stream = stream->next;
  }
  ogg_sync_clear(&sync);
  
  if ((verbose <= 1) && !print_mode) {
    print_progress(vstream->cur_frame, vstream->num_frames, "frame");
    if (!frontend_mode)
      fprintf(stdout, "\n");
  } else if ((verbose <= 1) && (print_mode & PM_FINDSPLITPOINTS)) {
    print_progress(file_size, file_size, "bytes");
    if (!frontend_mode)
      fprintf(stdout, "\n");
  }
}

int main(int argc, char *argv[]) {
  int          i, fdin = -1, byte_mode = 0;
  char        *fin_name, *b, *fout_name = NULL;
  ogg_int64_t  unit;
  
  nice(2);

  for (i = 1; i < argc; i++)
    if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version")) {
      fprintf(stdout, "ogmsplit v" VERSION "\n");
      exit(0);
    }
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
      verbose++;
    else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      usage(argv[0]);
      return 0;
    } else if (!strcmp(argv[i], "--frontend"))
      frontend_mode = 1;
    else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--size")) {
      if ((i + 1) > argc) {
        fprintf(stderr, "(%s) -s requires an argument.\n", __FILE__);
        exit(1);
      }
      b = &argv[i + 1][0];
      while (isspace(*b) || isdigit(*b))
        b++;
      unit = 1024 * 1024;
      if (!strcasecmp(b, "mib") || !strcasecmp(b, "mb")) 
        unit = 1024 * 1024;
      else if (!strcasecmp(b, "b"))
        unit = 1;
      else if (*b != 0) {
        fprintf(stderr, "(%s) '%s' is not a valid size.\n", __FILE__,
                argv[i + 1]);
        exit(1);
      }
      *b = 0;
      byte_mode = 1;
      split_bytes = (ogg_int64_t)strtol(argv[i + 1], NULL, 10) * unit;
      if (split_bytes <= 0) {
        fprintf(stderr, "(%s) '%s' is not a valid size.\n", __FILE__,
                argv[i + 1]);
        exit(1);
      }
      i++;
    } else if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--time")) {
      if ((i + 1) > argc) {
        fprintf(stderr, "(%s) -t requires an argument.\n", __FILE__);
        exit(1);
      }
      split_time = parse_time(argv[i + 1]);
      if (split_time <= 0.0) {
        fprintf(stderr, "(%s) '%s' is not a valid time.\n", __FILE__,
                argv[i + 1]);
        exit(1);
      }
      i++;
      split_time *= 1000.0;
    } else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--cuts")) {
      char    *p;
      double   last_cut_end = 0.0;
      cut_t  **last_cut = &cuts;

      if ((i + 1) >= argc) {
        fprintf(stderr, "(%s) -c requires an argument.\n", __FILE__);
        exit(1);
        
      }
      p = argv[i + 1];
      while (1) {
        char  *next, *sep, sepc;
        cut_t *cut;
        
        cut = (cut_t *)malloc(sizeof(cut_t));
        if (cut == NULL)
          die("malloc");
        cut->start = last_cut_end;

        next = strchr(p, ',');
        if (next != NULL)
          *next = '\0';

        sep = strchr(p, '-');
        if (sep == NULL)
          sep = strchr(p, '+');
        if (sep == NULL) {
          fprintf(stderr, "(%s) -c argument must be of the form start-end "
            "or start+len. Either start or end can be omitted.\n", __FILE__);
          exit(1);
        }
        sepc = *sep;
        *sep = '\0';

        if (sep != p)
          cut->start = parse_time(p) * 1000.0;
        if (sepc == '-') {
          if (*(sep + 1) == 0)
            cut->end = 999999 * 1000.0;
          else
            cut->end = parse_time(sep + 1) * 1000.0;
        } else {
          if (*(sep + 1) == 0) {
            fprintf(stderr, "(%s) -c argument must be of the form start-end "
              "or start+len. Either start or end can be omitted.\n", __FILE__);
            exit(1);
          }
          cut->end = cut->start + 1000.0 * parse_time(sep + 1);
        }
        
        if ((cut->start < last_cut_end) || (cut->start >= cut->end)) {
          fprintf(stderr, "(%s) -c arguments must be in order and not "
            "overlapping\n", __FILE__);
          exit(1);
        }
        last_cut_end = cut->end;

        *last_cut = cut;
        last_cut = &cut->next;
        cut->next = NULL;

        if (!next)
          break;
        p = next + 1;
      }
      cut_mode = 1;
      i++;
      current_cut = cuts;
    } else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
      if ((i + 1) > argc) {
        fprintf(stderr, "(%s) -o requires an argument.\n", __FILE__);
        exit(1);
      }
      fout_name = argv[i + 1];
      i++;
    } else if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--num")) {
      if ((i + 1) > argc) {
        fprintf(stderr, "(%s) -n requires an argument.\n", __FILE__);
        exit(1);
      }
      max_num_files = atoi(argv[i + 1]);
      if (max_num_files <= 0) {
        fprintf(stderr, "(%s) '%s' is not a valid number for -n.\n",
                __FILE__, argv[i + 1]);
        exit(1);
      }
      i++;
    } else if (!strcmp(argv[i], "-p") ||
               !strcmp(argv[i], "--print-splitpoints"))
      print_mode = 3;
    else {
      if (fdin != -1) {
        fprintf(stderr, "(%s) Only one input file allowed.\n", __FILE__);
        exit(1);
      }
      fdin = open(argv[i], O_RDONLY);
      if (fdin == -1) {
        fprintf(stderr, "(%s) Could not open \"%s\".\n", __FILE__,
                argv[i]);
        return 1;
      }
      fin_name = argv[i];
      file_size = lseek(fdin, 0, SEEK_END);
      if (file_size == (off_t)-1) {
        fprintf(stderr, "(%s) Could not seek to end of file.\n", __FILE__);
        return 1;
      }
      lseek(fdin, 0, SEEK_SET);
    }
  }

  if (cut_mode) {
    if ((split_time > 0.0) || (split_bytes > 0) || print_mode) {
      fprintf(stderr, "(%s) -c must not be used together with -t, -s or -p.\n",
              __FILE__);
      exit(1);
    }
    if (max_num_files != INT_MAX) {
      fprintf(stderr, "(%s) -n has no effect if -c is used.\n", __FILE__);
      exit(1);
    }
  } else {
    if ((split_time > 0.0) && (split_bytes > 0)) {
      fprintf(stderr, "(%s) -t and -s are mutually exclusive.\n", __FILE__);
      exit(1);
    } else if (print_mode && ((split_time > 0.0) || (split_bytes > 0))) {
      fprintf(stderr, "(%s) -p must not be used together with -t or -s.\n",
              __FILE__);
      exit(1);
    }
    if ((split_time < 0.0) && (split_bytes < 0))
      split_bytes = 700 * 1024 * 1024;
  }
  if (fdin == -1) {
    usage(argv[0]);
    exit(1);
  }

  if (fout_name == NULL)
    fout_name = strdup(fin_name);

  b = &fout_name[strlen(fout_name) - 1];
  while ((b != fout_name) && (*b != '.'))
    b--;
  fout_base = fout_name;
  if (*b == '.') {
    *b = 0;
    b++;
    fout_ext = b;
  } else
    fout_ext = "ogm";

  if (print_mode) {
    fprintf(stdout, "(%s) Will only print key frame numbers and the number "
            "of bytes encountered before each key frame. Will NOT create "
            "any file.\n", __FILE__);
    process_ogm(fdin);
  } else {
    if (verbose && !cut_mode) {
      fprintf(stdout, "(%s) Going to split \"%s\" to \"%s-00...%s\", " \
              "maximum number of files: ", __FILE__, fin_name, fout_base,
              fout_ext);
      if (max_num_files == INT_MAX)
        fprintf(stdout, "unlimited");
      else
        fprintf(stdout, "%d", max_num_files);
      fprintf(stdout, ", split after ");
      if (split_time < 0.0) {
        if (!byte_mode)
          fprintf(stdout, "%.2fMiB.\n", (double)split_bytes / 1024.0 / 1024.0);
        else
          fprintf(stdout, "%lldbytes.\n", split_bytes);
      }
      else
        fprintf(stdout, "%.3fs.\n", split_time / 1000.0);
    }
    fprintf(stdout, "(%s) First pass: finding split points. This may take "
            "a while. Get something to drink.\n", __FILE__);
    print_mode = PM_FINDSPLITPOINTS;
    process_ogm(fdin);
    print_mode = 0;
    bwritten_all = 0;
    bwritten = 0;
    bread = 0;
    current_time = 0;
    last_split = 0;
    close(fdin);
    if (!cut_mode && (num_splitpoints <= 1)) {
      fprintf(stdout, "(%s) No split points found - nothing to do.\n",
              __FILE__);
      exit(0);
    }
    fdin = open(fin_name, O_RDONLY);
    if (fdin == -1) {
      fprintf(stderr, "(%s) Could not re-open \"%s\".\n", __FILE__, fin_name);
      return 1;
    }
    fprintf(stdout, "(%s) Second pass: splitting the file. This will take "
            "even longer. Go and have a nice chat with your girlfriend.\n",
            __FILE__);
    process_ogm(fdin);  
    close(fdin);
  }

  return 0;
}
