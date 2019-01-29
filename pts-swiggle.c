/*
 * pts-swiggle: fast, command-line JPEG thumbnail generator
 * swiggle: le's simple web image gallery generator
 *
 * Copyright (c) 2003, 2004, 2006
 *  Lukas Ertl <l.ertl@univie.ac.at>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by the University of
 *  California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * $Id: swiggle.c,v 1.30 2007/01/14 12:03:36 le Exp $
 *
 */

/* TODO(pts): Remove unused command-line flags and their help. */
/* TODO(pts): Don't scale if target size is just a mit more or less than source size. (On exact match we already skip scaling.) */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef AIX
#include <strings.h>
#endif

/* This needs to be moved above #include <setjmp.h> */
#include <png.h>	/* includes zlib.h and setjmp.h */

#include <jpeglib.h>

#include <setjmp.h>

#include "cgif.h"

#define	SWIGGLE_VERSION	"0.4-pts"

static struct {
	char *progname;
	int scaleheight;
	int force;
	int bilinear;
	int recursive;
	int also_small;
	/* Bitwise or of:
	 * 1: Invalid command-line flags or arguments.
	 * 2: File not found, I/O error, runtime error or abnormal condition.
	 * 4: Data error (not I/O error) loading file as an image.
	 * 8: File specified on the command-line is not an image.
	 */
	int exit_code;
} g_flags = { "", 480, 0, 0, 0, 0, EXIT_SUCCESS /* 0 */ };

/*
 * Function declarations.
 */
static void process_dir(char *);
static int check_cache(char *, struct stat *);
static void create_thumbnail(char *);
static int sort_by_filename(const void *, const void *);
static void usage(void);
static void version(void);
static void resize_bicubic(
    unsigned num_components, unsigned output_width, unsigned output_height,
    unsigned, unsigned, const unsigned char *, unsigned char *);
static void resize_bilinear(
    unsigned num_components, unsigned output_width, unsigned output_height,
    unsigned, unsigned, const unsigned char *, unsigned char *);

static void check_alloc(const void *p) {
	if (!p) {
		const char msg[] = "pts-swiggle: Out of memory, aborting.\n";
		(void)-write(2, msg, sizeof(msg) - 1);
		abort();
	}
}

/* --- */


/*
 * swiggle generates a web image gallery. It scales down images in
 * given directories to thumbnail size and "normal view" size and
 * generates static html pages and thumbnail index pages for all images.
 */
int
main(int argc, char **argv)
{
	char *eptr;
	int i;
	struct stat sb;

	g_flags.progname = argv[0];

	while ((i = getopt(argc, argv, "c:d:h:H:r:s:floRva")) != -1) {
		switch (i) {
		case 'c':  /* cols, ignored */
			break;
		case 'd':  /* defaultdesc, ignored */
			break;
		case 'h':  /* thumbheight, ignored */
			break;
		case 'H':
			g_flags.scaleheight = (int) strtol(optarg, &eptr, 10);
			if (eptr == optarg || *eptr != '\0') {
				fprintf(stderr, "%s: invalid argument '-H "
				    "%s'\n", g_flags.progname, optarg);
				usage();
				exit(EXIT_FAILURE);  /* 1 */
			}
			break;
		case 'R':
			g_flags.recursive = 1;
			break;
		case 's':  /* Sorting, ignored. */
			break;
		case 'f':
			g_flags.force = 1;
			break;
		case 'l':
			g_flags.bilinear = 1;
			break;
		case 'o':  /* rm_orphans, ignored. */
			break;
		case 'a':
			g_flags.also_small = 1;
			break;
		case 'v':
			version();
			break;
		case '?':
			usage();
			exit(EXIT_SUCCESS);
		default:
			usage();
			exit(EXIT_FAILURE);  /* 1 */
		}
	}

	argc -= optind;
	argv += optind;
	if (argc < 1) {
		usage();
		exit(EXIT_FAILURE);  /* 1 */
	}

	/* Put the inputs to increasing order for deterministic processing. */
	qsort(argv, argc, sizeof argv[0], sort_by_filename);

	for (i = 0; i < argc; ++i) {
		if (stat(argv[i], &sb)) {
			fprintf(stderr, "%s: can't stat(%s): %s\n", g_flags.progname, argv[i],
			    strerror(errno));
			g_flags.exit_code |= 2;
			continue;
		}

		if (S_ISDIR(sb.st_mode)) {
			if (argv[i][strlen(argv[i])-1] == '/')
				argv[i][strlen(argv[i])-1] = '\0';
			process_dir(argv[i]);
		} else if (S_ISREG(sb.st_mode)) {
			create_thumbnail(argv[i]);
		} else {
			fprintf(stderr, "%s: not a file or directory: %s\n", g_flags.progname,
			    argv[i]);
			g_flags.exit_code |= 2;
		}

	}

	return g_flags.exit_code;
}

typedef enum imgfmt_t {
  IF_UNKNOWN = 0,
  IF_JPEG = 1,
  IF_PNG = 2,
  IF_GIF = 3,
} imgfmt_t;

static imgfmt_t detect_image_format(const char *header, unsigned header_size) {
	return header_size >= 4 && 0 == memcmp(header, "\xff\xd8\xff", 3) ? IF_JPEG
	     : header_size >= 24 && 0 == memcmp(header, "\211PNG\r\n\032\n", 8) ? IF_PNG
	     : header_size >= 7 && (0 == memcmp(header, "GIF87a", 6) || 0 == memcmp(header, "GIF89a", 6)) ? IF_GIF
	     : IF_UNKNOWN;
}

/*
 * Opens the directory given in parameter "dir" and reads the filenames
 * of all .jpg files, stores them in a list and initiates the creation
 * of the scaled images and html pages. Returns the number of images
 * found in this directory.
 */
