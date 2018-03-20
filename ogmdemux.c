/*
  ogminfo -- utility for extracting raw streams from an OGG media file

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

#include <avilib.h>

#include "ogmstreams.h"
#include "common.h"

#define BLOCK_SIZE 4096

#define ACVORBIS 0xffff
#define ACPCM    0x0001
#define ACMP3    0x0055
#define ACAC3    0x2000

typedef struct stream_t {
  int                 serial;
  int                 fd;
  double              sample_rate;
  int                 eos, comment;
  int                 sno;
  char                stype;
  ogg_stream_state    instate;
  struct stream_t    *next;

  int                 acodec;
  struct wave_header  wh;
  ogg_int64_t         bwritten;

  ogg_stream_state    outstate;
  int                 packetno;
  ogg_int64_t         max_granulepos;

  avi_t              *avi;

  int                 subnr;
} stream_t;

stream_t *first;
int      nastreams = 0, nvstreams = 0, ntstreams = 0, numstreams = 0;
int      xraw = 0;

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

#define NOAUDIO 0
#define NOVIDEO 1
#define NOTEXT  2

unsigned char *xaudio;
unsigned char *xvideo;
unsigned char *xtext;
int no[3];

char *basename;

int verbose = 0;

double highest_ts = 0;

void usage(char *fname) {
  fprintf(stdout,
    "Usage: %s [options] inname\n\n"
    " options:\n"
    "  inname           Use 'inname' as the source.\n"
    "  -o, --output out Use 'outname' as the base for destination file names."
  "\n                   '-v1', '-v2', '-a1', '-t1'... will be appended to\n"
    "                   this name. Default: use 'inname'.\n"
    "  -a, --astream n  Extract specified audio stream. Can be used more\n"
    "                   than once. Default: extract all streams.\n"
    "  -d, --vstream n  Extract specified video stream. Can be used more\n"
    "                   than once. Default: extract all streams.\n"
    "  -t, --tstream n  Extract specified text stream. Can be used more\n"
    "                   than once. Default: extract all streams.\n"
    "  -na, --noaudio   Don't extract any audio streams.\n"
    "  -nv, --novideo   Don't extract any video streams.\n"
    "  -nt, --notext    Don't extract any text streams.\n"
    "                   Default: extract all streams.\n"
    "  -r, --raw        Extract the raw streams only.\n"
    "                   Default: extract to useful formats\n"
    "                   (AVI, WAV, OGG, SRT...).\n"
    "  -v, --verbose    Increase verbosity.\n"
    "  -h, --help       Show this help.\n"
    "  -V, --version    Show version number.\n",
    fname);
}

int extraction_requested(unsigned char *s, int stream, int type) {
  int i;
  
  if (no[type])
    return 0;
  if (strlen((char *)s) == 0)
    return 1;
  for (i = 0; i < strlen((char *)s); i++)
    if (s[i] == stream)
      return 1;

  return 0;
}

void flush_pages(stream_t *stream, ogg_packet *op) {
  ogg_page page;
  int ih, ib;

  while (ogg_stream_flush(&stream->outstate, &page)) {
    ih = write(stream->fd, page.header, page.header_len);
    ib = write(stream->fd, page.body, page.body_len);
    if (verbose > 1)
      fprintf(stdout, "(%s) x/a%d: %d + %d written\n", __FILE__, stream->sno,
              ih, ib);
  }
}

void write_pages(stream_t *stream, ogg_packet *op) {
  ogg_page page;
  int ih, ib;

  while (ogg_stream_pageout(&stream->outstate, &page)) {
    ih = write(stream->fd, page.header, page.header_len);
    ib = write(stream->fd, page.body, page.body_len);
    if (verbose > 1)
      fprintf(stdout, "(%s) x/a%d: %d + %d written\n", __FILE__, stream->sno,
              ih, ib);
  }
}

void handle_packet(stream_t *stream, ogg_packet *pack, ogg_page *page) {
  int i, hdrlen, end;
  long long lenbytes;
  char *sub;
  char out[100];
  ogg_int64_t pgp, sst;
  
  if (pack->e_o_s) {
    stream->eos = 1;
    pack->e_o_s = 1;
  }

  if (((double)pack->granulepos * 1000.0 / (double)stream->sample_rate) >
      highest_ts)
    highest_ts = (double)pack->granulepos * 1000.0 /
                  (double)stream->sample_rate;

  switch (stream->stype) {
    case 'v': 
      if (!extraction_requested(xvideo, stream->sno, NOVIDEO))
        return;
      break;
    case 'a': 
      if (!extraction_requested(xaudio, stream->sno, NOAUDIO))
        return;
      break;
    case 't': 
      if (!extraction_requested(xtext, stream->sno, NOTEXT))
        return;
      break;
  }

  hdrlen = (*pack->packet & PACKET_LEN_BITS01) >> 6;
  hdrlen |= (*pack->packet & PACKET_LEN_BITS2) << 1;
  for (i = 0, lenbytes = 0; i < hdrlen; i++) {
    lenbytes = lenbytes << 8;
    lenbytes += *((unsigned char *)pack->packet + hdrlen - i);
  }

  switch (stream->stype) {
    case 'v':
      if (((*pack->packet & 3) == PACKET_TYPE_HEADER) ||
          ((*pack->packet & 3) == PACKET_TYPE_COMMENT))
        return;
      if (!xraw)
        i = AVI_write_frame(stream->avi, (char *)&pack->packet[hdrlen + 1],
                            pack->bytes - 1 - hdrlen,
                            (pack->packet[0] & PACKET_IS_SYNCPOINT) ? 1 : 0);
      else
        i = write(stream->fd, (char *)&pack->packet[hdrlen + 1],
                  pack->bytes - 1 - hdrlen);
      if (verbose > 1)
        fprintf(stdout, "(%s) x/v%d: %d written\n", __FILE__, stream->sno, i);
      break;
    case 't':
      if (((*pack->packet & 3) == PACKET_TYPE_HEADER) ||
          ((*pack->packet & 3) == PACKET_TYPE_COMMENT))
        return;

      if (xraw) {
        i = write(stream->fd, (char *)&pack->packet[hdrlen + 1],
                  pack->bytes - 1 - hdrlen);
        if (verbose > 1)
          fprintf(stdout, "(%s) x/t%d: %d written\n", __FILE__,
                  stream->sno, i);
        return;
      }

      sub = (char *)&pack->packet[hdrlen + 1];
      if ((strlen(sub) > 1) || ((strlen(sub) > 0) && (*sub != ' '))) {
        sst = (pack->granulepos / stream->sample_rate) * 1000;
        pgp = sst + lenbytes;
        sprintf(out, "%d\r\n%02d:%02d:%02d,%03d --> " \
                "%02d:%02d:%02d,%03d\r\n", stream->subnr + 1,
                (int)(sst / 3600000),
                (int)(sst / 60000) % 60,
                (int)(sst / 1000) % 60,
                (int)(sst % 1000),
                (int)(pgp / 3600000),
                (int)(pgp / 60000) % 60,
                (int)(pgp / 1000) % 60,
                (int)(pgp % 1000));
        i = write(stream->fd, out, strlen(out));
        end = strlen(sub) - 1;
        while ((end >= 0) && ((sub[end] == '\n') || (sub[end] == '\r'))) {
          sub[end] = 0;
          end--;
        }
        i += write(stream->fd, sub, strlen(sub));
        i += write(stream->fd, "\r\n\r\n", 4);
        stream->subnr++;
        if (verbose > 1)
          fprintf(stdout, "(%s) x/t%d: %d written\n", __FILE__,
                  stream->sno, i);
      }
      break;
    case 'a':
      switch (stream->acodec) {
        case ACVORBIS:
          if (xraw) {
            if (stream->packetno == 0)
              i = write(stream->fd, (char *)pack->packet, pack->bytes);
            else
              i = write(stream->fd, (char *)&pack->packet[1],
                        pack->bytes - 1);
            if (verbose > 1)
              fprintf(stdout, "(%s) x/a%d: %d written\n", __FILE__,
                      stream->sno, i);
            return;
          }
          stream->max_granulepos = (pack->granulepos > stream->max_granulepos ?
                                    pack->granulepos : stream->max_granulepos);
          if ((stream->packetno == 0) || (stream->packetno == 2)) {
            ogg_stream_packetin(&stream->outstate, pack);
            flush_pages(stream, pack);
          } else {
            ogg_stream_packetin(&stream->outstate, pack);
            write_pages(stream, pack);
          }
          stream->packetno++;
          break;
        default:
          if (((*pack->packet & 3) == PACKET_TYPE_HEADER) ||
              ((*pack->packet & 3) == PACKET_TYPE_COMMENT))
            return;

          i = write(stream->fd, pack->packet + 1 + hdrlen,
                    pack->bytes - 1 - hdrlen);
          stream->bwritten += i;
          if (verbose > 1)
            fprintf(stdout, "(%s) x/a%d: %d written\n", __FILE__,
                    stream->sno, i);
          break;
      }
    break;
  }
}

void process_ogm(int fdin)
{
  ogg_sync_state    sync;
  ogg_page          page;
  ogg_packet        pack;
  vorbis_info       vi;
  vorbis_comment    vc;
  char             *buf, *new_name;
  int               nread, np, sno;
  int               endofstream = 0;
  stream_t         *stream;

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
      if ((nread = read(fdin, buf, BLOCK_SIZE)) <= 0) {
        if (verbose > 0)
          fprintf(stdout, "(%s) end of stream\n", __FILE__);
        return;
      }
      ogg_sync_wrote(&sync, nread);
      continue;
    }

    if (!ogg_page_bos(&page)) {
      break;
    } else {
      ogg_stream_state sstate;
      sno = ogg_page_serialno(&page);
      if (ogg_stream_init(&sstate, sno)) {
        fprintf(stderr, "(%s) ogg_stream_init failed\n", __FILE__);
        return;
      }
      ogg_stream_pagein(&sstate, &page);
      ogg_stream_packetout(&sstate, &pack);

      if ((pack.bytes >= 7) && ! strncmp(&pack.packet[1], "vorbis", 6)) {
        stream = (stream_t *)malloc(sizeof(stream_t));
        if (stream == NULL) {
          fprintf(stderr, "malloc failed.\n");
          exit(1);
        }
        memset(stream, 0, sizeof(stream_t));
        if (verbose > 0) {
          vorbis_info_init(&vi);
          vorbis_comment_init(&vc);
          if (vorbis_synthesis_headerin(&vi, &vc, &pack) >= 0) {
            fprintf(stdout, "(%s) (a%d/%d) Vorbis audio (channels %d " \
                    "rate %ld)\n", __FILE__, nastreams + 1, numstreams + 1,
                    vi.channels, vi.rate);
            stream->sample_rate = vi.rate;
          } else
            fprintf(stdout, "(%s) (a%d/%d) Vorbis audio stream indicated " \
                    "but no Vorbis stream header found.\n", __FILE__,
                    nastreams + 1, numstreams + 1);
        }
        stream->serial = sno;
        stream->acodec = ACVORBIS;
        stream->sample_rate = -1;
        stream->sno = nastreams + 1;
        stream->stype = 'a';
        memcpy(&stream->instate, &sstate, sizeof(sstate));
        if (extraction_requested(xaudio, nastreams + 1, NOAUDIO)) {
          new_name = malloc(strlen(basename) + 20);
          if (!new_name) {
            fprintf(stderr, "(%s) Failed to allocate %d bytes.\n", __FILE__,
              strlen(basename) + 20);
            exit(1);
          }
          if (xraw)
            sprintf(new_name, "%s-a%d.raw", basename, nastreams + 1);
          else
            sprintf(new_name, "%s-a%d.ogg", basename, nastreams + 1);
          stream->fd = open(new_name, O_WRONLY | O_CREAT | O_TRUNC,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
          if (stream->fd == -1) {
            fprintf(stderr, "(%s) Failed to create \"%s\" (%d, %s).\n",
                    __FILE__, new_name, errno, strerror(errno));
            exit(1);
          }
          if (!xraw)
            ogg_stream_init(&stream->outstate, rand());
          if (verbose > 0)
            fprintf(stdout, "(%s) Extracting a%d to \"%s\".\n", __FILE__,
                    nastreams + 1, new_name);
          do
            handle_packet(stream, &pack, &page);
          while (ogg_stream_packetout(&stream->instate, &pack) == 1);
          free(new_name);
        }
        add_stream(stream);
        nastreams++;
        numstreams++;
      } else if ((pack.bytes >= 142) &&
                 !strncmp(&pack.packet[1],"Direct Show Samples embedded in "
                          "Ogg", 35) ) {
        if ((*(int32_t*)(pack.packet+96) == 0x05589f80) &&
            (pack.bytes >= 184)) {
           fprintf(stdout, "(%s) (v%d/%d) Found old video header. Not " \
                   "supported.\n", __FILE__, nvstreams + 1, numstreams + 1);
        } else if (*(int32_t*)pack.packet+96 == 0x05589F81) {
          fprintf(stdout, "(%s) (a%d/%d) Found old audio header. Not " \
                  "supported.\n", __FILE__, nastreams + 1, numstreams + 1);
        } else {
          if (verbose > 0)
            fprintf(stdout, "(%s) OGG stream %d has an old header with an " \
                    "unknown type.", __FILE__, numstreams + 1);
        }
      }  else if (((*pack.packet & PACKET_TYPE_BITS ) == PACKET_TYPE_HEADER) &&
	          (pack.bytes >= (int)(sizeof(old_stream_header) + 1))) {
        stream_header sth;
        copy_headers(&sth, (old_stream_header *)&pack.packet[1], pack.bytes);
        if (!strncmp(sth.streamtype, "video", 5)) {
          unsigned long codec;
          char ccodec[5];
          strncpy(ccodec, sth.subtype, 4);
          ccodec[4] = 0;
          codec = (sth.subtype[0] << 24) + 
            (sth.subtype[1] << 16) + (sth.subtype[2] << 8) + sth.subtype[3];
          if (verbose > 0)
            fprintf(stdout, "(%s) (v%d/%d) fps: %.3f width height: %dx%d " \
                    "codec: %p (%s)\n", __FILE__, nvstreams + 1,
                    numstreams + 1,
                    (double)10000000 / (double)get_uint64(&sth.time_unit),
                    get_uint32(&sth.sh.video.width),
                    get_uint32(&sth.sh.video.height), (void *)codec,
                    ccodec);
          stream = (stream_t *)malloc(sizeof(stream_t));
          if (stream == NULL) {
            fprintf(stderr, "malloc failed.\n");
            exit(1);
          }
          stream->stype = 'v';
          stream->serial = sno;
          stream->sample_rate = (double)10000000 /
            (double)get_uint64(&sth.time_unit);
          stream->sno = nvstreams + 1;
          memcpy(&stream->instate, &sstate, sizeof(sstate));
          if (extraction_requested(xvideo, nvstreams + 1, NOVIDEO)) {
            new_name = malloc(strlen(basename) + 20);
            if (!new_name) {
              fprintf(stderr, "(%s) Failed to allocate %d bytes.\n", __FILE__,
                strlen(basename) + 20);
              exit(1);
            }
            if (!xraw) {
              sprintf(new_name, "%s-v%d.avi", basename, nvstreams + 1);
              stream->avi = AVI_open_output_file(new_name);
              if (stream->avi == NULL) {
                fprintf(stderr, "(%s) Failed to create \"%s\" (%s).\n",
                        __FILE__, new_name, AVI_strerror());
                exit(1);
              }
              AVI_set_video(stream->avi, get_uint32(&sth.sh.video.width),
                            get_uint32(&sth.sh.video.height),
                            stream->sample_rate, ccodec);
            } else {
              sprintf(new_name, "%s-v%d.raw", basename, nvstreams + 1);
              stream->fd = open(new_name, O_WRONLY | O_CREAT | O_TRUNC,
                                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
              if (stream->fd == -1) {
                fprintf(stderr, "(%s) Failed to create \"%s\" (%d, %s).\n",
                        __FILE__, new_name, errno, strerror(errno));
                exit(1);
              }
            }
            if (verbose > 0)
              fprintf(stdout, "(%s) Extracting v%d to \"%s\".\n", __FILE__,
                      nvstreams + 1, new_name);
            do
              handle_packet(stream, &pack, &page);
            while (ogg_stream_packetout(&stream->instate, &pack) == 1);
          }
          add_stream(stream);
          nvstreams++;
          numstreams++;
        } else if (!strncmp(sth.streamtype, "audio", 5)) {
          int codec;
          char buf[5];
          memcpy(buf, sth.subtype, 4);
          buf[4] = 0;
          codec = strtoul(buf, NULL, 16);
          if (verbose > 0) {
            fprintf(stdout, "(%s) (a%d/%d) codec: %d (0x%04x) (%s), bits per "
                    "sample: %d channels: %hd  samples per second: %lld",
                    __FILE__, nastreams + 1, numstreams + 1, codec, codec,
                    codec == ACPCM ? "PCM" : codec == 55 ? "MP3" :
                    codec == ACMP3 ? "MP3" :
                    codec == ACAC3 ? "AC3" : "unknown",
                    get_uint16(&sth.bits_per_sample),
                    get_uint16(&sth.sh.audio.channels),
                    get_uint64(&sth.samples_per_unit));
            fprintf(stdout, " avgbytespersec: %d blockalign: %hd\n",
                    get_uint32(&sth.sh.audio.avgbytespersec),
                    get_uint16(&sth.sh.audio.blockalign));
          }
          stream = (stream_t *)malloc(sizeof(stream_t));
          if (stream == NULL) {
            fprintf(stderr, "malloc failed.\n");
            exit(1);
          }
          stream->sno = nastreams + 1;
          stream->stype = 'a';
          stream->sample_rate = get_uint64(&sth.samples_per_unit) *
                                get_uint16(&sth.sh.audio.channels);
          stream->serial = sno;
          stream->acodec = codec;
          strncpy(stream->wh.riff.id, "RIFF", 4);
          strncpy(stream->wh.riff.wave_id, "WAVE", 4);
          strncpy(stream->wh.format.id, "fmt ", 4);
          put_uint32(&stream->wh.format.len, 16);
          put_uint16(&stream->wh.common.wFormatTag, 1);
          put_uint16(&stream->wh.common.wChannels,
                     get_uint16(&sth.sh.audio.channels));
          put_uint32(&stream->wh.common.dwSamplesPerSec,
                     get_uint64(&sth.samples_per_unit));
          put_uint32(&stream->wh.common.dwAvgBytesPerSec,
                     get_uint16(&sth.sh.audio.channels) *
                     get_uint64(&sth.samples_per_unit) *
                     get_uint16(&sth.bits_per_sample) / 8);
          put_uint16(&stream->wh.common.wBlockAlign, 4);
          put_uint16(&stream->wh.common.wBitsPerSample,
                     get_uint16(&sth.bits_per_sample));
          strncpy(stream->wh.data.id, "data", 4);
          memcpy(&stream->instate, &sstate, sizeof(sstate));
          if (extraction_requested(xaudio, nastreams + 1, NOAUDIO)) {
            new_name = malloc(strlen(basename) + 30);
            if (!new_name) {
              fprintf(stderr, "(%s) Failed to allocate %d bytes.\n", __FILE__,
                strlen(basename) + 30);
              exit(1);
            }
            if (!xraw)
              sprintf(new_name, "%s-a%d.%s", basename, nastreams + 1,
                      codec == ACPCM ? "wav" :
                      codec == ACMP3 ? "mp3" :
                      codec == ACAC3 ? "ac3" : "audio");
            else
              sprintf(new_name, "%s-a%d.raw", basename, nastreams + 1);
            stream->fd = open(new_name, O_WRONLY | O_CREAT | O_TRUNC,
                              S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (stream->fd == -1) {
              fprintf(stderr, "(%s) Failed to create \"%s\" (%d, %s).\n",
                      __FILE__, new_name, errno, strerror(errno));
              exit(1);
            }
            if ((codec == ACPCM) && !xraw)
              write(stream->fd, &stream->wh, sizeof(stream->wh));
            if (verbose > 0)
              fprintf(stdout, "(%s) Extracting a%d to \"%s\".\n", __FILE__,
                      nastreams + 1, new_name);
            do
              handle_packet(stream, &pack, &page);
            while (ogg_stream_packetout(&stream->instate, &pack) == 1);
          }
          add_stream(stream);
          nastreams++;
          numstreams++;
        } else if (!strncmp(sth.streamtype, "text", 4)) {
          if (verbose > 0)
            fprintf(stdout, "(%s) (t%d/%d) text/subtitle stream\n", __FILE__,
                    ntstreams + 1, numstreams + 1);
          stream = (stream_t *)malloc(sizeof(stream_t));
          if (stream == NULL) {
            fprintf(stderr, "malloc failed.\n");
            exit(1);
          }
          stream->sno = ntstreams + 1;
          stream->stype = 't';
          stream->sample_rate = (double)10000000 /
            (double)get_uint64(&sth.time_unit);
          stream->serial = sno;
          memcpy(&stream->instate, &sstate, sizeof(sstate));
          if (extraction_requested(xtext, ntstreams + 1, NOTEXT)) {
            new_name = malloc(strlen(basename) + 20);
            if (!new_name) {
              fprintf(stderr, "(%s) Failed to allocate %d bytes.\n", __FILE__,
                strlen(basename) + 10);
              exit(1);
            }
            if (!xraw)
              sprintf(new_name, "%s-t%d.srt", basename, ntstreams + 1);
            else
              sprintf(new_name, "%s-t%d.raw", basename, ntstreams + 1);
            stream->fd = open(new_name, O_WRONLY | O_CREAT | O_TRUNC,
                              S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (stream->fd == -1) {
              fprintf(stderr, "(%s) Failed to create \"%s\" (%d, %s).\n",
                      __FILE__, new_name, errno, strerror(errno));
              exit(1);
            }
            if (verbose > 0)
              fprintf(stdout, "(%s) Extracting t%d to \"%s\".\n", __FILE__,
                      ntstreams + 1, new_name);
            do
              handle_packet(stream, &pack, &page);
            while (ogg_stream_packetout(&stream->instate, &pack) == 1);
          }
          add_stream(stream);
          ntstreams++;
          numstreams++;
        } else {
          fprintf(stdout, "(%s) (%d) found new header of unknown/" \
                  "unsupported type\n", __FILE__, numstreams + 1);
        }

      } else {
        fprintf(stdout, "(%s) OGG stream %d is of an unknown type " \
                "(bad header?)\n", __FILE__, numstreams + 1);
      }
    }
  }

  endofstream = 0;
  while (!endofstream) {
    sno = ogg_page_serialno(&page);
    stream = find_stream(sno);
    if (stream == NULL) {
      if (verbose > 1)
        fprintf(stdout, "(%s) Encountered packet for an unknown serial " \
                "%d !?\n", __FILE__, sno);
    } else {
      if (verbose > 1)
        fprintf(stdout, "(%s) %c%d: NEW PAGE\n",
                __FILE__, stream->stype, stream->sno);
                
      ogg_stream_pagein(&stream->instate, &page);
      while (ogg_stream_packetout(&stream->instate, &pack) == 1)
        handle_packet(stream, &pack, &page);
    }

    while (ogg_sync_pageseek(&sync, &page) <= 0) {
      buf = ogg_sync_buffer(&sync, BLOCK_SIZE);
      nread = read(fdin, buf, BLOCK_SIZE);
      if (nread <= 0) {
        stream = first;
        while (stream != NULL) {
          switch (stream->stype) {
            case 'v':
              if ((stream->avi != NULL) && !xraw)
                AVI_close(stream->avi);
              else if (stream->fd > 0)
                close(stream->fd);
              break;
            case 't':
              if (stream->fd > 0)
                close(stream->fd);
              break;
            case 'a':
              if ((stream->fd > 0) && !xraw) {
                switch (stream->acodec) {
                  case ACVORBIS:
                    if (!stream->eos) {
                      pack.b_o_s = 0;
                      pack.e_o_s = 1;
                      pack.packet = NULL;
                      pack.bytes = 0;
                      pack.granulepos = stream->max_granulepos;
                      pack.packetno = stream->packetno;
                      ogg_stream_packetin(&stream->outstate, &pack);
                    }
                    flush_pages(stream, &pack);
                    ogg_stream_clear(&stream->outstate);
                    break;
                  case ACPCM:
                    put_uint32(&stream->wh.riff.len,
                               stream->bwritten + sizeof(stream->wh) - 8);
                    put_uint32(&stream->wh.data.len, stream->bwritten);
                    lseek(stream->fd, 0, SEEK_SET);
                    write(stream->fd, &stream->wh, sizeof(stream->wh));
                    break;
                }
                close(stream->fd);
              } else if (stream->fd > 0)
                close(stream->fd);
              break;
          }
          stream = stream->next;
        }
        if (verbose > 0)
          fprintf(stdout, "(%s) end of stream\n", __FILE__);
        endofstream = 1;
        break;
      } else
        ogg_sync_wrote(&sync, nread);
    }
  }
}

int main(int argc, char *argv[]) {
  int i, fdin = -1, stream, l;
  
  nice(2);

  xaudio = (char *)malloc(1);
  xvideo = (char *)malloc(1);
  xtext = (char *)malloc(1);
  *xaudio = 0;
  *xvideo = 0;
  *xtext = 0;
  basename = NULL;
  no[NOAUDIO] = 0;
  no[NOVIDEO] = 0;
  no[NOTEXT] = 0;
  for (i = 1; i < argc; i++)
    if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version")) {
      fprintf(stdout, "ogmdemux v" VERSION "\n");
      exit(0);
    }
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
      verbose++;
    else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
      if ((i + 1) == argc) {
        fprintf(stderr, "(%s) -o needs a file name.\n", __FILE__);
        return 1;
      }
      basename = argv[i + 1];
      i++;
    } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
      usage(argv[0]);
      exit(0);
    } else if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--astream")) {
      if ((i + 1) == argc) {
        fprintf(stderr, "(%s) -a needs a stream number.\n", __FILE__);
        return 1;
      }
      if (no[NOAUDIO]) {
        fprintf(stderr, "(%s) -na was already given - aborting.\n", __FILE__);
        return 1;
      }
      stream = strtol(argv[i + 1], NULL, 10);
      if ((stream < 1) || (stream > 255)) {
        fprintf(stderr, "(%s) Audio stream range is 1..255.\n", __FILE__);
        return 1;
      }
      l = strlen(xaudio);
      xaudio = realloc(xaudio, l + 2);
      xaudio[l] = (unsigned char)stream;
      xaudio[l + 1] = 0;
      i++;
    } else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--vstream")) {
      if ((i + 1) == argc) {
        fprintf(stderr, "(%s) -d needs a stream number.\n", __FILE__);
        return 1;
      }
      if (no[NOVIDEO]) {
        fprintf(stderr, "(%s) -nd was already given - aborting.\n", __FILE__);
        return 1;
      }
      stream = strtol(argv[i + 1], NULL, 10);
      if ((stream < 1) || (stream > 255)) {
        fprintf(stderr, "(%s) Video stream range is 1..255.\n", __FILE__);
        return 1;
      }
      l = strlen(xvideo);
      xvideo = realloc(xvideo, l + 2);
      xvideo[l] = (unsigned char)stream;
      xvideo[l + 1] = 0;
      i++;
    } else if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--tstream")) {
      if ((i + 1) == argc) {
        fprintf(stderr, "(%s) -t needs a stream number.\n", __FILE__);
        return 1;
      }
      if (no[NOTEXT]) {
        fprintf(stderr, "(%s) -nt was already given - aborting.\n", __FILE__);
        return 1;
      }
      stream = strtol(argv[i + 1], NULL, 10);
      if ((stream < 1) || (stream > 255)) {
        fprintf(stderr, "(%s) Text stream range is 1..255.\n", __FILE__);
        return 1;
      }
      l = strlen(xtext);
      xtext = realloc(xtext, l + 2);
      xtext[l] = (unsigned char)stream;
      xtext[l + 1] = 0;
      i++;
    } else if (!strcmp(argv[i], "-na") || !strcmp(argv[i], "--noaudio"))
      no[NOAUDIO] = 1;
    else if (!strcmp(argv[i], "-nv") || !strcmp(argv[i], "--novideo"))
      no[NOVIDEO] = 1;
    else if (!strcmp(argv[i], "-nt") || !strcmp(argv[i], "--notext"))
      no[NOTEXT] = 1;
    else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--raw"))
      xraw = 1;
    else if (fdin < 0) {
      fdin = open(argv[i], O_RDONLY);
      if (fdin == -1) {
        fprintf(stderr, "(%s) Could not open \"%s\".\'\n", __FILE__, argv[i]);
        return 1;
      }
      if (basename == NULL)
        basename = argv[i];
    } else {
      fprintf(stderr, "(%s) More than one input file given. Aborting.\n",
              __FILE__);
      return 1;
    }
  }
  if (basename != NULL) {
    char *strp;
    strp = basename;
    while (*strp) {
      if (*strp == '/')
        basename = &strp[1];
      strp++;
    }
  }
  if (fdin == -1) {
    usage(argv[0]);
    exit(1);
  }
  
  process_ogm(fdin);  
  
  close(fdin);
  return 0;
}
