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
LDFLAGS_EXTRA += -Wl,--relax,--gc-sections

TOOLCHAIN		:= $(shell $(CC) --version|awk '/Debian/ {print "debian";exit;}')
#
# This bit adds extra flags depending of the distro, and the
# architecture. To make sure debian packages have the right
# set of 'native' flags on them
#
ifeq ($(TOOLCHAIN),debian)
DEBARCH			:= $(shell dpkg-architecture -qDEB_BUILD_ARCH)
ifeq ($(DEBARCH),$(filter $(DEBARCH),amd64 i386))
CFLAGS_EXTRA	+= -march=native
endif
ifeq ($(DEBARCH),armhf)
CFLAGS_EXTRA	+=  -march=armv7-a -mtune=cortex-a8 -mfpu=neon
endif
LDFLAGS_EXTRA	+= -L$(LIB) -Wl,-rpath,${shell readlink -f ${LIB}}
else
LDFLAGS_EXTRA	+= -L$(LIB) -Wl,-rpath-link,$(LIB)
endif


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
COMPILE=$(CC) $(INC) -c $(CCFLAGS)
SAVEDEP=$(CC) $(INC) -MM $(CCFLAGS)
LINK=$(CC) $(LLDFLAGS) -Isrc $(LIBS)

LIB=build/

EXISTING_OBJS := $(wildcard build/*.o)
-include $(EXISTING_OBJS:.o=.d)

COMMON_SRC  := $(wildcard src/common/*.c)
SERVER_SRC := $(wildcard src/server/*.c)
PROXY_SRC   := $(wildcard src/proxy/*.c)

COMMON_OBJ  := $(COMMON_SRC:src/%.c=build/%.o)
SERVER_OBJ := $(SERVER_SRC:src/%.c=build/%.o)
PROXY_OBJ   := $(PROXY_SRC:src/%.c=build/%.o)

SRCS := $(COMMON_SRC) $(SERVER_SRC) $(PROXY_SRC)
OBJS := $(COMMON_OBJ) $(SERVER_OBJ) $(PROXY_OBJ)


all: build/flexnbd build/flexnbd-proxy doc

build/%.o: %.c
	mkdir -p $(dir $@)
	$(COMPILE) $< -o $@
	$(SAVEDEP) $< > build/$*.d

objs: $(OBJS)

build/flexnbd: $(COMMON_OBJ) $(SERVER_OBJ) build/main.o
	$(LINK) $^ -o $@

build/flexnbd-proxy: $(COMMON_OBJ) $(PROXY_OBJ) build/proxy-main.o
	$(LINK) $^ -o $@

server: build/flexnbd
proxy: build/flexnbd-proxy


CHECK_SRC := $(wildcard tests/unit/*.c)
CHECK_OBJ := $(CHECK_SRC:tests/unit/%.c=build/tests/%.o)
# Why can't we reuse the build/%.o rule above? Not sure.
build/tests/%.o: tests/unit/%.c
	mkdir -p $(dir $@)
	$(COMPILE) $< -o $@
	$(SAVEDEP) $< > build/tests/$*.d

CHECK_BINS := $(CHECK_OBJ:build/tests/%.o=build/tests/%)
build/tests/%: build/tests/%.o $(OBJS)
	$(LINK) $^ -o $@ -lcheck

check_objs: $(CHECK_OBJ)

check_bins: $(CHECK_BINS)
check: $(CHECK_BINS)
	for bin in $^; do $$bin; done

build/flexnbd.1: README.txt
	a2x --destination-dir build --format manpage $<
build/flexnbd-proxy.1: README.proxy.txt
	a2x --destination-dir build --format manpage $<
# If we don't pipe to file, gzip clobbers the original, causing make
# to rebuild each time
%.1.gz: %.1
	gzip -c -f $< > $@


server-man: build/flexnbd.1.gz
proxy-man: build/flexnbd-proxy.1.gz

doc: server-man proxy-man

install:
	mkdir -p $(INSTALLDIR)
	cp build/flexnbd build/flexnbd-proxy $(INSTALLDIR)

clean:
	rm -rf build/*


.PHONY: clean objs check_objs all server proxy check_bins check server-man proxy-man doc