static void process_dir(char *dir) {
	char **imglist;
	unsigned imgcount, imgcapacity;
	char **subdirlist;
	unsigned subdircount, subdircapacity;
	unsigned i;
	char *fn;
	unsigned dir_size;
	struct dirent *dent;
	struct stat sb;
	DIR *thisdir;
	FILE *f;
	char header[24];
	unsigned header_size;
	int stat_result;

	if ((thisdir = opendir(dir)) == NULL) {
		fprintf(stderr, "%s: can't opendir(%s): %s\n", g_flags.progname, dir,
		    strerror(errno));
		g_flags.exit_code |= 2;
		return;
	}

	dir_size = strlen(dir);
	imglist = NULL;
	imgcount = imgcapacity = 0;
	subdirlist = NULL;
	subdircount = subdircapacity = 0;
	while ((dent = readdir(thisdir)) != NULL) {
		size_t d_name_size;
		if (dent->d_name[0] == '.' &&  /* Skip "." and ".." */
		    (dent->d_name[1] == '\0' || (dent->d_name[1] == '.' && dent->d_name[2] == '\0'))) continue;
		d_name_size = strlen(dent->d_name);
		if (d_name_size >= 7 && 0 == memcmp(dent->d_name + d_name_size - 7, ".th.jpg", 7 * sizeof(char))) continue;
		check_alloc(fn = malloc(dir_size + d_name_size + 2));
		sprintf(fn, "%s/%s", dir, dent->d_name);
		stat_result = 0;
		/* TODO(pts): Don't enter to symlinks to directories. */
		if (
#ifdef DT_UNKNOWN
		    dent->d_type == DT_REG ? 0 :
		    dent->d_type == DT_DIR ? !g_flags.recursive :
		    dent->d_type != DT_UNKNOWN ? 1 :
#endif
		    ((stat_result = stat(fn, &sb)) == 0 &&
		     !S_ISREG(sb.st_mode) &&
		     (!g_flags.recursive || !S_ISDIR(sb.st_mode)))) {
			free(fn);
			continue;
		}
		if (stat_result != 0) {
			fprintf(stderr, "%s: can't stat(%s): %s\n", g_flags.progname,
			    fn, strerror(errno));
			free(fn);
			g_flags.exit_code |= 2;
			continue;
		}
		if (/* is_dir = */
#ifdef DT_UNKNOWN
		    dent->d_type == DT_DIR ? 1 :
		    dent->d_type != DT_UNKNOWN ? 0 :
#endif
		    S_ISDIR(sb.st_mode)) {
			if (subdircount == subdircapacity) {
				subdircapacity = subdircapacity < 16 ? 16 : subdircapacity << 1;
				check_alloc(subdirlist = realloc(subdirlist, subdircapacity * sizeof(*subdirlist)));
			}
			subdirlist[subdircount++] = fn;  /* Takes ownership. */
			continue;
		}
		if ((f = fopen(fn, "rb")) != NULL) {
			header_size = fread(header, sizeof(char), sizeof(header), f);
			if (ferror(f) || header_size == 0 || detect_image_format(header, header_size) == IF_UNKNOWN) {
				fclose(f);
				free(fn);
				continue;  /* Silently skip non-JPEG files when scanning recursively (-R). */
			}
			fclose(f);
		}
		if (imgcount == imgcapacity) {
			imgcapacity = imgcapacity < 16 ? 16 : imgcapacity << 1;
			check_alloc(imglist = realloc(imglist, imgcapacity * sizeof(*imglist)));
		}
		imglist[imgcount++] = fn;  /* Takes ownership. */
	}

	if (closedir(thisdir)) {
		fprintf(stderr, "%s: error on closedir(%s): %s", g_flags.progname, dir,
		    strerror(errno));
		g_flags.exit_code |= 2;
	}
	/* Sort imglist according to desired sorting function. */
	qsort(imglist, imgcount, sizeof(*imglist), sort_by_filename);
	for (i = 0; i < imgcount; ++i) {
		create_thumbnail(imglist[i]);
		free(imglist[i]);
	}
	free(imglist);
	printf("%d image%s processed in dir: %s\n", imgcount, imgcount != 1 ? "s" : "", dir);
	qsort(subdirlist, subdircount, sizeof(*subdirlist), sort_by_filename);
	for (i = 0; i < subdircount; ++i) {
		process_dir(subdirlist[i]);
		free(subdirlist[i]);
	}
	free(subdirlist);
}

struct my_jpeg_error_mgr {
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
	/* char last_msg[JMSG_LENGTH_MAX]; */
};

static void my_jpeg_error_exit(j_common_ptr cinfo) {
	/* *(cinfo->err) is now equivalent to myerr->pub */
	struct my_jpeg_error_mgr *myerr = (struct my_jpeg_error_mgr*)cinfo->err;
	(*(cinfo->err->output_message))(cinfo);  /* Prints as libjpeg would print to stderr. */
	/* fprintf(stderr, "BYE!\n"); */
	/* exit(EXIT_FAILURE); */
	/* (*(cinfo->err->format_message))(cinfo, last_msg); */
	longjmp(myerr->setjmp_buffer, 1); /* Jump to the setjmp point */
}

struct image {
  unsigned num_components;
  unsigned width;
  unsigned height;
  unsigned output_width;
  unsigned output_height;
  J_COLOR_SPACE colorspace;

  unsigned scalewidth;
  unsigned scaleheight;
  unsigned char *data;
  FILE *outfile;
};

/* Returns whether the scaled image file should be produced. */
static char compute_scaledims(struct image *img, char is_input_jpeg) {
	/* ratio needed to scale image correctly. */
	double ratio = (double)img->width / (double)img->height;
	img->scaleheight = g_flags.scaleheight;
	img->scalewidth = (int)((double)img->scaleheight * ratio + 0.5);
	/* TODO(pts): Fix too large width. */
	/* Is the image smaller than the thumbnail? */
	if (img->scaleheight >= img->height && img->scalewidth >= img->width) {
		if (!is_input_jpeg || g_flags.also_small) {
			/* Re-encode to JPEG in original size. */
			img->scaleheight = img->height;
			img->scalewidth = img->width;
		} else {
			/* Skip creating the thumbnail. */
			return 0;
		}
	}
	return 1;
}

/* Called by load_image.
 * Returns whether the scaled image file should be produced.
 */
