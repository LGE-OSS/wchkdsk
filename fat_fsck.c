/* SPDX-License-Identifier : GPL-2.0 */

/* code from https://github.com/hclee/exfat-tools/blob/for-webos-next/webos/exfatfsck.c */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <time.h>
#include <sys/sysmacros.h>

#include "version.h"
#include "fat_fsck.h"

#define FSCK_PROG	"dosfsck"
#define MAX_FSCK_ARGS	32

/**
 * EFSCK_EXIT_FAILURE		Unknown or errors left
 * EFSCK_EXIT_VOLUME_DIRTY	VolumeDirty flag in boot sector
 * EFSCK_EXIT_USER_CANCEL	Killed by signal or device is removed
 */
#define EFSCK_EXIT_SUCCESS		0
#define EFSCK_EXIT_FAILURE		1
#define EFSCK_EXIT_SYNTAX_ERROR		2
#define EFSCK_EXIT_NOT_FAT_VOLUME	3
#define EFSCK_EXIT_RO_DEVICE		23
#define EFSCK_EXIT_VOLUME_DIRTY		100
#define EFSCK_EXIT_USER_CANCEL		160
#define EFSCK_EXIT_TIMEOUT		161

#ifndef __unused
#define __unused	__attribute__((__unused__))
#endif

pid_t fsck_pid;
/* automatically repair with reclaiming file and immediately writting */
char *fsck_param[] = {FSCK_PROG, "-afw", "", NULL};

static void usage(char *name)
{
    printf("fat_fsck version : %s\n", VERSION);
    fprintf(stderr, "Usage: %s [option] <device>\n", name);
    fprintf(stderr, "\t-h           Show help\n");
    fprintf(stderr, "\t-V           Show version\n");
    fprintf(stderr, "\t-a           Exit if Volume flag is clean. Auto-mode.\n");
    fprintf(stderr, "\t-y           Same as '-a' except not checking for dirty flag.\n");
    fprintf(stderr, "\t-t seconds   Run with a time limit\n");
    fprintf(stderr, "This util just runs %s.\n", FSCK_PROG);
}

static int kill_fsck(void)
{
    kill(fsck_pid, SIGTERM);
    waitpid(fsck_pid, NULL, 0);
    return 0;
}

static void handle_timeout(int sig __unused, siginfo_t *si __unused,
        void *u __unused)
{
    printf("WARN: timer is expired!\n");
}

static void handle_cancel_signals(int sig, siginfo_t *si __unused,
        void *u __unused)
{
    printf("ERR: killed by signal %d\n", sig);
    kill_fsck();
    exit(EFSCK_EXIT_USER_CANCEL);
}

static int setup_signal_handlers(unsigned long timeout_secs)
{
    struct sigaction sa;
    sigset_t sigmask;

    sigfillset(&sigmask);
    sigdelset(&sigmask, SIGCHLD);
    sigdelset(&sigmask, SIGALRM);
    sigdelset(&sigmask, SIGINT);
    sigdelset(&sigmask, SIGTERM);
    if (sigprocmask(SIG_SETMASK, &sigmask, NULL) != 0)
        printf("ERR: sigprocmask failed: %s\n", strerror(errno));

    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = handle_cancel_signals;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (timeout_secs) {
        sa.sa_sigaction = handle_timeout;
        if (sigaction(SIGALRM, &sa, NULL) != 0) {
            printf("ERR: failed to set signal handler: %s\n",
                    strerror(errno));
            return -1;
        }

        alarm((unsigned int)timeout_secs);
    }
    return 0;
}

static int wait_for_fsck(pid_t pid, int *exit_status)
{
    int wait_status;

    while (1) {
        if (waitpid(pid, &wait_status, 0) < 0) {
            /* timer is expired */
            if (errno == EINTR) {
                kill_fsck();
                *exit_status = EXIT_USER_CANCEL;
                return -EINTR;
            } else {
                printf("ERR: failed to waitpid: %s\n",
                        strerror(errno));
                kill_fsck();
                *exit_status = EFSCK_EXIT_FAILURE;
                return -EINVAL;
            }
        }
        if (WIFEXITED(wait_status)) {
            *exit_status = WEXITSTATUS(wait_status);
            return 0;
        }
    }
    return 0;
}

/* return TRUE if device's dirty flag is set,
 * otherwise return FALSE */
static int check_is_dirty(char *device_file)
{
    char *param[] = {FSCK_PROG, "-C", "", NULL};
    pid_t pid;
    int wait_status;
    int exit_status;

    param[2] = device_file;

    pid = fork();
    if (pid < 0) {
        printf("failed to fork for %s: %s\n", FSCK_PROG, strerror(errno));
        exit(EFSCK_EXIT_FAILURE);
    } else if (pid == 0) {  /* child */
        /* execute 'dosfsck -C <device>' for checking filesystem dirty flag */
        execvp(FSCK_PROG, param);
        printf("failed to exec %s: %s\n", FSCK_PROG, strerror(errno));
        exit(EFSCK_EXIT_FAILURE);
    }

    while (1) {
        if (waitpid(pid, &wait_status, 0) < 0) {
            /* timer is expired */
            if (errno == EINTR) {
                kill_fsck();
                exit_status = EXIT_USER_CANCEL;
                return -EINTR;
            } else {
                printf("ERR: failed to waitpid: %s\n",
                        strerror(errno));
                kill_fsck();
                exit_status = EFSCK_EXIT_FAILURE;
                return -EINVAL;
            }
        }

        if (WIFEXITED(wait_status)) {
            exit_status = WEXITSTATUS(wait_status);
            printf("EXIT STATUS: %d\n", exit_status);
            return exit_status ? TRUE : FALSE;
        } else
            break;
    }

    return -EINVAL;
}

