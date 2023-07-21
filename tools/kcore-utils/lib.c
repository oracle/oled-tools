/*
 * Copyright (c) 2021, Oracle and/or its affiliates.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, see <https://www.gnu.org/licenses/>.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include "makedumpfile.h"
#include "elf_info.h"
#include "print_info.h"
//#include "lib.h"

int is_uek4 = FALSE;
int is_uek5 = FALSE;
int is_uek6 = FALSE;

#define MAX_FILE_NAME_LEN 256
#define MAX_FILE_PATH_LEN 4096
/* structure used to build dentry path */
struct path {
	char	path[MAX_FILE_PATH_LEN]; /* buffer to store path */
	int	idx; /* first offset containing meaningful data */
};

extern struct path g_path;

static void uek4_setup_offset_table()
{
	OFFSET(file_system_type.next) = 40;
	OFFSET(file_system_type.fs_supers) = 48;
	OFFSET(file_system_type.name) = 0;
	OFFSET(super_block.s_instances) = 232;
	OFFSET(super_block.s_inodes) = 168;
	OFFSET(super_block.s_mounts) = 192;
	OFFSET(super_block.s_type) = 40;
	OFFSET(inode.i_sb_list) = 272;
	OFFSET(inode.i_mapping) = 48;
	OFFSET(inode.i_dentry) = 288;
	OFFSET(inode.i_sb) = 40;
	OFFSET(inode.i_ino) = 64;
	OFFSET(address_space.nrpages) = 80;
	OFFSET(address_space.page_tree) = 8;
	OFFSET(dentry.d_u) = 176;
	OFFSET(dentry.d_parent) = 24;
	OFFSET(dentry.d_name) = 32;
	OFFSET(dentry.d_sb) = 104;
	OFFSET(dentry.d_inode) = 48;
	OFFSET(qstr.len) = 4;
	OFFSET(qstr.name) = 8;
	OFFSET(hlist_node.next) = 0;
	OFFSET(mount.mnt) = 32;
	OFFSET(mount.mnt_mp) = 232;
	OFFSET(mount.mnt_instance) = 112;
	OFFSET(vfsmount.mnt_flags) = 16;
	OFFSET(mountpoint.m_dentry) = 16;
	OFFSET(radix_tree_root.rnode) = 8;
	OFFSET(radix_tree_root.height) = 0;
	OFFSET(radix_tree_node.slots) = 40;
	OFFSET(hlist_bl_head.first) = 0;
	OFFSET(hlist_bl_node.next) = 0;
	OFFSET(dentry.d_hash) = 8;

	NUMBER(MNT_INTERNAL) = 16384;
	NUMBER(RADIX_TREE_ENTRY_MASK) = 3;
	NUMBER(RADIX_TREE_EXCEPTIONAL_ENTRY) = 2;
	NUMBER(RADIX_TREE_INTERNAL_NODE) = 1;
	NUMBER(RADIX_TREE_MAP_SIZE) = 64;
	NUMBER(NODES_PGSHIFT) = 54;
	NUMBER(NODES_MASK) = 1023;
}

/* tested confirmed kernels:
 * 	4.14.35-1902.6.6
 * 	4.14.35-1902.301.1
 */
