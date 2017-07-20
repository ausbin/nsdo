/* 
 * Copyright (c) 2015 Austin Adams
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <ftw.h>

#define PROGRAM "nsdo"
#define VERSION "1.0"
#define NS_PATH "/var/run/netns"
#define VERSION_FLAG "--version"
#define VERSION_FLAG_SHORT "-V"
#define ETC_PATH "/etc"
#define ETC_NETNS_PATH "/etc/netns"

enum {
    EXIT_OK,
    EXIT_BAD_INVOCATION,
    EXIT_ALREADY_IN_NAMESPACE,
    EXIT_COULDNT_SETGUID,
    EXIT_BAD_NETNS,
    EXIT_FAILED_EXEC,
    EXIT_FAILED_BIND_MOUNT
};

enum {
    ARG_PROGRAM,
    ARG_NETNS,
    ARG_CMD,
    ARG_MIN_ARGS
};

static void print_version() {
    puts(PROGRAM " version " VERSION);
}

static void print_usage() {
    fprintf(stderr, "usage: " PROGRAM " <namespace> <command> [args...]\n"
                    "       " PROGRAM " { " VERSION_FLAG " | " VERSION_FLAG_SHORT " }\n");
}

static int is_version_flag(char *arg) {
    return !strcmp(arg, VERSION_FLAG) || !strcmp(arg, VERSION_FLAG_SHORT);
}

static int current_ns_inode(ino_t *inode) {
    struct stat nsstat;

    if (stat("/proc/self/ns/net", &nsstat) == -1) {
        perror(PROGRAM ": stat(\"/proc/self/ns/net\")");
        return 0;
    }

    *inode = nsstat.st_ino;
    return 1;
}

/* return values:
   0: inode is not a namespace in the nspath
   1: it is
  -1: error encountered
 */
static int inode_in_nspath(ino_t inode) {
    DIR *nsdir;
    char *nspath;
    struct dirent *ns;
    struct stat nsstat;

    if ((nsdir = opendir(NS_PATH)) == NULL) {
        perror(PROGRAM ": opendir(\"" NS_PATH "\")");
        return -1;
    }

    while ((ns = readdir(nsdir)) != NULL) {
        if (strcmp(".", ns->d_name) == 0 || strcmp("..", ns->d_name) == 0)
            continue;

        if (asprintf(&nspath, "%s/%s", NS_PATH, ns->d_name) == -1) {
            perror(PROGRAM ": asprintf");
            return -1;
        }

        if (stat(nspath, &nsstat) == -1) {
            /* i hate to break consistency and use fprintf() rather than
               perror(), but it's necessary here. */
            fprintf(stderr, PROGRAM ": stat(\"%s\"): %s\n", nspath, strerror(errno));
            return -1;
        }

        free(nspath);

        if (nsstat.st_ino == inode)
            return 1;
    }

    if (errno != 0) {
        perror(PROGRAM ": readdir(\"" NS_PATH "\")");
        return -1;
    }

    closedir(nsdir);

    return 0;
}

static int already_in_namespace() {
    int status;
    ino_t inode;

    if (!current_ns_inode(&inode))
        return 1;

    if ((status = inode_in_nspath(inode)) == 0) {
        return 0;
    } else {
        if (status == 1)
            fprintf(stderr, PROGRAM ": oops! i can run only in network namespaces not found in " NS_PATH "\n");

        return 1;
    }
}

static int bad_nsname(char *ns) {
    return strlen(ns) == 0 || strcmp("..", ns) == 0 || strcmp(".", ns) == 0 || strchr(ns, '/') != NULL;
}