static char load_image_gif(struct image *img, const char *filename, FILE *infile, const char *tmp_filename) {
	char const *err;
	GifFileType *giff;
	SavedImage *sp;
	ColorMapObject *cm;
	GifColorType *co;
	unsigned c;
	unsigned char *pr;
	const unsigned char *pi, *pi_end;

	if (0==(giff=DGifOpenFILE(infile)) || GIF_ERROR==DGifSlurp(giff, 1 /* do_decode_first_image_only */)) {
		fprintf(stderr, "%s: error reading GIF file: %s: %s\n", g_flags.progname, filename, ((err=GetGifError()) ? err : "unknown error"));
		g_flags.exit_code |= 4;
		if (giff) DGifCloseFile(giff);
		return 0;
	}
	if (giff->ImageCount<1) {
		fprintf(stderr, "%s: no image in GIF file: %s\n", g_flags.progname, filename);
		g_flags.exit_code |= 4;
		DGifCloseFile(giff);
		return 0;
	}
	sp = giff->SavedImages + 0;

	img->num_components = 3;
	img->width = img->output_width = sp->ImageDesc.Width;
	img->height = img->output_height = sp->ImageDesc.Height;
	img->colorspace = JCS_RGB;

	if (!compute_scaledims(img, 0)) {
		DGifCloseFile(giff);
		return 0;
	}

	if ((img->outfile = fopen(tmp_filename, "wb")) == NULL) {
		DGifCloseFile(giff);
		fprintf(stderr, "%s: can't fopen(%s): %s\n", g_flags.progname, tmp_filename, strerror(errno));
		g_flags.exit_code |= 2;
		return 0;
	}

	cm = sp->ImageDesc.ColorMap ? sp->ImageDesc.ColorMap : giff->SColorMap;
	co = cm->Colors;
	c = img->num_components * img->width * img->height;
	check_alloc(img->data = pr = malloc(c * sizeof(unsigned char)));
	pi = (const unsigned char*)sp->RasterBits;
	pi_end = pi + img->width * img->height;
	while (pi != pi_end) {
		GifColorType *coi = co + *pi++;
		/* We could check if the color value is smaller than cm->ColorCount. */
		*pr++ = coi->Red;
		*pr++ = coi->Green;
		*pr++ = coi->Blue;
	}
	/* sp->transp is the transparency color index: -1 or 0..255. We ignore it now. */
	DGifCloseFile(giff);  /* Also frees memory. */
	return 1;
}

/* --- PNG support */

/*
** based on png22pnm.c
** edited by pts@fazekas.hu at Tue Dec 10 16:33:53 CET 2002
**
** png22pnm.c has been tested with libpng 1.2 and 1.5. It should work with
** libpng 1.2 or later. Compatiblity with libpng versions earlier than 1.2 is
** not a goal.
**
** based on pngtopnm.c -
** read a Portable Network Graphics file and produce a portable anymap
**
** Copyright (C) 1995,1998 by Alexander Lehmann <alex@hal.rhein-main.de>
**                        and Willem van Schaik <willem@schaik.com>
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** modeled after giftopnm by David Koblas and
** with lots of bits pasted from libpng.txt by Guy Eric Schalnat
*/

/*
   BJH 20000408:  rename PPM_MAXMAXVAL to PPM_OVERALLMAXVAL
   BJH 20000303:  fix include statement so dependencies work out right.
*/
/* GRR 19991203:  moved VERSION to new version.h header file */

/* GRR 19990713:  fixed redundant freeing of png_ptr and info_ptr in setjmp()
 *  blocks and added "pm_close(ifp)" in each.  */

/* GRR 19990317:  declared "clobberable" automatic variables in convertpng()
 *  static to fix Solaris/gcc stack-corruption bug.  Also installed custom
 *  error-handler to avoid jmp_buf size-related problems (i.e., jmp_buf
 *  compiled with one size in libpng and another size here).  */

#include <math.h>
#include <png.h> /* includes zlib.h and setjmp.h */

/* GRR 19991205:  this is used as a test for pre-1999 versions of netpbm and
 *   pbmplus vs. 1999 or later (in which pm_close was split into two)
 */

struct swigpng_jmpbuf_wrapper {
  jmp_buf jmpbuf;
  const char *filename;
};

/* TODO(pts): Speed this up for non-16-bit PNGs. */
static png_uint_16 swigpng_get_png_val(png_byte **pp, int bit_depth) {
  const png_uint_16 c = (bit_depth == 16) ? (*((*pp)++)) << 8 : 0;
  return c | (*((*pp)++));
}

static png_uint_16 swigpng_gamma_correct(png_uint_16 v, float gamma, float maxval) {
  if (gamma != -1.0)
    return (png_uint_16) (pow ((double) v / maxval, (1.0 / gamma)) * maxval + 0.5);
  else
    return v;
}

static void swigpng_error_handler(png_structp png_ptr, png_const_charp msg) {
  struct swigpng_jmpbuf_wrapper *jmpbuf_ptr = png_get_error_ptr(png_ptr);
  if (jmpbuf_ptr == NULL) abort();  /* we are completely hosed now */

  /* this function, aside from the extra step of retrieving the "error
   * pointer" (below) and the fact that it exists within the application
   * rather than within libpng, is essentially identical to libpng's
   * default error handler.  The second point is critical:  since both
   * setjmp() and longjmp() are called from the same code, they are
   * guaranteed to have compatible notions of how big a jmp_buf is,
   * regardless of whether _BSD_SOURCE or anything else has (or has not)
   * been defined. */

  fprintf(stderr, "%s: error reading PNG file: %s: %s\n", g_flags.progname, jmpbuf_ptr->filename, msg);
  longjmp(jmpbuf_ptr->jmpbuf, 1);
}

