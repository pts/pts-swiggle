#! /bin/sh --
# Dependencies on Ubuntu Lucid:
#   sudo apt-get install gcc libc6-dev libexif-dev libjpeg62-dev
# Dependencies on Ubuntu Trusty:
#   sudo apt-get install gcc libc6-dev libexif-dev libjpeg8-dev
set -ex
gcc -s -O3 -W -Wall -Wextra -Werror \
    -o pts-swiggle \
    swiggle.c \
    -ljpeg -lexif \
;
ls -l pts-swiggle

