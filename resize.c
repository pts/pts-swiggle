/*
 * swiggle - le's simple web image gallery generator
 *
 * This module is responsible for the scaling of images.
 *
 * Copyright (c) 2003 
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
 * $Id: resize.c,v 1.11 2004/10/23 20:57:02 le Exp $
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef AIX
#include <strings.h>
#endif

#include <jpeglib.h>

#include "swiggle.h"

/*
 * Scales image (with pixels given in "p") according to the settings in
 * "dinfo" (the source image) and "cinfo" (the target image) and stores
 * the result in "o".
 * Scaling is done with a bicubic algorithm (stolen from ImageMagick :-)).
 */
int
resize_bicubic(struct jpeg_decompress_struct *dinfo,
    struct jpeg_compress_struct *cinfo, const unsigned char *p,
    unsigned char **o)
{
	unsigned char *q, *x_vector;
	int comp, i, next_col, next_row, num_rows;
	int s_row_width, ty, t_row_width, x, y;
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
	
	x_vector = malloc(s_row_width * SIZE_UCHAR);
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
	i = 0;
	
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
int
resize_bilinear(struct jpeg_decompress_struct *dinfo, 
    struct jpeg_compress_struct *cinfo, const unsigned char *p,
    unsigned char **o)
{
	double factor, fraction_x, fraction_y, one_minus_x, one_minus_y;
	int ceil_x, ceil_y, floor_x, floor_y, s_row_width;
	int tcx, tcy, tfx, tfy, tx, ty, t_row_width, x, y;
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
			floor_x = (int)(x * factor);
			floor_y = (int)(y * factor);
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