static char load_image_png(struct image *img, const char *filename, FILE *infile, const char *tmp_filename) {
  struct swigpng_jmpbuf_wrapper swigpng_jmpbuf_struct;
  unsigned char sig_buf[4];
  /* Without volatile, `gcc -O3' optimizes away some memory accesses. */
  png_struct * png_ptr;
  png_info * info_ptr;
  /* Without volatile, `gcc -O3' optimizes away some memory accesses. */
  png_byte ** volatile png_image;
  /* Without volatile, `gcc -O3' optimizes away some memory accesses. */
  unsigned char * volatile img_data;
  png_byte *png_pixel;
  png_uint_32 width;
  png_uint_32 height;
  int bit_depth;
  png_byte color_type;
  png_color_16p background;
  double gamma;
  png_bytep trans_alpha;
  int num_trans;
  png_color_16p trans_color;
  png_color_8p sig_bit;
  int num_palette;
  png_colorp palette;
  png_uint_32 x_pixels_per_unit, y_pixels_per_unit;
  int phys_unit_type;
  int has_phys;
  int x, y;
  int linesize;
  png_uint_16 r, g, b, a, pi;
  png_uint_16 trans_r, trans_g, trans_b, trans_gray;
  int i;
  int trans_mix;
  unsigned char *pr;
  png_uint_16 maxval;
  volatile png_uint_16 bgr, bgg, bgb;  /* Background colors. */
  float displaygamma;
  float totalgamma;
  char do_mix;
  (void)filename;

  do_mix = 0;  /* TODO(pts): Remove support for this. */
  displaygamma = -1.0; /* display gamma */
  totalgamma = -1.0;
  bgr = bgg = bgb = 0;  /* Black. qiv does #6f726d. */
  do_mix = 1;
  /* This (alpha channel mixing) works with both RGBA and palette. */
  /* displaygamma = ... */
  png_image = NULL;
  img_data = NULL;

  if (fread(sig_buf, 1, sizeof(sig_buf), infile) != sizeof(sig_buf)) {
    fprintf(stderr, "%s: not a PNG file (empty or too short): %s\n", g_flags.progname, filename);
    g_flags.exit_code |= 4;
    return 0;
  }
  if (png_sig_cmp(sig_buf, (png_size_t) 0, (png_size_t) sizeof(sig_buf)) != 0) {
    fprintf(stderr, "%s: not a PNG file (bad signature): %s\n", g_flags.progname, filename);
    g_flags.exit_code |= 4;
    return 0;
  }

  check_alloc(png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING,
    &swigpng_jmpbuf_struct, swigpng_error_handler, NULL));
  swigpng_jmpbuf_struct.filename = filename;

  check_alloc(info_ptr = png_create_info_struct (png_ptr));

  if (setjmp(swigpng_jmpbuf_struct.jmpbuf)) {
    /* We reach this if there was an error decoding the PNG file, already printed by swigpng_error_handler. */
    png_destroy_read_struct (&png_ptr, &info_ptr, (png_infopp)NULL);
    if (png_image) free(png_image[0]);
    free(png_image);
    free(img_data); img->data = NULL;  /* This shouldn't be needed. */
    g_flags.exit_code |= 4;
    return 0;
  }

  png_init_io (png_ptr, infile);
  png_set_sig_bytes (png_ptr, sizeof(sig_buf));
  png_read_info (png_ptr, info_ptr);

  bit_depth = png_get_bit_depth(png_ptr, info_ptr);
  color_type = png_get_color_type(png_ptr, info_ptr);
  width = png_get_image_width(png_ptr, info_ptr);
  height = png_get_image_height(png_ptr, info_ptr);
  maxval = (color_type == PNG_COLOR_TYPE_PALETTE) ? 255 : (1l << bit_depth) - 1;

  img->num_components = 3;
  img->width = img->output_width = width;
  img->height = img->output_height = height;
  img->colorspace = JCS_RGB;

  if (!compute_scaledims(img, 0)) {
    png_destroy_read_struct (&png_ptr, &info_ptr, (png_infopp)NULL);
    return 0;
  }

  if ((img->outfile = fopen(tmp_filename, "wb")) == NULL) {
    png_destroy_read_struct (&png_ptr, &info_ptr, (png_infopp)NULL);
    fprintf(stderr, "%s: can't fopen(%s): %s\n", g_flags.progname, tmp_filename, strerror(errno));
    g_flags.exit_code |= 2;
    return 0;
  }

  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_bKGD)) {
    png_get_bKGD(png_ptr, info_ptr, &background);
  } else {
    background = NULL;
  }
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_gAMA)) {
    png_get_gAMA(png_ptr, info_ptr, &gamma);
  } else {
    gamma = -1;
  }
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
    png_get_tRNS(png_ptr, info_ptr, &trans_alpha, &num_trans, &trans_color);
    if (num_trans == 0) num_trans = -1;
  } else {
    trans_alpha = NULL;
    num_trans = 0;
    trans_color = NULL;
  }
  if (trans_color) {
    trans_r = swigpng_gamma_correct(trans_color->red, totalgamma, maxval);
    trans_g = swigpng_gamma_correct(trans_color->green, totalgamma, maxval);
    trans_b = swigpng_gamma_correct(trans_color->blue, totalgamma, maxval);
    trans_gray = swigpng_gamma_correct(trans_color->gray, totalgamma, maxval);
  } else {
    trans_r = trans_g = trans_b = trans_gray = 0;
  }

  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_PLTE)) {
    png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);
  } else {
    palette = NULL;
    num_palette = 0;
  }
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_sBIT)) {
    png_get_sBIT(png_ptr, info_ptr, &sig_bit);
  } else {
    sig_bit = NULL;
  }
  if (0 != (has_phys = png_get_valid(png_ptr, info_ptr, PNG_INFO_pHYs))) {
    png_get_pHYs(png_ptr, info_ptr, &x_pixels_per_unit, &y_pixels_per_unit,
                 &phys_unit_type);
  }

  if (height > 0) check_alloc(png_image = (png_byte **)malloc (height * sizeof (png_byte*)));

  if (bit_depth == 16)
    linesize = 2 * width;
  else
    linesize = width;

  if (color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    linesize *= 2;
  } else if (color_type == PNG_COLOR_TYPE_RGB) {
    linesize *= 3;
  } else if (color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
    linesize *= 4;
  } else if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE) {
  } else {
    png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
    fprintf(stderr, "%s: unsupported PNG color type: %s: %d\n", g_flags.progname, filename, (int)color_type);
    g_flags.exit_code |= 4;
    return 0;
  }

  if (bit_depth < 8)
    png_set_packing (png_ptr);

  /* gamma-correction */
  if (displaygamma != -1.0) {
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_gAMA)) {
      if (displaygamma != gamma) {
        png_set_gamma (png_ptr, displaygamma, gamma);
	totalgamma = (double) gamma * (double) displaygamma;
	/* in case of gamma-corrections, sBIT's as in the PNG-file are not valid anymore */
	sig_bit = NULL;
      }
    } else {
      if (displaygamma != gamma) {
	png_set_gamma (png_ptr, displaygamma, 1.0);
	totalgamma = (double) displaygamma;
	sig_bit = NULL;
      }
    }
  }

  /* sBIT handling is very tricky. If we are extracting only the image, we
     can use the sBIT info for grayscale and color images, if the three
     values agree. If we extract the transparency/alpha mask, sBIT is
     irrelevant for trans and valid for alpha. If we mix both, the
     multiplication may result in values that require the normal bit depth,
     so we will use the sBIT info only for transparency, if we know that only
     solid and fully transparent is used */

  if (sig_bit != NULL) {
    switch (do_mix) {
      case 1:
        if (color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
            color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
          break;
        if (color_type == PNG_COLOR_TYPE_PALETTE && num_trans != 0) {
          trans_mix = 1;
          for (i = 0 ; i < num_trans ; i++)
            if (trans_alpha[i] != 0 && trans_alpha[i] != 255) {
              trans_mix = 0;
              break;
            }
          if (!trans_mix)
            break;
        }

        /* else fall though to normal case */

      case 0:
        if ((color_type == PNG_COLOR_TYPE_PALETTE ||
             color_type == PNG_COLOR_TYPE_RGB ||
             color_type == PNG_COLOR_TYPE_RGB_ALPHA) &&
            (sig_bit->red != sig_bit->green ||
             sig_bit->red != sig_bit->blue) &&
            !do_mix) {
#if 0
	  pm_message ("different bit depths for color channels not supported");
	  pm_message ("writing file with %d bit resolution", bit_depth);
#endif
        } else {
          if ((color_type == PNG_COLOR_TYPE_PALETTE) &&
	      (sig_bit->red < 255)) {
	    for (i = 0 ; i < num_palette ; i++) {
	      palette[i].red   >>= (8 - sig_bit->red);
	      palette[i].green >>= (8 - sig_bit->green);
	      palette[i].blue  >>= (8 - sig_bit->blue);
	    }
	    maxval = (1l << sig_bit->red) - 1;
          } else
          if ((color_type == PNG_COLOR_TYPE_RGB ||
               color_type == PNG_COLOR_TYPE_RGB_ALPHA) &&
	      (sig_bit->red < bit_depth)) {
	    png_set_shift (png_ptr, sig_bit);
	    maxval = (1l << sig_bit->red) - 1;
          } else
          if ((color_type == PNG_COLOR_TYPE_GRAY ||
               color_type == PNG_COLOR_TYPE_GRAY_ALPHA) &&
	      (sig_bit->gray < bit_depth)) {
	    png_set_shift (png_ptr, sig_bit);
	    maxval = (1l << sig_bit->gray) - 1;
          }
        }
        break;
      }
  }

  /* didn't manage to get libpng to work (bugs?) concerning background */
  /* processing, therefore we do our own using bgr, bgg and bgb        */

  if (background != NULL)
    switch (color_type) {
      case PNG_COLOR_TYPE_GRAY:
      case PNG_COLOR_TYPE_GRAY_ALPHA:
        bgr = bgg = bgb = swigpng_gamma_correct(background->gray, totalgamma, maxval);
        break;
      case PNG_COLOR_TYPE_PALETTE:
        bgr = swigpng_gamma_correct(palette[background->index].red, totalgamma, maxval);
        bgg = swigpng_gamma_correct(palette[background->index].green, totalgamma, maxval);
        bgb = swigpng_gamma_correct(palette[background->index].blue, totalgamma, maxval);
        break;
      case PNG_COLOR_TYPE_RGB:
      case PNG_COLOR_TYPE_RGB_ALPHA:
        bgr = swigpng_gamma_correct(background->red, totalgamma, maxval);
        bgg = swigpng_gamma_correct(background->green, totalgamma, maxval);
        bgb = swigpng_gamma_correct(background->blue, totalgamma, maxval);
        break;
    }

  if (height > 0) {
    check_alloc(png_image[0] = malloc(linesize * height));
    for (y = 1 ; y+0U < height ; y++) {
      png_image[y] = png_image[0] + linesize * y;
    }
    png_read_image(png_ptr, png_image);
    png_read_end(png_ptr, info_ptr);
  }
  /* This sets png_ptr=NULL as a side effect. Also calling it twice is a no-op. */
  png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);

  /* if (has_phys & x_pixels_per_unit != y_pixels_per_unit) warning("Non-square pixels."); */

  check_alloc(img_data = img->data = pr = malloc(3 * img->width * img->height * sizeof(unsigned char)));
  /* TODO(pts): Can't libpng decode directly to RGB, with mixing and gamma correction? Look for png_destroy_read_struct on Google. */
  for (y = 0 ; y+0U < height ; y++) {
    png_pixel = png_image[y];
    for (x = 0 ; x+0U < width ; x++) {
      r = swigpng_get_png_val(&png_pixel, bit_depth);
      switch (color_type) {
        case PNG_COLOR_TYPE_GRAY:
          g = b = r;
          a = trans_color && r == trans_gray ? 0 : maxval;
          break;

        case PNG_COLOR_TYPE_GRAY_ALPHA:
          g = b = r;
          a = swigpng_get_png_val(&png_pixel, bit_depth);
          break;

        case PNG_COLOR_TYPE_PALETTE:
          pi = r;
          r = palette[pi].red;
          g = palette[pi].green;
          b = palette[pi].blue;
          a = (int)pi < num_trans ? trans_alpha[pi] : maxval;
          break;

        case PNG_COLOR_TYPE_RGB:
          g = swigpng_get_png_val(&png_pixel, bit_depth);
          b = swigpng_get_png_val(&png_pixel, bit_depth);
          a = (trans_color && r == trans_r && g == trans_g && b == trans_b) ? 0 : maxval;
          break;

        case PNG_COLOR_TYPE_RGB_ALPHA:
        default:  /* Nothing else supported. */
          g = swigpng_get_png_val(&png_pixel, bit_depth);
          b = swigpng_get_png_val(&png_pixel, bit_depth);
          a = swigpng_get_png_val(&png_pixel, bit_depth);
          break;
      }
      if (do_mix && a != maxval) {
        /* TODO(pts): Convert to maxval=255 first, and only then mix, for better rounding. */
        r = r * (double)a / maxval + ((1.0 - (double)a / maxval) * bgr);
        g = g * (double)a / maxval + ((1.0 - (double)a / maxval) * bgg);
        b = b * (double)a / maxval + ((1.0 - (double)a / maxval) * bgb);
      }
      if (maxval == 255) {
      } else if (maxval <= 1) {
        r *= 255;
        g *= 255;
        b *= 255;
      } else if (maxval == 15) {
        r *= 17;
        g *= 17;
        b *= 17;
      } else if (maxval == 3) {
        r *= 85;
        g *= 85;
        b *= 85;
      } else if (maxval == 65535) {  /* 16-bit PNG. */
        r >>= 8;
        g >>= 8;
        b >>= 8;
      } else {  /* TODO(pts): Can this happen? */
        r = (png_uint_32)r * 255 / maxval;
        g = (png_uint_32)g * 255 / maxval;
        b = (png_uint_32)b * 255 / maxval;
      }
      *pr++ = r;
      *pr++ = g;
      *pr++ = b;
    }
  }

  if (png_image) free(png_image[0]);
  free(png_image);
  return 1;
}

