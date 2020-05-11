#include <limits.h>
#include "makedumpfile.h"
#include "elf_info.h"
#include "print_info.h"
#include "lib.h"

const char *version_str = "1.1";
/* version history:
 * 1.0	-- the first version
 * 1.1  -- fix dentry hash walking
 * 1.1  -- no-limit support
 */

/*
 * kaslr_off: the offset for kaslr.
 */
int dentrycache_dump(int limit, int negative_only,
		     unsigned long long *r_addresses)
{
	unsigned long long dentry_hashtable = r_addresses[0];
	unsigned long long d_hash_shift = r_addresses[1];
	unsigned long max_idx, i, addr, next, dentry_addr, inode;
	unsigned int d_hash_shift_val, file_idx = 0;
	char *path;

	MSG("kernel version: %s\n", info->release);
	MSG("dentrycache version: %s\n", version_str);
	if (!is_supported_kernel())
		return -1;

	hardcode_offsets();

	dentry_hashtable = read_pointer(dentry_hashtable, "dentry_hashtable");
	if (0 == dentry_hashtable) {
		ERRMSG("Invalid address of dentry_hashtable passed in\n");
		return -1;
	}
	d_hash_shift_val = read_unsigned(d_hash_shift);
	if (0 == d_hash_shift_val) {
		ERRMSG("Invalid address of d_hash_shift passed in\n");
		return -1;
	}

	if (is_uek4 || is_uek5)
		;
	else // uek6(+)
		d_hash_shift_val = 32 - d_hash_shift_val;
	max_idx = 1 << d_hash_shift_val;

	if (negative_only)
		MSG("Listing negative dentries, up to a limit of %d\n", limit);
	else
		MSG("Listing dentries, up to a limit of %d\n", limit);
	MSG("-------------------------------------------------------------\n");
	for (i = 0; i < max_idx; i++) {
		if (file_idx >= limit)
			break;

		// hlist_bl_head contains a pointer only
		addr = dentry_hashtable +  i * sizeof(void *);

		/* hlist_bl_head->first */
		addr += OFFSET(hlist_bl_head.first);
		addr = read_pointer(addr, "hlist_bl_node");
		if (!addr)
			continue;

		do {
			next = read_pointer(addr + OFFSET(hlist_bl_node.next),
					    "hlist_bl_node.next");	
			dentry_addr = addr - OFFSET(dentry.d_hash);
			path = dentry_path(dentry_addr);
			inode = read_pointer(dentry_addr + OFFSET(dentry.d_inode), "dentru.d_inode");
			if (negative_only && inode)
				continue;

			if (inode)
				MSG("%08d %s\n", ++file_idx, path);
			else
				MSG("%08d %s (negative)\n", ++file_idx, path);

			if (file_idx >= limit)
				break;
		} while ((addr = next));
	}

	return 0;
}

static void show_help()
{
	MSG("dentrycache: List a sample of file paths which have active dentries, on this system.\n");
	MSG("Usage: oled dentrycache [-l] [-n] [-k] [-h] [-v]\n");
	MSG("Options:\n");
	MSG("   -l, --limit <number>       list at most <number> dentries, 10000 by default\n");
	MSG("   -n, --negative             list negative dentries only, disabled by default\n");
	MSG("   -k, --kexec                list dentries for crashed production kernel\n");
	MSG("   -h, --help                 show this message\n");
	MSG("   -v, --version              show version\n");
	MSG("\n");
	MSG("Note: Works on Oracle UEK4/UEK5/UEK6 kernels only. Check the man page for more information.\n");
	MSG("\n");
}

static struct option longopts[] = {
	{"limit", required_argument, NULL, 'l'},
	{"negative", no_argument, NULL, 'n'},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'v'},
	{"kexec", no_argument, NULL, 'k'},
	{0, 0, 0, 0}
};

static char *shortopts = "l:nhvk";

int
main(int argc, char *argv[])
{
#define NR_SYM 2
	int opt, limit = 10000, negative_only = 0, i, help = 0, version = 0;
	// symbols to look for
	char *sym_names[] = {"dentry_hashtable", "d_hash_shift"};
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
		case 'l':
			limit = atoi(optarg);
			opt_args += 2;
			break;
		case 'n':
			negative_only = 1;
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
		MSG("dentrycache version: %s\n", version_str);
		return 0;
	}

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
		if (access("/proc/vmcore", R_OK) != 0) {
			MSG("kexec mode doesn't apply on live system.\n");
			goto out;
		}
		MSG("Running in kexec mode.\n");

		real_args[core_idx] = "/proc/vmcore";

		// relealse info for /proc/kcore
		free_info(info);

		// and rework with /proc/vmcore
		if (!init_core(argc, real_args, 1))
			goto out;
#ifdef KASLR
		if (!find_kaslr_offsets()) {
			ERRMSG("find_kaslr_offsets failed\n");
			goto out;
		}
		for (i = 0; i < NR_SYM; i++) {
			r_addresses[i] = o_addresses[i] + info->kaslr_offset;
		}
#endif
	}

	ret = dentrycache_dump(limit, negative_only, r_addresses);
out:
	MSG("\n");
	free_info(info);
	return ret;
}
