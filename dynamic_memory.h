/* lxcfs
 *
 * Author: River <river@vvl.me>
 *
 * See COPYING file for details.
 */

#ifndef __LXCFS_DYNAMIC_MEMORY_H
#define __LXCFS_DYNAMIC_MEMORY_H

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

#endif /* __LXCFS_DYNAMIC_MEMORY_H */
