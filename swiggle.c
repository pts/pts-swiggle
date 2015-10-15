/*
 * swiggle - le's simple web image gallery generator
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

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef AIX
#include <strings.h>
#endif
#include <time.h>
#include <unistd.h>

#include <jpeglib.h>
#include <libexif/exif-data.h>

#include "swiggle.h"

/*
 * Global variables.
 */
char *albumdesc = "";
char *defaultdesc = "my webgallery";
char *progname = "";
int cols = 5;
int rows = 3;
int scaleheight = 480;
int thumbheight = 96;
int force = 0;
int bilinear = 0;
int rm_orphans = 1;
int (*sort_func)();

#define	MAX_PER_PAGE	(cols*rows)
#define	EXIFSIZ		BUFSIZ

/*
 * Function declarations.
 */
int	processdir(char *);
int	check_cache(char *, struct stat *);
void	create_cache_dirs(char *);
void	create_images(struct imginfo *, int);
void	delete_image(struct imginfo *);
void	delete_images(struct imginfo *, int);
int	sort_by_filename(const void *, const void *);
int	sort_by_filesize(const void *, const void *);
int	sort_by_mtime(const void *, const void *);
int	sort_by_exiftime(const void *, const void *);
int	sort_dirs(const void *, const void *);
char *	get_exif_data(ExifData *, ExifTag);
void	usage(void);
void	version(void);

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

	progname = argv[0];
	sort_func = sort_by_filename;

	while ((i = getopt(argc, argv, "c:d:h:H:r:s:flov")) != -1) {
		switch (i) {
		case 'c':
			cols = (int) strtol(optarg, &eptr, 10);
			if (eptr == optarg || *eptr != '\0' || cols < 1) {
				fprintf(stderr, "%s: invalid argument '-c "
				    "%s'\n", progname, optarg);
				usage();
			}
			break;
		case 'd':
			defaultdesc = optarg;
			break;
		case 'h':
			thumbheight = (int) strtol(optarg, &eptr, 10);
			if (eptr == optarg || *eptr != '\0' || cols < 1) {
				fprintf(stderr, "%s: invalid argument '-h "
				    "%s'\n", progname, optarg);
				usage();
			}
			break;
		case 'H':
			scaleheight = (int) strtol(optarg, &eptr, 10);
			if (eptr == optarg || *eptr != '\0' || cols < 1) {
				fprintf(stderr, "%s: invalid argument '-H "
				    "%s'\n", progname, optarg);
				usage();
			}
			break;
		case 'r':
			rows = (int) strtol(optarg, &eptr, 10);
			if (eptr == optarg || *eptr != '\0' || rows < 1) {
				fprintf(stderr, "%s: invalid argument '-r "
				    "%s'\n", progname, optarg);
				usage();
			}
			break;
		case 's':
			if (strcmp(optarg, "name") == 0)
				sort_func = sort_by_filename;
			else if (strcmp(optarg, "size") == 0)
				sort_func = sort_by_filesize;
			else if (strcmp(optarg, "mtime") == 0)
				sort_func = sort_by_mtime;
			else {
				fprintf(stderr, "%s: invalid argument '-s "
				    "%s'\n", progname, optarg);
				usage();
			}
			break;
		case 'f':
			force = 1;
			break;
		case 'l':
			bilinear = 1;
			break;
		case 'o':
			rm_orphans = 0;
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

	/* Put the dirs to increasing order for deterministic processing. */
	qsort(argv, argc, sizeof argv[0],
	      (int(*)(const void*,const void*))strcmp);

	for (i = 0; i < argc; ++i) {
		if (stat(argv[i], &sb)) {
			fprintf(stderr, "%s: can't stat(%s): %s\n", progname, argv[i],
			    strerror(errno));
			exit_code = EXIT_FAILURE;
			continue;
		}

		if (!S_ISDIR(sb.st_mode)) {
			fprintf(stderr, "%s: '%s' is not a directory\n", progname,
			    argv[i]);
			exit_code = EXIT_FAILURE;
			continue;
		}

		if (argv[i][strlen(argv[i])-1] == '/')
			argv[i][strlen(argv[i])-1] = '\0';

		processdir(argv[i]);
	}

	return exit_code;
}

static int filename_cmp(const void *a, const void *b) {
	struct imginfo *ia = (struct imginfo*)a;
	struct imginfo *ib = (struct imginfo*)b;
	return strcmp(ia->filename, ib->filename);
}

static void check_alloc(const void *p) {
	if (!p) {
		const char msg[] = "pts-swiggle: Out of memory, aborting.\n";
		(void)-write(2, msg, sizeof(msg) - 1);
		abort();
	}
}

/*
 * Opens the directory given in parameter "dir" and reads the filenames
 * of all .jpg files, stores them in a list and initiates the creation
 * of the scaled images and html pages. Returns the number of images
 * found in this directory.
 */
int
processdir(char *dir)
{
	char *i, *p;
	int imgcount;
	unsigned dir_size;
	struct dirent *dent;
	struct imginfo *imglist, *img;
#ifdef NO_D_TYPE
	char buf[MAXPATHLEN];
	struct stat sb;
#endif
	DIR *thisdir;

	imgcount = 0;
	imglist = NULL;

	if ((thisdir = opendir(dir)) == NULL) {
		fprintf(stderr, "%s: can't opendir(%s): %s\n", progname, dir,
		    strerror(errno));
		exit(EXIT_FAILURE);
	}

	dir_size = strlen(dir);
	while ((dent = readdir(thisdir)) != NULL) {
        /* We only want regular files that have a filename suffix. */
#ifdef NO_D_TYPE
		sprintf(buf, "%s/%s", dir, dent->d_name);
		if ((stat(buf, &sb) == 0 && !S_ISREG(sb.st_mode)) ||
		    ((p = strrchr(dent->d_name, '.')) == NULL))
#else
		if ((dent->d_type != DT_REG) ||
		    (p = strrchr(dent->d_name, '.')) == NULL)
#endif
			continue;

		p++;

		/* We currently only handle .jpg files. */
		if (strcasecmp(p, "jpg" ) != 0 && strcasecmp(p, "jpeg") != 0)
			continue;

		/* Allocate memory for this image and store it in the list. */
		
		check_alloc(i = malloc(dir_size + strlen(dent->d_name) + 2));
		sprintf(i, "%s/%s", dir, dent->d_name);
		/* TODO(pts): Do exponential reallocation. */
		check_alloc(imglist = realloc(imglist, (imgcount + 1) * sizeof(struct imginfo)));
		img = &imglist[imgcount++];
		img->filename = i;
		/* TODO(pts): Why are these needed? */
		img->description = NULL;
		img->model = NULL;
		img->datetime = NULL;
		img->exposuretime = NULL;
		img->flash = NULL;
		img->aperture = NULL;
	}

	if (closedir(thisdir)) {
		fprintf(stderr, "%s: error on closedir(%s): %s", progname, dir,
		    strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Put the images to increasing order for deterministic processing. */
	qsort(imglist, imgcount, sizeof imglist[0], filename_cmp);

	if (imgcount) {
		create_images(imglist, imgcount);

		/* Sort the list according to desired sorting function. */
		qsort(imglist, imgcount, sizeof(struct imginfo), sort_func);

#if 0  /**** pts ****/
		create_html(dir, imglist, imgcount);
		x = create_thumbindex(dir, imglist, imgcount);
		printf("%d thumbnail index pages created.\n", x);
#endif
		delete_images(imglist, imgcount);
	}


	printf("%d image%s processed in album '%s'.\n", imgcount,
	    imgcount != 1 ? "s" : "", albumdesc != NULL ? albumdesc : dir);
	return (imgcount);
}

/* TODO(pts): Remove exif support completely. */
void delete_image(struct imginfo *img) {
	free(img->filename); img->filename = NULL;
	free(img->description); img->description = NULL;
	free(img->model); img->model = NULL;
	free(img->datetime); img->datetime = NULL;
	free(img->exposuretime); img->exposuretime = NULL;
	free(img->flash); img->flash = NULL;
	free(img->aperture); img->aperture = NULL;
}

void delete_images(struct imginfo *imglist, int imgcount) {
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
void
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
	ExifData *ed;
	JSAMPARRAY samp;
	JSAMPROW row_pointer[1];

	id = NULL;

	dinfo.err = jpeg_std_error(&jerr);
	cinfo.err = jpeg_std_error(&jerr);

	if (bilinear)
		resize_func = resize_bilinear;
	else
		resize_func = resize_bicubic;

	for (i = 0; i < imgcount; i++) {
		p = o = NULL;
		ori_in_mem = 0;
		ori = imglist[i].filename;

		printf("Image %s\n", ori);

		if (stat(ori, &sb)) {
			fprintf(stderr, "%s: can't stat(%s): %s\n", progname,
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
		if (force)
			cached = 0;
		else
			cached = check_cache(final, &sb);

		/*
		 * Open the file and get some basic image information.
		 * We use width and height from the JPEG header and not
		 * from the EXIF data, since it's possible that the EXIF
		 * data isn't correct.
		 */
		if ((infile = fopen(ori, "rb")) == NULL) {
			fprintf(stderr, "%s: can't fopen(%s): %s\n", progname,
			    ori, strerror(errno));
			fprintf(stderr, "ls -l /proc/%d/fd\n", getpid());
			sleep(999);
			exit(EXIT_FAILURE);
		}
		jpeg_create_decompress(&dinfo);
		jpeg_stdio_src(&dinfo, infile);
		(void)jpeg_read_header(&dinfo, FALSE);  /* TODO(pts): don't abort the program on failure */

		imglist[i].filesize = sb.st_size;
		imglist[i].mtime = sb.st_mtime;
		imglist[i].width = dinfo.image_width;
		imglist[i].height = dinfo.image_height;
		imglist[i].description = NULL;  /* TODO(pts): Remove this field. */

		/* Get the EXIF information from the file. */
		if ((ed = exif_data_new_from_file(ori)) != NULL) {
			imglist[i].model = get_exif_data(ed, EXIF_TAG_MODEL);
			imglist[i].datetime = get_exif_data(ed,
			    EXIF_TAG_DATE_TIME_ORIGINAL);
			imglist[i].exposuretime = get_exif_data(ed,
			    EXIF_TAG_EXPOSURE_TIME);
			imglist[i].aperture = get_exif_data(ed,
			    EXIF_TAG_APERTURE_VALUE);
			imglist[i].flash = get_exif_data(ed, EXIF_TAG_FLASH);
			exif_data_free(ed);  /* Close ori. */
			ed = NULL;
		} else {
			imglist[i].model = NULL;
			imglist[i].datetime = NULL;
			imglist[i].exposuretime = NULL;
			imglist[i].aperture = NULL;
			imglist[i].flash = NULL;
		}

		/* ratio needed to scale image correctly. */
		ratio = ((double)imglist[i].width / (double)imglist[i].height);
		imglist[i].scaleheight = scaleheight;
		imglist[i].scalewidth = (int)((double)imglist[i].scaleheight *
		    ratio + 0.5);
		/* TODO(pts): Fix too large width. */
		if (!(imglist[i].scaleheight < imglist[i].height ||
		      imglist[i].scalewidth < imglist[i].width)) {
			jpeg_destroy_decompress(&dinfo);
			fclose(infile);
			continue;
		}

		imglist[i].thumbheight = thumbheight;
		imglist[i].thumbwidth = (int)((double)imglist[i].thumbheight *
		    ratio + 0.5);

		/*
		 * If the image is not cached, we need to read it in,
		 * resize it, and write it out.
		 */
		if (cached == 0) {
			sprintf(tmp, "%s.tmp", final);
			if ((outfile = fopen(tmp, "wb")) == NULL) {
				fprintf(stderr, "%s: can't fopen(%s): %s\n",
				    progname, tmp, strerror(errno));
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
			check_alloc(p = malloc(row_width * dinfo.output_height * SIZE_UCHAR));
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

			check_alloc(o = malloc(cinfo.image_width * cinfo.image_height * cinfo.input_components * SIZE_UCHAR));

			/* Resize the image. */
			if (resize_func(&dinfo, &cinfo, p, &o)) {
				fprintf(stderr, "%s: can't resize image '%s': "
				    "%s\n", progname, ori, strerror(errno));
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
				    "%s\n", progname, tmp, final,
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

		if (ed != NULL)
			exif_data_free(ed);
	}

	if (id != NULL)
		free(id);
}

int
check_cache(char *filename, struct stat *sb_ori)
{
	struct stat sb;
	int cached;

	cached = 1;

	if (stat(filename, &sb)) {
		if (errno != ENOENT) {
			fprintf(stderr, "%s: can't stat(%s): %s\n", progname,
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
 * Returns the value for EXIF tag "t" from EXIF data structure "ed".
 */
char *
get_exif_data(ExifData *ed, ExifTag t)
{
	ExifEntry *ee;
	char p[EXIFSIZ], *x;
	int i;

	for (i = 0; i < EXIF_IFD_COUNT; i++) {
		if (ed->ifd[i] && ed->ifd[i]->count) {
			ee = exif_content_get_entry(ed->ifd[i], t);
			if (ee && exif_entry_get_value(ee, p, EXIFSIZ)
			    != NULL) {
				check_alloc(x = strdup(p));
				return (x);
			}
		}
	}

	return NULL;
}

/*
 * Comparision functions used by qsort().
 */
int
sort_by_filename(const void *a, const void *b)
{
	const struct imginfo *x, *y;

	x = a;
	y = b;

	return (strcoll(x->filename, y->filename));
}

int
sort_by_mtime(const void *a, const void *b)
{
	const struct imginfo *x, *y;

	x = a;
	y = b;

	if (x->mtime < y->mtime)
		return (-1);
	else if (x->mtime > y->mtime)
		return (1);
	else
		return (0);
}

int
sort_by_filesize(const void *a, const void *b)
{
	const struct imginfo *x, *y;

	x = a;
	y = b;

	if (x->filesize < y->filesize)
		return (-1);
	else if (x->filesize > y->filesize)
		return (1);
	else
		return (0);
}

int
sort_dirs(const void *a, const void *b)
{
	const struct dirent *x, *y;

	x = a;
	y = b;

	return (strcoll(x->d_name, y->d_name));
}

void
version(void)
{
	fprintf(stderr, "swiggle version %s\n", SWIGGLE_VERSION);
	exit(EXIT_SUCCESS);
}

void
usage(void)
{
	fprintf(stderr, "\nUsage:\n");
	fprintf(stderr, "pts-swiggle [options] /path/to/gallery\n\n");
	fprintf(stderr, "Available options:\n");
	fprintf(stderr, "   -c <x> ... columns per thumbnail index page "
	    "(default: %d)\n", cols);
	fprintf(stderr, "   -r <y> ... rows per thumbnail index page "
	    "(default: %d)\n", rows);
	fprintf(stderr, "   -h <i> ... height of the thumbnails in pixel "
	    "(default: %d)\n", thumbheight);
	fprintf(stderr, "   -H <j> ... height of the scaled images in pixel "
	    "(default: %d)\n", scaleheight);
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
	fprintf(stderr, "              '.description' files (default: '%s')\n",
	    defaultdesc);
	fprintf(stderr, "   -v     ... show version info\n\n");
	exit(EXIT_FAILURE);
}
