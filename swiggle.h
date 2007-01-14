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
 * $Id: swiggle.h,v 1.8 2006/11/12 21:41:46 le Exp $
 *
 */

#ifndef	SWIGGLE_H
#define	SWIGGLE_H

#include <sys/types.h>

#include <jpeglib.h>
#include <libexif/exif-data.h>

#define	SWIGGLE_VERSION	"0.4"
#define	SIZE_UCHAR	sizeof(unsigned char)

struct imginfo {
	char	*filename;
	char	*description;
	int	filesize;
	time_t	mtime;
	int	width;
	int	height;
	int	scalewidth;
	int	scaleheight;
	int	thumbwidth;
	int	thumbheight;
	char	*model;
	char	*datetime;
	char	*exposuretime;
	char	*flash;
	char	*aperture;
};

struct imgdesc {
	char	*filename;
	char	*desc;
};

void	create_html(char *, struct imginfo *, int);
int	create_thumbindex(char *, struct imginfo *, int);

int resize_bicubic(struct jpeg_decompress_struct *,
    struct jpeg_compress_struct *, const unsigned char *, unsigned char **);
int resize_bilinear(struct jpeg_decompress_struct *,
    struct jpeg_compress_struct *, const unsigned char *, unsigned char **);
#endif	/* SWIGGLE_H */
