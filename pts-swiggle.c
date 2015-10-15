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
/* TODO(pts): Try all files as JPEG, don't die on JPEG read errors. */
/* TODO(pts): Remove unused command-line flags and their help. */

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

#include <jpeglib.h>

#define	SWIGGLE_VERSION	"0.4-pts"

/* TODO(pts): Remove unused fields. */
struct imginfo {
	char	*filename;
	int	filesize;
	time_t	mtime;
	int	width;
	int	height;
	int	scalewidth;
	int	scaleheight;
	int	thumbwidth;
	int	thumbheight;
};

struct imgdesc {
	char	*filename;
	char	*desc;
};

static struct {
	char *progname;
	int scaleheight;
	int thumbheight;
	int force;
	int bilinear;
} g_flags = { "", 480, 96, 0, 0 };

/*
 * Function declarations.
 */
static void process_dir(char *);
static void process_images(struct imginfo *, int);
static int check_cache(char *, struct stat *);
static void create_images(struct imginfo *, int);
static void delete_image(struct imginfo *);
static void delete_images(struct imginfo *, int);
static int sort_by_filename(const void *, const void *);
static void usage(void);
static void version(void);
static int resize_bicubic(struct jpeg_decompress_struct *,
    struct jpeg_compress_struct *, const unsigned char *, unsigned char **);
static int resize_bilinear(struct jpeg_decompress_struct *,
    struct jpeg_compress_struct *, const unsigned char *, unsigned char **);

static void check_alloc(const void *p) {
	if (!p) {
		const char msg[] = "pts-swiggle: Out of memory, aborting.\n";
		(void)-write(2, msg, sizeof(msg) - 1);
		abort();
	}
}

