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

#include <unistd.h>
#include <sys/types.h>

#include "makedumpfile.h"
#include "elf_info.h"
#include "print_info.h"
#include "lib.h"

const char *version_str = "1.1";
/* version history:
 * 1.0	-- the first version
 * 1.1	-- support the run in kexec mode, looks at /proc/vmcore, rather than
 *         /proc/kcore.
 */

/* structure used to sort files */
struct sort_entry {
	int			nrpages;
	unsigned long long	inode;
};

/* store ordered dentries, larger nrpages first */
static struct sort_entry *sort_entries;
/* how many dentries stored in sort_entries */
static int len_sort_entries;
/* capacity of sort_entries */
static int max_sort_entries;

/* find the position, in sort_entries, where we should put new entry with
 * nrpages. Binary searching is used.
 */
static int find_position(int nrpages)
{
	int lower = 0, upper = len_sort_entries - 1;
	int index, val;

	if (len_sort_entries == 0)
		return 0;

	while (lower + 1 < upper) {
		index = (lower + upper) / 2;
		val = sort_entries[index].nrpages;
		if (val == nrpages)
			return index;
		if (val > nrpages)
			lower = index;
		else
			upper = index;
	}

	if (nrpages >= sort_entries[lower].nrpages)
		return lower;
	if (nrpages > sort_entries[upper].nrpages)
		return upper;
	return upper + 1;
}

static void sort_insert_inode(int nrpages, unsigned long long inode)
{
	int pos, copy_size = 0;

	pos = find_position(nrpages);

	if (pos > max_sort_entries) {
		/* this shouldn't happen */
		ERRMSG("BUG, pos >= max_sort_entries, pos=%d max_sort_entries="
			"%d\n", pos, max_sort_entries);
		return;
	}

	/* nrpages too small, ignore it */
	if (pos == max_sort_entries)
		return;

	if (len_sort_entries == 0)
		copy_size = 0;
	else
		copy_size = len_sort_entries  - pos;

	if (len_sort_entries == max_sort_entries)
		copy_size--;

	if (pos + 1 + copy_size > max_sort_entries)
		MSG("BUG, memory over range access: pos: %d copy_size: %d "
		    "max_sort_entries: %d\n", pos, copy_size, max_sort_entries);

	copy_size *= sizeof(struct sort_entry);

	/* shifts entries after pos backwards by one slot */
	memmove(&sort_entries[pos + 1], &sort_entries[pos], copy_size);
	sort_entries[pos].nrpages = nrpages;
	sort_entries[pos].inode = inode;
	if (len_sort_entries < max_sort_entries)
		len_sort_entries++;
}

static inline void add_inode(int nrpages, unsigned long long inode)
{
	sort_insert_inode(nrpages, inode);
}

#define ONEGB (1024*1024*1024)
#define ONEMB (1024*1024)
#define ONEKB 1024
char size_buf[128];
char *page_size_good_unit(unsigned long nr_pages)
{
	double size_b = nr_pages * (info->page_size);
	if (size_b >= ONEGB) {
		sprintf(size_buf, "%.2fGB", size_b / ONEGB);
	} else if (size_b >= ONEMB) {
		sprintf(size_buf, "%.2fMB", size_b / ONEMB);
	} else {
		sprintf(size_buf, "%.2fKB", size_b/ ONEKB);
	}
	return size_buf;
}

struct stack_node {
	unsigned long long node;	/* radix_tree_node */
	unsigned int height;            /* radix tree height, for uek4 */
	struct stack_node *next;
};

char *skip_FSs[] = {"hugetlbfs", "bdev"};
static int should_skip_fs(const char *fs_name)
{
	char *name;
	int i;

	for (i = 0; i < sizeof(skip_FSs) / sizeof(skip_FSs[0]); i++) {
		name = skip_FSs[i];
		if (strcmp(name, fs_name) == 0)
			return 1;
	}
	return 0;
}

