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

/* TODO(pts): Process directories recursively. */
/* TODO(pts): Remove unused command-line flags and their help. */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <setjmp.h>
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

#include <jpeglib.h>

#define	SWIGGLE_VERSION	"0.4-pts"

static struct {
	char *progname;
	int scaleheight;
	int force;
	int bilinear;
	int exit_code;
} g_flags = { "", 480, 0, 0, EXIT_SUCCESS };

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
    struct jpeg_decompress_struct *,
    unsigned, unsigned, const unsigned char *, unsigned char *);
static void resize_bilinear(
    struct jpeg_decompress_struct *,
    unsigned, unsigned, const unsigned char *, unsigned char *);

static void check_alloc(const void *p) {
	if (!p) {
		const char msg[] = "pts-swiggle: Out of memory, aborting.\n";
		(void)-write(2, msg, sizeof(msg) - 1);
		abort();
	}
}

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

	while ((i = getopt(argc, argv, "c:d:h:H:r:s:flov")) != -1) {
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
				exit(EXIT_FAILURE);
			}
			break;
		case 'r':  /* rows, ignored */
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
		case 'v':
			version();
			break;
		case '?':
			usage();
			exit(EXIT_SUCCESS);
		default:
			usage();
			exit(EXIT_FAILURE);
		}
	}

	argc -= optind;
	argv += optind;
	if (argc < 1) {
		usage();
		exit(EXIT_FAILURE);
	}

	/* Put the inputs to increasing order for deterministic processing. */
	qsort(argv, argc, sizeof argv[0],
	      (int(*)(const void*,const void*))strcmp);

	for (i = 0; i < argc; ++i) {
		if (stat(argv[i], &sb)) {
			fprintf(stderr, "%s: can't stat(%s): %s\n", g_flags.progname, argv[i],
			    strerror(errno));
			g_flags.exit_code = EXIT_FAILURE;
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
			g_flags.exit_code = EXIT_FAILURE;
		}

	}

	return g_flags.exit_code;
}

/*
 * Opens the directory given in parameter "dir" and reads the filenames
 * of all .jpg files, stores them in a list and initiates the creation
 * of the scaled images and html pages. Returns the number of images
 * found in this directory.
 */
