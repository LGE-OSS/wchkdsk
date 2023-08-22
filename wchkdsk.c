/* SPDX-License-Identifier : GPL-2.0 */

/*
 * This is fsck wrapper utility for ntfsprogs/exfat-tools/fatprogs.
 * Name from 'webOS check disk'
 */

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
#include "wchkdsk.h"

#ifndef __unused
#define __unused	__attribute__((__unused__))
#endif

/*
 * fs_name will expand like below:
 *
 * char *fs_name[] = {
 *	"ntfs",
 *	"exfat",
 *	"fat",
 * };
 */
static const char *fs_name[FSTYPE_MAX] = {
#define X(fstype, name, sig, fsck_progs, default_opt, check_opt, inter_opt) \
	name,
	FILESYSTEM_DEF
#undef X
};

/*
 * fs_sig will expand like below:
 *
 * char *fs_sig[] = {
 *	"NTFS    ",
 *	"EXFAT   ",
 *	"",
 * };
 */
static const char *fs_sig[FSTYPE_MAX] = {
#define X(fstype, name, sig, fsck_progs, default_opt, check_opt, inter_opt) \
	sig,
	FILESYSTEM_DEF
#undef X
};

/*
 * fsck_param will expand like below:
 *
 * char *fsck_pararm[][] = {
 *	{"ntfsck", "", "", NULL},
 *	{"fsck.exfat", "", "", NULL},
 *	{"dosfsck", "", "", NULL},
 * };
 *
 * It means {<fsck_progs>, <fsck_opts>, <device>} respectively.
 * fsck access each item using fstype and parameter index.
 *
 * variable PARAM_IDX_XXX is used to access array.
 *
 * example: fsck_pararm[fstype][idx]
 */
enum {
	PARAM_IDX_PROGS = 0,	/* index for fsck program name */
	PARAM_IDX_OPTS,		/* index for fsck option */
	PARAM_IDX_DEV,		/* index for target device */
	PARAM_IDX_MAX,
};

char *fsck_param[FSTYPE_MAX][PARAM_IDX_MAX + 1] = {
#define X(fstype, name, sig, fsck_progs, default_opt, check_opt, inter_opt) \
	{fsck_progs, "", "", NULL},
	FILESYSTEM_DEF
#undef X
};

/*
 * fsck_opts will expand like below:
 *
 * char *fsck_opts[][] = {
 *	{"-a", "-C", "-r"},	// for ntfs
 *	{"-ys", "", "-r"},	// for exfat
 *	{"-afw", "-C", "-r"},	// for fat
 * };
 *
 * It means {<default_opt>, <check_opt>, <interactive_opt>} respectively.
 * OPT_IDX_XXX is used to access array.
 */
enum {
	OPTS_IDX_DEFAULT = 0,
	OPTS_IDX_CHECK,
	OPTS_IDX_INTERACTIVE,
	OPTS_IDX_MAX,
};

char *fsck_opts[FSTYPE_MAX][OPTS_IDX_MAX] = {
#define X(fstype, name, sig, fsck_progs, default_opt, check_opt, inter_opt) \
	{default_opt, check_opt, inter_opt},
	FILESYSTEM_DEF
#undef X
};

pid_t fsck_pid;

static void usage(char *name)
{
	fprintf(stdout, "wchkdsk version : %s\n", VERSION);
	fprintf(stdout, "Usage: %s [option] <device>\n", name);
	fprintf(stdout, "\t-h		Show help\n");
	fprintf(stdout, "\t-V		Show version\n");
	fprintf(stdout, "\t-f fstype    set filesystem type, {ntfs, exfat, fat}\n");
	fprintf(stdout, "\t-a		Exit if Volume flag is clean. Auto-mode.\n");
	fprintf(stdout, "\t-y		Same as '-a' except not checking for dirty flag.\n");
	fprintf(stdout, "\t-r		execute fsck with interactive mode.\n");
	fprintf(stdout, "\t-t seconds	Run with a time limit\n");
	fprintf(stdout, "This util just runs wchkdsk for ntfsck/fsck.exfat/dosfsck.\n");
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
	fprintf(stdout, "WARN: timer is expired!\n");
}

static void handle_cancel_signals(int sig, siginfo_t *si __unused,
		void *u __unused)
{
	fprintf(stderr, "ERR: killed by signal %d\n", sig);
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
		fprintf(stderr, "ERR: sigprocmask failed: %s\n", strerror(errno));

	memset(&sa, 0, sizeof(sa));
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = handle_cancel_signals;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	if (timeout_secs) {
		sa.sa_sigaction = handle_timeout;
		if (sigaction(SIGALRM, &sa, NULL) != 0) {
			fprintf(stderr, "ERR: failed to set signal handler: %s\n",
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
				fprintf(stderr, "ERR: failed to waitpid: %s\n",
						strerror(errno));
				kill_fsck();
				*exit_status = EFSCK_EXIT_FAILURE;
				return -EINVAL;
			}
		}
		if (WIFEXITED(wait_status)) {
			*exit_status = WEXITSTATUS(wait_status);
			fprintf(stdout, "EXIT STATUS: %d\n", *exit_status);
			return 0;
		}
	}
	return 0;
}

