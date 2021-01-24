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

#define PROGRAM "nsdo"
#define VERSION "1.0"
#define NETNS_PATH "/var/run/netns"
#define MOUNTNS_PATH "/var/run/mountns"
#define VERSION_FLAG "--version"
#define VERSION_FLAG_SHORT "-V"

enum {
    EXIT_OK,
    EXIT_BAD_INVOCATION,
    EXIT_ALREADY_IN_NAMESPACE,
    EXIT_COULDNT_SETGUID,
    EXIT_BAD_NETNS,
    EXIT_COULDNT_GETCWD,
    EXIT_COULDNT_CHDIR,
    EXIT_FAILED_EXEC,
    EXIT_MOUNTNS_FAIL
};

enum {
    ARG_PROGRAM,
    ARG_NETNS,
    ARG_CMD,
    ARG_MIN_ARGS
};

static void print_version(void) {
    puts(PROGRAM " version " VERSION);
}

static void print_usage(void) {
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

    if ((nsdir = opendir(NETNS_PATH)) == NULL) {
        perror(PROGRAM ": opendir(\"" NETNS_PATH "\")");
        return -1;
    }

    while ((ns = readdir(nsdir)) != NULL) {
        if (strcmp(".", ns->d_name) == 0 || strcmp("..", ns->d_name) == 0)
            continue;

        if (asprintf(&nspath, "%s/%s", NETNS_PATH, ns->d_name) == -1) {
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
        perror(PROGRAM ": readdir(\"" NETNS_PATH "\")");
        return -1;
    }

    closedir(nsdir);

    return 0;
}

static int already_in_namespace(void) {
    int status;
    ino_t inode;

    if (!current_ns_inode(&inode))
        return 1;

    if ((status = inode_in_nspath(inode)) == 0) {
        return 0;
    } else {
        if (status == 1)
            fprintf(stderr, PROGRAM ": oops! i can run only in network namespaces not found in " NETNS_PATH "\n");

        return 1;
    }
}

static int bad_nsname(char *ns) {
    return strlen(ns) == 0 || strcmp("..", ns) == 0 || strcmp(".", ns) == 0 || strchr(ns, '/') != NULL;
}

static int lookup_and_setns(char *ns, char *nsdir, int nstype, int allow_fail) {
    int nsfd, perm_issue;
    char *nspath;

    if (bad_nsname(ns)) {
        fprintf(stderr, PROGRAM ": namespace names can't contain '/', be empty, or be '.' or '..'.\n");
        return 0;
    }

    if (asprintf(&nspath, "%s/%s", nsdir, ns) == -1) {
        perror(PROGRAM ": asprintf");
        return 0;
    }

    if ((nsfd = open(nspath, O_RDONLY | O_CLOEXEC)) == -1) {
        if (errno == ENOENT && allow_fail) {
            return 1;
        } else {
            fprintf(stderr, PROGRAM ": open(\"%s\"): %s\n", nspath, strerror(errno));
            free(nspath);
            return 0;
        }
    }

    free(nspath);

    if (setns(nsfd, nstype) == -1) {
        perm_issue = errno == EPERM;
        perror(PROGRAM ": setns");

        if (perm_issue)
            fprintf(stderr, "\nis the " PROGRAM " binary missing the setuid bit?\n");

        return 0;
    } else {
        return 1;
    }
}

/* set euid+egid to the real uid+gid */
static int deescalate(void) {
    if (setgid(getgid()) == -1 || setuid(getuid()) == -1) {
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
    char *old_cwd;

    if (argc-1 == 1 && is_version_flag(argv[1])) {
        print_version();
        return EXIT_OK;
    } else if (argc < ARG_MIN_ARGS) {
        print_usage();
        return EXIT_BAD_INVOCATION;
    } 

    if (already_in_namespace())
        return EXIT_ALREADY_IN_NAMESPACE;

    /* Change into network namespace */
    if (!lookup_and_setns(argv[ARG_NETNS], NETNS_PATH, CLONE_NEWNET, 0))
        return EXIT_BAD_NETNS;

    /* This is a GNU extension that I use shamelessly.
       When we setns() to a different mount namespace, the cwd gets
       reset to /. So save it here and chdir() to it after setns()ing.*/
    old_cwd = getcwd(NULL, 0);

    if (!old_cwd) {
        perror("getcwd");
        return EXIT_COULDNT_GETCWD;
    }

    /* Change into mount namespace if it exists in /var/run/mountns */
    if (!lookup_and_setns(argv[ARG_NETNS], MOUNTNS_PATH, CLONE_NEWNS, 1))
        return EXIT_BAD_NETNS;

    if (chdir(old_cwd) == -1) {
        perror("chdir");
        free(old_cwd);
        return EXIT_COULDNT_CHDIR;
    }

    free(old_cwd);

    if (!deescalate())
        return EXIT_COULDNT_SETGUID;

    if (!run(argv[ARG_CMD], argv+ARG_CMD))
        return EXIT_FAILED_EXEC;

    return EXIT_OK;
}