static void uek5_setup_offset_table()
{
	OFFSET(list_head.next) = 0;
	OFFSET(file_system_type.fs_supers) = 48;
	OFFSET(file_system_type.name) = 0;
	OFFSET(file_system_type.next) = 40;
	OFFSET(super_block.s_instances) = 232;
	OFFSET(super_block.s_inodes) = 1416;
	OFFSET(super_block.s_mounts) = 192;
	OFFSET(super_block.s_type) = 40;
	OFFSET(inode.i_sb_list) = 288;
	OFFSET(inode.i_mapping) = 48;
	OFFSET(inode.i_dentry) = 320;
	OFFSET(inode.i_sb) = 40;
	OFFSET(inode.i_ino) = 64;
	OFFSET(address_space.nrpages) = 88;
	OFFSET(address_space.page_tree) = 8;
	OFFSET(dentry.d_u) = 176;
	OFFSET(dentry.d_parent) = 24;
	OFFSET(dentry.d_name) = 32;
	OFFSET(dentry.d_sb) = 104;
	OFFSET(dentry.d_inode) = 48;
	OFFSET(qstr.len) = 4;
	OFFSET(qstr.name) = 8;
	OFFSET(hlist_node.next) = 0;
	OFFSET(mount.mnt) = 32;
	OFFSET(mount.mnt_mp) = 232;
	OFFSET(mount.mnt_instance) = 112;
	OFFSET(vfsmount.mnt_flags) = 16;
	OFFSET(mountpoint.m_dentry) = 16;
	OFFSET(radix_tree_root.rnode) = 8;
	OFFSET(radix_tree_node.slots) = 40;
	OFFSET(page.flags) = 0;
	OFFSET(hlist_bl_head.first) = 0;
	OFFSET(hlist_bl_node.next) = 0;
	OFFSET(dentry.d_hash) = 8;

	NUMBER(MNT_INTERNAL) = 16384;
	NUMBER(RADIX_TREE_ENTRY_MASK) = 3;
	NUMBER(RADIX_TREE_EXCEPTIONAL_ENTRY) = 2;
	NUMBER(RADIX_TREE_INTERNAL_NODE) = 1;
	NUMBER(RADIX_TREE_MAP_SIZE) = 64;
	NUMBER(NODES_PGSHIFT) = 54;
	NUMBER(NODES_MASK) = 1023;
}

/* confirmed kernel versions:
 * 	5.4.17-2028.1
 */
static void uek6_setup_offset_table()
{
	OFFSET(list_head.next) = 0;
	OFFSET(file_system_type.fs_supers) = 64;
	OFFSET(file_system_type.name) = 0;
	OFFSET(file_system_type.next) = 56;
	OFFSET(super_block.s_instances) = 240;
	OFFSET(super_block.s_inodes) = 1416;
	OFFSET(super_block.s_mounts) = 200;
	OFFSET(super_block.s_type) = 40;
	OFFSET(inode.i_sb_list) = 280;
	OFFSET(inode.i_mapping) = 48;
	OFFSET(inode.i_dentry) = 312;
	OFFSET(inode.i_sb) = 40;
	OFFSET(inode.i_ino) = 64;
	OFFSET(address_space.nrpages) = 88;
	OFFSET(address_space.i_pages) = 8;
	OFFSET(dentry.d_u) = 176;
	OFFSET(dentry.d_parent) = 24;
	OFFSET(dentry.d_name) = 32;
	OFFSET(dentry.d_sb) = 104;
	OFFSET(dentry.d_inode) = 48;
	OFFSET(qstr.len) = 4;
	OFFSET(qstr.name) = 8;
	OFFSET(hlist_node.next) = 0;
	OFFSET(mount.mnt) = 32;
	OFFSET(mount.mnt_mp) = 232;
	OFFSET(mount.mnt_instance) = 112;
	OFFSET(vfsmount.mnt_flags) = 16;
	OFFSET(mountpoint.m_dentry) = 16;
	OFFSET(page.flags) = 0;
	OFFSET(xarray.xa_head) = 8;
	OFFSET(xa_node.slots) = 40;
	OFFSET(hlist_bl_head.first) = 0;
	OFFSET(hlist_bl_node.next) = 0;
	OFFSET(dentry.d_hash) = 8;

	NUMBER(MNT_INTERNAL) = 16384;
	NUMBER(NODES_PGSHIFT) = 54;
	NUMBER(NODES_MASK) = 1023;
	NUMBER(XA_CHUNK_SIZE) = 64;
}

void hardcode_offsets()
{
	if (is_uek4)
		uek4_setup_offset_table();
	if (is_uek5)
		uek5_setup_offset_table();
	if (is_uek6)
		uek6_setup_offset_table();
}