/* if 'dev' is read-only dev, then return 1, otherwise return 0 */
int check_ro_device(char *dev)
{
    int maj;    /* major number */
    int min;    /* minor number */
    int fd;
    struct stat st;
    char *prefix = "/sys/dev/block";
    char syspath[512] = {0, };
    char buf[2] = {0, };
    int ret = -1;

    if (stat(dev, &st) != 0)
        exit(EFSCK_EXIT_FAILURE);

    if (!S_ISBLK(st.st_mode)) {
        if (!S_ISREG(st.st_mode)) {
            /* if 'dev' is not block device or file, then exit */
            printf("%s is not a block device or file\n", dev);
            exit(EFSCK_EXIT_FAILURE);
        }
        return 0;
    }

    maj = major(st.st_rdev);
    min = minor(st.st_rdev);

    snprintf(syspath, 512, "%s/%d:%d/ro", prefix, maj, min);

    fd = open(syspath, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open %s:%s\n", syspath, strerror(errno));
        exit(EFSCK_EXIT_FAILURE);
    }

    ret = read(fd, buf, 1);
    if (ret < 0) {
        fprintf(stderr, "read %s:%s\n", syspath, strerror(errno));
        close(fd);
        exit(EFSCK_EXIT_FAILURE);
    }

    if (strncmp(buf, "1", 1) == 0) {
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}

int main(int argc, char *argv[])
{
    int c;
    int force_fsck = 0;
    char *device_file;
    unsigned long timeout_secs = 0;
    int version_only = FALSE;
    int fsck_status;
    int exit_status = EFSCK_EXIT_SUCCESS;

    while ((c = getopt(argc, argv, "aht:Vy")) != EOF) {
        char *endptr = NULL;
        switch (c) {
            case 'a':
                force_fsck = 0;
                break;
            case 'h':
                usage(argv[0]);
                exit(EFSCK_EXIT_SUCCESS);
                break;
            case 'V':
                version_only = TRUE;
                break;
            case 'y':
                force_fsck = 1;
                break;
            case 't':
                timeout_secs = strtoul(optarg, &endptr, 10);
                if (endptr && *endptr != '\0') {
                    printf("Invalid timeout input! See below help\n\n");
                    usage(argv[0]);
                    exit(EFSCK_EXIT_SYNTAX_ERROR);
                }
                break;
            default:
                usage(argv[0]);
                exit(EFSCK_EXIT_SYNTAX_ERROR);
                break;
        }
    }

    if (optind != argc - 1) {
        usage(argv[0]);
        exit(EFSCK_EXIT_SYNTAX_ERROR);
    }

    device_file = argv[optind];
    fsck_param[2] = device_file;

    if (version_only) {
        printf("fat_fsck version : %s\n", VERSION);
        return EFSCK_EXIT_SUCCESS;
    }

    if (check_ro_device(device_file)) {
        printf("%s is read-only device!\n", device_file);
        return EFSCK_EXIT_RO_DEVICE;
    }

    if (!force_fsck && !check_is_dirty(device_file))
        exit(EFSCK_EXIT_SUCCESS);

    /* run fsck */
    fsck_pid = fork();
    if (fsck_pid < 0) {
        printf("failed to fork for %s: %s\n", FSCK_PROG, strerror(errno));
        exit(EFSCK_EXIT_FAILURE);
    } else if (fsck_pid == 0) {
        execvp(FSCK_PROG, fsck_param);
        printf("failed to exec %s: %s\n", FSCK_PROG, strerror(errno));
        exit(EFSCK_EXIT_FAILURE);
    }

    if (setup_signal_handlers(timeout_secs) != 0) {
        kill_fsck();
        exit_status = EFSCK_EXIT_FAILURE;
        goto out;
    }

    wait_for_fsck(fsck_pid, &fsck_status);

    /* handle exit status */
    if (fsck_status == EXIT_OPERATION_ERROR) {
        struct stat st;

        if (stat(device_file, &st) != 0) {
            if (errno == ENOENT)
                exit_status = EFSCK_EXIT_USER_CANCEL;
            else
                exit_status = EFSCK_EXIT_FAILURE;
            goto out;
        }

        if (st.st_mode & S_IWUSR)
            exit_status = EFSCK_EXIT_FAILURE;
        else
            exit_status = EFSCK_EXIT_RO_DEVICE;
    } else if (fsck_status == EXIT_USER_CANCEL) {
        printf("Timer is expired. %s is killed\n", FSCK_PROG);
        exit_status = EFSCK_EXIT_TIMEOUT;
    } else if (fsck_status == EXIT_SYNTAX_ERROR) {
        usage(argv[0]);
        exit(EFSCK_EXIT_SYNTAX_ERROR);
    } else if (fsck_status == EXIT_ERRORS_LEFT) {
        exit_status = EFSCK_EXIT_FAILURE;
    } else if (fsck_status == EXIT_NOT_SUPPORT) {
        exit_status = EFSCK_EXIT_NOT_FAT_VOLUME;
    }  else if (fsck_status != EXIT_NO_ERRORS &&
            fsck_status != EXIT_CORRECTED) {
        exit_status = EFSCK_EXIT_FAILURE;
    }
out:
    exit(exit_status);
}
