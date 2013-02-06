#!/usr/bin/make -f

all:
	rake build

all-debug:
	DEBUG=1 rake build

clean:
	rake clean
