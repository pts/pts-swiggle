#! /bin/sh --
# Dependencies on Ubuntu Lucid:
#   sudo apt-get install gcc libc6-dev libjpeg62-dev
# Dependencies on Ubuntu Trusty:
#   sudo apt-get install gcc libc6-dev libjpeg8-dev
set -ex
gcc -s -O3 -W -Wall -Wextra -Werror \
    -o pts-swiggle \
    pts-swiggle.c cgif.c \
    -ljpeg -lpng -lm \
;
ls -l pts-swiggle
