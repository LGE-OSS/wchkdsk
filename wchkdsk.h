#ifndef FAT_FSCK_H_
#define FAT_FSCK_H_

/* Constant definitions */
typedef enum {
	FALSE = 0,
	TRUE = 1,
} bool_t;

/* fsck return error code */
typedef enum {
	EXIT_NO_ERRORS		= 0x00, /* No errors */
	EXIT_CORRECTED		= 0x01, /* Filesystem errors corrected */
	EXIT_NEED_REBOOT	= 0x02, /* System should be rebooted */
	EXIT_ERRORS_LEFT	= 0x04, /* Filesystem errors left uncorrected */
	EXIT_OPERATION_ERROR	= 0x08, /* Operational error */
	EXIT_SYNTAX_ERROR	= 0x10, /* Usage or syntax error */
	EXIT_USER_CANCEL	= 0x20, /* Checking cacnceld by user request */
	EXIT_SYSCALL_ERROR	= 0x80, /* Shared-library error */

	EXIT_NOT_SUPPORT	= 0x40, /* Additional, Not supported filesystem */
} fsck_exit_t;

/* wchkdsk return error code */
typedef enum {
	EFSCK_EXIT_SUCCESS	= 0,
	EFSCK_EXIT_FAILURE	= 1,	/* Unknown or errors left */
	EFSCK_EXIT_SYNTAX_ERROR	= 2,
	EFSCK_EXIT_NOT_SUPPORT	= 3,
	EFSCK_EXIT_RO_DEVICE	= 23,
	EFSCK_EXIT_VOLUME_DIRTY	= 100,	/* Volume dirty set in boot sector */
	EFSCK_EXIT_USER_CANCEL	= 160,  /* Killed by signal or device is removed */
	EFSCK_EXIT_TIMEOUT	= 161,
} wchkdsk_exit_t;

/*
 * FILESYSTEM_DEF Macro consists of :
 * X (
 *	<filesystem variable>,
 *	<filesystem name>,
 *	<filesystem signature>,
 *	<fsck program>,
 *	<fsck default option>,
 *	<fsck dirty check option>,
 *	<fsck interactive option>,
 *   )
 */
#define FILESYSTEM_DEF \
	X(NTFS, "ntfs", "NTFS    ", "ntfsck", "-a", "-C", "-r") \
	X(EXFAT, "exfat", "EXFAT   ", "fsck.exfat", "-ys", "", "-r") \
	X(FAT, "fat", "", "dosfsck", "-a", "-C", "-r")

/*
 * fstype_t will expand code like as below
 *
 * typedef enum {
 *	NTFS,
 *	EXFAT,
 *	FAT,
 *	FSTYPE_MAX,
 * } fstype_t;
 *
 */
typedef enum {
	FSTYPE_NONE = -1,
#define X(fstype, fs_name, sig, fsck_progs, default_opt, check_opt, inter_opt) \
	fstype,
	FILESYSTEM_DEF
#undef X
	FSTYPE_MAX,
} fstype_t;

#endif /* FAT_FSCK_H_ */