/* --- */

/* Called by load_image.
 * Returns whether the scaled image file should be produced.
 */
static char load_image_jpeg(struct image *img, const char *filename, FILE *infile, const char *tmp_filename) {
        struct jpeg_decompress_struct dinfo;
        struct my_jpeg_error_mgr derrmgr;
        unsigned char *pr;
        char has_decompress_started = 0;
        unsigned row_width;
        JSAMPARRAY samp;
	(void)filename;

	dinfo.err = jpeg_std_error(&derrmgr.pub);
	derrmgr.pub.error_exit = my_jpeg_error_exit;
	if (setjmp(derrmgr.setjmp_buffer)) {
		/* If we get here, the JPEG code has signaled a fatal error, which was already printed to stderr in my_jpeg_error_exit.
		 * Example non-fatal error: Premature end of JPEG file
		 * Example fatal error: Not a JPEG file: starts with 0x66 0x6f
		 * TODO(pts): We should report (with fprintf(stderr, ...)) both fatal and non-fatal errors.
		 * After a non-fatal error, the error is printed to stderr, jpeg_read_scanlines can continue and will return gray pixels.
		 */
		if (has_decompress_started) jpeg_finish_decompress(&dinfo);
		jpeg_destroy_decompress(&dinfo);
		g_flags.exit_code |= 4;
		return 0;
	}
	jpeg_create_decompress(&dinfo);
	jpeg_stdio_src(&dinfo, infile);
	(void)jpeg_read_header(&dinfo, FALSE);

	img->width = dinfo.image_width;
	img->height = dinfo.image_height;
	img->num_components = dinfo.num_components;

	if (!compute_scaledims(img, 1)) {
		jpeg_destroy_decompress(&dinfo);
		return 0;
	}

	/*
	 * If the image is not cached, we need to read it in,
	 * resize it, and write it out.
	 */
	if ((img->outfile = fopen(tmp_filename, "wb")) == NULL) {
		fprintf(stderr, "%s: can't fopen(%s): %s\n", g_flags.progname, tmp_filename, strerror(errno));
		jpeg_destroy_decompress(&dinfo);
		g_flags.exit_code |= 2;
		return 0;
	}

	/*
	 * Use libjpeg's handy feature to downscale the
	 * original on the fly while reading it in.
	 */
	if (img->width >= 8 * img->scalewidth)
		dinfo.scale_denom = 8;
	else if (img->width >= 4 * img->scalewidth)
		dinfo.scale_denom = 4;
	else if (img->width >= 2 * img->scalewidth)
		dinfo.scale_denom = 2;

	has_decompress_started = 1;
	jpeg_start_decompress(&dinfo);
	img->output_width = dinfo.output_width;
	img->output_height = dinfo.output_height;
	img->colorspace = dinfo.out_color_space;
	row_width = dinfo.output_width * img->num_components;
	check_alloc(img->data = malloc(row_width * dinfo.output_height * sizeof(unsigned char)));
	samp = (*dinfo.mem->alloc_sarray)
	    ((j_common_ptr)&dinfo, JPOOL_IMAGE, row_width, 1);

	/* Read the image into memory. */
	pr = img->data;
	while (dinfo.output_scanline < dinfo.output_height) {
		jpeg_read_scanlines(&dinfo, samp, 1);
		memcpy(pr, *samp, row_width * sizeof(char));
		pr += row_width;
	}
	jpeg_finish_decompress(&dinfo);
	jpeg_destroy_decompress(&dinfo);
	/* if (setjmp(...)) above can't happen anymore. */
	return 1;
}

