
#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include "bitset.h"

#include <mhash.h>

static int verbose = 0;

static const char * fstr(const char *fname, const char *error) {
	char * ret;
	asprintf(&ret, "%s:%s", fname, error);
	return strdup(ret); // leaks on purpose; it's before we exit() anyway
}

static void
fill_check_pages(
		uint8_t *map,
		bitfield_p zmap,
		uint64_t current,
		uint64_t count,
		uint64_t blksize)
{
	uint64_t * base = (uint64_t*)(map + (current * blksize));
	const int wordcount = blksize / sizeof(uint64_t);

	while (count--) {
		int is_zero = 1;

		if (is_zero) {
			uint64_t *cur = base;
			for (int z = 0; z < wordcount && is_zero; z++)
				is_zero = 0 == *cur++;
		}
		if (!is_zero)
			bit_set(zmap, current);
		base += wordcount;
		current++;
	}
}

static int
read_file_data_ranges(
		int fd,
		bitfield_p bmap,
		struct stat st)
{
	off_t off = 0;
	int whence = SEEK_DATA;

	while (off < st.st_size) {
		if (verbose > 1)
			printf("offset %llu\n", off);

		off_t res = lseek(fd, off, whence);
		if (res == -1) {
			// end of file is OK
			if (errno == ENXIO)
				return 0;
			fprintf(stderr, "lseek fails at %llu/%llu\n", off, st.st_size);
			return res;
		}
		if (res > off && verbose)
			printf("%s from %llu to %llu\n",
					whence == SEEK_DATA ? "hole" : "data", off, res);
		if (whence != SEEK_DATA) {
			if (res > off) {
				uint64_t from = off / st.st_blksize,
						cnt = ((res + st.st_blksize - 1) - off) / st.st_blksize;
				bit_set_range(bmap, from, cnt);
				if (verbose > 1)
					printf("set bits from %u cnt %u\n", from, cnt);
			}
		}
		whence = whence == SEEK_DATA ? SEEK_HOLE : SEEK_DATA;
		off = res;
	}
	return 0;
}

