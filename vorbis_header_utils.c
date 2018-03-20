#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <vorbis/codec.h>

#include "ogmstreams.h"
#include "common.h"
#include "vorbis_header_utils.h"

char *mmalloc(int size) {
  char *p = malloc(size);
  if (p == NULL) {
    fprintf(stderr, "FATAL: could not allocate %d bytes of memory.\n", size);
    exit(1);
  }
  memset(p, 0, size);
  return p;
}

/*
 * Taken from libvorbis/lib/info.c ...
 */
int vorbis_unpack_comment(vorbis_comment *vc, char *buf, int len) {
  int i, pos;
  int vendorlen;
  
  if (len < 7) {
    vorbis_comment_clear(vc);
    return -1;
  }
  pos = 7;
  if ((pos + 4) > len) {
    vorbis_comment_clear(vc);
    return -1;
  }
// vendorlen = oggpack_read(opb,32);
  vendorlen = get_uint32(&buf[pos]);
  pos += 4;
  vc->vendor = mmalloc(vendorlen + 1);
//  _v_readstring(opb,vc->vendor,vendorlen);
  if ((pos + vendorlen) > len) {
    vorbis_comment_clear(vc);
    return -1;
  }
  memcpy(vc->vendor, &buf[pos], vendorlen);
  pos += vendorlen;
//  vc->comments=oggpack_read(opb,32);
  if ((pos + 4) > len) {
    vorbis_comment_clear(vc);
    return -1;
  }
  vc->comments = get_uint32(&buf[pos]);
  pos += 4;
  if (vc->comments < 0) {
    vorbis_comment_clear(vc);
    return -1;
  }

  vc->user_comments = (char **)mmalloc((vc->comments + 1) *
                                       sizeof(*vc->user_comments));
  vc->comment_lengths = (int *)mmalloc((vc->comments + 1) *
                                        sizeof(*vc->comment_lengths));

  for(i = 0; i < vc->comments; i++) {
    int clen;
//    clen = oggpack_read(opb,32);
    if ((pos + 4) > len) {
      vorbis_comment_clear(vc);
      return -1;
    }
    clen = get_uint32(&buf[pos]);
    pos += 4;
    if(clen < 0) {
      vorbis_comment_clear(vc);
      return -1;
    }
    vc->comment_lengths[i] = clen;
    vc->user_comments[i] = mmalloc(clen + 1);
    if ((pos + clen) > len) {
      vorbis_comment_clear(vc);
      return -1;
    }
//    _v_readstring(opb,vc->user_comments[i],len);
    memcpy(vc->user_comments[i], &buf[pos], clen);
    pos += clen;
  }       
//  if(oggpack_read(opb,1)!=1)goto err_out; /* EOP check */

  return 0;
}

void vorbis_comment_remove_number(vorbis_comment *vc, int num) {
  char **user_comments;
  int   *comment_lengths;

  if (num >= vc->comments)
    return;

  user_comments = (char **)malloc(vc->comments * sizeof(char *));
  if (user_comments == NULL)
    die("malloc");
  comment_lengths = (int *)malloc(vc->comments * sizeof(int));
  if (comment_lengths == NULL)
    die("malloc");
  free(vc->user_comments[num]);
  memcpy(&user_comments[0], &vc->user_comments[0], num * sizeof(char *));
  memcpy(&user_comments[num], &vc->user_comments[num + 1],
         (vc->comments - num) * sizeof(char *));
  memcpy(&comment_lengths[0], &vc->comment_lengths[0], num * sizeof(int));
  memcpy(&comment_lengths[num], &vc->comment_lengths[num + 1],
         (vc->comments - num) * sizeof(int));
  free(vc->user_comments);
  free(vc->comment_lengths);
  vc->user_comments = user_comments;
  vc->comment_lengths = comment_lengths;
  vc->comments--;
  if (vc->user_comments[vc->comments] != NULL) {
    fprintf(stderr, "DBG: nn\n");
  }
}

