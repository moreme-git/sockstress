CC=gcc
CFLAGS=-O3 -g -march=native -Wall
LDFLAGS=-Lext/patricia_trie -lpcap  -lpatricia

PROGNAME=sockstress
