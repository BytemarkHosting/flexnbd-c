
CPPFLAGS = -Wall -I../src/server -I../src/common -I../src/proxy \
			-Doff64_t=__off64_t \
			-Wunused-const-variable=0 -Wformat=0
		
CFLAGS = $(CPPFLAGS) -g -O3 -std=gnu99

all: holemap

# holemap requires libmhash-dev for md5 calculation
holemap: holemap.c ../src/server/*.h
	$(CC) $(CFLAGS) -o $@ $^ ${shell pkg-config mhash --cflags --libs}

clean:
	rm -f holemap