/* find the symbol address by looking at /proc/kallsyms */
unsigned long long proc_symbol_address(const char *sym)
{
	unsigned long long ret = 0;
	size_t len = 0, sym_len;
	ssize_t size;
	char *line, *addr, *name;
	FILE *fp;

	fp = fopen("/proc/kallsyms", "r");
	if (!fp) {
		ERRMSG("Failed to open /proc/kallsyms\n");
		return 0;
	}

	sym_len = strlen(sym);

	while ((size = getline(&line, &len, fp)) != -1) {
		addr = strtok(line, " ");
		strtok(NULL, " ");
		name = strtok(NULL, " ");
		if (name && strncmp(sym, name, sym_len) == 0) {
			if (name[sym_len] == ' ' || name[sym_len] == '\n') {
				ret = strtoull(addr, NULL, 16);
				break;
			}
		}
	}
	fclose(fp);
	free(line);
	return ret;
}

/*
 * get the randomized symbol addresses and the orginal ones
 * parameters:
 * nr_sym	-- number of symbols in sym_names to look for
 * sym_names	-- array of symbol names
 * r_addresses	-- [out] to store randomized addresses
 * addresses	-- [out] to store original addresses
 *
 * note: find_kaslr_offsets() should be called before this
 */
void symbol_addresses(int nr_sym, char *sym_names[],
		     unsigned long long *r_addresses,
		     unsigned long long *o_addresses)
{
	int i;
	for (i = 0; i < nr_sym; i++ ){
		r_addresses[i] = proc_symbol_address(sym_names[i]);
#ifdef KASLR
		o_addresses[i] = r_addresses[i] - info->kaslr_offset;
#else
		o_addresses[i] = r_addresses[i];
#endif
	}
}

#if defined(__x86_64__)
#define POINTER_PREFIX 0xff00000000000000
#else
#define POINTER_PREFIX 0x0000000000000000 /* TBD */
#endif

unsigned long
read_ulong(unsigned long long addr)
{
	unsigned long val;
	if (readmem(VADDR, addr, &val, sizeof(val)))
		return val;
	ERRMSG("read_ulong failed @%llx\n", addr);
	return 0;
}

unsigned int
read_unsigned(unsigned long long addr)
{
	unsigned int val;
	if (readmem(VADDR, addr, &val, sizeof(val)))
		return val;
	ERRMSG("read_unsigned failed @%llx\n", addr);
	return 0;
}

int
read_int(unsigned long long addr)
{
	int val;
	if (readmem(VADDR, addr, &val, sizeof(val)))
		return val;
	ERRMSG("read_unsigned failed @%llx\n", addr);
	return 0;
}

unsigned long long
good_pointer(unsigned long long p)
{
	if ((POINTER_PREFIX & p) == POINTER_PREFIX)
		return p;
	return 0;
}

unsigned long long
read_pointer(unsigned long long addr, const char *msg)
{
	unsigned long long val;

	if (readmem(VADDR, addr, &val, sizeof(val)))
		return good_pointer(val);

	ERRMSG("read_pointer Failed %s @%llx\n", msg, addr);
	return 0;
}

/* helper function to read string @ addr.
 * it at most read _len_ charactors and stop when hit a '\0',
 * force the latest a "\0".
 */
char *read_str(unsigned long long addr, char *buf, int len)
{
	int idx = 0;
	char c;

	for (idx = 0; idx < len; idx++) {
		if (!readmem(VADDR, addr + idx, &c, 1)) {
			ERRMSG("readmem failed, addr: %llx\n", addr + idx);
			return "READMEM ERR";
		}
		buf[idx] = c;
		if (c == 0)
			break;
	}
	buf[len-1] = 0;
	return buf;
}