static inline int inode_dump(unsigned long long inode, int pageLimit)
{
	unsigned long long address_space;
	unsigned long nrpages;

	address_space = inode + OFFSET(inode.i_mapping);
	address_space = read_pointer(address_space, "address_space");
	if (address_space == 0)
		return 0;

	nrpages = read_ulong(address_space + OFFSET(address_space.nrpages));
	if (nrpages < pageLimit)
		return 0;

	add_inode(nrpages, inode);
	return 1;
}

// super_block
static inline void sb_dump(unsigned long long sb, int pageLimit)
{
	unsigned long long list_head = sb, next, last, inode;
	long offset = OFFSET(inode.i_sb_list);

	last = list_head += OFFSET(super_block.s_inodes);
	next = list_head__next(list_head);
	/* also checking last avoid a simple infinate loop */
	for(; next != list_head && next != last; last = next, next = list_head__next(next)) {
		inode = next - offset;
		inode_dump(inode, pageLimit);
	}
}

static inline void fst_dump(unsigned long long fst, int pageLimit)
{
	unsigned long long hlist_head = fst, hlist_node, sb;
	unsigned long offset = OFFSET(super_block.s_instances);

	hlist_head += OFFSET(file_system_type.fs_supers);
	hlist_node = hlist_head__first(hlist_head);
	for (; hlist_node; hlist_node = hlist_node__next(hlist_node)) {
		sb = hlist_node - offset;
		sb_dump(sb, pageLimit);
	}
}

int nr_online_nodes = 0;

static int page_cb(unsigned long long addr, void *param)
{
	unsigned long flags;
	int *nodes = param;
	int node;

	flags = read_ulong(addr + OFFSET(page.flags));
	node = (flags >> NUMBER(NODES_PGSHIFT)) & NUMBER(NODES_MASK);
	if (node >= nr_online_nodes) {
		ERRMSG("BUG, node should less than nr_online_nodes\n");
		return 0;
	}
	nodes[node] += 1;
	// MSG("page: %llx\n", addr);
	return 0;
}

/*
 * fst: address of symbol file_systems. pass 0 for running kernel.
 */
