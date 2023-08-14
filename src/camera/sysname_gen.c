#include <stdio.h>
#include <string.h>

char *sysname[1024];

int main()
{
	int	i, entmax;
	memset(sysname, 0, sizeof(sysname));
#include "sysname.incl"
	for (i = 0; i < 1024; i++) {
		if (sysname[i] != 0) entmax = i;
	}
	printf("#ifndef _SYSNAME_H_\n#define _SYSNAME_H_\n");
	printf("#include \"test.h\"\n\n");
	printf("static char *sysname[%d] = {\n", entmax + 1);
	for (i = 0; i <= entmax; i++) {
		if (sysname[i]) {
			printf("\t\"%s\", \t/* %d */\n", sysname[i], i);
		} else {
			printf("\t0, \t\t/* %d */\n", i);
		}
	}

	printf("};\n");
	printf("#define SYSCALL_MAX\t%d\n\n", i);

	for (i = 0; i <= entmax; i++) {
		if (sysname[i]) {
			printf("long __attribute__((weak)) measure_%s(struct audit_syscall *sysc, int iter) { return 0; }\n", sysname[i]);
		}
	}

	printf("\n");
	printf("static inline long start_measure(struct audit_syscall *sysc, int iter) {\n");
	printf("\tlong rc = 0;\n\n");
	printf("\tswitch (sysc->number) {\n");
	for (i = 0; i <= entmax; i++) {
		if (sysname[i]) {
			printf("\tcase %d:\n", i);
			printf("\t\trc = measure_%s(sysc, iter);\n", sysname[i]);
			printf("\t\tbreak;\n");
		}
	}
	printf("\tdefault:\n");
	printf("\t\trc = -1;\n");
	printf("\t}\n");
	printf("\treturn rc;\n");
	printf("}\n");

	printf("#endif");
}