/* used to read file system type */
#define MAX_FST_NAME_LEN 256
char fst_name_tmp[MAX_FST_NAME_LEN];
char *fst_name(unsigned long long fst)
{
	unsigned long long fst_name;

	fst_name = read_pointer(fst + OFFSET(file_system_type.name), "file_system_type.name");
	if (fst_name == 0)
		return "NO_FST_NAME_FOUND";
	return read_str(fst_name, fst_name_tmp, MAX_FST_NAME_LEN);
}

const char *fst_name_by_inode(unsigned long long inode)
{
	unsigned long long sb, fst;

	sb = read_pointer(inode + OFFSET(inode.i_sb), "inode.i_sb");
	if (!sb)
		return "SUPER BLOCK NOT FOUND";
	fst = read_pointer(sb + OFFSET(super_block.s_type), "super_block.s_type");
	if (!fst)
		return "FS TYPE NOT FOUND";
	return fst_name(fst);
}

/* get the "first" pointer for a hlist_head */
unsigned long long
hlist_head__first(unsigned long long hlist_head)
{
	return read_pointer(hlist_head + 0, "hlist_head.first");
}

/* get the "next" pointer for a hlist_node */
unsigned long long
hlist_node__next(unsigned long long hlist_node)
{
	return read_pointer(hlist_node + OFFSET(hlist_node.next),
		"hlist_node.next");
}

/* get the "next" pointer for a file_system_type */
unsigned long long
next_fst(unsigned long long fst)
{
	return read_pointer(fst + OFFSET(file_system_type.next),
		"file_system_type.next");
}

/* get the "next" for a list_head */
unsigned long long
list_head__next(unsigned long long list_head)
{
	return read_pointer(list_head + OFFSET(list_head.next),
		"list_head.next");
}

struct path g_path;

static void init_path(struct path *path)
{
	path->path[MAX_FILE_PATH_LEN - 1] = 0;
	path->idx = MAX_FILE_PATH_LEN - 1;
}

static int is_btrfs_sub_volume(unsigned long long dentry)
{
	unsigned long long parent, sb, fst;
	char *name;

	parent = read_pointer(dentry + OFFSET(dentry.d_parent),
			"dentry.d_parent");
	if (!parent) {
		// shouldn't happen
		MSG("no parent\n");
		return 0;
	}

	dentry = parent;
	parent = read_pointer(dentry + OFFSET(dentry.d_parent),
			"dentry.d_parent");

	if (parent != dentry)
		return 0;

	sb = read_pointer(dentry + OFFSET(dentry.d_sb), "dentry.d_sb");
	if (!sb) {
		// shouldn't happen
		MSG("No sb\n");
		return 0;
	}

	fst = read_pointer(sb + OFFSET(super_block.s_type),
		"super_block.s_type");

	name = fst_name(fst);
	if (strcmp(name, "btrfs") != 0)
		return 0;
	return 1;
}

/* returns positive value indicating error happened or
 * the path buffer is full and should stop.
 * make use of global data, multiple thread not safe.
 */
static int add_dentry_to_path(unsigned long long dentry, struct path *path)
{
	unsigned long long dname = dentry + OFFSET(dentry.d_name);
	int len = read_unsigned(dname + OFFSET(qstr.len));
	char *name, tmp[MAX_FILE_NAME_LEN+1];
	unsigned long long name_addr;
	int err = 0;

	/* btrfs is a special case. It has sub-volume dentries which shouldn't
	   be used to build file path.
	*/
	if (is_btrfs_sub_volume(dentry))
		return 0;

	if (len == 0) {
		name = "BAD_NAME_LEN_0";
		len = strlen(name);
		ERRMSG("Unexpected 0 length file name\n");
		ERRMSG("dentry=%llx\n", dentry);
		return 1;
	}

	if (len > MAX_FILE_NAME_LEN) {
		ERRMSG("File name is too long: %d, cutting to %d\n",
			len, MAX_FILE_NAME_LEN);
		len = MAX_FILE_NAME_LEN;
	}
	name_addr = read_pointer(dname + OFFSET(qstr.name), "qstr.name");
	if (name_addr && readmem(VADDR, name_addr, &tmp[1], len)) {
		name = tmp;
		name[0] = '/';
		len += 1; /* for the extra / */
	} else {
		name = "NO NAME";
		len = strlen(name);
		err = 1;
	}

	if (len > (path->idx + 1)) {
		len = path->idx + 1;
		ERRMSG("Too long file path, over 4096");
		err = 1;
	}
	path->idx -= len;
	memcpy(path->path + path->idx, name, len);

	return err;
}

