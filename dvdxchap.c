/**
 * dvdxchap -- utility for extracting chapter start times from IFO files
 *
 * Maintained by Andrew Udvare <audvare@gmail.com>
 * Written by Moritz Bunkus <moritz@bunkus.org>
 *
 * Distributed under the GPL.
 * See the file COPYING.md for details or visit
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_print.h>
#include <dvdread/ifo_read.h>

static void usage() {
    fprintf(stdout,
            "dvdxchap v" VERSION "\n"
            "Usage: dvdxchap [options] DVD-SOURCE\n\n"
            " options:\n"
            "   -t, --title num           Use title 'num'. Default is 1.\n"
            "   -c, --chapter start[-end] Chapter to start at (to end at). "
            "Default 1.\n"
            "   -v, --verbose             Increase verbosity\n"
            "   -V, --version             Show version information\n"
            "   -h, --help                Show this help\n");
}

static int display_chapters(
    char *source, long int title, int start, int end, int verbose) {
    dvd_reader_t *dvd;
    ifo_handle_t *vmg;
    ifo_handle_t *vts;
    tt_srpt_t *tt_srpt;
    int ttn, pgn, pgc_id, start_cell, end_cell, i, j;
    vts_ptt_srpt_t *vts_ptt_srpt;
    pgc_t *cur_pgc;
    dvd_time_t *dt;
    double fps, ms, cur_time, overall_time, start_time, hour, minute, second;

    dvd = DVDOpen(source);
    if (dvd == NULL) {
        fprintf(
            stderr, "(%s) Could not open source '%s'.\n", __FILE__, source);
        return 1;
    }

    vmg = ifoOpen(dvd, 0);
    if (vmg == NULL) {
        fprintf(stderr, "(%s) Can't open VMG info.\n", __FILE__);
        DVDClose(dvd);
        return 1;
    }

    tt_srpt = vmg->tt_srpt;
    if (verbose) {
        fprintf(stderr,
                "(%s) This DVD contains %d titles.\n",
                __FILE__,
                tt_srpt->nr_of_srpts);
    }
    if (title > tt_srpt->nr_of_srpts) {
        fprintf(stderr,
                "(%s) The DVD only contains %d titles.\n",
                __FILE__,
                tt_srpt->nr_of_srpts);
        ifoClose(vmg);
        DVDClose(dvd);
        return 1;
    }
    title--;
    if (verbose) {
        fprintf(stderr,
                "(%s) Title %ld contains %d chapters.\n",
                __FILE__,
                title + 1,
                tt_srpt->title[title].nr_of_ptts);
    }

    vts = ifoOpen(dvd, tt_srpt->title[title].title_set_nr);
    if (vts == NULL) {
        fprintf(stderr, "(%s) Can't open VTS info.\n", __FILE__);
        ifoClose(vmg);
        DVDClose(dvd);
        return 1;
    }

    if (end > 0) {
        if ((end <= start) || (end > tt_srpt->title[title].nr_of_ptts)) {
            fprintf(stderr, "(%s) Invalid end chapter.\n", __FILE__);
            ifoClose(vmg);
            DVDClose(dvd);
            return 1;
        }
    }

    const int orig_rounding_mode = fegetround();
    fesetround(FE_DOWNWARD);

    ttn = tt_srpt->title[title].vts_ttn;
    vts_ptt_srpt = vts->vts_ptt_srpt;
    start_time = overall_time = 0;
    for (i = 0; i < tt_srpt->title[title].nr_of_ptts - 1; i++) {
        pgc_id = vts_ptt_srpt->title[ttn - 1].ptt[i].pgcn;
        pgn = vts_ptt_srpt->title[ttn - 1].ptt[i].pgn;
        cur_pgc = vts->vts_pgcit->pgci_srp[pgc_id - 1].pgc;
        start_cell = cur_pgc->program_map[pgn - 1] - 1;
        pgc_id = vts_ptt_srpt->title[ttn - 1].ptt[i + 1].pgcn;
        pgn = vts_ptt_srpt->title[ttn - 1].ptt[i + 1].pgn;
        cur_pgc = vts->vts_pgcit->pgci_srp[pgc_id - 1].pgc;
        end_cell = cur_pgc->program_map[pgn - 1] - 2;
        cur_time = 0;
        for (j = start_cell; j <= end_cell; j++) {
            dt = &cur_pgc->cell_playback[j].playback_time;
            hour = ((dt->hour & 0xf0) >> 4) * 10 + (dt->hour & 0x0f);
            minute = ((dt->minute & 0xf0) >> 4) * 10 + (dt->minute & 0x0f);
            second = ((dt->second & 0xf0) >> 4) * 10 + (dt->second & 0x0f);
            if (((dt->frame_u & 0xc0) >> 6) == 1) {
                fps = 25.0;
            } else {
                fps = 29.97;
            }
            dt->frame_u &= 0x3f;
            dt->frame_u = (uint8_t)((((dt->frame_u & 0xf0) >> 4) * 10) +
                                    (dt->frame_u & 0x0f));
            ms = dt->frame_u * 1000.0 / fps;
            cur_time += hour * 60 * 60 * 1000 + minute * 60 * 1000 +
                        second * 1000 + ms;
        }
        if (start == i) {
            start_time = overall_time;
        }
        if (i >= start && (i < end || end <= 0)) {
            const double diff = overall_time - start_time;
            fprintf(stdout,
                    "CHAPTER%02d=%02.0f:%02.0f:%02.0f.%03.0f\n",
                    i + 1 - start,
                    fabs(diff / 60 / 60 / 1000),
                    fabs(fmod(diff / 60 / 1000, 60)),
                    fabs(fmod(diff / 1000, 60)),
                    fabs(floor(1000 * fmod(diff, 1000) / 1000)));
            fprintf(stdout,
                    "CHAPTER%02dNAME=Chapter %02d\n",
                    i + 1 - start,
                    i + 1 - start);
        }
        overall_time += cur_time;
    }
    if (end <= 0 || i == end) {
        const double diff = overall_time - start_time;
        fprintf(
            stdout,
            "CHAPTER%02d=%02.0f:%02.0f:%02.0f.%03.0f\n",
            i + 1 - start,
            fabs(diff / 60 / 60 / 1000),
            fabs(fmod(diff / 60 / 1000, 60)),
            fabs(fmod(diff / 1000, 60)),
            fabs(floor(1000 * fmod(overall_time - start_time, 1000)) / 1000));
        fprintf(stdout,
                "CHAPTER%02dNAME=Chapter %02d\n",
                i + 1 - start,
                i + 1 - start);
    }

    ifoClose(vts);
    ifoClose(vmg);
    DVDClose(dvd);
    fesetround(orig_rounding_mode);

    return 0;
}

int main(int argc, char *argv[]) {
    long int title = 1;
    int start = 0, end = 0;
    int i;
    char *source = NULL;
    int verbose = 0;

    if (argc == 1) {
        usage();
        return 0;
    }
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage();
            return 0;
        } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
            verbose++;
        else if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version")) {
            fprintf(stdout, "dvdxchap v" VERSION "\n");
            return 0;
        } else if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--title")) {
            if ((i + 1) >= argc) {
                fprintf(stderr,
                        "(%s) Error: -t lacks a title number.\n",
                        __FILE__);
                return 1;
            }
            title = strtol(argv[i + 1], NULL, 10);
            if ((errno == ERANGE) || (errno == EINVAL) || (title < 1)) {
                fprintf(stderr,
                        "(%s) Error: '%s' is not a valid title number.\n",
                        __FILE__,
                        argv[i + 1]);
                return 1;
            }
            i++;
        } else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--chapter")) {
            if ((i + 1) >= argc) {
                fprintf(stderr,
                        "(%s) Error: -c lacks a chapter number.\n",
                        __FILE__);
                return 1;
            }
            if (sscanf(argv[i + 1], "%d-%d", &start, &end) < 1) {
                fprintf(stderr,
                        "(%s) Error: '%s' is not a valid chapter range.\n",
                        __FILE__,
                        argv[i + 1]);
                return 1;
            }
            if (start < 0) {
                end = -start;
                start = 0;
            }
            if ((start > end) && (end > 0)) {
                int tmp;
                tmp = start;
                start = end;
                end = tmp;
            }
            if (start > 0) {
                start--;
            }
            i++;
        } else {
            if (source != NULL) {
                fprintf(stderr,
                        "(%s) Error: more than one source given.\n",
                        __FILE__);
                return 1;
            }
            source = argv[i];
        }
    }
    if (source == NULL) {
        fprintf(stderr, "(%s) Error: No source given.\n", __FILE__);
        return 1;
    }

    return display_chapters(source, title, start, end, verbose);
}
