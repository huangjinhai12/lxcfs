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
#include <signal.h>
#include <stdlib.h>
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
static struct hash_map *bucket = NULL;

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
 * all unit is byte
 * TODO:
 *  research why the increase size must be 1024*8*N?
 *   return 1024*8*extra_mem bytes because of cgroup memory controller
 */
static unsigned long next_mem_limit(struct hash_map *node,
									 unsigned long memlimit,
									 unsigned long memusage) {
	struct sysinfo info;
	unsigned long int delta = 0, free_bytes;
    unsigned long int next_mem = memlimit;
	struct water_mark cg_mark;

	if (sysinfo(&info) != 0) {
		lxcfs_error("sysinfo fails!\n");
		goto out;
	}

	/* free memory is low, don't alloc extra memory for container */
	if (info.freeram/1024 <= phy_water_mark.low)
		goto out;

	free_bytes = (memlimit - memusage) / 1024;
	cal_mem_watermark(&cg_mark, memusage/1024);

	if (free_bytes < cg_mark.low) {
		delta = (node->value.hard_limit - memlimit) / (1 << node->value.count);
		delta = delta & 0xffffffffffff2000;
		lxcfs_v("delta: %ld\n", delta);
		if ((info.freeram-delta)/1024 >= phy_water_mark.min
			&& delta >= MIN_MEM) {
			next_mem += delta;
			node->value.count += 1;
		}
	} else if (free_bytes > cg_mark.high) {
		delta = ((unsigned long)((memlimit - memusage) * 0.1)) & 0xffffffffffff2000;
		delta = delta >= MIN_MEM ? delta : MIN_MEM;
		if (delta > 0) {
			next_mem = memlimit - delta;
			next_mem = next_mem < node->value.soft_limit ? \
							node->value.soft_limit : next_mem;
			node->value.count -= 1;
			node->value.count = node->value.count >= 1 ? node->value.count : 1;
		}
	}

	lxcfs_debug("'%s' next_mem = %lu\n", node->value.cg, next_mem);

out:
	return next_mem;
}

static void increase_mem_limit(const char *cg, const long int key,
							   const unsigned long mem_hardlimit,
							   const unsigned long mem_softlimit,
							   const unsigned long memusage) {
	unsigned long next_mem, memlimit;
	struct hash_map *node;

	memlimit = mem_hardlimit < mem_softlimit ? mem_hardlimit : mem_softlimit;

	HASH_FIND_LONG(bucket, &key, node);
	if (node == NULL) {
		node = malloc(sizeof(struct hash_map));
		node->id = key;
		strcpy(node->value.cg, cg);
		node->value.soft_limit = memlimit;
		node->value.hard_limit = 4 * \
						((memlimit + MIN_MEM-1) & 0xffffffffffff2000);
		node->value.count = 1;
		HASH_ADD_LONG(bucket, id, node);
		if (!set_mem_limit(cg, "memory.soft_limit_in_bytes", memlimit)) {
			lxcfs_error("set '%s' memory.soft_limit_in_bytes fails!\n", cg);
			HASH_DEL(bucket, node);
			free(node);
			return;
		}
	}

	next_mem = next_mem_limit(node, memlimit, memusage);
	if (next_mem != mem_hardlimit && \
			!set_mem_limit(cg, "memory.limit_in_bytes", next_mem))
		lxcfs_error("set_mem_limit('%s') fails!\n", cg);
}

static bool lxcfs_in_task(const char *lxcfs_mount, pid_t pid, bool *result)
{
	FILE *filp;
	char fina[32], *line = NULL;
	size_t len;
	bool retval = false;

	*result = false;
	if (snprintf(fina, sizeof(fina), "/proc/%d/mounts", pid) >= sizeof(fina))
		goto out;
	if ((filp = fopen(fina, "r")) == NULL)
		goto out;

	while (getline(&line, &len, filp) != -1) {
		if (strstr(line, lxcfs_mount) != NULL) {
			*result = true;
			break;
		}
	}

	free(line);
	fclose(filp);

	if (kill(pid, 0) == 0)
		retval = true;
out:
	return retval;
}