static int set_netns(char *ns) {
    int nsfd, perm_issue;
    char *nspath;

    if (bad_nsname(ns)) {
        fprintf(stderr, PROGRAM ": namespace names can't contain '/', be empty, or be '.' or '..'.\n");
        return 0;
    }

    if (asprintf(&nspath, "%s/%s", NS_PATH, ns) == -1) {
        perror(PROGRAM ": asprintf");
        return 0;
    }

    if ((nsfd = open(nspath, O_RDONLY)) == -1) {
        fprintf(stderr, PROGRAM ": open(\"%s\"): %s\n", nspath, strerror(errno));
        free(nspath);
        return 0;
    }

    free(nspath);

    if (setns(nsfd, CLONE_NEWNET) == -1) {
;    } else {
        return 1;
    }
}

static int bind_mount_file(const char *fn, const struct stat *fstat, int flags, struct FTW *ftw) {
    char newfn[PATH_MAX + 1];
    const char *relfn;
    int i;

    /* For files only */
    if (flags == FTW_F) {
        relfn = fn;
        /* Move past "/etc/netns/<ns>/" by skipping 4 slashes. */
        for (i = 0;  i < 4; i++) {
            relfn = strstr(relfn, "/");
            if (!relfn) {
                fprintf(stderr, PROGRAM ": bind mount pathname was mangled\n");
                return 1;
            }
            relfn++;
        }

        if (snprintf(newfn, PATH_MAX, ETC_PATH "/%s", relfn) >= PATH_MAX) {
            fprintf(stderr, PROGRAM ": bind mount pathname was too long\n");
            return 1;
        }

        if (mount(fn, newfn, NULL, MS_BIND | MS_PRIVATE, NULL) == -1) {
            perror(PROGRAM ": mount");
            return 0;
        }
    }

    return 0;
}

/* Bind mount every file in /etc/netns/<ns> to equivalent in /etc. */
int bind_mount_etc(char *ns) {
    char bind_path[PATH_MAX + 1];
    DIR *dir;

    if (snprintf(bind_path, PATH_MAX, ETC_NETNS_PATH "/%s", ns) >= PATH_MAX) {
        fprintf(stderr, PROGRAM ": bind mount pathname was too long\n");
        return 0;
    }

    /* If bind path is not there, ignore and return success. */
    if (!(dir = opendir(bind_path))) {
        return 1;
    }
    closedir(dir);

    /* Set up separate mount namespace. */
    if (unshare(CLONE_NEWNS) == -1) {
        perror(PROGRAM ": unshare");
        return 0;
    }

    /* Remount everything under / in our name space as private. */
    if (mount("none", "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1) {
        perror(PROGRAM ": mount");
        return 0;
    }

    /* Walk the /etc/netns/<ns> tree. */
    if (nftw(bind_path, bind_mount_file, 20, FTW_PHYS)) {
        perror(PROGRAM ": nftw");
        return 0;
    }

    return 1;
}

/* set euid+egid to the real uid+gid */
static int deescalate() {
    if (setuid(getuid()) == -1 || setgid(getgid()) == -1) {
        perror(PROGRAM ": set[gu]id");
        return 0;
    } else {
        return 1;
    }
}

static int run(char *cmd, char **argv) {
    if (execvp(cmd, argv) == -1) {
        perror(PROGRAM ": execvp");
        return 0;
    } else {
        return 1;
    }
}

int main(int argc, char **argv) {
    if (argc-1 == 1 && is_version_flag(argv[1])) {
        print_version();
        return EXIT_OK;
    } else if (argc < ARG_MIN_ARGS) {
        print_usage();
        return EXIT_BAD_INVOCATION;
    } 

    if (already_in_namespace())
        return EXIT_ALREADY_IN_NAMESPACE;

    if (!set_netns(argv[ARG_NETNS]))
        return EXIT_BAD_NETNS;

    if (!bind_mount_etc(argv[ARG_NETNS]))
        return EXIT_FAILED_BIND_MOUNT;

    if (!deescalate())
        return EXIT_COULDNT_SETGUID;

    if (!run(argv[ARG_CMD], argv+ARG_CMD))
        return EXIT_FAILED_EXEC;

    return EXIT_OK;
}

