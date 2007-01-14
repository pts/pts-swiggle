/*
 * swiggle - le's simple web image gallery generator
 *
 * This module is responsible for HTML output.
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
 * $Id: html.c,v 1.10 2007/01/14 12:03:36 le Exp $
 *
 */

#include <sys/param.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "swiggle.h"

#define	MAX_PER_PAGE	(cols*rows)

extern int	cols;
extern int	rows;
extern char	generated[];
extern char	*albumdesc;
extern char	*defaultdesc;
extern char	*progname;

/*
 * Creates one html page per image given in the argument "imglist", which
 * has "imgcount" members, in the directory "dir".
 */
void 
create_html(char *dir, struct imginfo *imglist, int imgcount)
{
	char final[MAXPATHLEN], tmp[MAXPATHLEN];
	int offset, x;
	FILE *html;

	offset = 1; /* Needed for correct link back to thumbnail index page. */
	
	for (x = 0; x < imgcount; x++) {
		sprintf(final, "%s/%s.html", dir, imglist[x].filename);
		sprintf(tmp, "%s.tmp", final);
		if ((html = fopen(tmp, "w")) == NULL) {
			fprintf(stderr, "%s: fopen(%s) failed: %s\n", progname,
			    tmp, strerror(errno));
			exit(EXIT_FAILURE);
		}
		
		fprintf(html, "<html>\n"
		    "<head>\n"
		    "<title>%s: %s</title>\n"
		    "<link rel=\"stylesheet\" type=\"text/css\" "
		    "href=\"../swiggle.css\" />\n"
		    "</head>\n"
		    "<body>\n"
		    "<table width=\"100%%\" cellpadding=\"5\">\n"
		    "<tr>\n"
		    "<td width=\"100%%\" align=\"center\" colspan=\"3\">"
		    "<a href=\"%s\"><img src=\".scaled/%s\" border=\"0\" "
		    "width=\"%d\" height=\"%d\" alt=\"%s\"></a>"
		    "<br />\n<b><em>%s</em></b>\n"
		    "</td></tr>\n<tr><td width=\"33%%\" align=\"center\" "
		    "valign=\"top\">\n", 
		    albumdesc,
		    imglist[x].filename, 
		    imglist[x].filename,
		    imglist[x].filename,
		    imglist[x].scalewidth,
		    imglist[x].scaleheight,
		    imglist[x].filename, 
		    imglist[x].description);
		
		/* Generate link with thumbnail to previous image. */
		if (x > 0) {
			fprintf(html, "<a href=\"%s.html\"><img "
			    "src=\".thumbs/%s\" border=\"0\" width=\"%d\" "
			    "height=\"%d\" alt=\"%s\"><br>"
			    "<small>previous image</small></a>", 
			    imglist[x-1].filename, 
			    imglist[x-1].filename, 
			    imglist[x-1].thumbwidth, 
			    imglist[x-1].thumbheight, 
			    imglist[x-1].filename);
		}
		
		fprintf(html, "</td>\n<td width=\"34%%\" align=\"center\">\n"
		    "<small><a href=\"%s\">Original size (%d x %d, %d KB)</a>"
		    "</small>\n<p>\n",
		    imglist[x].filename,
		    imglist[x].width,
		    imglist[x].height,
		    (int) (imglist[x].filesize/1024));
		
		fprintf(html, "<table width=\"100%%\" border=\"1\">\n");
		
		if (imglist[x].datetime != NULL) {
			fprintf(html, "<tr>\n<td><small>Taken:</small></td>"
			    "<td><small>%s</small></td>\n</tr>\n",
			    imglist[x].datetime);
		}
		if (imglist[x].aperture != NULL) {
			fprintf(html, "<tr>\n<td><small>Aperture:</small></td>"
			    "<td><small>%s</small></td>\n</tr>\n",
			    imglist[x].aperture);
		}
		if (imglist[x].exposuretime != NULL) {
			fprintf(html, "<tr>\n<td><small>Exposure Time</small>:"
			    "</td><td><small>%s</small></td>\n</tr>\n",
			    imglist[x].exposuretime);
		}
		if (imglist[x].flash != NULL) {
			fprintf(html, "<tr>\n<td><small>Flash:</small></td>"
			    "<td><small>%s</small></td>\n</tr>\n",
			    imglist[x].flash);
		}
		if (imglist[x].model != NULL) {
			fprintf(html, "<tr>\n<td><small>Model:</small></td>"
			    "<td><small>%s</small></td>\n</tr>\n",
			    imglist[x].model);
		}
		
		fprintf(html, "</table>\n<p>\n<small>\n");
		
		if (offset == 1) {
			fprintf(html, "<a href=\"index.html\">"
			    "Back to thumbnails</a>\n");
		} else {
			fprintf(html, "<a href=\"index%d.html\">"
			    "Back to thumbnails</a>\n", offset);
		}
		
		fprintf(html, "</small>\n</td>\n<td width=\"33%%\" "
		    "align=\"center\" valign=\"top\">");
		
		/* Generate link with thumbnail to next image. */
		if (x + 1 < imgcount) {
			fprintf(html, "<a href=\"%s.html\"><img "
			    "src=\".thumbs/%s\" border=\"0\" width=\"%d\" "
			    "height=\"%d\" alt=\"%s\"><br>"
			    "<small>next image</small></a>",
			    imglist[x+1].filename,
			    imglist[x+1].filename,
			    imglist[x+1].thumbwidth,
			    imglist[x+1].thumbheight, 
			    imglist[x+1].filename);
		}
		
		fprintf(html, "</td>\n</tr>\n</table>\n%s</body></html>",
		    generated);
		
		if (fclose(html) == EOF) {
			fprintf(stderr, "%s: can't fclose(%s): %s\n", progname,
			    tmp, strerror(errno));
			exit(EXIT_FAILURE);
		}
		
		if (rename(tmp, final)) {
			fprintf(stderr, "%s: can't rename(%s, %s): %s\n",
			    progname, tmp, final, strerror(errno));
			exit(EXIT_FAILURE);
		}
		
		if (x % MAX_PER_PAGE == MAX_PER_PAGE-1)
			offset++;
	}
}