static int read_fs_boot_sect(const char *device_file, char sect[], size_t len)
{
	int fd;
	ssize_t bytes;

	fd = open(device_file, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "failed to open %s to check exfat volume: %s\n",
				device_file, strerror(errno));
		return fd;
	}

	bytes = read(fd, sect, len);
	if (bytes != (ssize_t)len) {
		fprintf(stderr, "failed to read %s to check exfat volume\n",
				device_file);
		close(fd);
		return -EIO;
	}

	close(fd);
	return 0;
}

/*
 * Check filesystem signature for NTFS, EXFAT.
 * FAT filesystem signature is various. so FAT can't use this function.
 */
static int is_fstype_volume(const char *device_file, fstype_t fstype)
{
	char sect[512];

	if (read_fs_boot_sect(device_file, sect, sizeof(sect)) != 0)
		return FALSE;

	if (memcmp(sect + 3, fs_sig[fstype], 8) != 0)
		return FALSE;

	return TRUE;
}

static int is_exfat_clean(const char *device_file)
{
	char sect[512];

	if (read_fs_boot_sect(device_file, sect, sizeof(sect)) != 0)
		return FALSE;

	if (sect[106] && 0x2)
		return FALSE;

	return TRUE;
}

static int handle_child_return_error(char *device_file, int fstype, int fsck_status)
{

	if (!fsck_status)
		return EFSCK_EXIT_SUCCESS;

	if (fsck_status == EXIT_OPERATION_ERROR) {
		/*
		 * ntfs return EXIT_OPERATION_ERROR(0x08)
		 * when target is not ntfs format
		 */
		if (fstype == NTFS) {
			if (!is_fstype_volume(device_file, fstype))
				exit(EFSCK_EXIT_NOT_SUPPORT);
		}
	} else if (fsck_status == EXIT_ERRORS_LEFT) {
		/*
		 * EXFAT:exfat.fsck return EXIT_ERRORS_LEFT(0x01)
		 * when target is not exfat format.
		 */
		if (fstype == EXFAT) {
			if (!is_fstype_volume(device_file, fstype))
				exit(EFSCK_EXIT_NOT_SUPPORT);
		}
	} else if (fsck_status == EXIT_NEED_REBOOT) {
		/*
		 * old fatprogs:dostsck return EXIT_NEED_REBOOT(0x02)
		 * when target is not fat format. (until v2.13.0)
		 * this condition will be deprecated.
		 */
		if (fstype == FAT)
			exit(EFSCK_EXIT_NOT_SUPPORT);
	} else if (fsck_status == EXIT_NOT_SUPPORT) {
		/*
		 * fatprogs:dosfsck return EXIT_NOT_SUPPORT(0x40)
		 * when target is not fat format. (from v2.13.1)
		 */
		if (fstype == FAT)
			exit(EFSCK_EXIT_NOT_SUPPORT);
	}

	return EFSCK_EXIT_FAILURE;
}

/*
 * Return TRUE if device's dirty flag is set, otherwise return FALSE
 */
static int check_is_dirty(char *device_file, char *param[], int fstype)
{
	pid_t pid;
	int wait_status;
	int fsck_status;
	int exit_status;

	/* EXFAT fsck do not support volume dirty check option.
	 * Read it's volume dirty flag directly here.
	 */
	if (fstype == EXFAT) {
		return !is_exfat_clean(device_file);
	}

	/*
	 * In case of NTFS, FAT,
	 * execute fsck with only checking volume dirty flag option
	 */
	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "failed to fork for %s: %s\n",
				param[PARAM_IDX_PROGS], strerror(errno));
		exit(EFSCK_EXIT_FAILURE);
	} else if (pid == 0) {	/* child */
		/* execute 'dosfsck -C <device>' for checking filesystem dirty flag */
		execvp(param[PARAM_IDX_PROGS], param);
		fprintf(stderr, "failed to exec %s: %s\n",
				param[PARAM_IDX_PROGS], strerror(errno));
		exit(EFSCK_EXIT_FAILURE);
	}

	while (1) {
		if (waitpid(pid, &wait_status, 0) < 0) {
			/* timer is expired */
			if (errno == EINTR) {
				kill_fsck();
				exit_status = EXIT_USER_CANCEL;
			} else {
				fprintf(stderr, "ERR: failed to waitpid: %s\n",
						strerror(errno));
				kill_fsck();
				exit_status = EFSCK_EXIT_FAILURE;
			}
			exit(exit_status);
		}

		if (WIFEXITED(wait_status)) {
			fsck_status = WEXITSTATUS(wait_status);
			fprintf(stdout, "EXIT STATUS: %d\n", fsck_status);
			if (!fsck_status)
				return FALSE;

			handle_child_return_error(device_file, fstype, fsck_status);
			return TRUE;
		} else
			break;
	}

	return -EINVAL;
}

