/* lxcfs
 *
 * Author: River <river@vvl.me>
 *
 * See COPYING file for details.
 */

#ifndef __LXCFS_DYNAMIC_MEMORY_H
#define __LXCFS_DYNAMIC_MEMORY_H

#include "uthash.h"

/* memory water mark structure, unit is kbytes */
struct water_mark {
	unsigned long min;
	unsigned long low;
	unsigned long high;
};

struct dynmem_args {
	char *mc_mount;
	char *base_path;
};

/**
 * hash map structure
 *
 * @id: key
 * @hh: make this structure hashable
 */
struct hash_map {
	long int id;
	struct {
		char cg[512];
		unsigned long int soft_limit;
		unsigned long int hard_limit;
	} value;
	UT_hash_handle hh;
};

#endif /* __LXCFS_DYNAMIC_MEMORY_H */
