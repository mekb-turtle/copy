#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>
#define BLOCK 1024
#define eprintf(...) fprintf(stderr, __VA_ARGS__)
#define strerr strerror(errno)
int copydata(FILE *src, FILE *dest, size_t block) {
	uint8_t* data = malloc(block);
	size_t len;
	while (1) {
		if (feof(src)) break;
		if (ferror(src)) {
			eprintf("Error input\n");
			return 1;
		}
		if (ferror(dest)) {
			eprintf("Error output\n");
			return 1;
		}
		len = fread(data, 1, block, src);
		fwrite(data, 1, len, dest);
	}
	return 0;
}
struct stat *lstat_(const char *name) {
	struct stat *s = malloc(sizeof(struct stat));
	if (lstat(name, s) == 0) return s;
	free(s);
	return NULL;
}
int copy(char *src, char *dest, bool verbose, bool recurse) {
	struct stat *stat = lstat_(src);
	struct stat *dstat = lstat_(src);
	int res = 0;
	if (!stat) {
		eprintf("lstat: %s: %s\n", src, strerr);
		return 1;
	}
	if (dstat && !(dstat->st_mode & S_IFDIR) && !((stat->st_mode & S_IFREG) && (stat->st_mode & S_IFREG))) {
		if (verbose) printf("Removing already existing file: %s\n", dest);
		if (unlink(dest) != 0) {
			eprintf("unlink: %s: %s\n", dest, strerr);
			free(dstat);
			return 1;
		}
	}
	if (stat->st_mode & S_IFDIR) {
		if (!(dstat && (dstat->st_mode & S_IFDIR))) {
			if (verbose) printf("Creating directory: %s\n", dest);
			if (mkdir(dest, stat->st_mode) != 0) {
				eprintf("mkdir: %s: %s\n", dest, strerr);
				free(stat); if (dstat) free(dstat);
				return 1;
			}
		}
		if (recurse) {
			if (verbose) printf("Recursing into directory: %s\n", dest);
		}
	} else if (stat->st_mode & S_IFIFO) {
		if (verbose) printf("Creating FIFO: %s\n", dest);
		if (mkfifo(dest, stat->st_mode) != 0) {
			eprintf("mkfifo: %s: %s\n", dest, strerr);
			free(stat); if (dstat) free(dstat);
			return 1;
		}
	} else if (stat->st_mode & (S_IFCHR | S_IFBLK)) {
		if (verbose) printf("Creating %sdevice: %s\n", stat->st_mode & S_IFBLK ? "block " : stat->st_mode & S_IFCHR ? "character " : "", dest);
		if (mknod(dest, stat->st_mode, stat->st_dev) != 0) {
			eprintf("mknod: %s: %s\n", dest, strerr);
			free(stat); if (dstat) free(dstat);
			return 1;
		}
	} else if (stat->st_mode & S_IFREG) {
		FILE *f_src = fopen(src, "rb");
		if (!f_src) {
			eprintf("fopen: %s: %s\n", src, strerr);
			free(stat); if (dstat) free(dstat);
			return 1;
		}
		FILE *f_dest = fopen(dest, "wb");
		if (!f_dest) {
			fclose(f_src);
			eprintf("fopen: %s: %s\n", dest, strerr);
			free(stat); if (dstat) free(dstat);
			return 1;
		}
		res = copydata(f_src, f_dest, BLOCK);
		fclose(f_src);
		fclose(f_dest);
	} else if (stat->st_mode & S_IFLNK) {

	} else if (stat->st_mode & S_IFSOCK) {

	}
	/*
	if (fchmod(fileno(f_dest), stat->st_mode) != 0) {
		eprintf("fchmod: %s: %s\n", dest, strerr);
		free(stat); if (dstat) free(dstat);
		return 1;
	}
	*/
	free(stat); if (dstat) free(dstat);
	return res;
}
int usage(char *argv0) {
	eprintf("\
Usage: %s [src] [dest]\n", argv0);
	return 2;
}
int main(int argc, char *argv[]) {
#define INVALID return usage(argv[0])
	bool flag_done = 0;
	bool verbose = 0;
	bool recurse = 0;
	char *src = NULL;
	char *dest = NULL;
	for (int i = 1; i < argc; ++i) {
		if (argv[i][0] == '-' && argv[i][1] != '\0' && !flag_done) {
			if (argv[i][1] == '-' && argv[i][2] == '\0') flag_done = 1; else // -- denotes end of flags
			if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
				if (verbose) INVALID;
				verbose = 1;
			} else
			if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--recurse") == 0) {
				if (recurse) INVALID;
				recurse = 1;
			} else
			INVALID;
		} else {
			if (src) {
				if (dest) {
					INVALID;
				} else {
					dest = argv[i];
				}
			} else {
				src = argv[i];
			}
		}
	}
	if (!src || !dest) INVALID;
	bool a = 0;
	for (size_t i = 0;; ++i) {
		if (dest[i] == '\0') a = 1;
		if (dest[i] == '\0' || dest[i] == '/') {
			dest[i] = '\0';
			struct stat *stat = lstat_(dest);
			if (!stat) {
				if (verbose) printf("Creating output directory: %s\n", dest);
				if (mkdir(dest, 0755) != 0) {
					eprintf("mkdir: %s: %s\n", dest, strerr);
					free(stat);
					return 1;
				}
			} else {
				if (!(stat->st_mode & S_IFDIR)) {
					eprintf("Output already exists: %s\n", dest);
					free(stat);
					return 1;
				}
			}
			if (a) break;
			dest[i] = '/';
		}
	}
	int r = copy(src, dest, verbose, recurse);
	return r;
}
