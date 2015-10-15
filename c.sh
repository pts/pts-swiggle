#! /bin/sh --
# Dependencies on Ubuntu Lucid:
#   sudo apt-get install gcc libc6-dev libexif-dev libjpeg62-dev
# Dependencies on Ubuntu Trusty:
#   sudo apt-get install gcc libc6-dev libexif-dev libjpeg8-dev
set -ex
gcc -s -O3 -Wall -ljpeg -lexif -o swiggle swiggle.c resize.c html.c
ls -l swiggle