int filecache_dump(int topN, int pageLimit, int numa,
		   unsigned long long *r_addresses)
{
	unsigned long long fst = r_addresses[0];
	unsigned long long nr_on = r_addresses[1];
	int i, *numa_pages = NULL;
	unsigned long offset;

	MSG("kernel version: %s\n", info->release);
	MSG("filecache version: %s\n", version_str);

	if (!is_supported_kernel())
		return -1;

	hardcode_offsets();

	max_sort_entries = topN;
	len_sort_entries = 0;
	sort_entries = malloc(sizeof(struct sort_entry) * max_sort_entries);
	if (!sort_entries) {
		MSG("Failed to allocate memory for sort: topN=%d\n", topN);
		return -1;
	}

	fst = read_pointer(fst, "file_systems");
	if (0 == fst) {
		free(sort_entries);
		ERRMSG("Invalid address of file_systems passed in\n");
		return -1;
	}

	for (; fst; fst = next_fst(fst)) {
		const char *type_name = fst_name(fst);
		if (should_skip_fs(type_name))
			continue;
		fst_dump(fst, pageLimit);
	}

	if (numa) {
		nr_online_nodes = read_int(nr_on);
		MSG("Number of NUMA nodes in this system: %d\n", nr_online_nodes);
		/* single node system -- all from node 0, skip */
		if (nr_online_nodes > 1) {
			numa_pages = malloc(nr_online_nodes * sizeof(*numa_pages));
			if (!numa_pages) {
				ERRMSG("Failed to allocate memory"
				       " for numa\n");
			}
		} else {
			MSG("Numa info skipped\n");
		}
	}

	MSG("Top %d page cache consumer files:\n", len_sort_entries);
	if (numa) {
		MSG("PAGES  SIZE    FS_TYPE   FILE    NUMA_STATS\n");
		MSG("-----  ------  -------   ------  ------------\n");
	} else {
		MSG("PAGES  SIZE    FS_TYPE   FILE\n");
		MSG("-----  ------  -------   ------\n");
	}

	offset = OFFSET(dentry.d_alias);
	if (offset == NOT_FOUND_LONG_VALUE)
		offset = OFFSET(dentry.d_u);

	for (i = 0; i < len_sort_entries; i++, printf("\n")) {
		unsigned long long inode, dentry = 0, address_space, hlist_head, hlist_node;
		struct sort_entry *e;
		char *path;
		char tmp_path[64];
		int j;

		e = &sort_entries[i];
		inode = e->inode;
		hlist_head = inode + OFFSET(inode.i_dentry);
		hlist_node = hlist_head__first(hlist_head);

		if (hlist_node) {
			while (hlist_node) {
				dentry = hlist_node - offset;
				path = dentry_path(dentry);
				if (strlen(path) > 0)
					break;
				hlist_node = hlist_node__next(hlist_node);
			}
		} else {
			/* no dentry attached so not able to get a path,
			 * we use a <inode number> for this case.
			 * use g_path.path to store it.
			 */
			unsigned long inode_num;

			inode_num = read_ulong(inode + OFFSET(inode.i_ino));
			sprintf(tmp_path, "[inode# %lu]", inode_num);
			path = tmp_path;
		}

		MSG("%d  %s  %s  %s", e->nrpages,
		    page_size_good_unit(e->nrpages),
		    fst_name_by_inode(inode),
		    path);

		if (!numa || !numa_pages ||!dentry)
			continue;

		address_space = read_pointer(inode + OFFSET(inode.i_mapping), "inode.i_mapping");
		if (address_space == 0)
			continue;

		memset(numa_pages, 0, nr_online_nodes * sizeof(*numa_pages));
		if (is_uek4)
			walk_radix_tree_uek4(address_space + OFFSET(address_space.page_tree), page_cb, numa_pages);
		else if (is_uek5)
			walk_radix_tree_uek5(address_space + OFFSET(address_space.page_tree), page_cb, numa_pages);
		else
			walk_xarray(address_space + OFFSET(address_space.i_pages), page_cb, numa_pages);

		MSG(" NUMA ");
		for (j = 0; j < nr_online_nodes; j++) {
			if (j == nr_online_nodes - 1)
				MSG("Node[%d]:%d", j, numa_pages[j]);
			else
				MSG("Node[%d]:%d, ", j, numa_pages[j]);
		}
	}
	free(sort_entries);

	return 0;
}

static void show_help()
{
	MSG("filecache: List the file paths of the biggest page cache consumers on this system.\n");
	MSG("Usage: oled filecache [-n] [-m] [-u] [-k] [-h] [-v]\n");
	MSG("Options:\n");
	MSG("   -n, --topn <number>        report top <number> files, 50 by default\n");
	MSG("   -m, --min <number>         report files with <number> or more pages in the cache, 1000 by default\n");
	MSG("   -u, --numa                 report per-NUMA-node statistics, disabled by default\n");
	MSG("   -k, --kexec                report top files for crashed production kernel\n");
	MSG("   -h, --help                 show this message\n");
	MSG("   -v, --version              show version\n");
	MSG("\n");
	MSG("Note: Works on Oracle UEK4/UEK5/UEK6 kernels only. Check the man page for more information.\n");
	MSG("\n");
}

static struct option longopts[] = {
	{"topn", required_argument, NULL, 'n'},
	{"min", required_argument, NULL, 'm'},
	{"numa", no_argument, NULL, 'u'},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'v'},
	{"kexec", no_argument, NULL, 'k'},
	{0, 0, 0, 0}
};

static char *shortopts = "n:m:uhvk";

