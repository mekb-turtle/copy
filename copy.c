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
int copydata(FILE *src, FILE *dest, size_t block) {
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
int copy(char *src, char *dest, bool verbose, bool recurse, bool fork_recurse) {
	printf("Copying %s to %s...\n", src, dest);
	struct stat *stat = lstat_(src, 1);
	struct stat *dstat = lstat_(dest, 0);
	struct dirent *dir;
	DIR *d;
	int res = 0;
	if (!stat) {
		if (dstat) free(dstat);
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
	if (dstat && (dstat->st_mode & S_IFDIR) && !(stat->st_mode & S_IFDIR)) {
		eprintf("Destination %s is directory\n", dest);
		free(dstat);
		return 1;
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
			d = opendir(src);
			if (!d) {
				eprintf("opendir: %s: %s\n", src, strerr);
				free(stat); if (dstat) free(dstat);
				return 1;
			}
			if (verbose) printf("Recursing into directory: %s\n", dest);
			for (;;) {
				errno = 0;
				dir = readdir(d);
				if (errno) {
					eprintf("readdir: %s: %s\n", src, strerr);
					free(stat); if (dstat) free(dstat);
					return 1;
				}
				if (!dir) break;
				if (dir->d_name[0] == '.' && (dir->d_name[1] == '\0' || (dir->d_name[1] == '.' && dir->d_name[2] == '\0'))) continue;
				char *nsrcf  = malloc(strlen(src) +strlen(dir->d_name)+8);
				if (!nsrcf) {
					eprintf("malloc: %s\n", strerr);
					free(stat); if (dstat) free(dstat);
					return 1;
				}
				char *ndestf = malloc(strlen(dest)+strlen(dir->d_name)+8);
				if (!ndestf) {
					free(nsrcf);
					eprintf("malloc: %s\n", strerr);
					free(stat); if (dstat) free(dstat);
					return 1;
				}
				sprintf(nsrcf,  "%s/%s", src,  dir->d_name);
				sprintf(ndestf, "%s/%s", dest, dir->d_name);
				if (fork_recurse) {
					pid_t f = fork();
					if (f < 0) {
						eprintf("fork: %s\n", strerr);
						free(stat); if (dstat) free(dstat);
						return 1;
					} else if (f == 0) {
						printf("new fork: %i\n", getpid());
						copy(nsrcf, ndestf, verbose, recurse, 1);
						exit(0);
						return 0;
					}
				} else {
					copy(nsrcf, ndestf, verbose, recurse, 0);
				}
			}
		}
	} else if (stat->st_mode & S_IFIFO) {
		if (verbose) printf("Creating FIFO: %s\n", dest);
		if (mkfifo(dest, stat->st_mode) != 0) {
			eprintf("mkfifo: %s: %s\n", dest, strerr);
			free(stat); if (dstat) free(dstat);
			return 1;
		}
	} else if (stat->st_mode & S_IFLNK) {
		if (verbose) printf("Creating symlink: %s\n", dest);
		char *symdata = malloc(PATH_MAX);
		if (!symdata) {
			eprintf("malloc: %s\n", strerr);
			free(stat); if (dstat) free(dstat);
			return 1;
		}
		ssize_t len = readlink(src, symdata, PATH_MAX-1);
		if (len < 0 || len >= PATH_MAX-2) {
			if (len < 0) {
				eprintf("readlink: %s: %s\n", src, strerr);
			} else {
				eprintf("data of symlink %s is too big\n", src);
			}
			free(symdata); free(stat); if (dstat) free(dstat);
			return 1;
		}
		symdata[len] = 0;
		if (symlink(symdata, dest) != 0) {
			eprintf("symlink: %s: %s\n", dest, strerr);
			free(symdata); free(stat); if (dstat) free(dstat);
			return 1;
		}
		free(symdata);
	} else if (stat->st_mode & S_IFSOCK) {
		eprintf("Don't know how to copy socket %s to %s\n", src, dest);
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
Usage: %s [src] [dest]\n\
	-v --verbose   : extra information\n\
	-r --recursive : recurse down directories\n\
	-f --fork      : fork() when recursing\n", argv0);
	return 2;
}
int main(int argc, char *argv[]) {
#define INVALID return usage(argv[0])
	bool flag_done = 0;
	bool verbose = 0;
	bool recursive = 0;
	bool fork_recursive = 0;
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
			if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--fork") == 0) {
				if (fork_recursive) INVALID;
				fork_recursive = 1;
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
	if (fork_recursive && !recursive) INVALID;
	if (!src || !dest || !src[0] || !dest[0]) INVALID;
	int r = copy(src, dest, verbose, recursive, fork_recursive);
	return r;
}
