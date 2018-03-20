dnl PATH_AVILIB([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for avilib, and define AVILIB_CFLAGS and AVILIB_LIBS
dnl
AC_DEFUN(PATH_AVILIB,
[dnl 
dnl Get the cflags and libraries
dnl

AVILIB_CFLAGS="-Iavilib -D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64"
AVILIB_CXXFLAGS="-Iavilib -D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64"
AVILIB_LIBS="-Lavilib -lavi"

  AC_SUBST(AVILIB_CFLAGS)
  AC_SUBST(AVILIB_CXXFLAGS)
  AC_SUBST(AVILIB_LIBS)
])


# Configure paths for libogg
# Jack Moffitt <jack@icecast.org> 10-21-2000
# Shamelessly stolen from Owen Taylor and Manish Singh

dnl XIPH_PATH_OGG([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libogg, and define OGG_CFLAGS and OGG_LIBS
dnl
AC_DEFUN(XIPH_PATH_OGG,
[dnl 
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(ogg-prefix,[  --with-ogg-prefix=PFX      Prefix where libogg is installed (optional)], ogg_prefix="$withval", ogg_prefix="")
AC_ARG_ENABLE(oggtest, [  --disable-oggtest          Do not try to compile and run a test Ogg program],, enable_oggtest=yes)

  if test "x$ogg_prefix" != "x"; then
    ogg_args="$ogg_args --prefix=$ogg_prefix"
    OGG_CFLAGS="-I$ogg_prefix/include"
    OGG_LIBS="-L$ogg_prefix/lib"
  elif test "x$prefix" != "xNONE"; then
    ogg_args="$ogg_args --prefix=$prefix"
    OGG_CFLAGS="-I$prefix/include"
    OGG_LIBS="-L$prefix/lib"
  fi

  OGG_LIBS="$OGG_LIBS -logg"

  AC_MSG_CHECKING(for Ogg)
  no_ogg=""


  if test "x$enable_oggtest" = "xyes" ; then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $OGG_CFLAGS"
    LIBS="$LIBS $OGG_LIBS"
dnl
dnl Now check if the installed Ogg is sufficiently new.
dnl
      rm -f conf.oggtest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ogg/ogg.h>

int main ()
{
  system("touch conf.oggtest");
  return 0;
}

],, no_ogg=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
  fi

  if test "x$no_ogg" = "x" ; then
     AC_MSG_RESULT(yes)
     ifelse([$1], , :, [$1])     
  else
     AC_MSG_RESULT(no)
     if test -f conf.oggtest ; then
       :
     else
       echo "*** Could not run Ogg test program, checking why..."
       CFLAGS="$CFLAGS $OGG_CFLAGS"
       LIBS="$LIBS $OGG_LIBS"
       AC_TRY_LINK([
#include <stdio.h>
#include <ogg/ogg.h>
],     [ return 0; ],
       [ echo "*** The test program compiled, but did not run. This usually means"
       echo "*** that the run-time linker is not finding Ogg or finding the wrong"
       echo "*** version of Ogg. If it is not finding Ogg, you'll need to set your"
       echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
       echo "*** to the installed location  Also, make sure you have run ldconfig if that"
       echo "*** is required on your system"
       echo "***"
       echo "*** If you have an old version installed, it is best to remove it, although"
       echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
       [ echo "*** The test program failed to compile or link. See the file config.log for the"
       echo "*** exact error that occured. This usually means Ogg was incorrectly installed"
       echo "*** or that you have moved Ogg since it was installed. In the latter case, you"
       echo "*** may want to edit the ogg-config script: $OGG_CONFIG" ])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
     OGG_CFLAGS=""
     OGG_LIBS=""
     ifelse([$2], , :, [$2])
  fi
  AC_SUBST(OGG_CFLAGS)
  AC_SUBST(OGG_LIBS)
  rm -f conf.oggtest
])
# Configure paths for libvorbis
# Jack Moffitt <jack@icecast.org> 10-21-2000
# Shamelessly stolen from Owen Taylor and Manish Singh

dnl XIPH_PATH_VORBIS([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libvorbis, and define VORBIS_CFLAGS and VORBIS_LIBS
dnl
AC_DEFUN(XIPH_PATH_VORBIS,
[dnl 
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(vorbis-prefix,[  --with-vorbis-prefix=PFX   Prefix where libvorbis is installed (optional)], vorbis_prefix="$withval", vorbis_prefix="")
AC_ARG_ENABLE(vorbistest, [  --disable-vorbistest       Do not try to compile and run a test Vorbis program],, enable_vorbistest=yes)

  if test "x$vorbis_prefix" != "x" ; then
    vorbis_args="$vorbis_args --prefix=$vorbis_prefix"
    VORBIS_CFLAGS="-I$vorbis_prefix/include"
    VORBIS_LIBDIR="-L$vorbis_prefix/lib"
  elif test "x$prefix" != "xNONE"; then
    vorbis_args="$vorbis_args --prefix=$prefix"
    VORBIS_CFLAGS="-I$prefix/include"
    VORBIS_LIBDIR="-L$prefix/lib"
  fi

  VORBIS_LIBS="$VORBIS_LIBDIR -lvorbis -lm"
  VORBISFILE_LIBS="-lvorbisfile"
  VORBISENC_LIBS="-lvorbisenc"

  AC_MSG_CHECKING(for Vorbis)
  no_vorbis=""


  if test "x$enable_vorbistest" = "xyes" ; then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $VORBIS_CFLAGS"
    LIBS="$LIBS $VORBIS_LIBS $OGG_LIBS"
dnl
dnl Now check if the installed Vorbis is sufficiently new.
dnl
      rm -f conf.vorbistest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vorbis/codec.h>

int main ()
{
  system("touch conf.vorbistest");
  return 0;
}

],, no_vorbis=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
  fi

  if test "x$no_vorbis" = "x" ; then
     AC_MSG_RESULT(yes)
     ifelse([$1], , :, [$1])     
  else
     AC_MSG_RESULT(no)
     if test -f conf.vorbistest ; then
       :
     else
       echo "*** Could not run Vorbis test program, checking why..."
       CFLAGS="$CFLAGS $VORBIS_CFLAGS"
       LIBS="$LIBS $VORBIS_LIBS $OGG_LIBS"
       AC_TRY_LINK([
#include <stdio.h>
#include <vorbis/codec.h>
],     [ return 0; ],
       [ echo "*** The test program compiled, but did not run. This usually means"
       echo "*** that the run-time linker is not finding Vorbis or finding the wrong"
       echo "*** version of Vorbis. If it is not finding Vorbis, you'll need to set your"
       echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
       echo "*** to the installed location  Also, make sure you have run ldconfig if that"
       echo "*** is required on your system"
       echo "***"
       echo "*** If you have an old version installed, it is best to remove it, although"
       echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
       [ echo "*** The test program failed to compile or link. See the file config.log for the"
       echo "*** exact error that occured. This usually means Vorbis was incorrectly installed"
       echo "*** or that you have moved Vorbis since it was installed." ])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
     VORBIS_CFLAGS=""
     VORBIS_LIBS=""
     VORBISFILE_LIBS=""
     VORBISENC_LIBS=""
     ifelse([$2], , :, [$2])
  fi
  AC_SUBST(VORBIS_CFLAGS)
  AC_SUBST(VORBIS_LIBS)
  AC_SUBST(VORBISFILE_LIBS)
  AC_SUBST(VORBISENC_LIBS)
  rm -f conf.vorbistest
])

dnl AC_TRY_CFLAGS (CFLAGS, [ACTION-IF-WORKS], [ACTION-IF-FAILS])
dnl check if $CC supports a given set of cflags
AC_DEFUN([AC_TRY_CFLAGS],
    [AC_MSG_CHECKING([if $CC supports $1 flags])
    SAVE_CFLAGS="$CFLAGS"
    CFLAGS="$1"
    AC_TRY_COMPILE([],[],[ac_cv_try_cflags_ok=yes],[ac_cv_try_cflags_ok=no])
    CFLAGS="$SAVE_CFLAGS"
    AC_MSG_RESULT([$ac_cv_try_cflags_ok])
    if test x"$ac_cv_try_cflags_ok" = x"yes"; then
        ifelse([$2],[],[:],[$2])
    else
        ifelse([$3],[],[:],[$3])
    fi])

AC_DEFUN(PATH_DEBUG,[
AC_ARG_ENABLE([debug],
    [  --enable-debug             compile with debug information])
if test x"$enable_debug" = x"yes"; then
    dnl debug information
    DEBUG_CFLAGS="-g -DDEBUG"
else
    DEBUG_CFLAGS=""
fi
  AC_SUBST(DEBUG_CFLAGS)
])

AC_DEFUN(PATH_PROFILING,[
AC_ARG_ENABLE([profiling],
    [  --enable-profiling         compile with profiling information])
if test x"$enable_profiling" = x"yes"; then
    dnl profiling information
    PROFILING_CFLAGS="-pg"
    PROFILING_LIBS="-lc_p"
else
    PROFILING_CFLAGS=""
    PROFILING_LIBS=""
fi
  AC_SUBST(PROFILING_CFLAGS)
  AC_SUBST(PROFILING_LIBS)
])

dnl AM_PATH_LIBDVDREAD([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for LIBDVDREAD, and define LIBDVDREAD_CFLAGS and LIBDVDREAD_LIBS
dnl

AC_DEFUN(AM_PATH_LIBDVDREAD,
[

AC_ARG_WITH(dvdread,[  --with-dvdread               use installed libdvdread library (default=yes)],[case "${withval}" in
  yes) with_dvdread=yes;;
  no) with_dvdread=no ;;
  *) AC_MSG_ERROR(bad value ${withval} for --with-dvdread) ;;
esac], with_dvdread=yes)

AC_ARG_WITH(dvdread-includes,[  --with-dvdread-includes=PFX  prefix where local dvdread includes are installed (optional)],
          dvdread_includes="$withval",dvdread_includes="")

AC_ARG_WITH(dvdread-libs,[  --with-dvdread-libs=PFX      prefix where local dvdread lib is installed (optional)],
          dvdread_libs="$withval", dvdread_libs="")

DVDREAD_LIBS=""
DVDREAD_CFLAGS=""

have_dvdread=no

if test x$with_dvdread = "x"yes ; then

        if test x$dvdread_includes != "x" ; then
            with_dvdread_i="$dvdread_includes/include"
        else
            with_dvdread_i="/usr/include"
        fi

        if test x$dvdread_libs != x ; then
            with_dvdread_l="$dvdread_libs/lib"  
        else
            with_dvdread_l="/usr/lib"
        fi
        
        AC_CHECK_LIB(dvdread, DVDOpen,
        [DVDREAD_CFLAGS="-I$with_dvdread_i -I/usr/local/include" 
         DVDREAD_LIBS="-L$with_dvdread_l -ldvdread -lm"
        AC_DEFINE(HAVE_LIBDVDREAD) 
        have_dvdread=yes], have_dvdread=no, 
        -L$with_dvdread_l -ldvdread -lm)

AC_CHECK_FILE($with_dvdread_i/dvdread/dvd_reader.h, [AC_DEFINE(HAVE_LIBDVDREAD_INC) dvdread_inc=yes])
if test x"$dvdread_inc" != xyes; then 
AC_CHECK_FILE(/usr/local/include/dvdread/dvd_reader.h, [AC_DEFINE(HAVE_LIBDVDREAD_INC) dvdread_inc=yes])
fi

if test x"$have_dvdread" != "xyes"; then
        
        dnl use included lib

        with_dvdread_i="../dvdread"             
        with_dvdread_l="../dvdread"     
     
        AC_CHECK_FILE(./dvdread/dvd_reader.h, 
        [AC_DEFINE(HAVE_LIBDVDREAD)
        have_dvdread=yes
        DVDREAD_CFLAGS="-I$with_dvdread_i" 
        DVDREAD_LIBS="-L$with_dvdread_l -ldvdread_tc"], have_dvdread=no)
fi

if test x"$have_dvdread" != "xyes"; then

	echo '***********************************'
	echo 'libdvdread not found. dvdxchap will'
	echo 'not be compiled.'
	echo '***********************************'

fi

fi

AC_SUBST(DVDREAD_CFLAGS)
AC_SUBST(DVDREAD_LIBS)
])