static void create_image(char *filename_take, struct imginfo *img) {
	img->filename = filename_take;
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
	int exit_code;
	struct stat sb;

	g_flags.progname = argv[0];

	while ((i = getopt(argc, argv, "c:d:h:H:r:s:flov")) != -1) {
		switch (i) {
		case 'c':  /* cols, ignored */
			break;
		case 'd':  /* defaultdesc, ignored */
			break;
		case 'h':
			g_flags.thumbheight = (int) strtol(optarg, &eptr, 10);
			if (eptr == optarg || *eptr != '\0') {
				fprintf(stderr, "%s: invalid argument '-h "
				    "%s'\n", g_flags.progname, optarg);
				usage();
			}
			break;
		case 'H':
			g_flags.scaleheight = (int) strtol(optarg, &eptr, 10);
			if (eptr == optarg || *eptr != '\0') {
				fprintf(stderr, "%s: invalid argument '-H "
				    "%s'\n", g_flags.progname, optarg);
				usage();
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
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;
	if (argc < 1)
		usage();

	exit_code = EXIT_SUCCESS;

	/* Put the inputs to increasing order for deterministic processing. */
	qsort(argv, argc, sizeof argv[0],
	      (int(*)(const void*,const void*))strcmp);

	for (i = 0; i < argc; ++i) {
		if (stat(argv[i], &sb)) {
			fprintf(stderr, "%s: can't stat(%s): %s\n", g_flags.progname, argv[i],
			    strerror(errno));
			exit_code = EXIT_FAILURE;
			continue;
		}

		if (S_ISDIR(sb.st_mode)) {
			if (argv[i][strlen(argv[i])-1] == '/')
				argv[i][strlen(argv[i])-1] = '\0';
			process_dir(argv[i]);
		} else if (S_ISREG(sb.st_mode)) {
			struct imginfo img;
			char *fn;
			check_alloc(fn = strdup(argv[i]));
			create_image(fn, &img);
			process_images(&img, 1);
			delete_image(&img);
		} else {
			fprintf(stderr, "%s: not a file or directory: %s\n", g_flags.progname,
			    argv[i]);
			exit_code = EXIT_FAILURE;
		}

	}

	return exit_code;
}

/*
 * Opens the directory given in parameter "dir" and reads the filenames
 * of all .jpg files, stores them in a list and initiates the creation
 * of the scaled images and html pages. Returns the number of images
 * found in this directory.
 */
static void process_dir(char *dir) {
	struct imginfo *imglist;
	unsigned imgcount;
	unsigned imgcapacity;
	char *i, *p;
	unsigned dir_size;
	struct dirent *dent;
	struct stat sb;
	DIR *thisdir;

	if ((thisdir = opendir(dir)) == NULL) {
		fprintf(stderr, "%s: can't opendir(%s): %s\n", g_flags.progname, dir,
		    strerror(errno));
		exit(EXIT_FAILURE);
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
		check_alloc(i = malloc(dir_size + strlen(dent->d_name) + 2));
		sprintf(i, "%s/%s", dir, dent->d_name);
		if (
#ifdef DT_UNKNOWN
		    dent->d_type == DT_REG ? 0 :
		    dent->d_type != DT_UNKNOWN ? 1 :
#endif
		    (stat(i, &sb) == 0 && !S_ISREG(sb.st_mode))) {
			free(i);
			continue;
		}
		if (imgcount == imgcapacity) {
			imgcapacity = imgcapacity < 16 ? 16 : imgcapacity << 1;
			check_alloc(imglist = realloc(imglist, imgcapacity * sizeof(struct imginfo)));
		}
		create_image(i, &imglist[imgcount++]);
	}

	if (closedir(thisdir)) {
		fprintf(stderr, "%s: error on closedir(%s): %s", g_flags.progname, dir,
		    strerror(errno));
		exit(EXIT_FAILURE);
	}
	process_images(imglist, imgcount);
	delete_images(imglist, imgcount);
	printf("%d image%s processed in dir: %s\n", imgcount,
	    imgcount != 1 ? "s" : "", dir);
}

static void process_images(struct imginfo *imglist, int imgcount) {
	if (!imgcount) return;
	/* Sort the list according to desired sorting function. */
	qsort(imglist, imgcount, sizeof(struct imginfo), sort_by_filename);
	create_images(imglist, imgcount);
}

static void delete_image(struct imginfo *img) {
	free(img->filename); img->filename = NULL;
}

static void delete_images(struct imginfo *imglist, int imgcount) {
	int i;
	for (i = 0; i < imgcount; ++i) {
		delete_image(imglist + i);
	}
	free(imglist);
}

/*
 * Creates thumbnails and scaled images (if they aren't already cached)
 * for each image given in parameter "imglist", which has "imgcount"
 * members, in the subdirs '.scaled' and '.thumbs' of the directory given
 * in parameter "dir".
 * Also fills in various image information into each member of "imglist".
 */
static void
create_images(struct imginfo *imglist, int imgcount)
{
	char final[MAXPATHLEN], tmp[MAXPATHLEN], *ori;
	double factor, ratio;
	FILE *infile, *outfile;
	int cached, i, n, ori_in_mem, row_width;
	int (*resize_func) ();
	struct imgdesc *id;
	struct stat sb;
	struct jpeg_decompress_struct dinfo;
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	unsigned char *o, *p;
	JSAMPARRAY samp;
	JSAMPROW row_pointer[1];

	id = NULL;

	dinfo.err = jpeg_std_error(&jerr);
	cinfo.err = jpeg_std_error(&jerr);

	if (g_flags.bilinear)
		resize_func = resize_bilinear;
	else
		resize_func = resize_bicubic;

	for (i = 0; i < imgcount; i++) {
		p = o = NULL;
		ori_in_mem = 0;
		ori = imglist[i].filename;

		printf("Image %s\n", ori);

		if (stat(ori, &sb)) {
			fprintf(stderr, "%s: can't stat(%s): %s\n", g_flags.progname,
			    ori, strerror(errno));
			exit(EXIT_FAILURE);
		}

		{  /* Generate thumbnail filename. */
			const char* r = ori + strlen(ori);
			const char* p = r;
			size_t prefixlen;
			if (r - ori >= 7 && 0 == memcmp(r - 7, ".th.jpg", 7))
				continue;

			/* Replace image extension with .th.jpg, save result to th_ori */
			while (p != ori && p[-1] != '/' && p[-1] != '.') {
				--p;
			}
			prefixlen = (p != ori && p[-1] == '.') ? p - ori - 1 : r - ori;
			memcpy(final, ori, prefixlen);
			strcpy(final + prefixlen, ".th.jpg");
		}

		/*
		 * Check if the cached image exists and is newer than the
		 * original.
		 */
		cached = !g_flags.force && check_cache(final, &sb);

		/*
		 * Open the file and get some basic image information.
		 */
		if ((infile = fopen(ori, "rb")) == NULL) {
			fprintf(stderr, "%s: can't fopen(%s): %s\n", g_flags.progname,
			    ori, strerror(errno));
			exit(EXIT_FAILURE);
		}
		jpeg_create_decompress(&dinfo);
		jpeg_stdio_src(&dinfo, infile);
		(void)jpeg_read_header(&dinfo, FALSE);  /* TODO(pts): don't abort the program on failure */

		imglist[i].filesize = sb.st_size;
		imglist[i].mtime = sb.st_mtime;
		imglist[i].width = dinfo.image_width;
		imglist[i].height = dinfo.image_height;

		/* ratio needed to scale image correctly. */
		ratio = ((double)imglist[i].width / (double)imglist[i].height);
		imglist[i].scaleheight = g_flags.scaleheight;
		imglist[i].scalewidth = (int)((double)imglist[i].scaleheight *
		    ratio + 0.5);
		/* TODO(pts): Fix too large width. */
		if (!(imglist[i].scaleheight < imglist[i].height ||
		      imglist[i].scalewidth < imglist[i].width)) {
			jpeg_destroy_decompress(&dinfo);
			fclose(infile);
			continue;
		}

		imglist[i].thumbheight = g_flags.thumbheight;
		imglist[i].thumbwidth = (int)((double)imglist[i].thumbheight *
		    ratio + 0.5);

		/*
		 * If the image is not cached, we need to read it in,
		 * resize it, and write it out.
		 */
		if (!cached) {
			sprintf(tmp, "%s.tmp", final);
			if ((outfile = fopen(tmp, "wb")) == NULL) {
				fprintf(stderr, "%s: can't fopen(%s): %s\n",
				    g_flags.progname, tmp, strerror(errno));
				exit(EXIT_FAILURE);
			}

			/*
			 * Use libjpeg's handy feature to downscale the
			 * original on the fly while reading it in.
			 */
			factor = (double)dinfo.image_width /
			    (double)imglist[i].scalewidth;
			if ((int) factor >= 8)
				dinfo.scale_denom = 8;
			else if ((int) factor >= 4)
				dinfo.scale_denom = 4;
			else if ((int) factor >= 2)
				dinfo.scale_denom = 2;

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

			ori_in_mem = 1; /* The original is now in memory. */

			/* Prepare the compression object. */
			jpeg_create_compress(&cinfo);
			jpeg_stdio_dest(&cinfo, outfile);
			cinfo.image_width = imglist[i].scalewidth;
			cinfo.image_height = imglist[i].scaleheight;
			cinfo.input_components = dinfo.num_components;
			cinfo.in_color_space = dinfo.out_color_space;
			jpeg_set_defaults(&cinfo);
			jpeg_set_quality(&cinfo, 50, FALSE);  /**** pts ****/

			check_alloc(o = malloc(cinfo.image_width * cinfo.image_height * cinfo.input_components * sizeof(unsigned char)));

			/* Resize the image. */
			if (resize_func(&dinfo, &cinfo, p, &o)) {
				fprintf(stderr, "%s: can't resize image '%s': "
				    "%s\n", g_flags.progname, ori, strerror(errno));
				exit(EXIT_FAILURE);
			}

			/* Write the image out. */
			jpeg_start_compress(&cinfo, FALSE);
			while (cinfo.next_scanline < cinfo.image_height) {
				row_pointer[0] = &o[cinfo.input_components *
				    cinfo.image_width * cinfo.next_scanline];
				jpeg_write_scanlines(&cinfo, row_pointer, 1);
				n++;
			}
			jpeg_finish_compress(&cinfo);
			fclose(outfile);
			jpeg_destroy_compress(&cinfo);

			if (rename(tmp, final)) {
				fprintf(stderr, "%s: can't rename(%s, %s): "
				    "%s\n", g_flags.progname, tmp, final,
				    strerror(errno));
				exit(EXIT_FAILURE);
			}
		}

		fclose(infile);
		jpeg_destroy_decompress(&dinfo);

		/*
		 * If we have read the original, free any associated
		 * ressources.
		 */
		if (ori_in_mem) {
			free(o);
			free(p);
		}
	}

	if (id != NULL)
		free(id);
}

static int
check_cache(char *filename, struct stat *sb_ori)
{
	struct stat sb;
	int cached;

	cached = 1;

	if (stat(filename, &sb)) {
		if (errno != ENOENT) {
			fprintf(stderr, "%s: can't stat(%s): %s\n", g_flags.progname,
			    filename, strerror(errno));
			exit(EXIT_FAILURE);
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
	struct imginfo *ia = (struct imginfo*)a;
	struct imginfo *ib = (struct imginfo*)b;
	/* We could use strcoll instead of strcmp for locale-compatible sorting, but let's not go that way. */
	return strcmp(ia->filename, ib->filename);
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
	fprintf(stderr, "   -h <i> ... height of the thumbnails in pixel "
	    "(default: %d)\n", g_flags.thumbheight);
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
static int
resize_bicubic(struct jpeg_decompress_struct *dinfo,
    struct jpeg_compress_struct *cinfo, const unsigned char *p,
    unsigned char **o)
{
	unsigned char *q, *x_vector;
	int comp, next_col, next_row;
	unsigned s_row_width, ty, t_row_width, x, y, num_rows;
	double factor, *s, *scanline, *scale_scanline;
	double *t, x_scale, x_span, y_scale, y_span, *y_vector;

	q = NULL;

	/* RGB images have 3 components, grayscale images have only one. */
	comp = dinfo->num_components;
	s_row_width = dinfo->output_width * comp;
	t_row_width = cinfo->image_width  * comp;
	factor = (double)cinfo->image_width / (double)dinfo->output_width;

	if ( *o == NULL )
		return (-1);

	/* No scaling needed. */
	if (dinfo->output_width == cinfo->image_width) {
		memcpy(*o, p, s_row_width * dinfo->output_height);
		return (0);
	}

	x_vector = malloc(s_row_width * sizeof(unsigned char));
	if (x_vector == NULL)
		return (-1);
	y_vector = malloc(s_row_width * sizeof(double));
	if (y_vector == NULL)
		return (-1);
	scanline = malloc(s_row_width * sizeof(double));
	if (scanline == NULL)
		return (-1);
	scale_scanline = malloc((t_row_width + comp) * sizeof(double));
	if (scale_scanline == NULL)
		return (-1);

	num_rows = 0;
	next_row = 1;
	y_span = 1.0;
	y_scale = factor;

	for (y = 0; y < cinfo->image_height; y++) {
		ty = y * t_row_width;
		q = *o;

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
			q[ty+x] = (unsigned char)t[x];
	}

	free(x_vector);
	free(y_vector);
	free(scanline);
	free(scale_scanline);

	return (0);
}

/*
 * Scales image (with pixels given in "p") according to the settings in
 * "dinfo" (the source image) and "cinfo" (the target image) and stores
 * the result in "o".
 * Scaling is done with a bilinear algorithm.
 */
static int
resize_bilinear(struct jpeg_decompress_struct *dinfo,
    struct jpeg_compress_struct *cinfo, const unsigned char *p,
    unsigned char **o)
{
	double factor, fraction_x, fraction_y, one_minus_x, one_minus_y;
	unsigned ceil_x, ceil_y, floor_x, floor_y, s_row_width;
	unsigned tcx, tcy, tfx, tfy, tx, ty, t_row_width, x, y;
	unsigned char *q;

	/* RGB images have 3 components, grayscale images have only one. */
	s_row_width = dinfo->num_components * dinfo->output_width;
	t_row_width = dinfo->num_components * cinfo->image_width;

	factor = (double)dinfo->output_width / (double)cinfo->image_width;

	if (*o == NULL)
		return (-1);

	/* No scaling needed. */
	if (dinfo->output_width == cinfo->image_width) {
		memcpy(*o, p, s_row_width * dinfo->output_height);
		return (0);
	}

	q = *o;

	for (y = 0; y < cinfo->image_height; y++) {
		for (x = 0; x < cinfo->image_width; x++) {
			floor_x = (unsigned)(x * factor);
			floor_y = (unsigned)(y * factor);
			ceil_x = (floor_x + 1 > cinfo->image_width)
			    ? floor_x
			    : floor_x + 1;
			ceil_y = (floor_y + 1 > cinfo->image_height)
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

			q[tx + ty] = one_minus_y *
			    (one_minus_x * p[tfx + tfy] +
			    fraction_x * p[tcx + tfy]) +
			    fraction_y * (one_minus_x * p[tfx + tcy] +
			    fraction_x  * p[tcx + tcy]);

			if (dinfo->num_components != 1) {
				q[tx + ty + 1] = one_minus_y *
				    (one_minus_x * p[tfx + tfy + 1] +
				    fraction_x * p[tcx + tfy + 1]) +
				    fraction_y * (one_minus_x *
				    p[tfx + tcy + 1] + fraction_x *
				    p[tcx + tcy + 1]);

				q[tx + ty + 2] = one_minus_y *
				    (one_minus_x * p[tfx + tfy + 2] +
				    fraction_x * p[tcx + tfy + 2]) +
				    fraction_y * (one_minus_x *
				    p[tfx + tcy + 2] + fraction_x *
				    p[tcx + tcy + 2]);
			}
		}
	}

	return (0);
}
