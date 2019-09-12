#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

/*
 * Helper functions for cpuset_in-set
 */
static char *cpuset_nexttok(const char *c)
{
	char *r = strchr(c+1, ',');
	if (r)
		return r+1;
	return NULL;
}

static int cpuset_getrange(const char *c, int *a, int *b)
{
	int ret;

	ret = sscanf(c, "%d-%d", a, b);
	return ret;
}

/*
 * cpusets are in format "1,2-3,4"
 * iow, comma-delimited ranges
 */
bool cpu_in_cpuset(int cpu, const char *cpuset)
{
	const char *c;

	for (c = cpuset; c; c = cpuset_nexttok(c)) {
		int a, b, ret;

		ret = cpuset_getrange(c, &a, &b);
		if (ret == 1 && cpu == a) // "1" or "1,6"
			return true;
		else if (ret == 2 && cpu >= a && cpu <= b) // range match
			return true;
	}

	return false;
}

/*
 * cpusets are in format "1,2-3,4"
 * calculate the total cpu number of cpuset
 */
unsigned int cpus_in_cpuset(const char *cpuset)
{
	const char *c = NULL;
	unsigned int cpus = 0;
	int a, b, retval;

	for (c = cpuset; c != NULL; c = cpuset_nexttok(c)) {
		retval = cpuset_getrange(c, &a, &b);
		if (retval == 1) {
			cpus += 1;
		} else if (retval == 2) {
			cpus += (b + 1 - a);
		}
	}

	return cpus;
}

/*
 * get cpu number in cpuset
 */
int cpu_number_in_cpuset(const char *cpuset)
{
	const char *c;
	int cpu_number = 0;

	for (c = cpuset; c; c = cpuset_nexttok(c)) {
		int a, b, ret;

		ret = cpuset_getrange(c, &a, &b);
		if (ret == 1)
			cpu_number++;
		else if (ret == 2)
			cpu_number +=  a > b ? a - b + 1 : b - a + 1;
	}
	return cpu_number;
}
