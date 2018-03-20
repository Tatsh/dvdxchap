#ifndef __VORBIS_HEADER_UTILS_H
#define __VORBIS_HEADER_UTILS_H

#include <vorbis/codec.h>

#ifdef __cplusplus
extern "C" {
#endif

char *mmalloc(int size);
int vorbis_unpack_comment(vorbis_comment *vc, char *buf, int len);
void vorbis_comment_remove_tag(vorbis_comment *vc, char *tag);
void vorbis_comment_remove_number(vorbis_comment *vc, int num);
vorbis_comment *vorbis_comment_dup(vorbis_comment *vc);
vorbis_comment *vorbis_comment_cat(vorbis_comment *dst, vorbis_comment *src);

vorbis_comment *generate_vorbis_comment(char **s);
int comments_to_buffer(vorbis_comment *vc, char *tempbuf, int len);

#ifdef __cplusplus
}
#endif

#endif