/*
 * Creates one or more thumbnail index html pages in the directory 
 * given in the parameter "dir" from the list of images contained
 * in the parameter "imglist", which has "imgcount" members.
 * Returns the number of thumbnail index pages created.
 */
int 
create_thumbindex(char *dir, struct imginfo *imglist, int imgcount)
{
	char *desc, final[MAXPATHLEN], tmp[MAXPATHLEN];
	int pages, offset, x, y;
	FILE *html;
	
	pages = (int) (imgcount / MAX_PER_PAGE);
	if (imgcount % MAX_PER_PAGE != 0)
		pages++;
	
	desc = albumdesc != NULL ? albumdesc : defaultdesc;
	
	/* offset defines which index page we are working on. */
	for (offset = 1; offset <= pages; offset++) {
		if (offset == 1)
			sprintf(final, "%s/index.html", dir);
		else
			sprintf(final, "%s/index%d.html", dir, offset);
		
		sprintf(tmp, "%s.tmp", final);
		/*
		 * x and y are indices into imglist. x is the image we start
		 * with on this page and y is the high limit for x. y is either
		 * imgcount or x + MAX_PER_PAGE, depending on how many images
		 * are left.
		 */
		x = (offset - 1) * MAX_PER_PAGE;
		y = (offset * MAX_PER_PAGE > imgcount)
		    ? imgcount
		    : x + MAX_PER_PAGE;
		
		if ((html = fopen(tmp, "w")) == NULL) {
			fprintf(stderr, "%s: can't fopen(%s): %s\n", progname,
			    tmp, strerror(errno));
			exit(EXIT_FAILURE);
		}
		
		fprintf(html, "<html>\n<head>\n<title>%s / %d</title>\n"
		    "<link rel=\"stylesheet\" type=\"text/css\" "
		    "href=\"../swiggle.css\" />\n"
		    "</head>\n<body bgcolor=\"#ffffff\">\n"
		    "<h1 align=\"center\">%s / %d</h1>\n"
		    "<table width=\"100%%\" border=\"0\">\n<tr>\n"
		    "<th width=\"33%%\"><small>\n", desc, offset, desc, offset);
		
		/*
		 * Generate "previous pictures" link depending on current
		 * offset.
		 */
		if (offset == 2)
			fprintf(html, "<a href=\"index.html\">"
			    "previous %d pictures</a>\n", MAX_PER_PAGE);
		else if (offset > 2)
			fprintf(html, "<a href=\"index%d.html\">"
			    "previous %d pictures</a>\n", offset - 1,
			    MAX_PER_PAGE);
		    
		fprintf(html, "</small></th>\n<th width=\"34%%\"><small>\n"
		    "pictures %d - %d of %d\n</small></th>\n"
		    "<th width=\"33%%\"><small>\n", x+1, y, imgcount);
		
		/* Generate "next pictures" link depending on images left. */
		if (offset * MAX_PER_PAGE < imgcount) {
			fprintf(html, "<a href=\"index%d.html\">next ",
			    offset + 1); 
			
			if (imgcount - offset * MAX_PER_PAGE == 1)
				fprintf(html, "picture</a>");
			else
				fprintf(html, "%d pictures</a>", 
				    ((offset + 1) * MAX_PER_PAGE > imgcount) 
				    ? imgcount - offset * MAX_PER_PAGE 
				    : MAX_PER_PAGE );
		}
		
		fprintf(html, "</small></th>\n</tr>\n</table>\n<p>\n"
		    "<table width=\"100%%\" border=\"0\">\n");
		
		/* Now generate an entry for every image on this page. */
		while (x < y) {
			if (x % cols == 0)
				fprintf(html, "<tr>\n");
			
			fprintf(html, "<td align=\"center\">\n"
			    "<a href=\"%s.html\"><img src=\".thumbs/%s\" "
			    "border=\"0\" alt=\"%s\" width=\"%d\" "
			    "height=\"%d\"></a><br>\n<small>%d x %d, %d KB"
			    "</small>\n<br><br>\n</td>\n", 
			    imglist[x].filename, 
			    imglist[x].filename, 
			    imglist[x].filename, 
			    imglist[x].thumbwidth, 
			    imglist[x].thumbheight,
			    imglist[x].width,
			    imglist[x].height,
			    (int)(imglist[x].filesize / 1024));
			
			if ((x % cols == cols - 1) || (x + 1 == imgcount))
				fprintf(html, "</tr>\n");
			
			x++;
		}
		
		fprintf(html, "</table>\n<p align=\"center\">\n"
		    "<a href=\"../index.html\">"
		    "<small>Back to gallery index</small></a>\n</p>\n%s</body>"
		    "\n</html>\n", generated);
		
		if (fclose(html) == EOF) {
			fprintf(stderr, "%s: can't fclose(%s): %s\n", progname,
			    tmp, strerror(errno));
			exit(EXIT_FAILURE);
		}
		
		if (rename(tmp, final)) {
			fprintf(stderr, "%s: can't rename(%s, %s): %s\n",
			    progname, tmp, final, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}
	
	return (offset - 1);
}