/* Returns whether the scaled image file should be produced. */
static char load_image(struct image *img, const char *filename, const char *tmp_filename) {
	FILE *infile;
	char header[24];
	imgfmt_t fmt;
	unsigned header_size;
	char result;

	img->data = NULL;
	img->outfile = NULL;

	/*
	 * Open the file and get some basic image information.
	 */
	if ((infile = fopen(filename, "rb")) == NULL) {
		fprintf(stderr, "%s: can't fopen(%s): %s\n", g_flags.progname, filename, strerror(errno));
		g_flags.exit_code |= 2;
		return 0;
	}

	header_size = fread(header, sizeof(char), sizeof(header), infile);
	if (ferror(infile)) {
		fprintf(stderr, "%s: can't read headers from %s: %s\n", g_flags.progname, filename, strerror(errno));
		g_flags.exit_code |= 2;
		result = 0;
	} else if (fseek(infile, 0, SEEK_SET) != 0) {
		fprintf(stderr, "%s: can't seek in %s: %s\n", g_flags.progname, filename, strerror(errno));
		g_flags.exit_code |= 2;
		result = 0;
	} else if ((fmt = detect_image_format(header, header_size)) == IF_UNKNOWN) { do_unknown:
		/* This code is not reached for non-image files in a recursively scanned dir, detect_image_format was called earlier. */
		if (header_size == 0) {
			fprintf(stderr, "%s: empty image file: %s\n", g_flags.progname, filename);
		} else {
			fprintf(stderr, "%s: unknown image file format: %s\n", g_flags.progname, filename);
		}
		g_flags.exit_code |= 8;
		result = 0;
	} else if (fmt == IF_JPEG) {
		result = load_image_jpeg(img, filename, infile, tmp_filename);
	} else if (fmt == IF_PNG) {
		result = load_image_png(img, filename, infile, tmp_filename);
	} else if (fmt == IF_GIF) {
		result = load_image_gif(img, filename, infile, tmp_filename);
	} else {
		goto do_unknown;  /* Shouldn't happen. */
	}
	fclose(infile);
	return result;
}

