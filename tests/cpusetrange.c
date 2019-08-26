#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

/*
 * cpusets are in format "1,2-3,4"
 * iow, comma-delimited ranges
 */
extern bool cpu_in_cpuset(int cpu, const char *cpuset);
extern unsigned int cpus_in_cpuset(const char *cpuset);

void __verify(const char *func, bool condition) {
	if (condition) {
		printf("\t%s PASS\n", func);
	} else {
		printf("\t%s FAIL!\n", func);
		exit(1);
	}
}

#define verify(func, condition)	__verify(#func, (condition))

int main() {
	char *a = "1,2";
	char *b = "1-3,5";
	char *c = "1,4-5";
	char *d = "";
	char *e = "\n";

	printf("1 in %s:\n", a);
	verify(cpu_in_cpuset, cpu_in_cpuset(1, a));
	verify(cpus_in_cpuset, cpus_in_cpuset(a) == 2);
	printf("2 in %s:\n", a);
	verify(cpu_in_cpuset, cpu_in_cpuset(2, a));
	printf("NOT 4 in %s:\n", a);
	verify(cpu_in_cpuset, !cpu_in_cpuset(4, a));
	printf("1 in %s:\n", b);
	verify(cpu_in_cpuset, cpu_in_cpuset(1, b));
	verify(cpus_in_cpuset, cpus_in_cpuset(b) == 4);
	printf("NOT 4 in %s:\n", b);
	verify(cpu_in_cpuset, !cpu_in_cpuset(4, b));
	printf("5 in %s:\n", b);
	verify(cpu_in_cpuset, cpu_in_cpuset(5, b));
	printf("1 in %s:\n", c);
	verify(cpu_in_cpuset, cpu_in_cpuset(1, c));
	verify(cpus_in_cpuset, cpus_in_cpuset(c) == 3);
	printf("5 in %s:\n", c);
	verify(cpu_in_cpuset, cpu_in_cpuset(5, c));
	printf("NOT 6 in %s:\n", c);
	verify(cpu_in_cpuset, !cpu_in_cpuset(6, c));
	printf("NOT 6 in empty set:\n");
	verify(cpu_in_cpuset, !cpu_in_cpuset(6, d));
	verify(cpus_in_cpuset, cpus_in_cpuset(d) == 0);
	printf("NOT 6 in empty set(2):\n");
	verify(cpu_in_cpuset, !cpu_in_cpuset(6, e));
	verify(cpus_in_cpuset, cpus_in_cpuset(e) == 0);
}
