#include <stdio.h>
#include <string.h>

char *sysname[1024];

int main(int argc, char **argv)
{
	int	i, entmax = 0;
	memset(sysname, 0, sizeof(sysname));
#include "sysname.incl"
	for (i = 0; i < 1024; i++) {
		if (sysname[i] != 0) entmax = i;
	}
	printf("#ifndef _SYSNAME_H_\n#define _SYSNAME_H_\n");
	printf("#define IO_SYSCALL 0x01\n#define NONIO_SYSCALL 0x02\n\n");
	printf("struct filter_monitoring_info {\n\tint num_args;\n\tchar **args;\n};\n\n");
	printf("struct filter_syscall_info {\n\tchar *name;\n\tint sysno;\n"
			"\tstruct filter_monitoring_info *info;\n};\n\n");
	printf("static struct filter_syscall_info sysname[%d] = {\n", entmax + 1);
	for (i = 0; i <= entmax; i++) {
		if (sysname[i]) {
			printf("\t{ .name = \"%s\", .sysno = %d, .info = NULL, } , \t/* %d */\n", sysname[i], i, i);
		} else {
			printf("\t{ 0 } , \t\t/* %d */\n", i);
		}
	}

	printf("};\n");
	printf("#define SYSCALL_MAX\t%d\n\n", i);
	printf("#endif");
}

int generate_sysname_dict(void)
{
	
}
