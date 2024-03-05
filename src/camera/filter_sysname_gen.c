#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>

char *sysname[1024];

struct syscall_info {
	char *name;
	int sysno;
	int argpos;
};


// struct syscall_info fd_syscalls[] = {
/*
	{ "io_uring_enter", 0 },
	{ "io_uring_register", 0 },
	{ "fsetxattr", 0 },
	{ "fgetxattr", 0 },
	{ "flistxattr", 0 },
	{ "fremovexattr", 0 },
	{ "epoll_ctl", 2 },
	{ "inotify_add_watch", 0 },
	{ "inotify_rm_watch", 0 },
	{ "flock", 0 },
	{ "fstatfs", 0 },
	{ "ftruncate", 0 },
	{ "fallocate", 0 },
	{ "fchdir", 0 },
	{ "fchmod", 0 },
	{ "fchmodat", 0 },
	{ "fchownat", 0 },
	{ "fchown", 0 },
	{ "openat2", 0 },
	{ "close", 0 },
	{ "close_range", 0 },
	{ "quotactl_fd", 0 },
	{ "quotactl_fd", 0 },
	{ "lseek", 0 },
	{ "write", 0 },
	{ "readv", 0 },
	{ "writev", 0 },
	{ "pread", 0 },
	{ "preadv", 0 },
	{ "pwrite", 0 },

};

*/

#define sysno(name) SYS_##name

struct syscall_info fn_syscalls[] = {
	{ "setxattr", sysno(setxattr), 0 },
	{ "lsetxattr", sysno(lsetxattr), 0 },
	{ "getxattr", sysno(getxattr), 0 },
	{ "lgetxattr", sysno(lgetxattr), 0 },
	{ "listxattr", sysno(listxattr), 0 },
	{ "llistxattr", sysno(llistxattr), 0 },
	{ "removexattr", sysno(removexattr), 0 },
	{ "lremovexattr", sysno(lremovexattr), 0 },
	{ "symlinkat", sysno(symlinkat), 0 },
	{ "umount2", sysno(umount2), 0 },
	{ "pivot_root", sysno(pivot_root), 0 },
	{ "statfs", sysno(statfs), 0 },
	{ "truncate", sysno(truncate), 0 },
	{ "chdir", sysno(chdir), 0 },
	{ "chroot", sysno(chroot), 0 },
	{ "acct", sysno(acct), 0 },
	{ "execve", sysno(execve),0 },
	{ "swapon", sysno(swapon),0 },
	{ "swapoff", sysno(swapoff),0 },
	{ "inotify_add_watch", sysno(inotify_add_watch), 1 },
	{ "mknodat", sysno(mknodat), 1 },
	{ "mkdirat", sysno(mkdirat), 1 },
	{ "unlinkat", sysno(unlinkat), 1 },
	{ "mount", sysno(mount), 1 },
	{ "faccessat", sysno(faccessat), 1 },
//	{ "faccessat2", sysno(faccessat2), 1 },
	{ "fchmodat", sysno(fchmodat), 1 },
	{ "fchownat", sysno(fchownat), 1 },
	{ "openat", sysno(openat), 1 },
//	{ "openat2", sysno(openat2), 1 },
	{ "quotactl", sysno(quotactl), 1 }, 
	{ "readlinkat", sysno(readlinkat), 1 },
	{ "newfstatat", sysno(newfstatat), 1 }, 
	{ "utimensat", sysno(utimensat), 1 },
	{ "name_to_handle_at", sysno(name_to_handle_at), 1 },
	{ "execveat", sysno(execveat), 1 },
	{ "statx", sysno(statx), 1 },
	{ "open_tree", sysno(open_tree), 1 },
//	{ "mount_setattr", sysno(mount_setattr), 1 },
	{ "linkat", sysno(linkat), 3 },
	{ "renameat", sysno(renameat), 3 },
	{ "renameat2", sysno(renameat2), 3 },
	{ "move_mount", sysno(move_mount), 3 }, 
	{ "fanotify_mark", sysno(fanotify_mark), 4 }
};