int
main(int argc, char *argv[])
{
#define NR_SYM 2
	int opt, topn=50, pagelimit=1024, numa=0, i, help = 0, version = 0;
	// symbols to look for
	char *sym_names[] = {"file_systems", "nr_online_nodes"};
	// randomized addresses
	unsigned long long r_addresses[NR_SYM];
	// original addresses
	unsigned long long o_addresses[NR_SYM];
	char *real_args[12];
	int kexec_mode = 0;
	int opt_args = 0;
	int core_idx;
	int ret = -1;
	uid_t uid;

	message_level = DEFAULT_MSG_LEVEL;
	if (argc > 8) {
		MSG("Commandline parameter is invalid.\n");
		return -1;
	}

	/* user check, root only */
	uid = getuid();
	if (uid != 0) {
		MSG("run as root only.\n");
		return -1;
	}

	while ((opt = getopt_long(argc, argv, shortopts, longopts,
	    NULL)) != -1) {
		switch (opt) {
		case 'n':
			topn = atoi(optarg);
			opt_args += 2;
			break;
		case 'm':
			pagelimit = atoi(optarg);
			opt_args += 2;
			break;
		case 'u':
			numa = 1;
			opt_args += 1;
			break;
		case 'h':
			help = 1;
			opt_args += 1;
			break;
		case 'v':
			version = 1;
			opt_args += 1;
			break;
		case 'k':
			kexec_mode = 1;
			opt_args += 1;
			break;

		default:
			MSG("Invalid parameters, try with -h for help.");
			goto out;
		}
	}

	if (argc - opt_args > 1) {
		MSG("Invalid parameters, try with -h for help.");
		goto out;
	}

	for (i = 0; i < argc; i++)
		real_args[i] = argv[i];
	core_idx = i;

	real_args[i++] = "/proc/kcore";
	real_args[i] = "dummy_dumpfile";
	argc += 2;

	if (help) {
		show_help();
		return 0;
	}

	if (version) {
		MSG("filecache version: %s\n", version_str);
		return 0;
	}

	/* for /proc/kcore */
	if (!init_core(argc, real_args, 0))
		goto out;
#ifdef KASLR
	if (!find_kaslr_offsets()) {
		ERRMSG("find_kaslr_offsets failed\n");
		goto out;
	}
#endif
	/* get the randomized addresses and the original ones for kcore */
	symbol_addresses(NR_SYM, sym_names, r_addresses, o_addresses);
	for (i = 0; i < NR_SYM; i++) {
		if (r_addresses[i] == 0) {
			ERRMSG("failed to get address for %s\n", sym_names[i]);
			goto out;
		}
	}

	if (kexec_mode) {
		/*
		 * in kexec mode, we look at /proc/vmcore instead of /proc/kcore,
		 * symbol file_systems may have a different address due to kalsr,
		 * we now have "o_addresses" containing the orginal addresses
		 * we then calculated the randomized addresses in /proc/vmcore by
		 * r_address[i] = o_address[i] + info->kaslr_offset (for vmcore)
		 */
		if (access("/proc/vmcore", R_OK) != 0) {
			MSG("kexec mode doesn't apply on live system.\n");
			goto out;
		}
		MSG("Running in kexec mode.\n");
		real_args[core_idx] = "/proc/vmcore";

		// relealse info for /proc/kcore
		free_info(info);

		// and rework with /proc/vmcore
		info = init_core(argc, real_args, 1);
		if (!info)
			goto out;
#ifdef KASLR
		// fst for /proc/vmcore
		if (!find_kaslr_offsets()) {
			ERRMSG("find_kaslr_offsets failed\n");
			goto out;
		}
		for (i = 0; i < NR_SYM; i++) {
			r_addresses[i] = o_addresses[i] + info->kaslr_offset;
		}
#endif
	}

	ret = filecache_dump(topn, pagelimit, numa, r_addresses);
out:
	MSG("\n");
	free_info(info);
	return ret;
}
