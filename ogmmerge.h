/*
  ogmmerge -- utility for splicing together ogg bitstreams
      from component media subtypes

  ogmmerge.h
  general class and type definitions

  Written by Moritz Bunkus <moritz@bunkus.org>
  Based on Xiph.org's 'oggmerge' found in their CVS repository
  See http://www.xiph.org

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

#ifndef __ogmmerge_H__
#define __ogmmerge_H__

#include <stdio.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include "common.h"
#include "ogmstreams.h"

#define DISPLAYPRIORITY_HIGH   10
#define DISPLAYPRIORITY_MEDIUM  5
#define DISPLAYPRIORITY_LOW     1

typedef struct {
  int    displacement;
  double linear;
} audio_sync_t;

typedef struct {
  double start;
  double end;
} range_t;

typedef double stamp_t;
#define MAX_TIMESTAMP ((double)3.40282347e+38F)

typedef struct {
  ogg_page *og;
  stamp_t   timestamp;
  int       header_page;
  int       index_serial;
} ogmmerge_page_t;

typedef class generic_reader_c {
  protected:
    vorbis_comment *chapter_info;
  public:
    generic_reader_c();
    virtual ~generic_reader_c();
    virtual int              read() = 0;
    virtual int              serial_in_use(int serial) = 0;
    virtual ogmmerge_page_t *get_page() = 0;
    virtual ogmmerge_page_t *get_header_page(int header_type = 
                                             PACKET_TYPE_HEADER) = 0;
    virtual void             reset() = 0;
    virtual int              display_priority() = 0;
    virtual void             display_progress() = 0;
    virtual void             set_chapter_info(vorbis_comment *info);
} generic_reader_c;

typedef class generic_packetizer_c {
  protected:
    int             serialno;
    vorbis_comment *comments;
  public:
    generic_packetizer_c();
    virtual ~generic_packetizer_c() {};
    virtual int              page_available() = 0;
    virtual ogmmerge_page_t *get_page() = 0;
    virtual ogmmerge_page_t *get_header_page(int header_type =
                                             PACKET_TYPE_HEADER) = 0;
    virtual stamp_t          make_timestamp(ogg_int64_t granulepos) = 0;
    virtual int              serial_in_use(int serial);
    virtual int              flush_pages(int header_page = 0) = 0;
    virtual int              queue_pages(int header_page = 0) = 0;
    virtual stamp_t          get_smallest_timestamp() = 0;
    virtual void             produce_eos_packet() = 0;
    virtual void             produce_header_packets() = 0;
    virtual void             reset() = 0;
    virtual void             set_comments(vorbis_comment *ncomments);
} generic_packetizer_c;

class error_c {
  private:
    char *error;
  public:
    error_c(char *nerror) { error = nerror; };
    char *get_error()     { return error; };
};

#ifndef OGMSPLIT
extern int   force_flushing;
extern int   omit_empty_packets;
extern int   old_headers;
extern float video_fps; // needed for MicroDVD fps-to-time conversion
int          create_unique_serial();
extern void  add_index(int serial);
#endif

int             chapter_information_probe(FILE *file, off_t size);
vorbis_comment *chapter_information_read(char *name);
vorbis_comment *chapter_information_adjust(vorbis_comment *vc, double start,
                                           double end);

#endif  /* __ogmmerge_H__ */
