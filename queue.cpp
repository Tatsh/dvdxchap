/*
  ogmmerge -- utility for splicing together ogg bitstreams
      from component media subtypes

  queue.cpp
  OGG page queueing class used by every packetizer

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

#include "ogmmerge.h"
#include "queue.h"

q_c::q_c() throw (error_c) : generic_packetizer_c() {
  first = NULL;
  current = NULL;
  next_is_key = -1;
}

q_c::~q_c() {
  q_page_t *qpage, *tmppage;

  qpage = first;
  while (qpage) {
    if (qpage->mpage != NULL) {
      if (qpage->mpage->og != NULL) {
        if (qpage->mpage->og->header != NULL)
          free(qpage->mpage->og->header);
        if (qpage->mpage->og->body != NULL)
          free(qpage->mpage->og->body);
      free(qpage->mpage->og);
      }
      free(qpage->mpage);
    }
    tmppage = qpage->next;
    free(qpage);
    qpage = tmppage;
  }
}

ogg_page *q_c::copy_ogg_page(ogg_page *src) {
  ogg_page *dst;

  if (src == NULL)
    die("internal error");
  
  dst = (ogg_page *)malloc(sizeof(ogg_page));
  if (dst == NULL)
    die("malloc");
  if (src->header_len == 0) {
    fprintf(stderr, "FATAL: copy_ogg_page: src->header_len == 0.\n");
    exit(1);
  }
  dst->header = (unsigned char *)malloc(src->header_len);
  if (dst->header == NULL)
    die("malloc");
  dst->header_len = src->header_len;
  memcpy(dst->header, src->header, src->header_len);
  if (src->body_len != 0) {
    dst->body = (unsigned char *)malloc(src->body_len);
    if (dst->body == NULL)
      die("malloc");
    dst->body_len = src->body_len;
    memcpy(dst->body, src->body, src->body_len);
  } else {
    dst->body = (unsigned char *)malloc(1);
    if (dst->body == NULL)
      die("malloc");
    dst->body_len = 0;
    *(dst->body) = 0;
  }  
  return dst;
}

int q_c::add_ogg_page(ogg_page *opage, int header_page, int index_serial = -1) {
  q_page_t *qpage;
  
  if (opage == NULL)
    return EOTHER;
  if ((opage->header == NULL) || (opage->body == NULL)) {
    fprintf(stderr, "Warning: add_ogg_page with empty header or body.\n");
    return EOTHER;
  }
  qpage = (q_page_t *)malloc(sizeof(q_page_t));
  if (qpage == NULL)
    die("malloc");
  qpage->mpage = (ogmmerge_page_t *)malloc(sizeof(*qpage->mpage));
  if (qpage->mpage == NULL)
    die("malloc");
  qpage->mpage->og = q_c::copy_ogg_page(opage);
  qpage->mpage->timestamp = make_timestamp(ogg_page_granulepos(opage));
  qpage->mpage->header_page = header_page;
  qpage->mpage->index_serial = index_serial;
  qpage->next = NULL;
  if (current != NULL)
    current->next = qpage;
  if (first == NULL)
    first = qpage;
  current = qpage;

  return 0;
}

int q_c::flush_pages(int header_page) {
  ogg_page page;

  while (ogg_stream_flush(&os, &page)) {
    add_ogg_page(&page, header_page, next_is_key);
    next_is_key = -1;
  }

  return 0;
}

int q_c::queue_pages(int header_page) {
  ogg_page page;

  while (ogg_stream_pageout(&os, &page)) {
    add_ogg_page(&page, header_page, next_is_key);
    next_is_key = -1;
  }

  return 0;
}

ogmmerge_page_t *q_c::get_page() {
  ogmmerge_page_t *mp;
  q_page_t        *qpage;

  if (first && first->mpage) {
    mp = first->mpage;
    qpage = first->next;
    if (qpage == NULL)
      current = NULL;
    free(first);
    first = qpage;
    return mp;
  }
  
  return NULL;
}

ogmmerge_page_t *q_c::get_header_page(int header_type) {
  q_page_t *cur, *last;
  ogmmerge_page_t *omp;
  
  if (first == NULL)
    return NULL;
    
  last = NULL;
  cur = first;
  while (cur != NULL) {
    if (cur->mpage->header_page == header_type)
      break;
    last = cur;
    cur = cur->next;
  }
  
  if (cur == NULL)
    return NULL;
  
  omp = cur->mpage;
  if (!omp->header_page)
    return NULL;
  if (last != NULL)
    last->next = cur->next;
  else {
    if (current == first)
      current = first->next;
    first = first->next;
  }
  free(cur);
  
  return omp;
}

int q_c::page_available() {
  if ((first == NULL) || (first->mpage == NULL))
    return 0;
  else
    return 1;
}

int q_c::header_page_available() {
  q_page_t *cur = first;
  
  while (cur) {
    if (cur->mpage && cur->mpage->header_page)
      return 1;
    cur = cur->next;
  }
  return 0;
}

stamp_t q_c::get_smallest_timestamp() {
  if (first != NULL)
    return first->mpage->timestamp;
  else
    return MAX_TIMESTAMP;
}

long q_c::get_queued_bytes() {
  long      bytes;
  q_page_t *cur;
  
  bytes = 0;
  cur = first;
  while (cur != NULL) {
    if (cur->mpage != NULL)
      if (cur->mpage->og != NULL)
        bytes += cur->mpage->og->header_len + cur->mpage->og->body_len;
    cur = cur->next;
  }
  
  return bytes;
}

void q_c::next_page_contains_keyframe(int serial) {
  next_is_key = serial;
}
