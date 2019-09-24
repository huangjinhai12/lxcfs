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
#include <string.h>
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
 *   @mark: memory water mark, unit kbytes
 *   @total: memory size, unit is kbyte
 */
static void cal_mem_watermark(struct water_mark *mark, unsigned long total) {
	mark->min = (4 * (unsigned long)sqrtf((float)total));
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
	unsigned long extra_mem_bytes = 0;

	if (sysinfo(&info) != 0) {
		lxcfs_error("sysinfo fails!\n");
		goto out;
	}

	/* free memory is low, don't alloc extra memory for container */
	if (info.freeram/1024 <= phy_water_mark.low)
		goto out;

	// TODO
out:
	return ((unsigned long)extra_mem_bytes/(8*1024)) * (8*1024);
}

static void increase_mem_limit(const char *cg, const unsigned long memlimit,
							   const unsigned long memusage) {
	struct water_mark task_water_mark;
	unsigned long free_kbytes, extra_mem_bytes;

	free_kbytes = (memlimit - memusage) / 1024;
	cal_mem_watermark(&task_water_mark, memlimit/1024);

	lxcfs_debug("task_water_mark: min=%lu, low=%lu, high=%lu.\n",
				task_water_mark.min, task_water_mark.low, task_water_mark.high);
	lxcfs_debug("free_kbytes=%lu\n", free_kbytes);

	if ((free_kbytes <= task_water_mark.low)
			&& (extra_mem_bytes = extra_mem_limit(memlimit, memusage)) > 0) {
		if (!set_mem_limit(cg, memlimit, extra_mem_bytes))
			lxcfs_error("set_mem_limit('%s', '%lu', '%lu') fails!\n",
						cg, memlimit, extra_mem_bytes);
	}
}

void *dynmem_task(void *arg) {
	const char *mc_mount = ((struct dynmem_args *)arg)->mc_mount;
	const char *base_path = ((struct dynmem_args *)arg)->base_path;
	unsigned long memlimit, memusage;
	struct dirent *direntp;
	clock_t time1, time2;
	char cg[512];
	int cfd, fd;
	DIR *dirp;

	if (!find_mounted_controller("memory", &cfd)) {
		lxcfs_error("find_mounted_controller('memory') fails!\n");
		goto out;
	}

	if ((fd = openat(cfd, mc_mount, O_RDONLY)) < 0) {
		lxcfs_error("openat('%s') fails.\n", mc_mount);
		goto out;
	}
	if ((dirp = fdopendir(fd)) == NULL) {
		lxcfs_error("'fdopendir fails!\n");
		goto out;
	}

	while (!stop_dynmem) {
		time1 = clock();

		lxcfs_debug("start dynamic memory task.\n");
		seekdir(dirp, 0);

		/* traversal directory that length is 64 which is docker created */
		while ((direntp = readdir(dirp)) != NULL) {
			if (!(direntp->d_type == DT_DIR)
					|| !(strlen(direntp->d_name) == 64))
				continue;

			snprintf(cg, sizeof(cg)-1, "/%s/%s", mc_mount, direntp->d_name);

			/* the direcity may be not used by container */
			if (((memlimit = get_mem_limit(cg)) == -1)
				|| ((memusage = get_mem_usage(cg)) == -1))
				continue;

			lxcfs_debug("'%s': memlimit=%lu memusage=%lu\n",
						cg, memlimit, memusage);

			increase_mem_limit(cg, memlimit, memusage);
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

	cal_mem_watermark(&phy_water_mark, info.totalram/1024);
out:
	return retval;
}

pthread_t dynmem_daemon(char *mc_mount) {
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

	retval = pthread_create(&tid, NULL, dynmem_task, mc_mount);
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