void vorbis_comment_remove_tag(vorbis_comment *vc, char *tag) {
  int   i, done;
  char  *cmp;
  
  cmp = (char *)malloc(strlen(tag) + 2);
  if (cmp == NULL)
    die("malloc");
  sprintf(cmp, "%s=", tag);
  do {
    done = 1;
    for (i = 0; i < vc->comments; i++)
      if (!strncmp(vc->user_comments[i], cmp, strlen(cmp))) {
        free(cmp);
        vorbis_comment_remove_number(vc, i);
        done = 0;
        break;
      }
  } while (!done);
  free(cmp);
}

vorbis_comment *vorbis_comment_dup(vorbis_comment *vc) {
  vorbis_comment *new_vc;
  int             i;
  
  if (vc == NULL)
    return NULL;

  new_vc = (vorbis_comment *)malloc(sizeof(vorbis_comment));
  if (new_vc == NULL)
    die("malloc");
  
  memcpy(new_vc, vc, sizeof(vorbis_comment));
  new_vc->user_comments = (char **)malloc((vc->comments + 1) * sizeof(char *));
  new_vc->comment_lengths = (int *)malloc((vc->comments + 1) * sizeof(int));
  if ((new_vc->user_comments == NULL) || (new_vc->comment_lengths == NULL))
    die("malloc");
  for (i = 0; i < vc->comments; i++)
    new_vc->user_comments[i] = strdup(vc->user_comments[i]);
  new_vc->user_comments[vc->comments] = 0;
  memcpy(new_vc->comment_lengths, vc->comment_lengths,
         (vc->comments + 1) * sizeof(char *));
  new_vc->vendor = strdup(vc->vendor);
  
  return new_vc;
}

vorbis_comment *vorbis_comment_cat(vorbis_comment *dst, vorbis_comment *src) {
  int i;
  
  if (dst == NULL)
    return vorbis_comment_dup(src);
  if (src == NULL)
    return dst;
  
  for (i = 0; i < src->comments; i++)
    vorbis_comment_add(dst, src->user_comments[i]);
  return dst;
}

int comments_to_buffer(vorbis_comment *vc, char *tempbuf, int len) {
  int vclen, i, pos;
  
  // PACKET_TYPE_COMMENT, "vorbis", vendor_field_len, strlen(vendor),
  // comments
  vclen = 1 + 6 + 4 + strlen(vc->vendor) + 4;
  for (i = 0; i < vc->comments; i++)
    vclen += 4 + strlen(vc->user_comments[i]);
  vclen++;
  if (vclen > len)
    return -1 * vclen;
  
  *((unsigned char *)tempbuf) = PACKET_TYPE_COMMENT;
  strcpy(&tempbuf[1], "vorbis");
  pos = 7;
  put_uint32(&tempbuf[pos], strlen(vc->vendor));
  pos += 4;
  strcpy(&tempbuf[pos], vc->vendor);
  pos += strlen(vc->vendor);
  put_uint32(&tempbuf[pos], vc->comments);
  pos += 4;
  for (i = 0; i < vc->comments; i++) {
    put_uint32(&tempbuf[pos], strlen(vc->user_comments[i]));
    pos += 4;
    strcpy(&tempbuf[pos], vc->user_comments[i]);
    pos += strlen(vc->user_comments[i]);
  }
  *((unsigned char *)&tempbuf[pos]) = 1;
  
  return vclen;
}

vorbis_comment *generate_vorbis_comment(char **s) {
  vorbis_comment *vc;
  int nc, i;
  
  vc = (vorbis_comment *)mmalloc(sizeof(vorbis_comment));
  vc->vendor = strdup(VERSIONINFO);
  
  if ((s == NULL) || (s[0] == NULL)) {
    vc->user_comments = (char **)mmalloc(sizeof(char *));
    vc->comment_lengths = (int *)mmalloc(sizeof(int));
    vc->comments = 0;
  } else {
    for (nc = 0; s[nc] != NULL; nc++)
      ;
    vc->comment_lengths = (int *)mmalloc(sizeof(int) * (nc + 1));
    vc->user_comments = (char **)mmalloc(sizeof(char *) * (nc + 1));
    for (i = 0; i < nc; i++) {
      vc->comment_lengths[i] = strlen(s[i]);
      vc->user_comments[i] = strdup(s[i]);
      if (vc->user_comments[i] == NULL)
        die("strdup");
    }
    vc->comments = nc;
  }
  
  return vc;
}
