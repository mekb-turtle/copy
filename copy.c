#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <stdbool.h>
#include <dirent.h>
#define BLOCK 1024
#define eprintf(...) fprintf(stderr, __VA_ARGS__)
#define strerr strerror(errno)
uint8_t copyfiledata(FILE *src, FILE *dest, size_t block) {
	uint8_t* data = malloc(block);
	if (!data) {
		eprintf("malloc: %s\n", strerr);
		return 1;
	}
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
struct stat *lstat_(const char *name, bool err) {
	struct stat *s = malloc(sizeof(struct stat));
	if (!name) {
		eprintf("malloc: %s\n", strerr);
		return NULL;
	}
	if (lstat(name, s) == 0) return s;
	free(s);
	if (err) eprintf("lstat: %s: %s\n", name, strerr);
	return NULL;
}
struct copydata {
	char *src;
	char *dest;
	bool verbose, recurse;
};
uint8_t copy(struct copydata *copydata) {
	printf("Copying %s to %s...\n", copydata->src, copydata->dest);
	struct stat *stat = lstat_(copydata->src, 1);
	struct stat *dstat = lstat_(copydata->dest, 0);
	struct dirent *dir;
	mode_t filetype = stat->st_mode & S_IFMT;
	DIR *d;
	int res = 0;
	if (!stat) {
		if (dstat) free(dstat);
		return 1;
	}
	if (dstat) {
		mode_t dfiletype = dstat->st_mode & S_IFMT;
		if (dfiletype != S_IFDIR && !(filetype == S_IFREG && filetype == S_IFREG)) {
			if (copydata->verbose) printf("Removing already existing file: %s\n", copydata->dest);
			if (unlink(copydata->dest) != 0) {
				eprintf("unlink: %s: %s\n", copydata->dest, strerr);
				free(dstat);
				return 1;
			}
		}
		if (dfiletype == S_IFDIR && filetype != S_IFDIR) {
			eprintf("Destination %s is directory\n", copydata->dest);
			free(dstat);
			return 1;
		}
	}
	if (filetype == S_IFDIR) {
		if (!(dstat && (dstat->st_mode & S_IFMT) == S_IFDIR)) {
			if (copydata->verbose) printf("Creating directory: %s\n", copydata->dest);
			if (mkdir(copydata->dest, stat->st_mode) != 0) {
				eprintf("mkdir: %s: %s\n", copydata->dest, strerr);
				free(stat); if (dstat) free(dstat);
				return 1;
			}
		}
		if (copydata->recurse) {
			d = opendir(copydata->src);
			if (!d) {
				eprintf("opendir: %s: %s\n", copydata->src, strerr);
				free(stat); if (dstat) free(dstat);
				return 1;
			}
			if (copydata->verbose) printf("Recursing into directory: %s\n", copydata->dest);
			for (;;) {
				errno = 0;
				dir = readdir(d);
				if (errno) {
					eprintf("readdir: %s: %s\n", copydata->src, strerr);
					free(stat); if (dstat) free(dstat);
					return 1;
				}
				if (!dir) break;
				if (dir->d_name[0] == '.' && (dir->d_name[1] == '\0' || (dir->d_name[1] == '.' && dir->d_name[2] == '\0'))) continue;
				char *nsrcf  = malloc(strlen(copydata->src) +strlen(dir->d_name)+8);
				if (!nsrcf) {
					eprintf("malloc: %s\n", strerr);
					free(stat); if (dstat) free(dstat);
					return 1;
				}
				char *ndestf = malloc(strlen(copydata->dest)+strlen(dir->d_name)+8);
				if (!ndestf) {
					free(nsrcf);
					eprintf("malloc: %s\n", strerr);
					free(stat); if (dstat) free(dstat);
					return 1;
				}
				sprintf(nsrcf,  "%s/%s", copydata->src,  dir->d_name);
				sprintf(ndestf, "%s/%s", copydata->dest, dir->d_name);
				struct copydata newcopydata = { .src = nsrcf, .dest = ndestf, .verbose = copydata->verbose, .recurse = 1 };
				copy(&newcopydata);
			}
		}
	} else if (filetype == S_IFIFO) {
		if (copydata->verbose) printf("Creating FIFO: %s\n", copydata->dest);
		if (mkfifo(copydata->dest, stat->st_mode) != 0) {
			eprintf("mkfifo: %s: %s\n", copydata->dest, strerr);
			free(stat); if (dstat) free(dstat);
			return 1;
		}
	} else if (filetype == S_IFLNK) {
		if (copydata->verbose) printf("Creating symlink: %s\n", copydata->dest);
		char *symdata = malloc(PATH_MAX);
		if (!symdata) {
			eprintf("malloc: %s\n", strerr);
			free(stat); if (dstat) free(dstat);
			return 1;
		}
		ssize_t len = readlink(copydata->src, symdata, PATH_MAX-1);
		if (len < 0 || len >= PATH_MAX-2) {
			if (len < 0) {
				eprintf("readlink: %s: %s\n", copydata->src, strerr);
			} else {
				eprintf("data of symlink %s is too big\n", copydata->src);
			}
			free(symdata); free(stat); if (dstat) free(dstat);
			return 1;
		}
		symdata[len] = 0;
		if (symlink(symdata, copydata->dest) != 0) {
			eprintf("symlink: %s: %s\n", copydata->dest, strerr);
			free(symdata); free(stat); if (dstat) free(dstat);
			return 1;
		}
		free(symdata);
	} else if (filetype == S_IFSOCK) {
		eprintf("Don't know how to copy socket %s to %s\n", copydata->src, copydata->dest);
	} else if (filetype == S_IFCHR || filetype == S_IFBLK) {
		if (copydata->verbose) printf("Creating %sdevice: %s\n", filetype == S_IFBLK ? "block " : filetype == S_IFCHR ? "character " : "", copydata->dest);
		if (mknod(copydata->dest, stat->st_mode, stat->st_dev) != 0) {
			eprintf("mknod: %s: %s\n", copydata->dest, strerr);
			free(stat); if (dstat) free(dstat);
			return 1;
		}
	} else if (filetype == S_IFREG) {
		FILE *f_src = fopen(copydata->src, "rb");
		if (!f_src) {
			eprintf("fopen: %s: %s\n", copydata->src, strerr);
			free(stat); if (dstat) free(dstat);
			return 1;
		}
		FILE *f_dest = fopen(copydata->dest, "wb");
		if (!f_dest) {
			fclose(f_src);
			eprintf("fopen: %s: %s\n", copydata->dest, strerr);
			free(stat); if (dstat) free(dstat);
			return 1;
		}
		res = copyfiledata(f_src, f_dest, BLOCK);
		fclose(f_src);
		fclose(f_dest);
	}
	/*
	if (fchmod(fileno(f_dest), stat->st_mode) != 0) {
		eprintf("fchmod: %s: %s\n", copydata->dest, strerr);
		free(stat); if (dstat) free(dstat);
		return 1;
	}
	*/
	free(stat); if (dstat) free(dstat);
	return res;
}
int usage(char *argv0) {
	eprintf("\
Usage: %s [src] [dest]\n\
	-v --verbose   : extra information\n\
	-r --recursive : recurse down directories\n\
	-t --threads   : use threads when recursing\n", argv0);
	return 2;
}
int main(int argc, char *argv[]) {
#define INVALID return usage(argv[0])
	bool flag_done = 0;
	bool verbose = 0;
	bool recursive = 0;
	bool threads = 0;
	char *src = NULL;
	char *dest = NULL;
	for (int i = 1; i < argc; ++i) {
		if (argv[i][0] == '-' && argv[i][1] != '\0' && !flag_done) {
			if (argv[i][1] == '-' && argv[i][2] == '\0') flag_done = 1; else // -- denotes end of flags
			if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
				if (verbose) INVALID;
				verbose = 1;
			} else
			if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--recursive") == 0) {
				if (recursive) INVALID;
				recursive = 1;
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
	if (threads && !recursive) INVALID;
	if (!src || !dest || !src[0] || !dest[0]) INVALID;
	struct copydata copydata = { .src = src, .dest = dest, .verbose = verbose, .recurse = recursive };
	return copy(&copydata);
}
