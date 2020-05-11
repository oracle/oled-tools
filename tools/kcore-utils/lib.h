extern int is_uek4;
extern int is_uek5;
extern int is_uek6;

extern unsigned long long read_pointer(unsigned long long addr, const char *msg);
extern unsigned long long good_pointer(unsigned long long p);
extern unsigned long read_ulong(unsigned long long addr);
extern unsigned int read_unsigned(unsigned long long addr);
extern int read_int(unsigned long long addr);
extern char *read_str(unsigned long long addr, char *buf, int len);
extern char *fst_name(unsigned long long fst);
extern unsigned long long hlist_head__first(unsigned long long hlist_head);
extern unsigned long long hlist_node__next(unsigned long long hlist_node);
extern unsigned long long list_head__next(unsigned long long list_head);

extern void hardcode_offsets();
extern unsigned long long symbol_address(const char *sym);
extern char *fst_name(unsigned long long fst);
extern unsigned long long next_fst(unsigned long long fst);
extern unsigned long long get_first_mount_from_sb(unsigned long long sb);
extern char *dentry_path(unsigned long long dentry);
extern int walk_radix_tree_uek4(unsigned long long root,
	int (*func)(unsigned long long, void *), void *param);
extern int walk_radix_tree_uek5(unsigned long long root,
	int (*func)(unsigned long long, void *), void *param);
extern int walk_xarray(unsigned long long xarray,
	int (*func)(unsigned long long, void *), void *param);
extern const char *fst_name_by_inode(unsigned long long inode);
extern void free_info(struct DumpInfo *info);
extern struct DumpInfo *init_core(int argc, char *argv[], int kexec_mode);
extern int is_supported_kernel();
extern void symbol_addresses(int nr_sym, char *sym_names[],
			     unsigned long long *r_addresses,
			     unsigned long long *o_addresses);
