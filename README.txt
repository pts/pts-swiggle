pts-swiggle: fast, command-line JPEG thumbnail generator
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
pts-swiggle is a fast, command-line JPEG thumbnail generator: it reads JPEG,
PNG and GIF images and generates the corresponding smaller (thumbnail) images.
It needs libjpeg and libpng.

To compile, install libjpeg (v62 and v8 do work), libpng (>= 1.2) and run the
shell script ./c.sh on Unix.

Example usage to generate thumbnails of at most 768 pixels high:

  pts-swiggle -H 768 .

pts-swiggle is written in C, and its source code is based on swiggle
(http://homepage.univie.ac.at/l.ertl/swiggle/).
README of the original swiggle-0.4:

-------------------------------------------------
swiggle - le's simple web image gallery generator
-------------------------------------------------
$Id: README,v 1.2 2003/01/10 19:27:42 le Exp $

Homepage: <http://mailbox.univie.ac.at/~le/swiggle/>

* About swiggle
---------------
swiggle is a small command line tool that generates HTML pages, including
thumbnail indexes, for given images (a so called "web gallery"). It's
intended to be easy to use, and since it is written in C, it's quite
speedy. It uses libjpeg for decompression and compression of images,
libexif for getting EXIF information contained in the images, and it
caches scaled images so that subsequent runs don't need to scale images
again and are faster. Of course, the original images aren't changed.

Currently, it only processes JPEG images, and it's thought to be used
primarily with images taken with digital cameras.

swiggle was developed and runs on FreeBSD. It may run on other Un*x
variants, too, but that wasn't tested yet.

swiggle is Open Source software.


* How to use it
---------------
As the name says it, swiggle is simple. It takes one argument on the
command line: the 'main gallery' directory, which in turn contains
subdirectories meant to be the 'albums'. It descends into every 'album
directory' and processes every JPEG file in there. It creates
thumbnails, scaled images and index pages for every album, and of course a
gallery index in the 'main gallery' directory.

There are several options that you can pass to swiggle to change the
way swiggle generates images and HTML pages. Just run swiggle without
any arguments to get the list of available options.


* Example usage
---------------
Start with an empty directory, for example ~/gallery. Now create one
or more subdirectories in ~/gallery and copy your desired images into
each of the subdirectories. You might end up with a directory structure
like this one:

  ~/
   |- gallery/
       |- album1/
       |   |- IMG_001.JPG
       |   |- IMG_002.JPG
       |   |- IMG_003.JPG
       |   | ....
       |- album2/
       |   |- IMG_011.JPG
       |   |- IMG_022.JPG
       |   |- IMG_033.JPG
       |   | ....
       |- album3/
           |- IMG_111.JPG
           |- IMG_221.JPG
           |- IMG_331.JPG
           | ....

Now just run "swiggle ~/gallery" and wait until it completes. That's
all: copy over the whole ~/gallery directory to your webspace and
enjoy :-). Don't delete ~/gallery if you intend to add or change
images later: you need to re-run swiggle, and it will take less time if
it can use the already created images and thumbnails.


* Image descriptions
--------------------
By default, swiggle displays just the filenames or directory names on
the HTML pages. If you want to give some or all images a specific
description, just create a file called '.description' in the directory
that contains the image. This file should contain one entry per line,
the first word of each record is the image filename (without directory
prefix) and the rest of the line is the desired description. An entry
for the filename '.' is treated specially as the description for the
current album.