static void create_thumbnail(char *filename) {
	/* TODO(pts): Don't use MAXPATHLEN. */
	char final[MAXPATHLEN], tmp_filename[MAXPATHLEN];
	void (*resize_func)(unsigned num_components, unsigned output_width, unsigned output_height, unsigned, unsigned, const unsigned char *p, unsigned char *o);
	struct stat sb;
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr cerr;
	unsigned char *o;
	struct image imgs, *img = &imgs;
	unsigned img_datasize;
	JSAMPROW row_pointer[1];

	printf("Image %s\n", filename);
	fflush(stdout);
	if (stat(filename, &sb)) {
		fprintf(stderr, "%s: can't stat(%s): %s\n", g_flags.progname,
		    filename, strerror(errno));
		g_flags.exit_code |= 2;
		return;
	}

	{  /* Generate thumbnail filename. */
		const char* r = filename + strlen(filename);
		const char* p = r;
		size_t prefixlen;
		if (r - filename >= 7 && 0 == memcmp(r - 7, ".th.jpg", 7 * sizeof(char)))
			return;  /* Already a thumbnail. */

		/* Replace image extension with .th.jpg, save result to th_filename */
		for (; p != filename && p[-1] != '/' && p[-1] != '.'; --p) {}
		prefixlen = (p != filename && p[-1] == '.') ? p - filename - 1 : r - filename;
		memcpy(final, filename, prefixlen * sizeof(char));
		strcpy(final + prefixlen, ".th.jpg");
		sprintf(tmp_filename, "%s.tmp", final);
	}

	/*
	 * Check if the cached image exists and is newer than the
	 * original.
	 */
	if (!g_flags.force && check_cache(final, &sb)) return;

	if (!load_image(img, filename, tmp_filename)) {
		free(img->data);
		if (img->outfile) {
			fclose(img->outfile);
			unlink(tmp_filename);
		}
		return;
	}

	/* Resize the image. */
	img_datasize = img->scalewidth * img->scaleheight * img->num_components;
#if 0
	fprintf(stderr, "img->scalewidth=%d img->scaleheight=%d img->num_components=%d img_datasize=%d\n", img->scalewidth, img->scaleheight, img->num_components, img_datasize);
	fprintf(stderr, "img->output_width=%d img->output_height=%d s_row_width=%d\n", img->output_width, img->output_height, img->output_width * img->num_components);
#endif
	/* Typically, if img->output_width == img->scalewidth, then the heights are also the same.
	 * A notable excaption when it is +1: input JPEG 700x961, -H 480, img->scale_denom=2, img->output_width=350 img->output_height=481.
	 */
	if (img->output_width == img->scalewidth &&
	    (img->output_height == img->scaleheight || img->output_height == img->scaleheight + 1)) {
		/* No scaling needed, input (p) is already of the right size.
		 * We ignore the last row of p in the +1 case.
		 */
		o = img->data;
	} else {
		check_alloc(o = malloc(img_datasize * sizeof(unsigned char)));
		resize_func = g_flags.bilinear ? resize_bilinear : resize_bicubic;
		resize_func(img->num_components, img->output_width, img->output_height, img->scalewidth, img->scaleheight, img->data, o);
		free(img->data);
	}
	img->data = NULL;  /* Extra carefulness to prevent a double free. */

	/* Prepare the compression object. */
	cinfo.err = jpeg_std_error(&cerr);
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, img->outfile);
	cinfo.image_width = img->scalewidth;
	cinfo.image_height = img->scaleheight;
	cinfo.input_components = img->num_components;
	cinfo.in_color_space = img->colorspace;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 50, FALSE);  /* TODO(pts): Make quality configurable. */

	/* Write the image out. */
	jpeg_start_compress(&cinfo, FALSE);
	{
		char comment_text[16 + sizeof(unsigned) * 6];
		sprintf(comment_text, "REALDIMEN:%ux%u",
		        img->width, img->height);
		jpeg_write_marker(&cinfo, JPEG_COM, (void*)comment_text,
		                  strlen(comment_text));
	}
	while (cinfo.next_scanline < cinfo.image_height) {
		row_pointer[0] = &o[cinfo.input_components *
		    cinfo.image_width * cinfo.next_scanline];
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}
	jpeg_finish_compress(&cinfo);
	fflush(img->outfile);
	if (ferror(img->outfile)) {
		fprintf(stderr, "%s: error writing data to: %s\n", g_flags.progname, tmp_filename);
		fclose(img->outfile);
		jpeg_destroy_compress(&cinfo);
		free(o);
		unlink(tmp_filename);
		g_flags.exit_code |= 2;
		return;
	}
	fclose(img->outfile);
	img->outfile = NULL;
	jpeg_destroy_compress(&cinfo);
	free(o);

	if (rename(tmp_filename, final)) {
		fprintf(stderr, "%s: can't rename(%s, %s): "
		    "%s\n", g_flags.progname, tmp_filename, final,
		    strerror(errno));
		unlink(tmp_filename);
		g_flags.exit_code |= 2;
		return;
	}
}

static int
check_cache(char *filename, struct stat *sb_ori)
{
	struct stat sb;
	int cached;

	cached = 1;

	if (stat(filename, &sb)) {
		if (errno != ENOENT) {
			fprintf(stderr, "%s: warning: can't stat(%s): %s\n", g_flags.progname,
			    filename, strerror(errno));
			cached = 0;
		} else {
		    cached = 0;
		}
	} else if (sb.st_mtime < sb_ori->st_mtime)
		cached = 0;

	return (cached);
}

/*
 * Comparision functions used by qsort().
 */
static int
sort_by_filename(const void *a, const void *b)
{
	char **ia = (char**)a;
	char **ib = (char**)b;
	/* We could use strcoll instead of strcmp for locale-compatible sorting, but let's not go that way. */
	return strcmp(*ia, *ib);
}

static void
version(void)
{
	fprintf(stderr, "swiggle version %s\n", SWIGGLE_VERSION);
	exit(EXIT_SUCCESS);
}

static void
usage(void)
{
	fprintf(stderr, "\nUsage:\n");
	fprintf(stderr, "pts-swiggle [options] /path/to/gallery\n\n");
	fprintf(stderr, "Available options:\n");
	fprintf(stderr, "   -R         process directories recursively\n");
	fprintf(stderr, "   -c <x> ... columns per thumbnail index page\n");
	fprintf(stderr, "   -r <y> ... rows per thumbnail index page\n");
	fprintf(stderr, "   -h <i> ... height of the thumbnails in pixel\n");
	fprintf(stderr, "   -H <j> ... height of the scaled images in pixel "
	    "(default: %d)\n", g_flags.scaleheight);
	fprintf(stderr, "   -f     ... force rebuild of everything; ignore "
	    "cache\n");
	fprintf(stderr, "   -o     ... don't remove orphaned files\n");
	fprintf(stderr, "   -l     ... use bilinear resizing instead of "
	    "bicubic\n");
	fprintf(stderr, "              (faster, but image quality is poor)\n");
	fprintf(stderr, "   -s <n> ... sort images according to argument\n");
	fprintf(stderr, "              ('name', 'size', 'mtime'; default is "
	    "'name')\n");
	fprintf(stderr, "   -d     ... title string for gallery and albums, "
	    "if not provided in\n");
	fprintf(stderr, "              '.description' files\n");
	fprintf(stderr, "   -a     ... also create thumbnails for small files (no scaling)\n");
	fprintf(stderr, "   -v     ... show version info\n\n");
}

