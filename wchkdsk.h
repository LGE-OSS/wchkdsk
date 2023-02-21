#ifndef FAT_FSCK_H_
#define FAT_FSCK_H_

/* Constant definitions */
#define TRUE 1			/* Boolean constants */
#define FALSE 0

typedef enum {
	EXIT_NO_ERRORS		= 0x00,
	EXIT_CORRECTED		= 0x01,
	EXIT_NOT_SUPPORT	= 0x02,
	EXIT_ERRORS_LEFT	= 0x04,
	EXIT_OPERATION_ERROR	= 0x08,
	EXIT_SYNTAX_ERROR	= 0x10,
	EXIT_USER_CANCEL	= 0x20,
	EXIT_SYSCALL_ERROR	= 0x40,
} exit_type_t;

#endif /* FAT_FSCK_H_ */