static void process_dir(char *dir) {
	char **imglist;
	unsigned imgcount;
	unsigned imgcapacity;
	unsigned i;
	char *fn, *p;
	unsigned dir_size;
	struct dirent *dent;
	struct stat sb;
	DIR *thisdir;

	if ((thisdir = opendir(dir)) == NULL) {
		fprintf(stderr, "%s: can't opendir(%s): %s\n", g_flags.progname, dir,
		    strerror(errno));
		g_flags.exit_code = EXIT_FAILURE;
		return;
	}

	dir_size = strlen(dir);
	imglist = NULL;
	imgcount = 0;
	imgcapacity = 0;
	while ((dent = readdir(thisdir)) != NULL) {
	        /* We only want regular files that have a filename suffix. */
		if ((p = strrchr(dent->d_name, '.')) == NULL) continue;
		++p;
		/* We currently only handle .jpg files. */
		/* TODO(pts): Remove this `if' */
		if (strcasecmp(p, "jpg" ) != 0 && strcasecmp(p, "jpeg") != 0) continue;
		check_alloc(fn = malloc(dir_size + strlen(dent->d_name) + 2));
		sprintf(fn, "%s/%s", dir, dent->d_name);
		if (
#ifdef DT_UNKNOWN
		    dent->d_type == DT_REG ? 0 :
		    dent->d_type != DT_UNKNOWN ? 1 :
#endif
		    (stat(fn, &sb) == 0 && !S_ISREG(sb.st_mode))) {
			free(fn);
			continue;
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
		g_flags.exit_code = EXIT_FAILURE;
	}
	/* Sort the list according to desired sorting function. */
	qsort(imglist, imgcount, sizeof(*imglist), sort_by_filename);
	for (i = 0; i < imgcount; ++i) {
		create_thumbnail(imglist[i]);
	}
	for (i = 0; i < imgcount; ++i) {
		free(imglist[i]);
	}
	free(imglist);
	printf("%d image%s processed in dir: %s\n", imgcount, imgcount != 1 ? "s" : "", dir);
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

static void create_thumbnail(char *filename) {
	/* TODO(pts): Don't use MAXPATHLEN. */
	char final[MAXPATHLEN], tmp[MAXPATHLEN];
	double factor, ratio;
	FILE *infile, *outfile;
	unsigned n, row_width;
	void (*resize_func)(struct jpeg_decompress_struct *dinfo, unsigned, unsigned, const unsigned char *p, unsigned char *o);
	struct stat sb;
	struct jpeg_decompress_struct dinfo;
	struct jpeg_compress_struct cinfo;
	struct my_jpeg_error_mgr derr;
	struct jpeg_error_mgr cerr;
	unsigned char *o, *p;
	JSAMPARRAY samp;
	JSAMPROW row_pointer[1];
	unsigned img_width;
	unsigned img_height;
	unsigned img_scalewidth;
	unsigned img_scaleheight;
	int has_decompress_started;

	dinfo.err = jpeg_std_error(&derr.pub);
	derr.pub.error_exit = my_jpeg_error_exit;
	cinfo.err = jpeg_std_error(&cerr);
	if (g_flags.bilinear)
		resize_func = resize_bilinear;
	else
		resize_func = resize_bicubic;

	printf("Image %s\n", filename);
	fflush(stdout);
	if (stat(filename, &sb)) {
		fprintf(stderr, "%s: can't stat(%s): %s\n", g_flags.progname,
		    filename, strerror(errno));
		g_flags.exit_code = EXIT_FAILURE;
		return;
	}

	{  /* Generate thumbnail filename. */
		const char* r = filename + strlen(filename);
		const char* p = r;
		size_t prefixlen;
		if (r - filename >= 7 && 0 == memcmp(r - 7, ".th.jpg", 7))
			return;  /* Already a thumbnail. */

		/* Replace image extension with .th.jpg, save result to th_filename */
		for (; p != filename && p[-1] != '/' && p[-1] != '.'; --p) {}
		prefixlen = (p != filename && p[-1] == '.') ? p - filename - 1 : r - filename;
		memcpy(final, filename, prefixlen);
		strcpy(final + prefixlen, ".th.jpg");
		sprintf(tmp, "%s.tmp", final);
	}

	/*
	 * Check if the cached image exists and is newer than the
	 * original.
	 */
	if (!g_flags.force && check_cache(final, &sb)) return;

	/*
	 * Open the file and get some basic image information.
	 */
	if ((infile = fopen(filename, "rb")) == NULL) {
		fprintf(stderr, "%s: can't fopen(%s): %s\n", g_flags.progname,
		    filename, strerror(errno));
		g_flags.exit_code = EXIT_FAILURE;
		return;
	}
	p = NULL;
	outfile = NULL;
	has_decompress_started = 0;
	if (setjmp(derr.setjmp_buffer)) {
		/* If we get here, the JPEG code has signaled a fatal error, which was already printed to stderr in my_jpeg_error_exit.
		 * Example non-fatal error: Premature end of JPEG file
		 * Example fatal error: Not a JPEG file: starts with 0x66 0x6f
		 * TODO(pts): We should report (with fprintf(stderr, ...)) both fatal and non-fatal errors.
		 * After a non-fatal error, the error is printed to stderr, jpeg_read_scanlines can continue and will return gray pixels.
		 */
		if (has_decompress_started) jpeg_finish_decompress(&dinfo);
		jpeg_destroy_decompress(&dinfo);
		fclose(infile);
		free(p);
		if (outfile) {
			fclose(outfile);
			unlink(tmp);
		}
		g_flags.exit_code = EXIT_FAILURE;
		return;
	}
	jpeg_create_decompress(&dinfo);
	jpeg_stdio_src(&dinfo, infile);
	(void)jpeg_read_header(&dinfo, FALSE);

	img_width = dinfo.image_width;
	img_height = dinfo.image_height;

	/* ratio needed to scale image correctly. */
	ratio = (double)img_width / (double)img_height;
	img_scaleheight = g_flags.scaleheight;
	img_scalewidth = (int)((double)img_scaleheight * ratio + 0.5);
	/* TODO(pts): Fix too large width. */
	if (!(img_scaleheight < img_height ||
	      img_scalewidth < img_width)) {
		jpeg_destroy_decompress(&dinfo);
		fclose(infile);
		return;
	}

	/*
	 * If the image is not cached, we need to read it in,
	 * resize it, and write it out.
	 */
	if ((outfile = fopen(tmp, "wb")) == NULL) {
		fprintf(stderr, "%s: can't fopen(%s): %s\n",
		    g_flags.progname, tmp, strerror(errno));
		jpeg_destroy_decompress(&dinfo);
		fclose(infile);
		g_flags.exit_code = EXIT_FAILURE;
		return;
	}

	/*
	 * Use libjpeg's handy feature to downscale the
	 * original on the fly while reading it in.
	 */
	factor = (double)img_width / (double)img_scalewidth;
	if ((int) factor >= 8)
		dinfo.scale_denom = 8;
	else if ((int) factor >= 4)
		dinfo.scale_denom = 4;
	else if ((int) factor >= 2)
		dinfo.scale_denom = 2;

	has_decompress_started = 1;
	jpeg_start_decompress(&dinfo);
	row_width = dinfo.output_width * dinfo.num_components;
	check_alloc(p = malloc(row_width * dinfo.output_height * sizeof(unsigned char)));
	samp = (*dinfo.mem->alloc_sarray)
	    ((j_common_ptr)&dinfo, JPOOL_IMAGE, row_width, 1);

	/* Read the image into memory. */
	n = 0;
	while (dinfo.output_scanline < dinfo.output_height) {
		jpeg_read_scanlines(&dinfo, samp, 1);
		memcpy(&p[n*row_width], *samp, row_width);
		n++;
	}
	jpeg_finish_decompress(&dinfo);
	jpeg_destroy_decompress(&dinfo);
	/* if (setjmp(...)) above can't happen anymore. */
	fclose(infile);

	/* Resize the image. */
	check_alloc(o = malloc(img_scalewidth * img_scaleheight * dinfo.num_components * sizeof(unsigned char)));
	resize_func(&dinfo, img_scalewidth, img_scaleheight, p, o);
	free(p);

	/* Prepare the compression object. */
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, outfile);
	cinfo.image_width = img_scalewidth;
	cinfo.image_height = img_scaleheight;
	cinfo.input_components = dinfo.num_components;
	cinfo.in_color_space = dinfo.out_color_space;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 50, FALSE);  /* TODO(pts): Make quality configurable. */

	/* Write the image out. */
	jpeg_start_compress(&cinfo, FALSE);
	while (cinfo.next_scanline < cinfo.image_height) {
		row_pointer[0] = &o[cinfo.input_components *
		    cinfo.image_width * cinfo.next_scanline];
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}
	jpeg_finish_compress(&cinfo);
	fclose(outfile);
	outfile = NULL;
	jpeg_destroy_compress(&cinfo);
	free(o);

	if (rename(tmp, final)) {
		fprintf(stderr, "%s: can't rename(%s, %s): "
		    "%s\n", g_flags.progname, tmp, final,
		    strerror(errno));
		unlink(tmp);
		g_flags.exit_code = EXIT_FAILURE;
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
	fprintf(stderr, "   -v     ... show version info\n\n");
	exit(EXIT_FAILURE);
}

/*
 * Scales image (with pixels given in "p") according to the settings in
 * "dinfo" (the source image) and "cinfo" (the target image) and stores
 * the result in "o".
 * Scaling is done with a bicubic algorithm (stolen from ImageMagick :-)).
 */
static void resize_bicubic(
    struct jpeg_decompress_struct *dinfo,
    unsigned out_width, unsigned out_height, const unsigned char *p,
    unsigned char *o) {
	unsigned char *x_vector;
	int comp, next_col, next_row;
	unsigned s_row_width, ty, t_row_width, x, y, num_rows;
	double factor, *s, *scanline, *scale_scanline;
	double *t, x_scale, x_span, y_scale, y_span, *y_vector;

	/* RGB images have 3 components, grayscale images have only one. */
	comp = dinfo->num_components;
	s_row_width = dinfo->output_width * comp;
	t_row_width = out_width  * comp;
	factor = (double)out_width / (double)dinfo->output_width;

	/* No scaling needed. */
	if (dinfo->output_width == out_width) {
		memcpy(o, p, s_row_width * dinfo->output_height);
		return;
	}

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
			if (next_row && num_rows < dinfo->output_height) {
				/* Read a new scanline.  */
				memcpy(x_vector, p, s_row_width);
				p += s_row_width;
				num_rows++;
			}
			for (x = 0; x < s_row_width; x++)
				y_vector[x] += y_scale * (double)x_vector[x];
			y_span  -= y_scale;
			y_scale  = factor;
			next_row = 1;
		}
		if (next_row && num_rows < dinfo->output_height) {
			/* Read a new scanline.  */
			memcpy(x_vector, p, s_row_width);
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
		for (x = 0; x < dinfo->output_width; x++) {
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
    struct jpeg_decompress_struct *dinfo,
    unsigned out_width, unsigned out_height, const unsigned char *p,
    unsigned char *o) {
	double factor, fraction_x, fraction_y, one_minus_x, one_minus_y;
	unsigned ceil_x, ceil_y, floor_x, floor_y, s_row_width;
	unsigned tcx, tcy, tfx, tfy, tx, ty, t_row_width, x, y;

	/* RGB images have 3 components, grayscale images have only one. */
	s_row_width = dinfo->num_components * dinfo->output_width;
	t_row_width = dinfo->num_components * out_width;

	factor = (double)dinfo->output_width / (double)out_width;

	/* No scaling needed. */
	if (dinfo->output_width == out_width) {
		memcpy(o, p, s_row_width * dinfo->output_height);
		return;
	}

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

			tx  = x * dinfo->num_components;
			ty  = y * t_row_width;
			tfx = floor_x * dinfo->num_components;
			tfy = floor_y * s_row_width;
			tcx = ceil_x * dinfo->num_components;
			tcy = ceil_y * s_row_width;

			o[tx + ty] = one_minus_y *
			    (one_minus_x * p[tfx + tfy] +
			    fraction_x * p[tcx + tfy]) +
			    fraction_y * (one_minus_x * p[tfx + tcy] +
			    fraction_x  * p[tcx + tcy]);

			if (dinfo->num_components != 1) {
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