unsigned long long
get_first_mount_from_sb(unsigned long long sb)
{
	unsigned long long list_head = sb + OFFSET(super_block.s_mounts);
	unsigned long long next, mount;
	int mnt_flags;

	next = list_head__next(list_head);
	for (; next != list_head; next = list_head__next(next)) {
		mount = next - OFFSET(mount.mnt_instance);
		mnt_flags = read_int(mount + OFFSET(mount.mnt) + OFFSET(vfsmount.mnt_flags));
		if (mnt_flags & NUMBER(MNT_INTERNAL))
			continue;
		return mount;
	}
	return 0;
}

char *dentry_path(unsigned long long dentry)
{
	unsigned long long parent, sb, mount, mnt_point;
	int stop;

	init_path(&g_path);
	do {
		parent = read_pointer(dentry + OFFSET(dentry.d_parent),
					"dentry.d_parent");

		if (parent == dentry) {
			/* root for current mount, look for upper mount */
			sb = read_pointer(dentry + OFFSET(dentry.d_sb), "dentry.d_sb");
			if (sb == 0)
				break;
			mount = get_first_mount_from_sb(sb);
			if (mount == 0)
				break;

			mnt_point = read_pointer(mount + OFFSET(mount.mnt_mp), "mount.mnt_mp");
			if (mnt_point == 0)
				break;

			dentry = read_pointer(mnt_point + OFFSET(mountpoint.m_dentry), "mountpoint.m_dentry");
		} else {
			stop = add_dentry_to_path(dentry, &g_path);
			if (stop)
				break;
			dentry = parent;
		}
	} while (dentry);

	return &g_path.path[g_path.idx];
}

struct stack_node {
	unsigned long long node;	/* radix_tree_node */
	unsigned int height;            /* radix tree height, for uek4 */
	struct stack_node *next;
};

struct stack_node *stack_head = NULL;
static void push(struct stack_node *node)
{
	node->next = stack_head;
	stack_head = node;
}

static struct stack_node *pop()
{
	struct stack_node *ret = stack_head;
	if (stack_head)
		stack_head = stack_head->next;
	return ret;
}

static struct stack_node *alloc_stack_node(unsigned long long node)
{
	struct stack_node *ret;

	ret = malloc(sizeof(*ret));
	if (ret) {
		ret->node = node;
		ret->next = NULL;
	} else {
		ERRMSG("Alloc stack node failed\n");
	}
	return ret;
}

/* if node is a radix tree internal node */
static int is_internal_node(unsigned long long node)
{
	return (node & NUMBER(RADIX_TREE_ENTRY_MASK)) == NUMBER(RADIX_TREE_INTERNAL_NODE);
}

/* if node is a exceptional tree internal node */
static int is_exceptional_node(unsigned long long node)
{
	return (node & NUMBER(RADIX_TREE_EXCEPTIONAL_ENTRY));
}

/* walk radix tree, call the callback function for each entries,
 * root is the address of radix_tree_root structure
 * func is the callback function,
 * param is the second parameter for func
 * return 0 for success, error otherwise
 * for uek4
 */
