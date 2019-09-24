/* lxcfs
 *
 * Author: River <river.vvl.me>
 *
 * See COPYING file for details.
 */

//#include <search.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <sys/user.h> // define PAGE_SIZE
#include <sys/sysinfo.h>

#include "dynamic_memory.h"
#include "bindings.h"

/*
#define DYNMEM_SIZE	100
*/
#define FLUSH_TIME 3

static bool stop_dynmem = false;

static struct water_mark phy_water_mark;

/*
 * calculate the memory water mark, unit is page
 *   @mark: memory water mark
 *   @total: memory size, unit is byte
 */
static void cal_mem_watermark(struct water_mark *mark, unsigned long total) {
	mark->min = 4 * (unsigned long)sqrtf((float)total) / PAGE_SIZE;
	mark->low = mark->min * 5/4;
	mark->high = mark->low * 3/2;
}

/*
 * TODO:
 *  research why the increase size must be 1024*8*N?
 *   return 1024*8*extra_mem bytes because of cgroup memory controller
 */
static unsigned long extra_mem_limit(unsigned long memlimit,
									 unsigned long memusage) {
	struct sysinfo info;
	unsigned long avail_pages;
	unsigned long extra_mem = 0;

	if (sysinfo(&info) != 0) {
		lxcfs_error("sysinfo fails!\n");
		goto out;
	}

	avail_pages = info.freeram / PAGE_SIZE;
	/* free memory is low, don't alloc extra memory for container */
	if (avail_pages <= phy_water_mark.low)
		goto out;

	// TODO
out:
	return ((unsigned long)extra_mem/(8*1024)) * (8*1024);
}

static bool is_increase_memlimit(const unsigned long memlimit,
								 const unsigned long memusage) {
	struct water_mark task_water_mark;
	unsigned long used_pages;

	used_pages = memusage / PAGE_SIZE;
	cal_mem_watermark(&task_water_mark, memlimit);

	return used_pages > task_water_mark.high;
}

void *dynmem_task(void *arg) {
	unsigned long memlimit, memusage, extra_mem;
	struct dirent *direntp;
	clock_t time1, time2;
	char cg[512];
	int cfd;
	DIR *dirp;

	if (!find_mounted_controller("memory", &cfd)) {
		lxcfs_error("find_mounted_controller('memory') fails!\n");
		goto out;
	}
	if ((dirp = fdopendir(cfd)) == NULL) {
		lxcfs_error("'fdopendir fails!\n");
		goto out;
	}

	while (!stop_dynmem) {
		time1 = clock();

		while ((direntp = readdir(dirp)) != NULL) {
			snprintf(cg, sizeof(cg)-1, "/docker/%s", direntp->d_name);
			memlimit = get_mem_limit(cg);
			memusage = get_mem_usage(cg);

			lxcfs_debug("'%s': memlimit=%lu memusage=%lu\n",
						cg, memlimit, memusage);
			if (is_increase_memlimit(memlimit, memusage)) {
				extra_mem = extra_mem_limit(memlimit, memusage);
				lxcfs_debug("increase memory to %lu\n", extra_mem);
				if (!set_mem_limit(cg, memlimit, extra_mem))
					lxcfs_error("set_mem_limit('%s', '%lu', '%lu') fails!\n",
								cg, memlimit, extra_mem);
			}
		}

		time2 = clock();
		usleep(FLUSH_TIME * 1000000 - \
			   (int)((time2 - time1) * 1000000 / CLOCKS_PER_SEC));
	}

	while ((closedir(dirp) == -1) && (errno == EINTR))
		continue;

out:
	return 0;
}

static int detect_mem_watermark(void) {
	int retval;
	struct sysinfo info;

	retval = sysinfo(&info);
	if (retval != 0) {
		lxcfs_error("sysinfo fails!\n");
		goto out;
	}

	cal_mem_watermark(&phy_water_mark, info.totalram);
out:
	return retval;
}

pthread_t dynmem_daemon(void) {
	int retval;
	pthread_t tid = 0;

	/*
	if (hcreate(DYNMEM_SIZE) == 0) {
		lxcfs_error("hcreate fails!\n");
		goto out;
	}
	*/

	if (detect_mem_watermark() != 0)
		goto out;

	retval = pthread_create(&tid, NULL, dynmem_task, NULL);
	if (retval != 0)
		lxcfs_error("Create pthread fails in 'dymmem_task'!\n");

out:
	return tid;
}

int stop_dynmem_daemon(pthread_t tid) {
	int retval;

	stop_dynmem = true;
	retval = pthread_join(tid, NULL);
	if (retval != 0) {
		lxcfs_error("stop_dynamem_daemon error!\n");
		goto out;
	}
	/*
	hdestroy();
	*/

	stop_dynmem = false;

out:
	return retval;
}