static bool is_active_lxcfs(const char *lxcfs_mount, const char *cg) {
	bool retval = false;
	pid_t pid;
	int fd;
	FILE *filp;

	fd = get_tasks_fd("memory", cg);
	if (fd < 0) {
		lxcfs_error("get '%s/tasks' fd fails!\n", cg);
		goto out;
	}

	if ((filp = fdopen(fd, "r")) == NULL) {
		close(fd);
		goto out;
	}

	while (!feof(filp)) {
		if (fscanf(filp, "%d", &pid) != 1)
			continue;

		if (lxcfs_in_task(lxcfs_mount, pid, &retval)) {
			break;
		}
	}

	lxcfs_v("'%s' enable lxcfs? '%s'\n", cg, retval ? "true" : "false");

	fclose(filp);
out:
	return retval;
}

static void kick_off_unused_cg(void) {
	struct hash_map *node, *tmp;
	int fd;

	lxcfs_v("there are %u node in bucket\n", HASH_COUNT(bucket));

	HASH_ITER(hh, bucket, node, tmp) {

		if ((fd = get_tasks_fd("memory", node->value.cg)) < 0) {
			lxcfs_v("kick off '%s'\n", node->value.cg);
			HASH_DEL(bucket, node);
			free(node);
		} else
			close(fd);
	}
}

void *dynmem_task(void *arg) {
	const char *mc_mount = ((struct dynmem_args *)arg)->mc_mount;
	const char *base_path = ((struct dynmem_args *)arg)->base_path;
	unsigned long mem_softlimit, mem_hardlimit, memusage;
	struct dirent *direntp;
	clock_t time1, time2;
	char cg[512], key_str[16];
	long int key;
	int cfd, fd, len;
	int cycle = 0;
	DIR *dirp = NULL;

	if (!find_mounted_controller("memory", &cfd)) {
		lxcfs_error("find_mounted_controller('memory') fails!\n");
		goto out;
	}

	while (!stop_dynmem) {
		time1 = clock();

		if ((fd = openat(cfd, mc_mount, O_RDONLY)) < 0
					|| (dirp = fdopendir(fd)) == NULL) {
			lxcfs_v("openat('%s') fails.\n", mc_mount);
			continue;
		} else {

			/* traversal directory that length is 64 which is docker created */
			while ((direntp = readdir(dirp)) != NULL) {
				if (!(direntp->d_type == DT_DIR)
						|| !(strlen(direntp->d_name) == 64))
					continue;

				len = snprintf(cg, sizeof(cg), "/%s/%s",
							   mc_mount, direntp->d_name);
				if (len < 0 || len > sizeof(cg)) {
					lxcfs_error("filename '/%s/%s' is too long!\n",
								mc_mount, direntp->d_name);
					continue;
				}

				/* the direcity may be not used by container */
				if (!is_active_lxcfs(base_path, cg)
					|| ((mem_hardlimit = get_mem_limit(cg, false)) == -1)
					|| ((mem_softlimit = get_mem_limit(cg, true)) == -1)
					|| ((memusage = get_mem_usage(cg)) == -1))
					continue;

				lxcfs_debug("'%s': "
							"mem_hardlimit=%lu mem_softlimit=%lu memusage=%lu\n",
							cg, mem_hardlimit, mem_softlimit, memusage);

				strncat(key_str, direntp->d_name, 12);
				key = strtol(key_str, NULL, 16);

				increase_mem_limit(cg, key,
								   mem_hardlimit, mem_softlimit, memusage);
			}

			cycle = (cycle + 1) % 16;
			if (cycle == 0)
				kick_off_unused_cg();
		}

		while ((closedir(dirp) == -1) && (errno == EINTR))
			continue;

		time2 = clock();
		usleep(FLUSH_TIME * 1000000 - \
			   (int)((time2 - time1) * 1000000 / CLOCKS_PER_SEC));
	}

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