int walk_radix_tree_uek4(unsigned long long root,
			 int (*func)(unsigned long long, void *),
			 void *param)
{
	struct stack_node *stack_node;
	unsigned long long node, slots;
	unsigned int height;
	int i;

	if (root == 0)
		return 0;

	node = read_pointer(root + OFFSET(radix_tree_root.rnode), "radix_tree_root.rnode");
	if (node == 0)
		return 0;

	height = read_unsigned(root + OFFSET(radix_tree_root.height));
	if (height == 0)
		return 0;

	stack_node = alloc_stack_node(node);
	if (!stack_node)
		return -1;

	stack_node->height = height;
	push(stack_node);
	for (stack_node = pop(); stack_node; stack_node = pop()) {
		node = stack_node->node;
		height = stack_node->height;
		free(stack_node);

		node &= ~NUMBER(RADIX_TREE_ENTRY_MASK);
		slots = node + OFFSET(radix_tree_node.slots);
		for (i = 0; i < NUMBER(RADIX_TREE_MAP_SIZE); i++) {
			node = slots + i * sizeof(unsigned long long);
			node = read_pointer(node, "slots + i * xxx");
			if (!node)
				continue;
			/* objects */
			if (height == 1) {
				func(node, param);
				continue;
			}

			/* internal node */
			stack_node = alloc_stack_node(node);
			if (!stack_node)
				goto failed;
			stack_node->height = height - 1;
			push(stack_node);
		}
	}
	return 0;
failed:
	for (stack_node = pop(); stack_node; stack_node = pop())
		free(stack_node);
	return -1;
}

/* walk radix tree, call the callback function for each entries,
 * root is the address of radix_tree_root structure
 * func is the callback function,
 * param is the second parameter for func
 * return 0 for success, error otherwise
 * for uek5
 */
int walk_radix_tree_uek5(unsigned long long root,
			 int (*func)(unsigned long long, void *),
			 void *param)
{
	struct stack_node *stack_node;
	unsigned long long node, slots;
	int i;

	if (root == 0)
		return 0;

	node = read_pointer(root + OFFSET(radix_tree_root.rnode), "radix_tree_root.rnode");
	if (node == 0)
		return 0;

	stack_node = alloc_stack_node(node);
	if (!stack_node)
		return -1;

	for (; stack_node; stack_node = pop()) {
		node = stack_node->node;
		free(stack_node);

		if (is_exceptional_node(node))
			continue;

		if (!is_internal_node(node)) {
			func(node, param);
			continue;
		}

		/* now is internal node */
		node &= ~NUMBER(RADIX_TREE_ENTRY_MASK);
		slots = node + OFFSET(radix_tree_node.slots);
		for (i = 0; i < NUMBER(RADIX_TREE_MAP_SIZE); i++) {
			node = slots + i * sizeof(unsigned long long);
			node = read_pointer(node, "slots + i * xxx");
			if (!node)
				continue;
			stack_node = alloc_stack_node(node);
			if (!stack_node)
				goto failed;
			push(stack_node);
		}
	}

	return 0;
failed:
	for (stack_node = pop(); stack_node; stack_node = pop())
		free(stack_node);
	return -1;
}

static int xa_is_internal(unsigned long long node)
{
	return (node & 3) == 2;
}

int walk_xarray(unsigned long long xarray,
		int (*func)(unsigned long long, void *),
		void *param)
{
	struct stack_node *stack_node;
	unsigned long long node, slots;
	int i;

	if (xarray == 0)
		return 0;

	node = read_pointer(xarray + OFFSET(xarray.xa_head), "xarray.xa_head");
	if (node == 0)
		return 0;

	stack_node = alloc_stack_node(node);
	if (!stack_node)
		return -1;

	for (; stack_node; stack_node = pop()) {
		node = stack_node->node;
		if (!xa_is_internal(node)) {
			func(node, param);
			continue;
		}

		node -= 2;
		slots = node + OFFSET(xa_node.slots);
		for (i = 0; i < NUMBER(XA_CHUNK_SIZE); i++) {
			node = slots + i * sizeof(unsigned long long);
			node = read_pointer(node, "slots + i * xxx");
			if (!node)
				continue;
			stack_node = alloc_stack_node(node);
			if (!stack_node)
				goto failed;
			push(stack_node);
		}
	}
	return 0;
failed:
	for (stack_node = pop(); stack_node; stack_node = pop())
		free(stack_node);
	return -1;
}