int main(int argc, const char *argv[])
{
	int dohash = 0;

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose")) {
				verbose++;
				continue;
			} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--hash")) {
				dohash++;
				continue;
			}

			fprintf(stderr, "%s: invalid argument: %s\n", argv[0], argv[i]);
			exit(1);
		}
		const char *fname = argv[i];
		struct stat st;
		if (stat(fname, &st)) {
			perror(fname);
			exit(1);
		}
		int fd = open(fname, O_RDONLY);
		if (fd == -1) {
			perror(fname);
			exit(1);
		}
		if (verbose)
			printf("%s is %u (%u times %u)\n", fname,
				st.st_size, st.st_blocks, st.st_blksize);

		/* number of used blocks for this file size, also number of bits in bitmaps */
		size_t maxblocks = (st.st_size + st.st_blksize -1) / st.st_blksize;

		/* this is the original bitmap made using the operating system */
		bitfield_p bmap = (bitfield_p)calloc(
				1 + (maxblocks / BITS_PER_WORD), BITFIELD_WORD_SIZE);

		if (read_file_data_ranges(fd, bmap, st)) {
			perror(fstr(fname, "read_file_data_ranges"));
			exit(1);
		}

		if (verbose > 3) {
			printf("original map: ");
			for (int z = 0; z <= (maxblocks / BITS_PER_WORD); z++)
				printf("%016llx ", bmap[z]);
			printf("\n");
		}

		uint8_t * map = mmap(NULL, maxblocks * st.st_blksize,
								PROT_READ, MAP_PRIVATE,
								fd, 0);
		if (map == MAP_FAILED) {
			perror(fname);
			exit(1);
		}
		/* This one bitmap will have a subset, or be a copy of the bmap one when
		 * we're done
		 */
		bitfield_p zmap = (bitfield_p)calloc(
				1 + (maxblocks / BITS_PER_WORD), BITFIELD_WORD_SIZE);
		{
			uint64_t current = 0, count;
			int which;

			while (current < maxblocks &&
					(count = bit_run_count(bmap, current, maxblocks - current, &which)) > 0) {
				if (verbose)
					printf("map is %s from %u to %u\n",
						which ? "set" : "zero",
						current * st.st_blksize,
						(current+count) * st.st_blksize);
				/* if bits were sets in the extents, double check them */
				if (which)
					fill_check_pages(map, zmap, current, count, st.st_blksize);

				current += count;
			}
		}
		if (verbose > 2) {
			printf("optimized map: ");
			for (int z = 0; z <= (maxblocks / BITS_PER_WORD); z++)
				printf("%016llx ", zmap[z]);
			printf("\n");
		}

		{
			bitfield_p b[2] = { bmap, zmap };
			uint64_t set[2] = {0};

			for (int bs = 0; bs < 2; bs++) {
				for (int z = 0; z <= (maxblocks / BITS_PER_WORD); z++)
					set[bs] += __builtin_popcountll(b[bs][z]);
			}

			printf("%s is %d%% sparse, saving %llu blocks (%lluMB)\n", fname,
					100 - ((set[0] * 100) / maxblocks),
					100 - (maxblocks - set[0]),
					((maxblocks - set[0]) * st.st_blksize) / (1024*1024));
			if (set[1] < set[0])
				printf("%s could be %d%% sparse, saving %llu blocks (%lluMB)\n", fname,
						100 - ((set[1] * 100) / maxblocks),
						100 - (maxblocks - set[1]),
						((maxblocks - set[1]) * st.st_blksize) / (1024*1024));
		}


		if (dohash) {
			const int hs = mhash_get_block_size(MHASH_MD5);
			struct {
				char h[hs + 1];
			} hash[3];
			MHASH td;

			if (verbose)
				printf("%s: calculating hashes, be patient\n", argv[i]);
			td = mhash_init(MHASH_MD5);
			mhash(td, map, maxblocks * st.st_blksize);
			mhash_deinit(td, hash[0].h);

			if (verbose)
				printf("%s: hash 1/3 done\n", argv[i]);

			{
				uint64_t current = 0, count;
				int which;

				td = mhash_init(MHASH_MD5);

				while (current < maxblocks &&
						(count = bit_run_count(bmap, current, maxblocks - current, &which)) > 0) {
					/* if bits were sets in the extents, double check them */
					if (which)
						mhash(td, map + (current * st.st_blksize),
								count * st.st_blksize);
					current += count;
				}
				mhash_deinit(td, hash[1].h);
			}
			if (verbose)
				printf("%s: hash 2/3 done\n", argv[i]);

			{
				uint64_t current = 0, count;
				int which;

				td = mhash_init(MHASH_MD5);

				while (current < maxblocks &&
						(count = bit_run_count(zmap, current, maxblocks - current, &which)) > 0) {
					/* if bits were sets in the extents, double check them */
					if (which)
						mhash(td, map + (current * st.st_blksize),
								count * st.st_blksize);
					current += count;
				}
				mhash_deinit(td, hash[2].h);
			}
			if (memcmp(hash[0].h, hash[1].h, hs) || memcmp(hash[0].h, hash[2].h, hs)) {
				fprintf(stderr, "%s: hash mismatch: ", argv[i]);
				for (int h = 0; h < 3; h++) {
					for (int hb = 0; hb < hs; hb++)
						fprintf(stderr, "%.2x", (uint8_t)hash[h].h[hb]);
					fprintf(stderr, " ");
				}
				fprintf(stderr, "\n");
			} else if (verbose) {
				fprintf(stdout, "%s: hash 3/3 match: ", argv[i]);
				for (int hb = 0; hb < hs; hb++)
					fprintf(stdout, "%.2x", (uint8_t)hash[0].h[hb]);
				fprintf(stdout, "\n");
			}
		}
		munmap(map, maxblocks * st.st_blksize);
		close(fd);
		free(bmap);
		free(zmap);
	}
	
	
}
