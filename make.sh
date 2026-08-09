#!/bin/bash
gcc -o lyntersh lyntersh.c $(pkg-config --cflags --libs xft fontconfig freetype2) -lX11 -lpng -lm -O2