void free_info(struct DumpInfo *info)
{
	if (info) {
		if (info->dh_memory)
			free(info->dh_memory);
		if (info->kh_memory)
			free(info->kh_memory);
		if (info->valid_pages)
			free(info->valid_pages);
		if (info->bitmap_memory)
			free(info->bitmap_memory);
		if (info->fd_memory)
			close(info->fd_memory);
		if (info->fd_dumpfile)
			close(info->fd_dumpfile);
		if (info->fd_bitmap)
			close(info->fd_bitmap);
		if (vt.node_online_map != NULL)
			free(vt.node_online_map);
		if (info->mem_map_data != NULL)
			free(info->mem_map_data);
		if (info->dump_header != NULL)
			free(info->dump_header);
		if (info->splitting_info != NULL)
			free(info->splitting_info);
		if (info->p2m_mfn_frame_list != NULL)
			free(info->p2m_mfn_frame_list);
		if (info->page_buf != NULL)
			free(info->page_buf);
		free(info);
		info = NULL;
	}
	free_elf_info();
}

struct DumpInfo *init_core(int argc, char *argv[], int kexec_mode)
{
	if ((info = calloc(1, sizeof(struct DumpInfo))) == NULL) {
		ERRMSG("Can't allocate memory for the pagedesc cache. %s.\n",
		    strerror(errno));
		goto bad_out;
	}
	if ((info->dump_header = calloc(1, sizeof(struct disk_dump_header)))
	    == NULL) {
		ERRMSG("Can't allocate memory for the dump header. %s\n",
		    strerror(errno));
		goto bad_out;
	}
	initialize_tables();

	/*
	 * By default, makedumpfile works in constant memory space.
	 */
	info->flag_cyclic = TRUE;

	/*
	 * By default, makedumpfile try to use mmap(2) to read /proc/vmcore.
	 */
	info->flag_usemmap = MMAP_TRY;

	info->block_order = DEFAULT_ORDER;

	if (!check_param_for_creating_dumpfile(argc, argv)) {
		ERRMSG("Commandline parameter is invalid.\n");
		goto bad_out;
	}

	if (!open_files_for_creating_dumpfile()) {
		ERRMSG("open_files_for_creating_dumpfile failed\n");
		goto bad_out;
	}

	if (!get_elf_info(info->fd_memory, info->name_memory)) {
		ERRMSG("get_elf_info failed\n");
		goto bad_out;
	}

	if (!get_page_offset()) {
		ERRMSG("get_page_offset failed\n");
		goto bad_out;
	}

	if (!kexec_mode) {
		uint64_t vmcoreinfo_addr, vmcoreinfo_len;
		if (!get_sys_kernel_vmcoreinfo(&vmcoreinfo_addr,
					       &vmcoreinfo_len)) {
			ERRMSG("get_sys_kernel_vmcoreinfo failed\n");
			goto bad_out;
		}

		if (!set_kcore_vmcoreinfo(vmcoreinfo_addr, vmcoreinfo_len)) {
			ERRMSG("set_kcore_vmcoreinfo failed\n");
			goto bad_out;
		}

		if (!get_kcore_dump_loads())
			goto bad_out;
	}

	if (!initial()) {
		ERRMSG("initial failed\n");
		goto bad_out;
	};

	return info;
bad_out:
	free_info(info);
	return NULL;
}

int is_supported_kernel()
{
	if (info->kernel_version == KERNEL_VERSION(4,1,12))
		is_uek4 = TRUE;
	else if (info->kernel_version == KERNEL_VERSION(4,14,35))
		is_uek5 = TRUE;
	else if (info->kernel_version == KERNEL_VERSION(5,4,17))
		is_uek6 = TRUE;

	if (!is_uek4 && !is_uek5 && !is_uek6) {
		ERRMSG("kernel not supported: %s\n", info->release);
		return 0;
	}
	return 1;
}