struct syscall_info fd_syscalls[] = {
	{ "bind", sysno(bind), 0 },
	{ "close", sysno(close), 0 },
	{ "eventfd2", sysno(eventfd2), 7 }, // in the return value
	{ "faccessat", sysno(faccessat), 0 },
	{ "fcntl", sysno(fcntl), 0 },
	{ "fstat", sysno(fstat), 0 },
	{ "getdents64", sysno(getdents64), 0 },
	{ "getsockname", sysno(getsockname), 0 },
	{ "getsockopt", sysno(getsockopt), 0 },
	{ "ioctl", sysno(ioctl), 0 },
	{ "lseek", sysno(lseek), 0 },
	{ "mmap", sysno(mmap), 3 },
	{ "newfstatat", sysno(newfstatat), 0 },
	{ "openat", sysno(openat), 0 },
	{ "pipe2", sysno(pipe2), 0 }, // fd[2]
	{ "ppoll", sysno(ppoll), 0 }, // multiple fds
	{ "read", sysno(read), 0 },
	{ "readlinkat", sysno(readlinkat), 0 },
	{ "sendmmsg", sysno(sendmmsg), 0 },
	{ "setsockopt", sysno(setsockopt), 0 },
	{ "socket", sysno(socket), 7 },
	{ "socketpair", sysno(socketpair), 3 },
	{ "write", sysno(write), 0 },
};

/*
struct syscall_info addr_syscalls[] = {
	{ "futex", 0 },
	{ "madvise", 0 },
	{ "mlock", 0 },
	{ "mmap", 0 }, // if addr is not NULL
	{ "mprotect", 0 },
	{ "munmap", 0 },
};
*/

#define SYSCALL_DEFAULT	0x01
#define SYSCALL_PATH	0x02
#define SYSCALL_FD		0x04
#define SYSCALL_ADDR	0x08

#define ARG_POS1		0x01
#define ARG_POS2		0x02
#define ARG_POS3		0x04
#define ARG_POS4		0x08
#define ARG_POS5		0x10
#define ARG_POS6		0x20
#define ARG_RET			0x80

int main(int argc, char **argv)
{
	int	i, entmax = 0;
	memset(sysname, 0, sizeof(sysname));
#include "sysname.incl"
	for (i = 0; i < 1024; i++) {
		if (sysname[i] != 0) entmax = i;
	}
	printf("#ifndef _SYSNAME_H_\n#define _SYSNAME_H_\n\n");

	printf("struct filter_syscall_info {\n"
			"\tchar *name;\n"
			"\tunsigned int sysno;\n"
			"\tunsigned int type;\n"
			"\tunsigned int args;\n"
//			"\tint (*ptr_parse)(char *, int);\n"
			"};\n\n");

	printf("#define SYSCALL_DEFAULT\t0x01\n"
			"#define SYSCALL_PATH\t0x02\n"
			"#define SYSCALL_FD\t\t0x04\n"
			"#define SYSCALL_ADDR\t0x08\n\n");

	printf("#define ARG_POS1\t0x01\n"
		"#define ARG_POS2\t0x02\n"
		"#define ARG_POS3\t0x04\n"
		"#define ARG_POS4\t0x08\n"
		"#define ARG_POS5\t0x10\n"
		"#define ARG_POS6\t0x20\n"
		"#define ARG_RET\t\t0x80\n\n");

	printf("static struct filter_syscall_info systable[%d] = {\n", entmax + 1);
	for (i = 0; i <= entmax; i++) {
		unsigned short pos = 0x0;
		unsigned short type = 0x0;

		if (sysname[i]) {
			int j; 
			for (j = 0; j < sizeof(fn_syscalls)/sizeof(fn_syscalls[0]); j++) {
				if (i == fn_syscalls[j].sysno) {
					type |= SYSCALL_PATH;
					pos |= 1 << fn_syscalls[j].argpos;
					break;
				}
			}

			for (j = 0; j < sizeof(fd_syscalls)/sizeof(fd_syscalls[0]); j++) {
				if (i == fd_syscalls[j].sysno) {
					type |= SYSCALL_FD;
					pos |= 1 << fd_syscalls[j].argpos;
					break;
				}
			}

			printf("\t{ .name = \"%s\", .sysno = %d, .type = 0x%02x, .args = 0x%02x, } , \t/* %d */\n",
				sysname[i], i, type?type:SYSCALL_DEFAULT, pos?pos:ARG_POS1, i);
		} else {
			printf("\t{ 0 } , \t\t/* %d */\n", i);
		}
	}

	printf("};\n");
	printf("#define SYSCALL_MAX\t%d\n\n", i);
	printf("#endif");

	return 0;
}
