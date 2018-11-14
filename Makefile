#!/usr/bin/make -f

VPATH=src:tests/unit
DESTDIR?=/
PREFIX?=/usr/local/bin
INSTALLDIR=$(DESTDIR)/$(PREFIX)

ifdef DEBUG
	CFLAGS_EXTRA=-g -DDEBUG
	LDFLAGS_EXTRA=-g
else
	CFLAGS_EXTRA=-O2
endif

CFLAGS_EXTRA  += -fPIC --std=gnu99
LDFLAGS_EXTRA += -Wl,--relax,--gc-sections -L$(LIB) -Wl,-rpath-link,$(LIB)

# The -Wunreachable-code warning is only implemented in clang, but it
# doesn't break anything for gcc to see it.
WARNINGS=-Wall \
				 -Wextra \
				 -Werror-implicit-function-declaration \
				 -Wstrict-prototypes \
				 -Wno-missing-field-initializers \
				 -Wunreachable-code

CCFLAGS=-D_GNU_SOURCE=1 $(WARNINGS) $(CFLAGS_EXTRA) $(CFLAGS)
LLDFLAGS=-lm -lrt -lev $(LDFLAGS_EXTRA) $(LDFLAGS)

CC?=gcc

LIBS=-lpthread
INC=-I/usr/include/libev -Isrc/common -Isrc/server -Isrc/proxy
COMPILE=$(CC) -MMD $(INC) -c $(CCFLAGS)
LINK=$(CC) $(LLDFLAGS) -Isrc $(LIBS)

LIB=build/

COMMON_SRC  := $(wildcard src/common/*.c)
SERVER_SRC := $(wildcard src/server/*.c)
PROXY_SRC   := $(wildcard src/proxy/*.c)

COMMON_OBJ  := $(COMMON_SRC:src/%.c=build/%.o)
SERVER_OBJ := $(SERVER_SRC:src/%.c=build/%.o)
PROXY_OBJ   := $(PROXY_SRC:src/%.c=build/%.o)

SRCS := $(COMMON_SRC) $(SERVER_SRC) $(PROXY_SRC)
OBJS := $(COMMON_OBJ) $(SERVER_OBJ) $(PROXY_OBJ)


all: build doc

build: server proxy

build/%.o: %.c
	mkdir -p $(dir $@)
	$(COMPILE) $< -o $@

objs: $(OBJS)

build/flexnbd: $(COMMON_OBJ) $(SERVER_OBJ) build/main.o
	$(LINK) $^ -o $@

build/flexnbd-proxy: $(COMMON_OBJ) $(PROXY_OBJ) build/proxy-main.o
	$(LINK) $^ -o $@

server: build/flexnbd

proxy: build/flexnbd-proxy

CHECK_SRC := $(wildcard tests/unit/*.c)
CHECK_OBJ := $(CHECK_SRC:tests/unit/%.c=build/%.o)
# Why can't we reuse the build/%.o rule above? Not sure.

CHECK_BINS := $(CHECK_SRC:tests/unit/%.c=build/%)

build/check_%: build/check_%.o
	$(LINK) $^ -o $@ $(COMMON_OBJ) $(SERVER_OBJ) -lcheck -lsubunit

check_objs: $(CHECK_OBJ)

check_bins: $(CHECK_BINS)

check: $(OBJS) $(CHECK_BINS)
	r=true ; for bin in $(CHECK_BINS); do $$bin || r=false; done ; $$r

acceptance: build
	cd tests/acceptance && RUBYOPT='-I.' ruby nbd_scenarios -v

test: check acceptance

build/flexnbd.1: README.txt
	txt2man -t flexnbd -s 1 $< > $@

build/flexnbd-proxy.1: README.proxy.txt
	txt2man -t flexnbd-proxy -s 1 $< > $@

# If we don't pipe to file, gzip clobbers the original, causing make
# to rebuild each time
%.1.gz: %.1
	gzip -c -f $< > $@

doc: build/flexnbd.1.gz build/flexnbd-proxy.1.gz

install:
	mkdir -p $(INSTALLDIR)
	cp build/flexnbd build/flexnbd-proxy $(INSTALLDIR)

clean:
	rm -rf build/*

.PHONY: clean objs check_objs all server proxy check_bins check doc build test acceptance

# Include extra dependencies at the end, NOT before 'all'
-include $(wildcard build/*.d)
