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

#define PROGRAM "nsdo"
#define VERSION "1.0"
#define NS_PATH "/var/run/netns"
#define VERSION_FLAG "--version"
#define VERSION_FLAG_SHORT "-V"

enum {
    EXIT_OK,
    EXIT_BAD_INVOCATION,
    EXIT_ALREADY_IN_NAMESPACE,
    EXIT_COULDNT_SETGUID,
    EXIT_BAD_NETNS,
    EXIT_FAILED_EXEC
};

enum {
    ARG_PROGRAM,
    ARG_NETNS,
    ARG_CMD,
    ARG_MIN_ARGS
};

void print_version() {
    puts(PROGRAM " version " VERSION);
}

void print_usage() {
    fprintf(stderr, "usage: " PROGRAM " <namespace> <command> [args...]\n"
                    "       " PROGRAM " { " VERSION_FLAG " | " VERSION_FLAG_SHORT " }\n");
}

int is_version_flag(char *arg) {
    return !strcmp(arg, VERSION_FLAG) || !strcmp(arg, VERSION_FLAG_SHORT);
}

int current_ns_inode(ino_t *inode) {
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
int inode_in_nspath(ino_t inode) {
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

int already_in_namespace() {
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

int bad_nsname(char *ns) {
    return strlen(ns) == 0 || strcmp("..", ns) == 0 || strcmp(".", ns) == 0 || strchr(ns, '/') != NULL;
}

int set_netns(char *ns) {
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

    if ((nsfd = open(nspath, O_RDONLY | O_CLOEXEC)) == -1) {
        fprintf(stderr, PROGRAM ": open(\"%s\"): %s\n", nspath, strerror(errno));
        return 0;
    }

    free(nspath);

    if (setns(nsfd, CLONE_NEWNET) == -1) {
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
int deescalate() {
    if (setuid(getuid()) == -1 || setgid(getgid()) == -1) {
        perror(PROGRAM ": set[gu]id");
        return 0;
    } else {
        return 1;
    }
}

int run(char *cmd, char **argv) {
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

    if (!deescalate())
        return EXIT_COULDNT_SETGUID;

    if (!run(argv[ARG_CMD], argv+ARG_CMD))
        return EXIT_FAILED_EXEC;

    return EXIT_OK;
}