/* if 'dev' is read-only dev, then return 1, otherwise return 0 */
int check_ro_device(char *dev)
{
	int maj;	/* major number */
	int min;	/* minor number */
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
			fprintf(stderr, "%s is not a block device or file\n", dev);
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
	int force_fsck = 1;
	char *device_file;
	unsigned long timeout_secs = 0;
	int version_only = FALSE;
	int fsck_status;
	int user_interactive = FALSE;
	int exit_status = EFSCK_EXIT_SUCCESS;
	fstype_t fstype = FSTYPE_NONE;
	struct stat st;

	while ((c = getopt(argc, argv, "ahf:rt:Vy")) != EOF) {
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
					fprintf(stderr, "Invalid timeout input! See below help\n\n");
					usage(argv[0]);
					exit(EFSCK_EXIT_SYNTAX_ERROR);
				}
				break;
			case 'f':
				if (strncmp(optarg, fs_name[NTFS], strlen(optarg)) == 0) {
					fstype = NTFS;
				} else if (strncmp(optarg, fs_name[EXFAT], strlen(optarg)) == 0) {
					fstype = EXFAT;
				} else if (strncmp(optarg, fs_name[FAT], strlen(optarg)) == 0) {
					fstype = FAT;
				} else {
					usage(argv[0]);
					exit(EFSCK_EXIT_SYNTAX_ERROR);
				}
				break;
			case 'r':
				user_interactive = TRUE;
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

	if (fstype < 0) {
		fprintf(stderr, "wchkdsk: '-f' option should be specified to set filesystem\n");
		exit(EFSCK_EXIT_SYNTAX_ERROR);
	}

	device_file = argv[optind];
	fsck_param[fstype][PARAM_IDX_DEV] = device_file;

	if (version_only) {
		fprintf(stdout, "wchkdsk version : %s\n", VERSION);
		return EFSCK_EXIT_SUCCESS;
	}

	if (check_ro_device(device_file)) {
		fprintf(stderr, "%s is read-only device!\n", device_file);
		return EFSCK_EXIT_RO_DEVICE;
	}

	fsck_param[fstype][PARAM_IDX_OPTS] = fsck_opts[fstype][OPTS_IDX_CHECK];
	if (!force_fsck && !check_is_dirty(device_file, fsck_param[fstype], fstype))
		exit(EFSCK_EXIT_SUCCESS);

	if (!user_interactive)
		fsck_param[fstype][PARAM_IDX_OPTS] =
			fsck_opts[fstype][OPTS_IDX_DEFAULT];
	else
		fsck_param[fstype][PARAM_IDX_OPTS] =
			fsck_opts[fstype][OPTS_IDX_INTERACTIVE];

	/* run fsck */
	fsck_pid = fork();
	if (fsck_pid < 0) {
		fprintf(stderr, "failed to fork for %s: %s\n",
				fsck_param[fstype][PARAM_IDX_PROGS], strerror(errno));
		exit(EFSCK_EXIT_FAILURE);
	} else if (fsck_pid == 0) {
		if (nice(19) < 0)
			fprintf(stderr, "failed to lower schedule priority: %s\n",
				strerror(errno));
		execvp(fsck_param[fstype][PARAM_IDX_PROGS], fsck_param[fstype]);
		fprintf(stderr, "failed to exec %s: %s\n",
				fsck_param[fstype][PARAM_IDX_PROGS], strerror(errno));
		exit(EFSCK_EXIT_FAILURE);
	}

	if (setup_signal_handlers(timeout_secs) != 0) {
		kill_fsck();
		exit_status = EFSCK_EXIT_FAILURE;
		goto out;
	}

	wait_for_fsck(fsck_pid, &fsck_status);

	if (stat(device_file, &st) != 0) {
		if (errno == ENOENT)
			exit_status = EFSCK_EXIT_USER_CANCEL;
		else
			exit_status = EFSCK_EXIT_FAILURE;
		goto out;
	}

	/* handle exit status */
	if (fsck_status == EXIT_OPERATION_ERROR) {
		/*
		 * ntfs return EXIT_OPERATION_ERROR(0x08)
		 * when target is not ntfs format
		 */
		if (fstype == NTFS) {
			if (!is_fstype_volume(device_file, fstype))
				exit(EFSCK_EXIT_NOT_SUPPORT);
		}

		exit_status = EFSCK_EXIT_FAILURE;
	} else if (fsck_status == EXIT_USER_CANCEL) {
		fprintf(stderr, "Timer is expired. %s is killed\n",
				fsck_param[fstype][PARAM_IDX_PROGS]);
		exit_status = EFSCK_EXIT_TIMEOUT;
	} else if (fsck_status == EXIT_SYNTAX_ERROR) {
		usage(argv[0]);
		exit(EFSCK_EXIT_SYNTAX_ERROR);
	} else if (fsck_status != EXIT_NO_ERRORS &&
			fsck_status != EXIT_CORRECTED) {
		exit_status =
			handle_child_return_error(device_file, fstype, fsck_status);
	}
out:
	exit(exit_status);
}
