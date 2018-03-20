## process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = dist-zip foreign

SUBDIRS = avilib

if HAVE_LIBDVDREAD
DVDXCHAP=dvdxchap
DVDXCHAP_MAN=dvdxchap.1
INCLUDES=$(DVDREAD_CFLAGS)
endif

bin_PROGRAMS = ogmmerge ogmdemux ogminfo ogmsplit ogmcat $(DVDXCHAP)

ogmmerge_SOURCES = ogmmerge.cpp ogmmerge.h ogmstreams.h \
			r_avi.cpp r_avi.h \
			r_ogm.cpp r_ogm.h \
			r_ac3.cpp r_ac3.h \
			r_mp3.cpp r_mp3.h \
			r_wav.cpp r_wav.h \
			r_microdvd.cpp r_microdvd.h \
			r_srt.cpp r_srt.h \
			r_vobsub.cpp r_vobsub.h \
			p_video.cpp p_video.h \
			p_index.cpp p_index.h \
			p_ac3.cpp p_ac3.h \
			p_mp3.cpp p_mp3.h \
			p_pcm.cpp p_pcm.h \
			p_vorbis.cpp p_vorbis.h \
			p_textsubs.cpp p_textsubs.h \
			p_vobsub.cpp p_vobsub.h \
			ac3_common.c ac3_common.h \
			mp3_common.c mp3_common.h \
			subtitles.cpp subtitles.h \
			queue.cpp queue.h \
			vorbis_header_utils.c vorbis_header_utils.h \
			generic.cpp generic.h \
			common.c common.h


ogmmerge_LDADD = @OGG_LIBS@ @VORBIS_LIBS@ @VORBISENC_LIBS@ @AVILIB_LIBS@ \
		@PROFILING_LIBS@

ogmdemux_SOURCES = ogmdemux.c ogmstreams.h \
			vorbis_header_utils.c vorbis_header_utils.h \
			common.c common.h
ogmdemux_LDADD = @OGG_LIBS@ @VORBIS_LIBS@ @AVILIB_LIBS@ @PROFILING_LIBS@

ogminfo_SOURCES = ogminfo.c ogmstreams.h \
			vorbis_header_utils.c vorbis_header_utils.h \
			common.c common.h
ogminfo_LDADD = @OGG_LIBS@ @VORBIS_LIBS@ @PROFILING_LIBS@

ogmsplit_SOURCES = ogmsplit.cpp \
			queue.cpp queue.h \
			ogmmerge.h ogmstreams.h \
			generic.cpp generic.h \
			vorbis_header_utils.c vorbis_header_utils.h \
			common.c common.h
ogmsplit_LDADD = @OGG_LIBS@ @VORBIS_LIBS@ @PROFILING_LIBS@ 

ogmcat_SOURCES = ogmcat.cpp \
			queue.cpp queue.h \
			ogmmerge.h ogmstreams.h \
			generic.cpp generic.h \
			vorbis_header_utils.c vorbis_header_utils.h \
			common.c common.h
ogmcat_LDADD = @OGG_LIBS@ @VORBIS_LIBS@ @PROFILING_LIBS@

dvdxchap_SOURCES = dvdxchap.c
dvdxchap_LDADD = @DVDREAD_LIBS@

man_MANS=ogmsplit.1 ogminfo.1 ogmmerge.1 ogmdemux.1 ogmcat.1 $(DVDXCHAP_MAN)

EXTRA_DIST = $(man_MANS)