/*
 * Scales image (with pixels given in "p") according to the settings in
 * "dinfo" (the source image) and "cinfo" (the target image) and stores
 * the result in "o".
 * Scaling is done with a bicubic algorithm (stolen from ImageMagick :-)).
 */
static void resize_bicubic(
    unsigned num_components, unsigned output_width, unsigned output_height,
    unsigned out_width, unsigned out_height, const unsigned char *p,
    unsigned char *o) {
	unsigned char *x_vector;
	int comp, next_col, next_row;
	unsigned s_row_width, ty, t_row_width, x, y, num_rows;
	double factor, *s, *scanline, *scale_scanline;
	double *t, x_scale, x_span, y_scale, y_span, *y_vector;

	/* RGB images have 3 components, grayscale images have only one. */
	comp = num_components;
	s_row_width = output_width * comp;
	t_row_width = out_width  * comp;
	factor = (double)out_width / (double)output_width;

	check_alloc(x_vector = malloc(s_row_width * sizeof(unsigned char)));
	check_alloc(y_vector = malloc(s_row_width * sizeof(double)));
	check_alloc(scanline = malloc(s_row_width * sizeof(double)));
	check_alloc(scale_scanline = malloc((t_row_width + comp) * sizeof(double)));

	num_rows = 0;
	next_row = 1;
	y_span = 1.0;
	y_scale = factor;

	for (y = 0; y < out_height; y++) {
		ty = y * t_row_width;

		bzero(y_vector, s_row_width * sizeof(double));
		bzero(scale_scanline, t_row_width * sizeof(double));

		/* Scale Y-dimension. */
		while (y_scale < y_span) {
			if (next_row && num_rows < output_height) {
				/* Read a new scanline.  */
				memcpy(x_vector, p, s_row_width * sizeof(unsigned char));
				p += s_row_width;
				num_rows++;
			}
			for (x = 0; x < s_row_width; x++)
				y_vector[x] += y_scale * (double)x_vector[x];
			y_span  -= y_scale;
			y_scale  = factor;
			next_row = 1;
		}
		if (next_row && num_rows < output_height) {
			/* Read a new scanline.  */
			memcpy(x_vector, p, s_row_width * sizeof(unsigned char));
			p += s_row_width;
			num_rows++;
			next_row = 0;
		}
		s = scanline;
		for (x = 0; x < s_row_width; x++) {
			y_vector[x] += y_span * (double) x_vector[x];
			*s = y_vector[x];
			s++;
		}
		y_scale -= y_span;
		if (y_scale <= 0) {
			y_scale  = factor;
			next_row = 1;
		}
		y_span = 1.0;

		next_col = 0;
		x_span   = 1.0;
		s = scanline;
		t = scale_scanline;

		/* Scale X dimension. */
		for (x = 0; x < output_width; x++) {
			x_scale = factor;
			while (x_scale >= x_span) {
				if (next_col)
					t += comp;
				t[0] += x_span * s[0];
				if (comp != 1) {
					t[1] += x_span * s[1];
					t[2] += x_span * s[2];
				}
				x_scale -= x_span;
				x_span   = 1.0;
				next_col = 1;
			}
			if (x_scale > 0) {
				if (next_col) {
					next_col = 0;
					t += comp;
				}
				t[0] += x_scale * s[0];
				if (comp != 1) {
					t[1] += x_scale * s[1];
					t[2] += x_scale * s[2];
				}
				x_span -= x_scale;
			}
			s += comp;
		}

		/* Copy scanline to target. */
		t = scale_scanline;
		for (x = 0; x < t_row_width; x++)
			o[ty+x] = (unsigned char)t[x];
	}

	free(x_vector);
	free(y_vector);
	free(scanline);
	free(scale_scanline);
}

/*
 * Scales image (with pixels given in "p") according to the settings in
 * "dinfo" (the source image) and "cinfo" (the target image) and stores
 * the result in "o".
 * Scaling is done with a bilinear algorithm.
 */
static void resize_bilinear(
    unsigned num_components, unsigned output_width, unsigned output_height,
    unsigned out_width, unsigned out_height, const unsigned char *p,
    unsigned char *o) {
	double factor, fraction_x, fraction_y, one_minus_x, one_minus_y;
	unsigned ceil_x, ceil_y, floor_x, floor_y, s_row_width;
	unsigned tcx, tcy, tfx, tfy, tx, ty, t_row_width, x, y;
	(void)output_height;

	/* RGB images have 3 components, grayscale images have only one. */
	s_row_width = num_components * output_width;
	t_row_width = num_components * out_width;
	factor = (double)output_width / (double)out_width;
	for (y = 0; y < out_height; y++) {
		for (x = 0; x < out_width; x++) {
			floor_x = (unsigned)(x * factor);
			floor_y = (unsigned)(y * factor);
			ceil_x = (floor_x + 1 > out_width)
			    ? floor_x
			    : floor_x + 1;
			ceil_y = (floor_y + 1 > out_height)
			    ? floor_y
			    : floor_y + 1;
			fraction_x = (x * factor) - floor_x;
			fraction_y = (y * factor) - floor_y;
			one_minus_x = 1.0 - fraction_x;
			one_minus_y = 1.0 - fraction_y;

			tx  = x * num_components;
			ty  = y * t_row_width;
			tfx = floor_x * num_components;
			tfy = floor_y * s_row_width;
			tcx = ceil_x * num_components;
			tcy = ceil_y * s_row_width;

			o[tx + ty] = one_minus_y *
			    (one_minus_x * p[tfx + tfy] +
			    fraction_x * p[tcx + tfy]) +
			    fraction_y * (one_minus_x * p[tfx + tcy] +
			    fraction_x  * p[tcx + tcy]);

			if (num_components != 1) {
				o[tx + ty + 1] = one_minus_y *
				    (one_minus_x * p[tfx + tfy + 1] +
				    fraction_x * p[tcx + tfy + 1]) +
				    fraction_y * (one_minus_x *
				    p[tfx + tcy + 1] + fraction_x *
				    p[tcx + tcy + 1]);

				o[tx + ty + 2] = one_minus_y *
				    (one_minus_x * p[tfx + tfy + 2] +
				    fraction_x * p[tcx + tfy + 2]) +
				    fraction_y * (one_minus_x *
				    p[tfx + tcy + 2] + fraction_x *
				    p[tcx + tcy + 2]);
			}
		}
	}
}
