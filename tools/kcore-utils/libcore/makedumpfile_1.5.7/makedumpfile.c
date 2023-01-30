/*
 * makedumpfile.c
 *
 * Copyright (C) 2006, 2007, 2008, 2009, 2011  NEC Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include "makedumpfile.h"
#include "print_info.h"
#include "dwarf_info.h"
#include "elf_info.h"
#include "erase_info.h"
#include "sadump_info.h"
#include "cache.h"
#include <stddef.h>
#include <ctype.h>
#include <sys/time.h>
#include <limits.h>

struct symbol_table	symbol_table;
struct size_table	size_table;
struct offset_table	offset_table;
struct array_table	array_table;
struct number_table	number_table;
struct srcfile_table	srcfile_table;

struct vm_table		vt = { 0 };
struct DumpInfo		*info = NULL;

char filename_stdout[] = FILENAME_STDOUT;

static void first_cycle(mdf_pfn_t start, mdf_pfn_t max, struct cycle *cycle)
{
	cycle->start_pfn = round(start, info->pfn_cyclic);
	cycle->end_pfn = cycle->start_pfn + info->pfn_cyclic;

	if (cycle->end_pfn > max)
		cycle->end_pfn = max;

	cycle->exclude_pfn_start = 0;
	cycle->exclude_pfn_end = 0;
}

static void update_cycle(mdf_pfn_t max, struct cycle *cycle)
{
	cycle->start_pfn= cycle->end_pfn;
	cycle->end_pfn=  cycle->start_pfn + info->pfn_cyclic;

	if (cycle->end_pfn > max)
		cycle->end_pfn = max;
}

static int end_cycle(mdf_pfn_t max, struct cycle *cycle)
{
	return (cycle->start_pfn >=  max)?TRUE:FALSE;
}

#define for_each_cycle(start, max, C) \
	for (first_cycle(start, max, C); !end_cycle(max, C); \
	     update_cycle(max, C))

/*
 * The numbers of the excluded pages
 */
mdf_pfn_t pfn_zero;
mdf_pfn_t pfn_memhole;
mdf_pfn_t pfn_cache;
mdf_pfn_t pfn_cache_private;
mdf_pfn_t pfn_user;
mdf_pfn_t pfn_free;
mdf_pfn_t pfn_hwpoison;

mdf_pfn_t num_dumped;

int retcd = FAILED;	/* return code */

#define INITIALIZE_LONG_TABLE(table, value) \
do { \
	size_member = sizeof(long); \
	num_member  = sizeof(table) / size_member; \
	ptr_long_table = (long *)&table; \
	for (i = 0; i < num_member; i++, ptr_long_table++) \
		*ptr_long_table = value; \
} while (0)

static void setup_page_is_buddy(void);

void
initialize_tables(void)
{
	int i, size_member, num_member;
	unsigned long long *ptr_symtable;
	long *ptr_long_table;

	/*
	 * Initialize the symbol table.
	 */
	size_member = sizeof(symbol_table.mem_map);
	num_member  = sizeof(symbol_table) / size_member;

	ptr_symtable = (unsigned long long *)&symbol_table;

	for (i = 0; i < num_member; i++, ptr_symtable++)
		*ptr_symtable = NOT_FOUND_SYMBOL;

	INITIALIZE_LONG_TABLE(size_table, NOT_FOUND_STRUCTURE);
	INITIALIZE_LONG_TABLE(offset_table, NOT_FOUND_STRUCTURE);
	INITIALIZE_LONG_TABLE(array_table, NOT_FOUND_STRUCTURE);
	INITIALIZE_LONG_TABLE(number_table, NOT_FOUND_NUMBER);
}

/*
 * Translate a domain-0's physical address to machine address.
 */
unsigned long long
ptom_xen(unsigned long long paddr)
{
	unsigned long mfn;
	unsigned long long maddr;
	mdf_pfn_t pfn;
	unsigned long long mfn_idx, frame_idx;

	pfn = paddr_to_pfn(paddr);
	mfn_idx   = pfn / MFNS_PER_FRAME;
	frame_idx = pfn % MFNS_PER_FRAME;

	if (mfn_idx >= info->p2m_frames) {
		ERRMSG("Invalid mfn_idx(%llu).\n", mfn_idx);
		return NOT_PADDR;
	}
	maddr = pfn_to_paddr(info->p2m_mfn_frame_list[mfn_idx])
		+ sizeof(unsigned long) * frame_idx;
	if (!readmem(MADDR_XEN, maddr, &mfn, sizeof(mfn))) {
		ERRMSG("Can't get mfn.\n");
		return NOT_PADDR;
	}
	maddr  = pfn_to_paddr(mfn);
	maddr |= PAGEOFFSET(paddr);

	return maddr;
}

/*
 * Get the number of the page descriptors from the ELF info.
 */
int
get_max_mapnr(void)
{
	unsigned long long max_paddr;

	if (info->flag_refiltering) {
		if (info->dh_memory->header_version >= 6)
			info->max_mapnr = info->kh_memory->max_mapnr_64;
		else
			info->max_mapnr = info->dh_memory->max_mapnr;
		return TRUE;
	}

	if (info->flag_sadump) {
		info->max_mapnr = sadump_get_max_mapnr();
		return TRUE;
	}

	max_paddr = get_max_paddr();
	info->max_mapnr = paddr_to_pfn(max_paddr);

	DEBUG_MSG("\n");
	DEBUG_MSG("max_mapnr    : %llx\n", info->max_mapnr);

	return TRUE;
}

/*
 * Get the number of the page descriptors for Xen.
 */
int
get_dom0_mapnr()
{
	unsigned long max_pfn;

	if (SYMBOL(max_pfn) != NOT_FOUND_SYMBOL) {
		if (!readmem(VADDR, SYMBOL(max_pfn), &max_pfn, sizeof max_pfn)) {
			ERRMSG("Can't read domain-0 max_pfn.\n");
			return FALSE;
		}

		info->dom0_mapnr = max_pfn;
	} else if (info->p2m_frames) {
		unsigned long mfns[MFNS_PER_FRAME];
		unsigned long mfn_idx = info->p2m_frames - 1;
		unsigned long long maddr;
		unsigned i;

		maddr = pfn_to_paddr(info->p2m_mfn_frame_list[mfn_idx]);
		if (!readmem(MADDR_XEN, maddr, &mfns, sizeof(mfns))) {
			ERRMSG("Can't read %ld domain-0 mfns at 0x%llu\n",
				(long)MFNS_PER_FRAME, maddr);
			return FALSE;
		}

		for (i = 0; i < MFNS_PER_FRAME; ++i)
			if (!mfns[i])
				break;

		info->dom0_mapnr = mfn_idx * MFNS_PER_FRAME + i;
	} else {
		/* dom0_mapnr is unavailable, which may be non-critical */
		return TRUE;
	}

	DEBUG_MSG("domain-0 pfn : %llx\n", info->dom0_mapnr);
	return TRUE;
}

int
is_in_same_page(unsigned long vaddr1, unsigned long vaddr2)
{
	if (round(vaddr1, info->page_size) == round(vaddr2, info->page_size))
		return TRUE;

	return FALSE;
}

#define BITMAP_SECT_LEN 4096
static inline int is_dumpable(struct dump_bitmap *, mdf_pfn_t);
static inline int is_dumpable_cyclic(char *bitmap, mdf_pfn_t, struct cycle *cycle);
unsigned long
pfn_to_pos(mdf_pfn_t pfn)
{
	unsigned long desc_pos;
	mdf_pfn_t i;

	desc_pos = info->valid_pages[pfn / BITMAP_SECT_LEN];
	for (i = round(pfn, BITMAP_SECT_LEN); i < pfn; i++)
		if (is_dumpable(info->bitmap_memory, i))
			desc_pos++;

	return desc_pos;
}

int
read_page_desc(unsigned long long paddr, page_desc_t *pd)
{
	struct disk_dump_header *dh;
	unsigned long desc_pos;
	mdf_pfn_t pfn;
	off_t offset;

	/*
	 * Find page descriptor
	 */
	dh = info->dh_memory;
	offset
	    = (DISKDUMP_HEADER_BLOCKS + dh->sub_hdr_size + dh->bitmap_blocks)
		* dh->block_size;
	pfn = paddr_to_pfn(paddr);
	desc_pos = pfn_to_pos(pfn);
	offset += (off_t)desc_pos * sizeof(page_desc_t);
	if (lseek(info->fd_memory, offset, SEEK_SET) < 0) {
		ERRMSG("Can't seek %s. %s\n",
				 info->name_memory, strerror(errno));
		return FALSE;
	}

	/*
	 * Read page descriptor
	 */
	if (read(info->fd_memory, pd, sizeof(*pd)) != sizeof(*pd)) {
		ERRMSG("Can't read %s. %s\n",
				info->name_memory, strerror(errno));
		return FALSE;
	}

	/*
	 * Sanity check
	 */
	if (pd->size > dh->block_size)
		return FALSE;

	return TRUE;
}

static int
update_mmap_range(off_t offset, int initial) {
	off_t start_offset, end_offset;
	off_t map_size;
	off_t max_offset = get_max_file_offset();
	off_t pt_load_end = offset_to_pt_load_end(offset);

	munmap(info->mmap_buf,
	       info->mmap_end_offset - info->mmap_start_offset);

	/*
	 * offset for mmap() must be page aligned.
	 */
	start_offset = roundup(offset, info->page_size);
	end_offset = MIN(max_offset, round(pt_load_end, info->page_size));

	if (!pt_load_end || (end_offset - start_offset) <= 0)
		return FALSE;

	map_size = MIN(end_offset - start_offset, info->mmap_region_size);

	info->mmap_buf = mmap(NULL, map_size, PROT_READ, MAP_PRIVATE,
				     info->fd_memory, start_offset);

	if (info->mmap_buf == MAP_FAILED) {
		if (!initial)
			DEBUG_MSG("Can't map [%llx-%llx] with mmap()\n %s",
				  (ulonglong)start_offset,
				  (ulonglong)(start_offset + map_size),
				  strerror(errno));
		return FALSE;
	}

	info->mmap_start_offset = start_offset;
	info->mmap_end_offset = start_offset + map_size;

	return TRUE;
}

static int
is_mapped_with_mmap(off_t offset) {

	if (info->flag_usemmap == MMAP_ENABLE
	    && offset >= info->mmap_start_offset
	    && offset < info->mmap_end_offset)
		return TRUE;
	else
		return FALSE;
}

int
initialize_mmap(void) {
	unsigned long long phys_start;
	info->mmap_region_size = MAP_REGION;
	info->mmap_buf = MAP_FAILED;

	get_pt_load(0, &phys_start, NULL, NULL, NULL);
	if (!update_mmap_range(phys_start, 1))
		return FALSE;

	return TRUE;
}

static int
read_with_mmap(off_t offset, void *bufptr, unsigned long size) {
	size_t read_size;

next_region:

	if (!is_mapped_with_mmap(offset))
		if (!update_mmap_range(offset, 0))
			return FALSE;

	read_size = MIN(info->mmap_end_offset - offset, size);

	memcpy(bufptr, info->mmap_buf +
	       (offset - info->mmap_start_offset), read_size);

	offset += read_size;
	bufptr += read_size;
	size -= read_size;

	if (size > 0)
		goto next_region;

	return TRUE;
}

static int
read_from_vmcore(off_t offset, void *bufptr, unsigned long size)
{
	const off_t failed = (off_t)-1;

	if (info->flag_usemmap == MMAP_ENABLE &&
	    page_is_fractional(offset) == FALSE) {
		if (!read_with_mmap(offset, bufptr, size)) {
			ERRMSG("Can't read the dump memory(%s) with mmap().\n",
			       info->name_memory);
			return FALSE;
		}
	} else {
		if (lseek(info->fd_memory, offset, SEEK_SET) == failed) {
			ERRMSG("Can't seek the dump memory(%s). (offset: %llx) %s\n",
			       info->name_memory, (unsigned long long)offset, strerror(errno));
			return FALSE;
		}

		if (read(info->fd_memory, bufptr, size) != size) {
			ERRMSG("Can't read the dump memory(%s). %s\n",
			       info->name_memory, strerror(errno));
			return FALSE;
		}
	}

	return TRUE;
}

/*
 * This function is specific for reading page from ELF.
 *
 * If reading the separated page on different PT_LOAD segments,
 * this function gets the page data from both segments. This is
 * worthy of ia64 /proc/vmcore. In ia64 /proc/vmcore, region 5
 * segment is overlapping to region 7 segment. The following is
 * example (page_size is 16KBytes):
 *
 *  region |       paddr        |       memsz
 * --------+--------------------+--------------------
 *     5   | 0x0000000004000000 | 0x0000000000638ce0
 *     7   | 0x0000000004000000 | 0x0000000000db3000
 *
 * In the above example, the last page of region 5 is 0x4638000
 * and the segment does not contain complete data of this page.
 * Then this function gets the data of 0x4638000 - 0x4638ce0
 * from region 5, and gets the remaining data from region 7.
 */
static int
readpage_elf(unsigned long long paddr, void *bufptr)
{
	off_t offset1, offset2;
	size_t size1, size2;
	unsigned long long phys_start, phys_end, frac_head = 0;

	offset1 = paddr_to_offset(paddr);
	offset2 = paddr_to_offset(paddr + info->page_size);
	phys_start = paddr;
	phys_end = paddr + info->page_size;

	/*
	 * Check the case phys_start isn't aligned by page size like below:
	 *
	 *                           phys_start
	 *                           = 0x40ffda7000
	 *         |<-- frac_head -->|------------- PT_LOAD -------------
	 *     ----+-----------------------+---------------------+----
	 *         |         pfn:N         |       pfn:N+1       | ...
	 *     ----+-----------------------+---------------------+----
	 *         |
	 *     pfn_to_paddr(pfn:N)               # page size = 16k
	 *     = 0x40ffda4000
	 */
	if (!offset1) {
		phys_start = page_head_to_phys_start(paddr);
		offset1 = paddr_to_offset(phys_start);
		frac_head = phys_start - paddr;
		memset(bufptr, 0, frac_head);
	}

	/*
	 * Check the case phys_end isn't aligned by page size like the
	 * phys_start's case.
	 */
	if (!offset2) {
		phys_end = page_head_to_phys_end(paddr);
		offset2 = paddr_to_offset(phys_end);
		memset(bufptr + (phys_end - paddr), 0, info->page_size - (phys_end - paddr));
	}

	/*
	 * Check the separated page on different PT_LOAD segments.
	 */
	if (offset1 + (phys_end - phys_start) == offset2) {
		size1 = phys_end - phys_start;
	} else {
		for (size1 = 1; size1 < info->page_size - frac_head; size1++) {
			offset2 = paddr_to_offset(phys_start + size1);
			if (offset1 + size1 != offset2)
				break;
		}
	}

	if(!read_from_vmcore(offset1, bufptr + frac_head, size1)) {
		ERRMSG("Can't read the dump memory(%s).\n",
		       info->name_memory);
		return FALSE;
	}

	if (size1 + frac_head != info->page_size) {
		size2 = phys_end - (phys_start + size1);

		if(!read_from_vmcore(offset2, bufptr + frac_head + size1, size2)) {
			ERRMSG("Can't read the dump memory(%s).\n",
			       info->name_memory);
			return FALSE;
		}
	}

	return TRUE;
}

static int
readpage_kdump_compressed(unsigned long long paddr, void *bufptr)
{
	page_desc_t pd;
	char buf[info->page_size], *rdbuf;
	int ret;
	unsigned long retlen;

	if (!is_dumpable(info->bitmap_memory, paddr_to_pfn(paddr))) {
		ERRMSG("pfn(%llx) is excluded from %s.\n",
				paddr_to_pfn(paddr), info->name_memory);
		return FALSE;
	}

	if (!read_page_desc(paddr, &pd)) {
		ERRMSG("Can't read page_desc: %llx\n", paddr);
		return FALSE;
	}

	if (lseek(info->fd_memory, pd.offset, SEEK_SET) < 0) {
		ERRMSG("Can't seek %s. %s\n",
				info->name_memory, strerror(errno));
		return FALSE;
	}

	/*
	 * Read page data
	 */
	rdbuf = pd.flags & (DUMP_DH_COMPRESSED_ZLIB | DUMP_DH_COMPRESSED_LZO |
		DUMP_DH_COMPRESSED_SNAPPY) ? buf : bufptr;
	if (read(info->fd_memory, rdbuf, pd.size) != pd.size) {
		ERRMSG("Can't read %s. %s\n",
				info->name_memory, strerror(errno));
		return FALSE;
	}

	if (pd.flags & DUMP_DH_COMPRESSED_ZLIB) {
		retlen = info->page_size;
		ret = uncompress((unsigned char *)bufptr, &retlen,
					(unsigned char *)buf, pd.size);
		if ((ret != Z_OK) || (retlen != info->page_size)) {
			ERRMSG("Uncompress failed: %d\n", ret);
			return FALSE;
		}
#ifdef USELZO
	} else if (info->flag_lzo_support
		   && (pd.flags & DUMP_DH_COMPRESSED_LZO)) {
		retlen = info->page_size;
		ret = lzo1x_decompress_safe((unsigned char *)buf, pd.size,
					    (unsigned char *)bufptr, &retlen,
					    LZO1X_MEM_DECOMPRESS);
		if ((ret != LZO_E_OK) || (retlen != info->page_size)) {
			ERRMSG("Uncompress failed: %d\n", ret);
			return FALSE;
		}
#endif
#ifdef USESNAPPY
	} else if ((pd.flags & DUMP_DH_COMPRESSED_SNAPPY)) {

		ret = snappy_uncompressed_length(buf, pd.size, (size_t *)&retlen);
		if (ret != SNAPPY_OK) {
			ERRMSG("Uncompress failed: %d\n", ret);
			return FALSE;
		}

		ret = snappy_uncompress(buf, pd.size, bufptr, (size_t *)&retlen);
		if ((ret != SNAPPY_OK) || (retlen != info->page_size)) {
			ERRMSG("Uncompress failed: %d\n", ret);
			return FALSE;
		}
#endif
	}

	return TRUE;
}

int
readmem(int type_addr, unsigned long long addr, void *bufptr, size_t size)
{
	size_t read_size, size_orig = size;
	unsigned long long paddr, maddr = NOT_PADDR;
	unsigned long long pgaddr;
	void *pgbuf;

next_page:
	switch (type_addr) {
	case VADDR:
		if ((paddr = vaddr_to_paddr(addr)) == NOT_PADDR) {
			ERRMSG("Can't convert a virtual address(%llx) to physical address.\n",
			    addr);
			goto error;
		}
		if (is_xen_memory()) {
			if ((maddr = ptom_xen(paddr)) == NOT_PADDR) {
				ERRMSG("Can't convert a physical address(%llx) to machine address.\n",
				    paddr);
				return FALSE;
			}
			paddr = maddr;
		}
		break;
	case PADDR:
		paddr = addr;
		if (is_xen_memory()) {
			if ((maddr  = ptom_xen(paddr)) == NOT_PADDR) {
				ERRMSG("Can't convert a physical address(%llx) to machine address.\n",
				    paddr);
				return FALSE;
			}
			paddr = maddr;
		}
		break;
	case VADDR_XEN:
		if ((paddr = kvtop_xen(addr)) == NOT_PADDR) {
			ERRMSG("Can't convert a virtual address(%llx) to machine address.\n",
			    addr);
			goto error;
		}
		break;
	case MADDR_XEN:
		paddr = addr;
		break;
	default:
		ERRMSG("Invalid address type (%d).\n", type_addr);
		goto error;
	}

	/*
	 * Read each page, because pages are not necessarily continuous.
	 * Ex) pages in vmalloc area
	 */
	read_size = MIN(info->page_size - PAGEOFFSET(paddr), size);

	pgaddr = PAGEBASE(paddr);
	pgbuf = cache_search(pgaddr);
	if (!pgbuf) {
		pgbuf = cache_alloc(pgaddr);
		if (!pgbuf)
			goto error;

		if (info->flag_refiltering) {
			if (!readpage_kdump_compressed(pgaddr, pgbuf))
				goto error;
		} else if (info->flag_sadump) {
			if (!readpage_sadump(pgaddr, pgbuf))
				goto error;
		} else {
			if (!readpage_elf(pgaddr, pgbuf))
				goto error;
		}
		cache_add(pgaddr);
	}

	memcpy(bufptr, pgbuf + PAGEOFFSET(paddr), read_size);

	addr += read_size;
	bufptr += read_size;
	size -= read_size;

	if (size > 0)
		goto next_page;

	return size_orig;

error:
	ERRMSG("type_addr: %d, addr:%llx, size:%zd\n", type_addr, addr, size_orig);
	return FALSE;
}

int32_t
get_kernel_version(char *release)
{
	int32_t version;
	long maj, min, rel;
	char *start, *end;

	if (info->kernel_version)
		return info->kernel_version;

	/*
	 * This method checks that vmlinux and vmcore are same kernel version.
	 */
	start = release;
	maj = strtol(start, &end, 10);
	if (maj == LONG_MAX)
		return FALSE;

	start = end + 1;
	min = strtol(start, &end, 10);
	if (min == LONG_MAX)
		return FALSE;

	start = end + 1;
	rel = strtol(start, &end, 10);
	if (rel == LONG_MAX)
		return FALSE;

	version = KERNEL_VERSION(maj, min, rel);

	return version;
}

int
is_page_size(long page_size)
{
	/*
	 * Page size is restricted to a hamming weight of 1.
	 */
	if (page_size > 0 && !(page_size & (page_size - 1)))
		return TRUE;

	return FALSE;
}

int
set_page_size(long page_size)
{
	if (!is_page_size(page_size)) {
		ERRMSG("Invalid page_size: %ld", page_size);
		return FALSE;
	}
	info->page_size = page_size;
	info->page_shift = ffs(info->page_size) - 1;
	DEBUG_MSG("page_size    : %ld\n", info->page_size);

	return TRUE;
}

int
fallback_to_current_page_size(void)
{

	if (!set_page_size(sysconf(_SC_PAGE_SIZE)))
		return FALSE;

	DEBUG_MSG("WARNING: Cannot determine page size (no vmcoreinfo).\n");
	DEBUG_MSG("Using the dump kernel page size: %ld\n",
	    info->page_size);

	return TRUE;
}

int
check_release(void)
{
	unsigned long utsname;

	/*
	 * Get the kernel version.
	 */
	if (SYMBOL(system_utsname) != NOT_FOUND_SYMBOL) {
		utsname = SYMBOL(system_utsname);
	} else if (SYMBOL(init_uts_ns) != NOT_FOUND_SYMBOL) {
		utsname = SYMBOL(init_uts_ns) + sizeof(int);
	} else {
		ERRMSG("Can't get the symbol of system_utsname.\n");
		return FALSE;
	}
	if (!readmem(VADDR, utsname, &info->system_utsname,
					sizeof(struct utsname))) {
		ERRMSG("Can't get the address of system_utsname.\n");
		return FALSE;
	}

	if (info->flag_read_vmcoreinfo) {
		if (strcmp(info->system_utsname.release, info->release)) {
			ERRMSG("%s and %s don't match.\n",
			    info->name_vmcoreinfo, info->name_memory);
			retcd = WRONG_RELEASE;
			return FALSE;
		}
	}

	info->kernel_version = get_kernel_version(info->system_utsname.release);
	if (info->kernel_version == FALSE) {
		ERRMSG("Can't get the kernel version.\n");
		return FALSE;
	}

	return TRUE;
}

int
open_vmcoreinfo(char *mode)
{
	FILE *file_vmcoreinfo;

	if ((file_vmcoreinfo = fopen(info->name_vmcoreinfo, mode)) == NULL) {
		ERRMSG("Can't open the vmcoreinfo file(%s). %s\n",
		    info->name_vmcoreinfo, strerror(errno));
		return FALSE;
	}
	info->file_vmcoreinfo = file_vmcoreinfo;
	return TRUE;
}

int
open_kernel_file(void)
{
	int fd;

	if (info->name_vmlinux) {
		if ((fd = open(info->name_vmlinux, O_RDONLY)) < 0) {
			ERRMSG("Can't open the kernel file(%s). %s\n",
			    info->name_vmlinux, strerror(errno));
			return FALSE;
		}
		info->fd_vmlinux = fd;
	}
	if (info->name_xen_syms) {
		if ((fd = open(info->name_xen_syms, O_RDONLY)) < 0) {
			ERRMSG("Can't open the kernel file(%s). %s\n",
			    info->name_xen_syms, strerror(errno));
			return FALSE;
		}
		info->fd_xen_syms = fd;
	}
	return TRUE;
}

int
check_kdump_compressed(char *filename)
{
	struct disk_dump_header dh;

	if (!__read_disk_dump_header(&dh, filename))
		return ERROR;

	if (strncmp(dh.signature, KDUMP_SIGNATURE, SIG_LEN))
		return FALSE;

	return TRUE;
}

int
get_kdump_compressed_header_info(char *filename)
{
	struct disk_dump_header dh;
	struct kdump_sub_header kh;

	if (!read_disk_dump_header(&dh, filename))
		return FALSE;

	if (!read_kdump_sub_header(&kh, filename))
		return FALSE;

	if (dh.header_version < 1) {
		ERRMSG("header does not have dump_level member\n");
		return FALSE;
	}
	DEBUG_MSG("diskdump main header\n");
	DEBUG_MSG("  signature        : %s\n", dh.signature);
	DEBUG_MSG("  header_version   : %d\n", dh.header_version);
	DEBUG_MSG("  status           : %d\n", dh.status);
	DEBUG_MSG("  block_size       : %d\n", dh.block_size);
	DEBUG_MSG("  sub_hdr_size     : %d\n", dh.sub_hdr_size);
	DEBUG_MSG("  bitmap_blocks    : %d\n", dh.bitmap_blocks);
	DEBUG_MSG("  max_mapnr        : 0x%x\n", dh.max_mapnr);
	DEBUG_MSG("  total_ram_blocks : %d\n", dh.total_ram_blocks);
	DEBUG_MSG("  device_blocks    : %d\n", dh.device_blocks);
	DEBUG_MSG("  written_blocks   : %d\n", dh.written_blocks);
	DEBUG_MSG("  current_cpu      : %d\n", dh.current_cpu);
	DEBUG_MSG("  nr_cpus          : %d\n", dh.nr_cpus);
	DEBUG_MSG("kdump sub header\n");
	DEBUG_MSG("  phys_base        : 0x%lx\n", kh.phys_base);
	DEBUG_MSG("  dump_level       : %d\n", kh.dump_level);
	DEBUG_MSG("  split            : %d\n", kh.split);
	DEBUG_MSG("  start_pfn        : 0x%lx\n", kh.start_pfn);
	DEBUG_MSG("  end_pfn          : 0x%lx\n", kh.end_pfn);
	if (dh.header_version >= 6) {
		/* A dumpfile contains full 64bit values. */
		DEBUG_MSG("  start_pfn_64     : 0x%llx\n", kh.start_pfn_64);
		DEBUG_MSG("  end_pfn_64       : 0x%llx\n", kh.end_pfn_64);
		DEBUG_MSG("  max_mapnr_64     : 0x%llx\n", kh.max_mapnr_64);
	}

	info->dh_memory = malloc(sizeof(dh));
	if (info->dh_memory == NULL) {
		ERRMSG("Can't allocate memory for the header. %s\n",
		    strerror(errno));
		return FALSE;
	}
	memcpy(info->dh_memory, &dh, sizeof(dh));
	memcpy(&info->timestamp, &dh.timestamp, sizeof(dh.timestamp));

	info->kh_memory = malloc(sizeof(kh));
	if (info->kh_memory == NULL) {
		ERRMSG("Can't allocate memory for the sub header. %s\n",
		    strerror(errno));
		goto error;
	}
	memcpy(info->kh_memory, &kh, sizeof(kh));
	set_nr_cpus(dh.nr_cpus);

	if (dh.header_version >= 3) {
		/* A dumpfile contains vmcoreinfo data. */
		set_vmcoreinfo(kh.offset_vmcoreinfo, kh.size_vmcoreinfo);
		DEBUG_MSG("  offset_vmcoreinfo: 0x%llx\n",
				(unsigned long long)kh.offset_vmcoreinfo);
		DEBUG_MSG("  size_vmcoreinfo  : 0x%ld\n", kh.size_vmcoreinfo);
	}
	if (dh.header_version >= 4) {
		/* A dumpfile contains ELF note section. */
		set_pt_note(kh.offset_note, kh.size_note);
		DEBUG_MSG("  offset_note      : 0x%llx\n",
				(unsigned long long)kh.offset_note);
		DEBUG_MSG("  size_note        : 0x%ld\n", kh.size_note);
	}
	if (dh.header_version >= 5) {
		/* A dumpfile contains erased information. */
		set_eraseinfo(kh.offset_eraseinfo, kh.size_eraseinfo);
		DEBUG_MSG("  offset_eraseinfo : 0x%llx\n",
				(unsigned long long)kh.offset_eraseinfo);
		DEBUG_MSG("  size_eraseinfo   : 0x%ld\n", kh.size_eraseinfo);
	}
	return TRUE;
error:
	free(info->dh_memory);
	info->dh_memory = NULL;

	return FALSE;
}

int
open_dump_memory(void)
{
	int fd, status;

	if ((fd = open(info->name_memory, O_RDONLY)) < 0) {
		ERRMSG("Can't open the dump memory(%s). %s\n",
		    info->name_memory, strerror(errno));
		return FALSE;
	}
	info->fd_memory = fd;

	status = check_kdump_compressed(info->name_memory);
	if (status == TRUE) {
		info->flag_refiltering = TRUE;
		return get_kdump_compressed_header_info(info->name_memory);
	}

	status = check_and_get_sadump_header_info(info->name_memory);
	if (status == TRUE)
		return TRUE;

	if (status == ERROR)
		return TRUE;

	return FALSE;
}

int
open_dump_file(void)
{
	int fd;
	int open_flags = O_RDWR|O_CREAT|O_TRUNC;

	if (!info->flag_force)
		open_flags |= O_EXCL;

	if (info->flag_flatten) {
		fd = STDOUT_FILENO;
		info->name_dumpfile = filename_stdout;
	} else if ((fd = open(info->name_dumpfile, open_flags,
	    S_IRUSR|S_IWUSR)) < 0) {
		ERRMSG("Can't open the dump file(%s). %s\n",
		    info->name_dumpfile, strerror(errno));
		return FALSE;
	}
	info->fd_dumpfile = fd;
	return TRUE;
}

int
check_dump_file(const char *path)
{
	char *err_str;

	if (access(path, F_OK) != 0)
		return TRUE; /* File does not exist */
	if (info->flag_force) {
		if (access(path, W_OK) == 0)
			return TRUE; /* We have write permission */
		err_str = strerror(errno);
	} else {
		err_str = strerror(EEXIST);
	}
	ERRMSG("Can't open the dump file (%s). %s\n", path, err_str);
	return FALSE;
}

int
open_dump_bitmap(void)
{
	int i, fd;
	char *tmpname;

	tmpname = getenv("TMPDIR");
	if (!tmpname)
		tmpname = "/tmp";

	if ((info->name_bitmap = (char *)malloc(sizeof(FILENAME_BITMAP) +
						strlen(tmpname) + 1)) == NULL) {
		ERRMSG("Can't allocate memory for the filename. %s\n",
		    strerror(errno));
		return FALSE;
	}
	strcpy(info->name_bitmap, tmpname);
	strcat(info->name_bitmap, "/");
	strcat(info->name_bitmap, FILENAME_BITMAP);
	if ((fd = mkstemp(info->name_bitmap)) < 0) {
		ERRMSG("Can't open the bitmap file(%s). %s\n",
		    info->name_bitmap, strerror(errno));
		return FALSE;
	}
	info->fd_bitmap = fd;

	if (info->flag_split) {
		/*
		 * Reserve file descriptors of bitmap for creating split
		 * dumpfiles by multiple processes, because a bitmap file will
		 * be unlinked just after this and it is not possible to open
		 * a bitmap file later.
		 */
		for (i = 0; i < info->num_dumpfile; i++) {
			if ((fd = open(info->name_bitmap, O_RDONLY)) < 0) {
				ERRMSG("Can't open the bitmap file(%s). %s\n",
				    info->name_bitmap, strerror(errno));
				return FALSE;
			}
			SPLITTING_FD_BITMAP(i) = fd;
		}
	}
	unlink(info->name_bitmap);

	return TRUE;
}

/*
 * Open the following files when it generates the vmcoreinfo file.
 * - vmlinux
 * - vmcoreinfo file
 */
int
open_files_for_generating_vmcoreinfo(void)
{
	if (!open_kernel_file())
		return FALSE;

	if (!open_vmcoreinfo("w"))
		return FALSE;

	return TRUE;
}

/*
 * Open the following file when it rearranges the dump data.
 * - dump file
 */
int
open_files_for_rearranging_dumpdata(void)
{
	if (!open_dump_file())
		return FALSE;

	return TRUE;
}

/*
 * Open the following files when it creates the dump file.
 * - dump mem
 * - bit map
 * if it reads the vmcoreinfo file
 *   - vmcoreinfo file
 * else
 *   - vmlinux
 */
int
open_files_for_creating_dumpfile(void)
{
	if (info->flag_read_vmcoreinfo) {
		if (!open_vmcoreinfo("r"))
			return FALSE;
	} else {
		if (!open_kernel_file())
			return FALSE;
	}
	if (!open_dump_memory())
		return FALSE;

	if (!open_dump_bitmap())
		return FALSE;

	return TRUE;
}

int
is_kvaddr(unsigned long long addr)
{
	return (addr >= (unsigned long long)(KVBASE));
}

int
get_symbol_info(void)
{
	/*
	 * Get symbol info.
	 */
	SYMBOL_INIT(mem_map, "mem_map");
	SYMBOL_INIT(vmem_map, "vmem_map");
	SYMBOL_INIT(mem_section, "mem_section");
	SYMBOL_INIT(pkmap_count, "pkmap_count");
	SYMBOL_INIT_NEXT(pkmap_count_next, "pkmap_count");
	SYMBOL_INIT(system_utsname, "system_utsname");
	SYMBOL_INIT(init_uts_ns, "init_uts_ns");
	SYMBOL_INIT(_stext, "_stext");
	SYMBOL_INIT(swapper_pg_dir, "swapper_pg_dir");
	SYMBOL_INIT(init_level4_pgt, "init_level4_pgt");
	SYMBOL_INIT(vmlist, "vmlist");
	SYMBOL_INIT(vmap_area_list, "vmap_area_list");
	SYMBOL_INIT(phys_base, "phys_base");
	SYMBOL_INIT(node_online_map, "node_online_map");
	SYMBOL_INIT(node_states, "node_states");
	SYMBOL_INIT(node_memblk, "node_memblk");
	SYMBOL_INIT(node_data, "node_data");
	SYMBOL_INIT(pgdat_list, "pgdat_list");
	SYMBOL_INIT(contig_page_data, "contig_page_data");
	SYMBOL_INIT(log_buf, "log_buf");
	SYMBOL_INIT(log_buf_len, "log_buf_len");
	SYMBOL_INIT(log_end, "log_end");
	SYMBOL_INIT(log_first_idx, "log_first_idx");
	SYMBOL_INIT(log_next_idx, "log_next_idx");
	SYMBOL_INIT(max_pfn, "max_pfn");
	SYMBOL_INIT(modules, "modules");
	SYMBOL_INIT(high_memory, "high_memory");
	SYMBOL_INIT(linux_banner, "linux_banner");
	SYMBOL_INIT(bios_cpu_apicid, "bios_cpu_apicid");
	SYMBOL_INIT(x86_bios_cpu_apicid, "x86_bios_cpu_apicid");
	if (SYMBOL(x86_bios_cpu_apicid) == NOT_FOUND_SYMBOL)
		SYMBOL_INIT(x86_bios_cpu_apicid,
			    "per_cpu__x86_bios_cpu_apicid");
	SYMBOL_INIT(x86_bios_cpu_apicid_early_ptr,
		    "x86_bios_cpu_apicid_early_ptr");
	SYMBOL_INIT(x86_bios_cpu_apicid_early_map,
		    "x86_bios_cpu_apicid_early_map");
	SYMBOL_INIT(crash_notes, "crash_notes");
	SYMBOL_INIT(__per_cpu_load, "__per_cpu_load");
	SYMBOL_INIT(__per_cpu_offset, "__per_cpu_offset");
	SYMBOL_INIT(cpu_online_mask, "cpu_online_mask");
	if (SYMBOL(cpu_online_mask) == NOT_FOUND_SYMBOL)
		SYMBOL_INIT(cpu_online_mask, "cpu_online_map");
	SYMBOL_INIT(kexec_crash_image, "kexec_crash_image");
	SYMBOL_INIT(node_remap_start_vaddr, "node_remap_start_vaddr");
	SYMBOL_INIT(node_remap_end_vaddr, "node_remap_end_vaddr");
	SYMBOL_INIT(node_remap_start_pfn, "node_remap_start_pfn");

	if (SYMBOL(node_data) != NOT_FOUND_SYMBOL)
		SYMBOL_ARRAY_TYPE_INIT(node_data, "node_data");
	if (SYMBOL(pgdat_list) != NOT_FOUND_SYMBOL)
		SYMBOL_ARRAY_LENGTH_INIT(pgdat_list, "pgdat_list");
	if (SYMBOL(mem_section) != NOT_FOUND_SYMBOL)
		SYMBOL_ARRAY_LENGTH_INIT(mem_section, "mem_section");
	if (SYMBOL(node_memblk) != NOT_FOUND_SYMBOL)
		SYMBOL_ARRAY_LENGTH_INIT(node_memblk, "node_memblk");
	if (SYMBOL(__per_cpu_offset) != NOT_FOUND_SYMBOL)
		SYMBOL_ARRAY_LENGTH_INIT(__per_cpu_offset, "__per_cpu_offset");
	if (SYMBOL(node_remap_start_pfn) != NOT_FOUND_SYMBOL)
		SYMBOL_ARRAY_LENGTH_INIT(node_remap_start_pfn,
					"node_remap_start_pfn");

	SYMBOL_INIT(vmemmap_list, "vmemmap_list");
	SYMBOL_INIT(mmu_psize_defs, "mmu_psize_defs");
	SYMBOL_INIT(mmu_vmemmap_psize, "mmu_vmemmap_psize");
	SYMBOL_INIT(free_huge_page, "free_huge_page");

	SYMBOL_INIT(cpu_pgd, "cpu_pgd");
	SYMBOL_INIT(demote_segment_4k, "demote_segment_4k");

	return TRUE;
}

int
get_structure_info(void)
{
	/*
	 * Get offsets of the page_discriptor's members.
	 */
	SIZE_INIT(page, "page");
	OFFSET_INIT(page.flags, "page", "flags");
	OFFSET_INIT(page._count, "page", "_count");
	OFFSET_INIT(page.mapping, "page", "mapping");
	OFFSET_INIT(page._mapcount, "page", "_mapcount");
	OFFSET_INIT(page.private, "page", "private");

	/*
	 * Some vmlinux(s) don't have debugging information about
	 * page.mapping. Then, makedumpfile assumes that there is
	 * "mapping" next to "private(unsigned long)" in the first
	 * union.
	 */
	if (OFFSET(page.mapping) == NOT_FOUND_STRUCTURE) {
		OFFSET(page.mapping) = get_member_offset("page", NULL,
		    DWARF_INFO_GET_MEMBER_OFFSET_1ST_UNION);
		if (OFFSET(page.mapping) == FAILED_DWARFINFO)
			return FALSE;
		if (OFFSET(page.mapping) != NOT_FOUND_STRUCTURE)
			OFFSET(page.mapping) += sizeof(unsigned long);
	}

	OFFSET_INIT(page.lru, "page", "lru");

	/*
	 * Get offsets of the mem_section's members.
	 */
	SIZE_INIT(mem_section, "mem_section");
	OFFSET_INIT(mem_section.section_mem_map, "mem_section",
	    "section_mem_map");

	/*
	 * Get offsets of the pglist_data's members.
	 */
	SIZE_INIT(pglist_data, "pglist_data");
	OFFSET_INIT(pglist_data.node_zones, "pglist_data", "node_zones");
	OFFSET_INIT(pglist_data.nr_zones, "pglist_data", "nr_zones");
	OFFSET_INIT(pglist_data.node_mem_map, "pglist_data", "node_mem_map");
	OFFSET_INIT(pglist_data.node_start_pfn, "pglist_data","node_start_pfn");
	OFFSET_INIT(pglist_data.node_spanned_pages, "pglist_data",
	    "node_spanned_pages");
	OFFSET_INIT(pglist_data.pgdat_next, "pglist_data", "pgdat_next");

	/*
	 * Get offsets of the zone's members.
	 */
	SIZE_INIT(zone, "zone");
	OFFSET_INIT(zone.free_pages, "zone", "free_pages");
	OFFSET_INIT(zone.free_area, "zone", "free_area");
	OFFSET_INIT(zone.vm_stat, "zone", "vm_stat");
	OFFSET_INIT(zone.spanned_pages, "zone", "spanned_pages");
	MEMBER_ARRAY_LENGTH_INIT(zone.free_area, "zone", "free_area");

	/*
	 * Get offsets of the free_area's members.
	 */
	SIZE_INIT(free_area, "free_area");
	OFFSET_INIT(free_area.free_list, "free_area", "free_list");
	MEMBER_ARRAY_LENGTH_INIT(free_area.free_list, "free_area", "free_list");

	/*
	 * Get offsets of the list_head's members.
	 */
	SIZE_INIT(list_head, "list_head");
	OFFSET_INIT(list_head.next, "list_head", "next");
	OFFSET_INIT(list_head.prev, "list_head", "prev");

	/*
	 * Get offsets of the node_memblk_s's members.
	 */
	SIZE_INIT(node_memblk_s, "node_memblk_s");
	OFFSET_INIT(node_memblk_s.start_paddr, "node_memblk_s", "start_paddr");
	OFFSET_INIT(node_memblk_s.size, "node_memblk_s", "size");
	OFFSET_INIT(node_memblk_s.nid, "node_memblk_s", "nid");

	OFFSET_INIT(vm_struct.addr, "vm_struct", "addr");
	OFFSET_INIT(vmap_area.va_start, "vmap_area", "va_start");
	OFFSET_INIT(vmap_area.list, "vmap_area", "list");

	/*
	 * Get offset of the module members.
	 */
	SIZE_INIT(module, "module");
	OFFSET_INIT(module.strtab, "module", "strtab");
	OFFSET_INIT(module.symtab, "module", "symtab");
	OFFSET_INIT(module.num_symtab, "module", "num_symtab");
	OFFSET_INIT(module.list, "module", "list");
	OFFSET_INIT(module.name, "module", "name");
	OFFSET_INIT(module.module_core, "module", "module_core");
	OFFSET_INIT(module.core_size, "module", "core_size");
	OFFSET_INIT(module.module_init, "module", "module_init");
	OFFSET_INIT(module.init_size, "module", "init_size");

	ENUM_NUMBER_INIT(NR_FREE_PAGES, "NR_FREE_PAGES");
	ENUM_NUMBER_INIT(N_ONLINE, "N_ONLINE");

	ENUM_NUMBER_INIT(PG_lru, "PG_lru");
	ENUM_NUMBER_INIT(PG_private, "PG_private");
	ENUM_NUMBER_INIT(PG_swapcache, "PG_swapcache");
	ENUM_NUMBER_INIT(PG_buddy, "PG_buddy");
	ENUM_NUMBER_INIT(PG_slab, "PG_slab");
	ENUM_NUMBER_INIT(PG_hwpoison, "PG_hwpoison");

	ENUM_NUMBER_INIT(PG_head_mask, "PG_head_mask");
	if (NUMBER(PG_head_mask) == NOT_FOUND_NUMBER) {
		ENUM_NUMBER_INIT(PG_head, "PG_head");
		if (NUMBER(PG_head) == NOT_FOUND_NUMBER)
			ENUM_NUMBER_INIT(PG_head, "PG_compound");
		if (NUMBER(PG_head) != NOT_FOUND_NUMBER)
			NUMBER(PG_head_mask) = 1UL << NUMBER(PG_head);
	}

	ENUM_TYPE_SIZE_INIT(pageflags, "pageflags");

	TYPEDEF_SIZE_INIT(nodemask_t, "nodemask_t");

	SIZE_INIT(percpu_data, "percpu_data");

	/*
	 * Get offset of the elf_prstatus members.
	 */
	SIZE_INIT(elf_prstatus, "elf_prstatus");
	OFFSET_INIT(elf_prstatus.pr_reg, "elf_prstatus", "pr_reg");

	/*
	 * Get size of cpumask and cpumask_t.
	 */
	SIZE_INIT(cpumask, "cpumask");

	TYPEDEF_SIZE_INIT(cpumask_t, "cpumask_t");

	/*
	 * Get offset of the user_regs_struct members.
	 */
	SIZE_INIT(user_regs_struct, "user_regs_struct");

#ifdef __x86__
	if (SIZE(user_regs_struct) != NOT_FOUND_STRUCTURE) {
		OFFSET_INIT(user_regs_struct.bx, "user_regs_struct", "bx");
		OFFSET_INIT(user_regs_struct.cx, "user_regs_struct", "cx");
		OFFSET_INIT(user_regs_struct.dx, "user_regs_struct", "dx");
		OFFSET_INIT(user_regs_struct.si, "user_regs_struct", "si");
		OFFSET_INIT(user_regs_struct.di, "user_regs_struct", "di");
		OFFSET_INIT(user_regs_struct.bp, "user_regs_struct", "bp");
		OFFSET_INIT(user_regs_struct.ax, "user_regs_struct", "ax");
		OFFSET_INIT(user_regs_struct.ds, "user_regs_struct", "ds");
		OFFSET_INIT(user_regs_struct.es, "user_regs_struct", "es");
		OFFSET_INIT(user_regs_struct.fs, "user_regs_struct", "fs");
		OFFSET_INIT(user_regs_struct.gs, "user_regs_struct", "gs");
		OFFSET_INIT(user_regs_struct.orig_ax, "user_regs_struct",
			    "orig_ax");
		OFFSET_INIT(user_regs_struct.ip, "user_regs_struct", "ip");
		OFFSET_INIT(user_regs_struct.cs, "user_regs_struct", "cs");
		OFFSET_INIT(user_regs_struct.flags, "user_regs_struct",
			    "flags");
		OFFSET_INIT(user_regs_struct.sp, "user_regs_struct", "sp");
		OFFSET_INIT(user_regs_struct.ss, "user_regs_struct", "ss");

		if (OFFSET(user_regs_struct.bx) == NOT_FOUND_STRUCTURE)
			OFFSET_INIT(user_regs_struct.bx, "user_regs_struct", "ebx");
		if (OFFSET(user_regs_struct.cx) == NOT_FOUND_STRUCTURE)
			OFFSET_INIT(user_regs_struct.cx, "user_regs_struct", "ecx");
		if (OFFSET(user_regs_struct.dx) == NOT_FOUND_STRUCTURE)
			OFFSET_INIT(user_regs_struct.dx, "user_regs_struct", "edx");
		if (OFFSET(user_regs_struct.si) == NOT_FOUND_STRUCTURE)
			OFFSET_INIT(user_regs_struct.si, "user_regs_struct", "esi");
		if (OFFSET(user_regs_struct.di) == NOT_FOUND_STRUCTURE)
			OFFSET_INIT(user_regs_struct.di, "user_regs_struct", "edi");
		if (OFFSET(user_regs_struct.bp) == NOT_FOUND_STRUCTURE)
			OFFSET_INIT(user_regs_struct.bp, "user_regs_struct", "ebp");
		if (OFFSET(user_regs_struct.ax) == NOT_FOUND_STRUCTURE)
			OFFSET_INIT(user_regs_struct.ax, "user_regs_struct", "eax");
		if (OFFSET(user_regs_struct.orig_ax) == NOT_FOUND_STRUCTURE)
			OFFSET_INIT(user_regs_struct.orig_ax, "user_regs_struct", "orig_eax");
		if (OFFSET(user_regs_struct.ip) == NOT_FOUND_STRUCTURE)
			OFFSET_INIT(user_regs_struct.ip, "user_regs_struct", "eip");
		if (OFFSET(user_regs_struct.flags) == NOT_FOUND_STRUCTURE)
			OFFSET_INIT(user_regs_struct.flags, "user_regs_struct", "eflags");
		if (OFFSET(user_regs_struct.sp) == NOT_FOUND_STRUCTURE)
			OFFSET_INIT(user_regs_struct.sp, "user_regs_struct", "esp");
	} else {
		/*
		 * Note: Sometimes kernel debuginfo doesn't contain
		 * user_regs_struct structure information. Instead, we
		 * take offsets from actual datatype.
		 */
		OFFSET(user_regs_struct.bx) = offsetof(struct user_regs_struct, bx);
		OFFSET(user_regs_struct.cx) = offsetof(struct user_regs_struct, cx);
		OFFSET(user_regs_struct.dx) = offsetof(struct user_regs_struct, dx);
		OFFSET(user_regs_struct.si) = offsetof(struct user_regs_struct, si);
		OFFSET(user_regs_struct.di) = offsetof(struct user_regs_struct, di);
		OFFSET(user_regs_struct.bp) = offsetof(struct user_regs_struct, bp);
		OFFSET(user_regs_struct.ax) = offsetof(struct user_regs_struct, ax);
		OFFSET(user_regs_struct.ds) = offsetof(struct user_regs_struct, ds);
		OFFSET(user_regs_struct.es) = offsetof(struct user_regs_struct, es);
		OFFSET(user_regs_struct.fs) = offsetof(struct user_regs_struct, fs);
		OFFSET(user_regs_struct.gs) = offsetof(struct user_regs_struct, gs);
		OFFSET(user_regs_struct.orig_ax) = offsetof(struct user_regs_struct, orig_ax);
		OFFSET(user_regs_struct.ip) = offsetof(struct user_regs_struct, ip);
		OFFSET(user_regs_struct.cs) = offsetof(struct user_regs_struct, cs);
		OFFSET(user_regs_struct.flags) = offsetof(struct user_regs_struct, flags);
		OFFSET(user_regs_struct.sp) = offsetof(struct user_regs_struct, sp);
		OFFSET(user_regs_struct.ss) = offsetof(struct user_regs_struct, ss);
	}
#endif /* __x86__ */

#ifdef __x86_64__
	if (SIZE(user_regs_struct) != NOT_FOUND_STRUCTURE) {
		OFFSET_INIT(user_regs_struct.r15, "user_regs_struct", "r15");
		OFFSET_INIT(user_regs_struct.r14, "user_regs_struct", "r14");
		OFFSET_INIT(user_regs_struct.r13, "user_regs_struct", "r13");
		OFFSET_INIT(user_regs_struct.r12, "user_regs_struct", "r12");
		OFFSET_INIT(user_regs_struct.bp, "user_regs_struct", "bp");
		OFFSET_INIT(user_regs_struct.bx, "user_regs_struct", "bx");
		OFFSET_INIT(user_regs_struct.r11, "user_regs_struct", "r11");
		OFFSET_INIT(user_regs_struct.r10, "user_regs_struct", "r10");
		OFFSET_INIT(user_regs_struct.r9, "user_regs_struct", "r9");
		OFFSET_INIT(user_regs_struct.r8, "user_regs_struct", "r8");
		OFFSET_INIT(user_regs_struct.ax, "user_regs_struct", "ax");
		OFFSET_INIT(user_regs_struct.cx, "user_regs_struct", "cx");
		OFFSET_INIT(user_regs_struct.dx, "user_regs_struct", "dx");
		OFFSET_INIT(user_regs_struct.si, "user_regs_struct", "si");
		OFFSET_INIT(user_regs_struct.di, "user_regs_struct", "di");
		OFFSET_INIT(user_regs_struct.orig_ax, "user_regs_struct",
			    "orig_ax");
		OFFSET_INIT(user_regs_struct.ip, "user_regs_struct", "ip");
		OFFSET_INIT(user_regs_struct.cs, "user_regs_struct", "cs");
		OFFSET_INIT(user_regs_struct.flags, "user_regs_struct",
			    "flags");
		OFFSET_INIT(user_regs_struct.sp, "user_regs_struct", "sp");
		OFFSET_INIT(user_regs_struct.ss, "user_regs_struct", "ss");
		OFFSET_INIT(user_regs_struct.fs_base, "user_regs_struct",
			    "fs_base");
		OFFSET_INIT(user_regs_struct.gs_base, "user_regs_struct",
			    "gs_base");
		OFFSET_INIT(user_regs_struct.ds, "user_regs_struct", "ds");
		OFFSET_INIT(user_regs_struct.es, "user_regs_struct", "es");
		OFFSET_INIT(user_regs_struct.fs, "user_regs_struct", "fs");
		OFFSET_INIT(user_regs_struct.gs, "user_regs_struct", "gs");
	} else {
		/*
		 * Note: Sometimes kernel debuginfo doesn't contain
		 * user_regs_struct structure information. Instead, we
		 * take offsets from actual datatype.
		 */
		OFFSET(user_regs_struct.r15) = offsetof(struct user_regs_struct, r15);
		OFFSET(user_regs_struct.r14) = offsetof(struct user_regs_struct, r14);
		OFFSET(user_regs_struct.r13) = offsetof(struct user_regs_struct, r13);
		OFFSET(user_regs_struct.r12) = offsetof(struct user_regs_struct, r12);
		OFFSET(user_regs_struct.bp) = offsetof(struct user_regs_struct, bp);
		OFFSET(user_regs_struct.bx) = offsetof(struct user_regs_struct, bx);
		OFFSET(user_regs_struct.r11) = offsetof(struct user_regs_struct, r11);
		OFFSET(user_regs_struct.r10) = offsetof(struct user_regs_struct, r10);
		OFFSET(user_regs_struct.r9) = offsetof(struct user_regs_struct, r9);
		OFFSET(user_regs_struct.r8) = offsetof(struct user_regs_struct, r8);
		OFFSET(user_regs_struct.ax) = offsetof(struct user_regs_struct, ax);
		OFFSET(user_regs_struct.cx) = offsetof(struct user_regs_struct, cx);
		OFFSET(user_regs_struct.dx) = offsetof(struct user_regs_struct, dx);
		OFFSET(user_regs_struct.si) = offsetof(struct user_regs_struct, si);
		OFFSET(user_regs_struct.di) = offsetof(struct user_regs_struct, di);
		OFFSET(user_regs_struct.orig_ax) = offsetof(struct user_regs_struct, orig_ax);
		OFFSET(user_regs_struct.ip) = offsetof(struct user_regs_struct, ip);
		OFFSET(user_regs_struct.cs) = offsetof(struct user_regs_struct, cs);
		OFFSET(user_regs_struct.flags) = offsetof(struct user_regs_struct, flags);
		OFFSET(user_regs_struct.sp) = offsetof(struct user_regs_struct, sp);
		OFFSET(user_regs_struct.ss) = offsetof(struct user_regs_struct, ss);
		OFFSET(user_regs_struct.fs_base) = offsetof(struct user_regs_struct, fs_base);
		OFFSET(user_regs_struct.gs_base) = offsetof(struct user_regs_struct, gs_base);
		OFFSET(user_regs_struct.ds) = offsetof(struct user_regs_struct, ds);
		OFFSET(user_regs_struct.es) = offsetof(struct user_regs_struct, es);
		OFFSET(user_regs_struct.fs) = offsetof(struct user_regs_struct, fs);
		OFFSET(user_regs_struct.gs) = offsetof(struct user_regs_struct, gs);
	}
#endif /* __x86_64__ */

	OFFSET_INIT(kimage.segment, "kimage", "segment");

	MEMBER_ARRAY_LENGTH_INIT(kimage.segment, "kimage", "segment");

	SIZE_INIT(kexec_segment, "kexec_segment");
	OFFSET_INIT(kexec_segment.mem, "kexec_segment", "mem");

	OFFSET_INIT(elf64_hdr.e_phnum, "elf64_hdr", "e_phnum");
	OFFSET_INIT(elf64_hdr.e_phentsize, "elf64_hdr", "e_phentsize");
	OFFSET_INIT(elf64_hdr.e_phoff, "elf64_hdr", "e_phoff");

	SIZE_INIT(elf64_hdr, "elf64_hdr");
	OFFSET_INIT(elf64_phdr.p_type, "elf64_phdr", "p_type");
	OFFSET_INIT(elf64_phdr.p_offset, "elf64_phdr", "p_offset");
	OFFSET_INIT(elf64_phdr.p_paddr, "elf64_phdr", "p_paddr");
	OFFSET_INIT(elf64_phdr.p_memsz, "elf64_phdr", "p_memsz");

	SIZE_INIT(printk_log, "printk_log");
	if (SIZE(printk_log) != NOT_FOUND_STRUCTURE) {
		/*
		 * In kernel 3.11-rc4 the log structure name was renamed
		 * to "printk_log".
		 */
		info->flag_use_printk_log = TRUE;
		OFFSET_INIT(printk_log.ts_nsec, "printk_log", "ts_nsec");
		OFFSET_INIT(printk_log.len, "printk_log", "len");
		OFFSET_INIT(printk_log.text_len, "printk_log", "text_len");
	} else {
		info->flag_use_printk_log = FALSE;
		SIZE_INIT(printk_log, "log");
		OFFSET_INIT(printk_log.ts_nsec, "log", "ts_nsec");
		OFFSET_INIT(printk_log.len, "log", "len");
		OFFSET_INIT(printk_log.text_len, "log", "text_len");
	}

	/*
	 * Get offsets of the vmemmap_backing's members.
	 */
	SIZE_INIT(vmemmap_backing, "vmemmap_backing");
	OFFSET_INIT(vmemmap_backing.phys, "vmemmap_backing", "phys");
	OFFSET_INIT(vmemmap_backing.virt_addr, "vmemmap_backing", "virt_addr");
	OFFSET_INIT(vmemmap_backing.list, "vmemmap_backing", "list");

	/*
	 * Get offsets of the mmu_psize_def's members.
	 */
	SIZE_INIT(mmu_psize_def, "mmu_psize_def");
	OFFSET_INIT(mmu_psize_def.shift, "mmu_psize_def", "shift");

	return TRUE;
}

int
get_srcfile_info(void)
{
	TYPEDEF_SRCFILE_INIT(pud_t, "pud_t");

	return TRUE;
}

int
get_value_for_old_linux(void)
{
	if (NUMBER(PG_lru) == NOT_FOUND_NUMBER)
		NUMBER(PG_lru) = PG_lru_ORIGINAL;
	if (NUMBER(PG_private) == NOT_FOUND_NUMBER)
		NUMBER(PG_private) = PG_private_ORIGINAL;
	if (NUMBER(PG_swapcache) == NOT_FOUND_NUMBER)
		NUMBER(PG_swapcache) = PG_swapcache_ORIGINAL;
	if (NUMBER(PG_slab) == NOT_FOUND_NUMBER)
		NUMBER(PG_slab) = PG_slab_ORIGINAL;
	if (NUMBER(PG_head_mask) == NOT_FOUND_NUMBER)
		NUMBER(PG_head_mask) = 1L << PG_compound_ORIGINAL;

	/*
	 * The values from here are for free page filtering based on
	 * mem_map array. These are minimum effort to cover old
	 * kernels.
	 *
	 * The logic also needs offset values for some members of page
	 * structure. But it much depends on kernel versions. We avoid
	 * to hard code the values.
	 */
	if (NUMBER(PAGE_BUDDY_MAPCOUNT_VALUE) == NOT_FOUND_NUMBER) {
		if (info->kernel_version == KERNEL_VERSION(2, 6, 38))
			NUMBER(PAGE_BUDDY_MAPCOUNT_VALUE) =
				PAGE_BUDDY_MAPCOUNT_VALUE_v2_6_38;
		if (info->kernel_version >= KERNEL_VERSION(2, 6, 39))
			NUMBER(PAGE_BUDDY_MAPCOUNT_VALUE) =
			PAGE_BUDDY_MAPCOUNT_VALUE_v2_6_39_to_latest_version;
	}
	if (SIZE(pageflags) == NOT_FOUND_STRUCTURE) {
		if (info->kernel_version >= KERNEL_VERSION(2, 6, 27))
			SIZE(pageflags) =
				PAGE_FLAGS_SIZE_v2_6_27_to_latest_version;
	}
	return TRUE;
}

int
get_str_osrelease_from_vmlinux(void)
{
	int fd;
	char *name;
	struct utsname system_utsname;
	unsigned long long utsname;
	off_t offset;
	const off_t failed = (off_t)-1;

	/*
	 * Get the kernel version.
	 */
	if (SYMBOL(system_utsname) != NOT_FOUND_SYMBOL) {
		utsname = SYMBOL(system_utsname);
	} else if (SYMBOL(init_uts_ns) != NOT_FOUND_SYMBOL) {
		utsname = SYMBOL(init_uts_ns) + sizeof(int);
	} else {
		ERRMSG("Can't get the symbol of system_utsname.\n");
		return FALSE;
	}
	get_fileinfo_of_debuginfo(&fd, &name);

	offset = vaddr_to_offset_slow(fd, name, utsname);
	if (!offset) {
		ERRMSG("Can't convert vaddr (%llx) of utsname to an offset.\n",
		    utsname);
		return FALSE;
	}
	if (lseek(fd, offset, SEEK_SET) == failed) {
		ERRMSG("Can't seek %s. %s\n", name, strerror(errno));
		return FALSE;
	}
	if (read(fd, &system_utsname, sizeof system_utsname)
	    != sizeof system_utsname) {
		ERRMSG("Can't read %s. %s\n", name, strerror(errno));
		return FALSE;
	}
	if (!strncpy(info->release, system_utsname.release, STRLEN_OSRELEASE)){
		ERRMSG("Can't do strncpy for osrelease.");
		return FALSE;
	}
	return TRUE;
}

int
is_sparsemem_extreme(void)
{
	if (ARRAY_LENGTH(mem_section)
	     == divideup(NR_MEM_SECTIONS(), _SECTIONS_PER_ROOT_EXTREME()))
		return TRUE;
	else
		return FALSE;
}

int
get_mem_type(void)
{
	int ret;

	if ((SIZE(page) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(page.flags) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(page._count) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(page.mapping) == NOT_FOUND_STRUCTURE)) {
		ret = NOT_FOUND_MEMTYPE;
	} else if ((((SYMBOL(node_data) != NOT_FOUND_SYMBOL)
	        && (ARRAY_LENGTH(node_data) != NOT_FOUND_STRUCTURE))
	    || ((SYMBOL(pgdat_list) != NOT_FOUND_SYMBOL)
	        && (OFFSET(pglist_data.pgdat_next) != NOT_FOUND_STRUCTURE))
	    || ((SYMBOL(pgdat_list) != NOT_FOUND_SYMBOL)
	        && (ARRAY_LENGTH(pgdat_list) != NOT_FOUND_STRUCTURE)))
	    && (SIZE(pglist_data) != NOT_FOUND_STRUCTURE)
	    && (OFFSET(pglist_data.node_mem_map) != NOT_FOUND_STRUCTURE)
	    && (OFFSET(pglist_data.node_start_pfn) != NOT_FOUND_STRUCTURE)
	    && (OFFSET(pglist_data.node_spanned_pages) !=NOT_FOUND_STRUCTURE)){
		ret = DISCONTIGMEM;
	} else if ((SYMBOL(mem_section) != NOT_FOUND_SYMBOL)
	    && (SIZE(mem_section) != NOT_FOUND_STRUCTURE)
	    && (OFFSET(mem_section.section_mem_map) != NOT_FOUND_STRUCTURE)
	    && (ARRAY_LENGTH(mem_section) != NOT_FOUND_STRUCTURE)) {
		if (is_sparsemem_extreme())
			ret = SPARSEMEM_EX;
		else
			ret = SPARSEMEM;
	} else if (SYMBOL(mem_map) != NOT_FOUND_SYMBOL) {
		ret = FLATMEM;
	} else {
		ret = NOT_FOUND_MEMTYPE;
	}

	return ret;
}

void
write_vmcoreinfo_data(void)
{
	/*
	 * write 1st kernel's OSRELEASE
	 */
	fprintf(info->file_vmcoreinfo, "%s%s\n", STR_OSRELEASE,
	    info->release);

	/*
	 * write 1st kernel's PAGESIZE
	 */
	fprintf(info->file_vmcoreinfo, "%s%ld\n", STR_PAGESIZE,
	    info->page_size);

	/*
	 * write the symbol of 1st kernel
	 */
	WRITE_SYMBOL("mem_map", mem_map);
	WRITE_SYMBOL("vmem_map", vmem_map);
	WRITE_SYMBOL("mem_section", mem_section);
	WRITE_SYMBOL("pkmap_count", pkmap_count);
	WRITE_SYMBOL("pkmap_count_next", pkmap_count_next);
	WRITE_SYMBOL("system_utsname", system_utsname);
	WRITE_SYMBOL("init_uts_ns", init_uts_ns);
	WRITE_SYMBOL("_stext", _stext);
	WRITE_SYMBOL("swapper_pg_dir", swapper_pg_dir);
	WRITE_SYMBOL("init_level4_pgt", init_level4_pgt);
	WRITE_SYMBOL("vmlist", vmlist);
	WRITE_SYMBOL("vmap_area_list", vmap_area_list);
	WRITE_SYMBOL("phys_base", phys_base);
	WRITE_SYMBOL("node_online_map", node_online_map);
	WRITE_SYMBOL("node_states", node_states);
	WRITE_SYMBOL("node_data", node_data);
	WRITE_SYMBOL("pgdat_list", pgdat_list);
	WRITE_SYMBOL("contig_page_data", contig_page_data);
	WRITE_SYMBOL("log_buf", log_buf);
	WRITE_SYMBOL("log_buf_len", log_buf_len);
	WRITE_SYMBOL("log_end", log_end);
	WRITE_SYMBOL("log_first_idx", log_first_idx);
	WRITE_SYMBOL("log_next_idx", log_next_idx);
	WRITE_SYMBOL("max_pfn", max_pfn);
	WRITE_SYMBOL("high_memory", high_memory);
	WRITE_SYMBOL("node_remap_start_vaddr", node_remap_start_vaddr);
	WRITE_SYMBOL("node_remap_end_vaddr", node_remap_end_vaddr);
	WRITE_SYMBOL("node_remap_start_pfn", node_remap_start_pfn);
	WRITE_SYMBOL("vmemmap_list", vmemmap_list);
	WRITE_SYMBOL("mmu_psize_defs", mmu_psize_defs);
	WRITE_SYMBOL("mmu_vmemmap_psize", mmu_vmemmap_psize);
	WRITE_SYMBOL("cpu_pgd", cpu_pgd);
	WRITE_SYMBOL("demote_segment_4k", demote_segment_4k);
	WRITE_SYMBOL("free_huge_page", free_huge_page);

	/*
	 * write the structure size of 1st kernel
	 */
	WRITE_STRUCTURE_SIZE("page", page);
	WRITE_STRUCTURE_SIZE("mem_section", mem_section);
	WRITE_STRUCTURE_SIZE("pglist_data", pglist_data);
	WRITE_STRUCTURE_SIZE("zone", zone);
	WRITE_STRUCTURE_SIZE("free_area", free_area);
	WRITE_STRUCTURE_SIZE("list_head", list_head);
	WRITE_STRUCTURE_SIZE("node_memblk_s", node_memblk_s);
	WRITE_STRUCTURE_SIZE("nodemask_t", nodemask_t);
	WRITE_STRUCTURE_SIZE("pageflags", pageflags);
	if (info->flag_use_printk_log)
		WRITE_STRUCTURE_SIZE("printk_log", printk_log);
	else
		WRITE_STRUCTURE_SIZE("log", printk_log);
	WRITE_STRUCTURE_SIZE("vmemmap_backing", vmemmap_backing);
	WRITE_STRUCTURE_SIZE("mmu_psize_def", mmu_psize_def);

	/*
	 * write the member offset of 1st kernel
	 */
	WRITE_MEMBER_OFFSET("page.flags", page.flags);
	WRITE_MEMBER_OFFSET("page._count", page._count);
	WRITE_MEMBER_OFFSET("page.mapping", page.mapping);
	WRITE_MEMBER_OFFSET("page.lru", page.lru);
	WRITE_MEMBER_OFFSET("page._mapcount", page._mapcount);
	WRITE_MEMBER_OFFSET("page.private", page.private);
	WRITE_MEMBER_OFFSET("mem_section.section_mem_map",
	    mem_section.section_mem_map);
	WRITE_MEMBER_OFFSET("pglist_data.node_zones", pglist_data.node_zones);
	WRITE_MEMBER_OFFSET("pglist_data.nr_zones", pglist_data.nr_zones);
	WRITE_MEMBER_OFFSET("pglist_data.node_mem_map",
	    pglist_data.node_mem_map);
	WRITE_MEMBER_OFFSET("pglist_data.node_start_pfn",
	    pglist_data.node_start_pfn);
	WRITE_MEMBER_OFFSET("pglist_data.node_spanned_pages",
	    pglist_data.node_spanned_pages);
	WRITE_MEMBER_OFFSET("pglist_data.pgdat_next", pglist_data.pgdat_next);
	WRITE_MEMBER_OFFSET("zone.free_pages", zone.free_pages);
	WRITE_MEMBER_OFFSET("zone.free_area", zone.free_area);
	WRITE_MEMBER_OFFSET("zone.vm_stat", zone.vm_stat);
	WRITE_MEMBER_OFFSET("zone.spanned_pages", zone.spanned_pages);
	WRITE_MEMBER_OFFSET("free_area.free_list", free_area.free_list);
	WRITE_MEMBER_OFFSET("list_head.next", list_head.next);
	WRITE_MEMBER_OFFSET("list_head.prev", list_head.prev);
	WRITE_MEMBER_OFFSET("node_memblk_s.start_paddr", node_memblk_s.start_paddr);
	WRITE_MEMBER_OFFSET("node_memblk_s.size", node_memblk_s.size);
	WRITE_MEMBER_OFFSET("node_memblk_s.nid", node_memblk_s.nid);
	WRITE_MEMBER_OFFSET("vm_struct.addr", vm_struct.addr);
	WRITE_MEMBER_OFFSET("vmap_area.va_start", vmap_area.va_start);
	WRITE_MEMBER_OFFSET("vmap_area.list", vmap_area.list);
	if (info->flag_use_printk_log) {
		WRITE_MEMBER_OFFSET("printk_log.ts_nsec", printk_log.ts_nsec);
		WRITE_MEMBER_OFFSET("printk_log.len", printk_log.len);
		WRITE_MEMBER_OFFSET("printk_log.text_len", printk_log.text_len);
	} else {
		/* Compatibility with pre-3.11-rc4 */
		WRITE_MEMBER_OFFSET("log.ts_nsec", printk_log.ts_nsec);
		WRITE_MEMBER_OFFSET("log.len", printk_log.len);
		WRITE_MEMBER_OFFSET("log.text_len", printk_log.text_len);
	}
	WRITE_MEMBER_OFFSET("vmemmap_backing.phys", vmemmap_backing.phys);
	WRITE_MEMBER_OFFSET("vmemmap_backing.virt_addr",
	    vmemmap_backing.virt_addr);
	WRITE_MEMBER_OFFSET("vmemmap_backing.list", vmemmap_backing.list);
	WRITE_MEMBER_OFFSET("mmu_psize_def.shift", mmu_psize_def.shift);

	if (SYMBOL(node_data) != NOT_FOUND_SYMBOL)
		WRITE_ARRAY_LENGTH("node_data", node_data);
	if (SYMBOL(pgdat_list) != NOT_FOUND_SYMBOL)
		WRITE_ARRAY_LENGTH("pgdat_list", pgdat_list);
	if (SYMBOL(mem_section) != NOT_FOUND_SYMBOL)
		WRITE_ARRAY_LENGTH("mem_section", mem_section);
	if (SYMBOL(node_memblk) != NOT_FOUND_SYMBOL)
		WRITE_ARRAY_LENGTH("node_memblk", node_memblk);
	if (SYMBOL(node_remap_start_pfn) != NOT_FOUND_SYMBOL)
		WRITE_ARRAY_LENGTH("node_remap_start_pfn",
				   node_remap_start_pfn);

	WRITE_ARRAY_LENGTH("zone.free_area", zone.free_area);
	WRITE_ARRAY_LENGTH("free_area.free_list", free_area.free_list);

	WRITE_NUMBER("NR_FREE_PAGES", NR_FREE_PAGES);
	WRITE_NUMBER("N_ONLINE", N_ONLINE);

	WRITE_NUMBER("PG_lru", PG_lru);
	WRITE_NUMBER("PG_private", PG_private);
	WRITE_NUMBER("PG_head_mask", PG_head_mask);
	WRITE_NUMBER("PG_swapcache", PG_swapcache);
	WRITE_NUMBER("PG_buddy", PG_buddy);
	WRITE_NUMBER("PG_slab", PG_slab);
	WRITE_NUMBER("PG_hwpoison", PG_hwpoison);

	WRITE_NUMBER("PAGE_BUDDY_MAPCOUNT_VALUE", PAGE_BUDDY_MAPCOUNT_VALUE);

	/*
	 * write the source file of 1st kernel
	 */
	WRITE_SRCFILE("pud_t", pud_t);
}

int
generate_vmcoreinfo(void)
{
	if (!set_page_size(sysconf(_SC_PAGE_SIZE)))
		return FALSE;

	set_dwarf_debuginfo("vmlinux", NULL,
			    info->name_vmlinux, info->fd_vmlinux);

	if (!get_symbol_info())
		return FALSE;

	if (!get_structure_info())
		return FALSE;

	if (!get_srcfile_info())
		return FALSE;

	if ((SYMBOL(system_utsname) == NOT_FOUND_SYMBOL)
	    && (SYMBOL(init_uts_ns) == NOT_FOUND_SYMBOL)) {
		ERRMSG("Can't get the symbol of system_utsname.\n");
		return FALSE;
	}
	if (!get_str_osrelease_from_vmlinux())
		return FALSE;

	if (!(info->kernel_version = get_kernel_version(info->release)))
		return FALSE;

	if (get_mem_type() == NOT_FOUND_MEMTYPE) {
		ERRMSG("Can't find the memory type.\n");
		return FALSE;
	}

	write_vmcoreinfo_data();

	return TRUE;
}

int
read_vmcoreinfo_basic_info(void)
{
	time_t tv_sec = 0;
	long page_size = FALSE;
	char buf[BUFSIZE_FGETS], *endp;
	unsigned int get_release = FALSE, i;

	if (fseek(info->file_vmcoreinfo, 0, SEEK_SET) < 0) {
		ERRMSG("Can't seek the vmcoreinfo file(%s). %s\n",
		    info->name_vmcoreinfo, strerror(errno));
		return FALSE;
	}

	while (fgets(buf, BUFSIZE_FGETS, info->file_vmcoreinfo)) {
		i = strlen(buf);
		if (!i)
			break;
		if (buf[i - 1] == '\n')
			buf[i - 1] = '\0';
		if (strncmp(buf, STR_OSRELEASE, strlen(STR_OSRELEASE)) == 0) {
			get_release = TRUE;
			/* if the release have been stored, skip this time. */
			if (strlen(info->release))
				continue;
			strcpy(info->release, buf + strlen(STR_OSRELEASE));
		}
		if (strncmp(buf, STR_PAGESIZE, strlen(STR_PAGESIZE)) == 0) {
			page_size = strtol(buf+strlen(STR_PAGESIZE),&endp,10);
			if ((!page_size || page_size == LONG_MAX)
			    || strlen(endp) != 0) {
				ERRMSG("Invalid data in %s: %s",
				    info->name_vmcoreinfo, buf);
				return FALSE;
			}
			if (!set_page_size(page_size)) {
				ERRMSG("Invalid data in %s: %s",
				    info->name_vmcoreinfo, buf);
				return FALSE;
			}
		}
		if (strncmp(buf, STR_CRASHTIME, strlen(STR_CRASHTIME)) == 0) {
			tv_sec = strtol(buf+strlen(STR_CRASHTIME),&endp,10);
			if ((!tv_sec || tv_sec == LONG_MAX)
			    || strlen(endp) != 0) {
				ERRMSG("Invalid data in %s: %s",
				    info->name_vmcoreinfo, buf);
				return FALSE;
			}
			info->timestamp.tv_sec = tv_sec;
		}
		if (strncmp(buf, STR_CONFIG_X86_PAE,
		    strlen(STR_CONFIG_X86_PAE)) == 0)
			vt.mem_flags |= MEMORY_X86_PAE;

		if (strncmp(buf, STR_CONFIG_PGTABLE_3,
		    strlen(STR_CONFIG_PGTABLE_3)) == 0)
			vt.mem_flags |= MEMORY_PAGETABLE_3L;

		if (strncmp(buf, STR_CONFIG_PGTABLE_4,
		    strlen(STR_CONFIG_PGTABLE_4)) == 0)
			vt.mem_flags |= MEMORY_PAGETABLE_4L;
	}
	if (!get_release || !info->page_size) {
		ERRMSG("Invalid format in %s", info->name_vmcoreinfo);
		return FALSE;
	}
	return TRUE;
}

unsigned long
read_vmcoreinfo_symbol(char *str_symbol)
{
	unsigned long symbol = NOT_FOUND_SYMBOL;
	char buf[BUFSIZE_FGETS], *endp;
	unsigned int i;

	if (fseek(info->file_vmcoreinfo, 0, SEEK_SET) < 0) {
		ERRMSG("Can't seek the vmcoreinfo file(%s). %s\n",
		    info->name_vmcoreinfo, strerror(errno));
		return INVALID_SYMBOL_DATA;
	}

	while (fgets(buf, BUFSIZE_FGETS, info->file_vmcoreinfo)) {
		i = strlen(buf);
		if (!i)
			break;
		if (buf[i - 1] == '\n')
			buf[i - 1] = '\0';
		if (strncmp(buf, str_symbol, strlen(str_symbol)) == 0) {
			symbol = strtoul(buf + strlen(str_symbol), &endp, 16);
			if ((!symbol || symbol == ULONG_MAX)
			    || strlen(endp) != 0) {
				ERRMSG("Invalid data in %s: %s",
				    info->name_vmcoreinfo, buf);
				return INVALID_SYMBOL_DATA;
			}
			break;
		}
	}
	return symbol;
}

long
read_vmcoreinfo_long(char *str_structure)
{
	long data = NOT_FOUND_LONG_VALUE;
	char buf[BUFSIZE_FGETS], *endp;
	unsigned int i;

	if (fseek(info->file_vmcoreinfo, 0, SEEK_SET) < 0) {
		ERRMSG("Can't seek the vmcoreinfo file(%s). %s\n",
		    info->name_vmcoreinfo, strerror(errno));
		return INVALID_STRUCTURE_DATA;
	}

	while (fgets(buf, BUFSIZE_FGETS, info->file_vmcoreinfo)) {
		i = strlen(buf);
		if (!i)
			break;
		if (buf[i - 1] == '\n')
			buf[i - 1] = '\0';
		if (strncmp(buf, str_structure, strlen(str_structure)) == 0) {
			data = strtol(buf + strlen(str_structure), &endp, 10);
			if ((data == LONG_MAX) || strlen(endp) != 0) {
				ERRMSG("Invalid data in %s: %s",
				    info->name_vmcoreinfo, buf);
				return INVALID_STRUCTURE_DATA;
			}
			break;
		}
	}
	return data;
}

int
read_vmcoreinfo_string(char *str_in, char *str_out)
{
	char buf[BUFSIZE_FGETS];
	unsigned int i;

	if (fseek(info->file_vmcoreinfo, 0, SEEK_SET) < 0) {
		ERRMSG("Can't seek the vmcoreinfo file(%s). %s\n",
		    info->name_vmcoreinfo, strerror(errno));
		return FALSE;
	}

	while (fgets(buf, BUFSIZE_FGETS, info->file_vmcoreinfo)) {
		i = strlen(buf);
		if (!i)
			break;
		if (buf[i - 1] == '\n')
			buf[i - 1] = '\0';
		if (strncmp(buf, str_in, strlen(str_in)) == 0) {
			strncpy(str_out, buf + strlen(str_in), LEN_SRCFILE - strlen(str_in));
			break;
		}
	}
	return TRUE;
}

int
read_vmcoreinfo(void)
{
	if (!read_vmcoreinfo_basic_info())
		return FALSE;

	READ_SYMBOL("mem_map", mem_map);
	READ_SYMBOL("vmem_map", vmem_map);
	READ_SYMBOL("mem_section", mem_section);
	READ_SYMBOL("pkmap_count", pkmap_count);
	READ_SYMBOL("pkmap_count_next", pkmap_count_next);
	READ_SYMBOL("system_utsname", system_utsname);
	READ_SYMBOL("init_uts_ns", init_uts_ns);
	READ_SYMBOL("_stext", _stext);
	READ_SYMBOL("swapper_pg_dir", swapper_pg_dir);
	READ_SYMBOL("init_level4_pgt", init_level4_pgt);
	READ_SYMBOL("vmlist", vmlist);
	READ_SYMBOL("vmap_area_list", vmap_area_list);
	READ_SYMBOL("phys_base", phys_base);
	READ_SYMBOL("node_online_map", node_online_map);
	READ_SYMBOL("node_states", node_states);
	READ_SYMBOL("node_data", node_data);
	READ_SYMBOL("pgdat_list", pgdat_list);
	READ_SYMBOL("contig_page_data", contig_page_data);
	READ_SYMBOL("log_buf", log_buf);
	READ_SYMBOL("log_buf_len", log_buf_len);
	READ_SYMBOL("log_end", log_end);
	READ_SYMBOL("log_first_idx", log_first_idx);
	READ_SYMBOL("log_next_idx", log_next_idx);
	READ_SYMBOL("max_pfn", max_pfn);
	READ_SYMBOL("high_memory", high_memory);
	READ_SYMBOL("node_remap_start_vaddr", node_remap_start_vaddr);
	READ_SYMBOL("node_remap_end_vaddr", node_remap_end_vaddr);
	READ_SYMBOL("node_remap_start_pfn", node_remap_start_pfn);
	READ_SYMBOL("vmemmap_list", vmemmap_list);
	READ_SYMBOL("mmu_psize_defs", mmu_psize_defs);
	READ_SYMBOL("mmu_vmemmap_psize", mmu_vmemmap_psize);
	READ_SYMBOL("cpu_pgd", cpu_pgd);
	READ_SYMBOL("demote_segment_4k", demote_segment_4k);
	READ_SYMBOL("free_huge_page", free_huge_page);

	READ_STRUCTURE_SIZE("page", page);
	READ_STRUCTURE_SIZE("mem_section", mem_section);
	READ_STRUCTURE_SIZE("pglist_data", pglist_data);
	READ_STRUCTURE_SIZE("zone", zone);
	READ_STRUCTURE_SIZE("free_area", free_area);
	READ_STRUCTURE_SIZE("list_head", list_head);
	READ_STRUCTURE_SIZE("node_memblk_s", node_memblk_s);
	READ_STRUCTURE_SIZE("nodemask_t", nodemask_t);
	READ_STRUCTURE_SIZE("pageflags", pageflags);
	READ_STRUCTURE_SIZE("vmemmap_backing", vmemmap_backing);
	READ_STRUCTURE_SIZE("mmu_psize_def", mmu_psize_def);


	READ_MEMBER_OFFSET("page.flags", page.flags);
	READ_MEMBER_OFFSET("page._count", page._count);
	READ_MEMBER_OFFSET("page.mapping", page.mapping);
	READ_MEMBER_OFFSET("page.lru", page.lru);
	READ_MEMBER_OFFSET("page._mapcount", page._mapcount);
	READ_MEMBER_OFFSET("page.private", page.private);
	READ_MEMBER_OFFSET("mem_section.section_mem_map",
	    mem_section.section_mem_map);
	READ_MEMBER_OFFSET("pglist_data.node_zones", pglist_data.node_zones);
	READ_MEMBER_OFFSET("pglist_data.nr_zones", pglist_data.nr_zones);
	READ_MEMBER_OFFSET("pglist_data.node_mem_map",pglist_data.node_mem_map);
	READ_MEMBER_OFFSET("pglist_data.node_start_pfn",
	    pglist_data.node_start_pfn);
	READ_MEMBER_OFFSET("pglist_data.node_spanned_pages",
	    pglist_data.node_spanned_pages);
	READ_MEMBER_OFFSET("pglist_data.pgdat_next", pglist_data.pgdat_next);
	READ_MEMBER_OFFSET("zone.free_pages", zone.free_pages);
	READ_MEMBER_OFFSET("zone.free_area", zone.free_area);
	READ_MEMBER_OFFSET("zone.vm_stat", zone.vm_stat);
	READ_MEMBER_OFFSET("zone.spanned_pages", zone.spanned_pages);
	READ_MEMBER_OFFSET("free_area.free_list", free_area.free_list);
	READ_MEMBER_OFFSET("list_head.next", list_head.next);
	READ_MEMBER_OFFSET("list_head.prev", list_head.prev);
	READ_MEMBER_OFFSET("node_memblk_s.start_paddr", node_memblk_s.start_paddr);
	READ_MEMBER_OFFSET("node_memblk_s.size", node_memblk_s.size);
	READ_MEMBER_OFFSET("node_memblk_s.nid", node_memblk_s.nid);
	READ_MEMBER_OFFSET("vm_struct.addr", vm_struct.addr);
	READ_MEMBER_OFFSET("vmap_area.va_start", vmap_area.va_start);
	READ_MEMBER_OFFSET("vmap_area.list", vmap_area.list);
	READ_MEMBER_OFFSET("vmemmap_backing.phys", vmemmap_backing.phys);
	READ_MEMBER_OFFSET("vmemmap_backing.virt_addr",
	    vmemmap_backing.virt_addr);
	READ_MEMBER_OFFSET("vmemmap_backing.list", vmemmap_backing.list);
	READ_MEMBER_OFFSET("mmu_psize_def.shift", mmu_psize_def.shift);

	READ_STRUCTURE_SIZE("printk_log", printk_log);
	if (SIZE(printk_log) != NOT_FOUND_STRUCTURE) {
		info->flag_use_printk_log = TRUE;
		READ_MEMBER_OFFSET("printk_log.ts_nsec", printk_log.ts_nsec);
		READ_MEMBER_OFFSET("printk_log.len", printk_log.len);
		READ_MEMBER_OFFSET("printk_log.text_len", printk_log.text_len);
	} else {
		info->flag_use_printk_log = FALSE;
		READ_STRUCTURE_SIZE("log", printk_log);
		READ_MEMBER_OFFSET("log.ts_nsec", printk_log.ts_nsec);
		READ_MEMBER_OFFSET("log.len", printk_log.len);
		READ_MEMBER_OFFSET("log.text_len", printk_log.text_len);
	}

	READ_ARRAY_LENGTH("node_data", node_data);
	READ_ARRAY_LENGTH("pgdat_list", pgdat_list);
	READ_ARRAY_LENGTH("mem_section", mem_section);
	READ_ARRAY_LENGTH("node_memblk", node_memblk);
	READ_ARRAY_LENGTH("zone.free_area", zone.free_area);
	READ_ARRAY_LENGTH("free_area.free_list", free_area.free_list);
	READ_ARRAY_LENGTH("node_remap_start_pfn", node_remap_start_pfn);

	READ_NUMBER("NR_FREE_PAGES", NR_FREE_PAGES);
	READ_NUMBER("N_ONLINE", N_ONLINE);

	READ_NUMBER("PG_lru", PG_lru);
	READ_NUMBER("PG_private", PG_private);
	READ_NUMBER("PG_head_mask", PG_head_mask);
	READ_NUMBER("PG_swapcache", PG_swapcache);
	READ_NUMBER("PG_slab", PG_slab);
	READ_NUMBER("PG_buddy", PG_buddy);
	READ_NUMBER("PG_hwpoison", PG_hwpoison);
	READ_NUMBER("SECTION_SIZE_BITS", SECTION_SIZE_BITS);
	READ_NUMBER("MAX_PHYSMEM_BITS", MAX_PHYSMEM_BITS);

	READ_SRCFILE("pud_t", pud_t);

	READ_NUMBER("PAGE_BUDDY_MAPCOUNT_VALUE", PAGE_BUDDY_MAPCOUNT_VALUE);

	return TRUE;
}

/*
 * Extract vmcoreinfo from /proc/vmcore and output it to /tmp/vmcoreinfo.tmp.
 */
int
copy_vmcoreinfo(off_t offset, unsigned long size)
{
	int fd;
	char *buf = NULL;
	const off_t failed = (off_t)-1;

	if (!offset || !size)
		return FALSE;

	if ((fd = mkstemp(info->name_vmcoreinfo)) < 0) {
		ERRMSG("Can't open the vmcoreinfo file(%s). %s\n",
		    info->name_vmcoreinfo, strerror(errno));
		return FALSE;
	}
	if (lseek(info->fd_memory, offset, SEEK_SET) == failed) {
		ERRMSG("Can't seek the dump memory(%s). %s\n",
		    info->name_memory, strerror(errno));
		return FALSE;
	}
	if ((buf = malloc(size)) == NULL) {
		ERRMSG("Can't allocate memory for vmcoreinfo buffer. %s\n",
			strerror(errno));
		return FALSE;
	}
	if (read(info->fd_memory, buf, size) != size) {
		ERRMSG("Can't read the dump memory(%s). %s\n",
		    info->name_memory, strerror(errno));
		goto out;
	}
	if (write(fd, buf, size) != size) {
		ERRMSG("Can't write the vmcoreinfo file(%s). %s\n",
		    info->name_vmcoreinfo, strerror(errno));
		goto out;
	}
	if (close(fd) < 0) {
		ERRMSG("Can't close the vmcoreinfo file(%s). %s\n",
		    info->name_vmcoreinfo, strerror(errno));
		goto out;
	}
	free(buf);
	return TRUE;
out:
	free(buf);
	return FALSE;
}

int
read_vmcoreinfo_from_vmcore(off_t offset, unsigned long size, int flag_xen_hv)
{
	int ret = FALSE;

	/*
	 * Copy vmcoreinfo to /tmp/vmcoreinfoXXXXXX.
	 */
	if (!(info->name_vmcoreinfo = strdup(FILENAME_VMCOREINFO))) {
		MSG("Can't duplicate strings(%s).\n", FILENAME_VMCOREINFO);
		return FALSE;
	}
	if (!copy_vmcoreinfo(offset, size))
		goto out;

	/*
	 * Read vmcoreinfo from /tmp/vmcoreinfoXXXXXX.
	 */
	if (!open_vmcoreinfo("r"))
		goto out;

	unlink(info->name_vmcoreinfo);

	if (flag_xen_hv) {
		if (!read_vmcoreinfo_xen())
			goto out;
	} else {
		if (!read_vmcoreinfo())
			goto out;
	}
	close_vmcoreinfo();

	ret = TRUE;
out:
	free(info->name_vmcoreinfo);
	info->name_vmcoreinfo = NULL;

	return ret;
}

/*
 * Get the number of online nodes.
 */
int
get_nodes_online(void)
{
	int len, i, j, online;
	unsigned long node_online_map = 0, bitbuf, *maskptr;

	if ((SYMBOL(node_online_map) == NOT_FOUND_SYMBOL)
	    && (SYMBOL(node_states) == NOT_FOUND_SYMBOL))
		return 0;

	if (SIZE(nodemask_t) == NOT_FOUND_STRUCTURE) {
		ERRMSG("Can't get the size of nodemask_t.\n");
		return 0;
	}

	len = SIZE(nodemask_t);
	vt.node_online_map_len = len/sizeof(unsigned long);
	if (!(vt.node_online_map = (unsigned long *)malloc(len))) {
		ERRMSG("Can't allocate memory for the node online map. %s\n",
		    strerror(errno));
		return 0;
	}
	if (SYMBOL(node_online_map) != NOT_FOUND_SYMBOL) {
		node_online_map = SYMBOL(node_online_map);
	} else if (SYMBOL(node_states) != NOT_FOUND_SYMBOL) {
		/*
		 * For linux-2.6.23-rc4-mm1
		 */
		node_online_map = SYMBOL(node_states)
		     + (SIZE(nodemask_t) * NUMBER(N_ONLINE));
	}
	if (!readmem(VADDR, node_online_map, vt.node_online_map, len)){
		ERRMSG("Can't get the node online map.\n");
		return 0;
	}
	online = 0;
	maskptr = (unsigned long *)vt.node_online_map;
	for (i = 0; i < vt.node_online_map_len; i++, maskptr++) {
		bitbuf = *maskptr;
		for (j = 0; j < sizeof(bitbuf) * 8; j++) {
			online += bitbuf & 1;
			bitbuf = bitbuf >> 1;
		}
	}
	return online;
}

int
get_numnodes(void)
{
	if (!(vt.numnodes = get_nodes_online())) {
		vt.numnodes = 1;
	}
	DEBUG_MSG("\n");
	DEBUG_MSG("num of NODEs : %d\n", vt.numnodes);
	DEBUG_MSG("\n");

	return TRUE;
}

int
next_online_node(int first)
{
	int i, j, node;
	unsigned long mask, *maskptr;

	/* It cannot occur */
	if ((first/(sizeof(unsigned long) * 8)) >= vt.node_online_map_len) {
		ERRMSG("next_online_node: %d is too large!\n", first);
		return -1;
	}

	maskptr = (unsigned long *)vt.node_online_map;
	for (i = node = 0; i <  vt.node_online_map_len; i++, maskptr++) {
		mask = *maskptr;
		for (j = 0; j < (sizeof(unsigned long) * 8); j++, node++) {
			if (mask & 1) {
				if (node >= first)
					return node;
			}
			mask >>= 1;
		}
	}
	return -1;
}

unsigned long
next_online_pgdat(int node)
{
	int i;
	unsigned long pgdat;

	/*
	 * Get the pglist_data structure from symbol "node_data".
	 *     The array number of symbol "node_data" cannot be gotten
	 *     from vmlinux. Instead, check it is DW_TAG_array_type.
	 */
	if ((SYMBOL(node_data) == NOT_FOUND_SYMBOL)
	    || (ARRAY_LENGTH(node_data) == NOT_FOUND_STRUCTURE))
		goto pgdat2;

	if (!readmem(VADDR, SYMBOL(node_data) + (node * sizeof(void *)),
	    &pgdat, sizeof pgdat))
		goto pgdat2;

	if (!is_kvaddr(pgdat))
		goto pgdat2;

	return pgdat;

pgdat2:
	/*
	 * Get the pglist_data structure from symbol "pgdat_list".
	 */
	if (SYMBOL(pgdat_list) == NOT_FOUND_SYMBOL)
		goto pgdat3;

	else if ((0 < node)
	    && (ARRAY_LENGTH(pgdat_list) == NOT_FOUND_STRUCTURE))
		goto pgdat3;

	else if ((ARRAY_LENGTH(pgdat_list) != NOT_FOUND_STRUCTURE)
	    && (ARRAY_LENGTH(pgdat_list) < node))
		goto pgdat3;

	if (!readmem(VADDR, SYMBOL(pgdat_list) + (node * sizeof(void *)),
	    &pgdat, sizeof pgdat))
		goto pgdat3;

	if (!is_kvaddr(pgdat))
		goto pgdat3;

	return pgdat;

pgdat3:
	/*
	 * linux-2.6.16 or former
	 */
	if ((SYMBOL(pgdat_list) == NOT_FOUND_SYMBOL)
	    || (OFFSET(pglist_data.pgdat_next) == NOT_FOUND_STRUCTURE))
		goto pgdat4;

	if (!readmem(VADDR, SYMBOL(pgdat_list), &pgdat, sizeof pgdat))
		goto pgdat4;

	if (!is_kvaddr(pgdat))
		goto pgdat4;

	if (node == 0)
		return pgdat;

	for (i = 1; i <= node; i++) {
		if (!readmem(VADDR, pgdat+OFFSET(pglist_data.pgdat_next),
		    &pgdat, sizeof pgdat))
			goto pgdat4;

		if (!is_kvaddr(pgdat))
			goto pgdat4;
	}
	return pgdat;

pgdat4:
	/*
	 * Get the pglist_data structure from symbol "contig_page_data".
	 */
	if (SYMBOL(contig_page_data) == NOT_FOUND_SYMBOL)
		return FALSE;

	if (node != 0)
		return FALSE;

	return SYMBOL(contig_page_data);
}

void
dump_mem_map(mdf_pfn_t pfn_start, mdf_pfn_t pfn_end,
    unsigned long mem_map, int num_mm)
{
	struct mem_map_data *mmd;

	mmd = &info->mem_map_data[num_mm];
	mmd->pfn_start = pfn_start;
	mmd->pfn_end   = pfn_end;
	mmd->mem_map   = mem_map;

	DEBUG_MSG("mem_map (%d)\n", num_mm);
	DEBUG_MSG("  mem_map    : %lx\n", mem_map);
	DEBUG_MSG("  pfn_start  : %llx\n", pfn_start);
	DEBUG_MSG("  pfn_end    : %llx\n", pfn_end);

	return;
}

int
get_mm_flatmem(void)
{
	unsigned long mem_map;

	/*
	 * Get the address of the symbol "mem_map".
	 */
	if (!readmem(VADDR, SYMBOL(mem_map), &mem_map, sizeof mem_map)
	    || !mem_map) {
		ERRMSG("Can't get the address of mem_map.\n");
		return FALSE;
	}
	info->num_mem_map = 1;
	if ((info->mem_map_data = (struct mem_map_data *)
	    malloc(sizeof(struct mem_map_data)*info->num_mem_map)) == NULL) {
		ERRMSG("Can't allocate memory for the mem_map_data. %s\n",
		    strerror(errno));
		return FALSE;
	}
	if (is_xen_memory())
		dump_mem_map(0, info->dom0_mapnr, mem_map, 0);
	else
		dump_mem_map(0, info->max_mapnr, mem_map, 0);

	return TRUE;
}

int
get_node_memblk(int num_memblk,
    unsigned long *start_paddr, unsigned long *size, int *nid)
{
	unsigned long node_memblk;

	if (ARRAY_LENGTH(node_memblk) <= num_memblk) {
		ERRMSG("Invalid num_memblk.\n");
		return FALSE;
	}
	node_memblk = SYMBOL(node_memblk) + SIZE(node_memblk_s) * num_memblk;
	if (!readmem(VADDR, node_memblk+OFFSET(node_memblk_s.start_paddr),
	    start_paddr, sizeof(unsigned long))) {
		ERRMSG("Can't get node_memblk_s.start_paddr.\n");
		return FALSE;
	}
	if (!readmem(VADDR, node_memblk + OFFSET(node_memblk_s.size),
	    size, sizeof(unsigned long))) {
		ERRMSG("Can't get node_memblk_s.size.\n");
		return FALSE;
	}
	if (!readmem(VADDR, node_memblk + OFFSET(node_memblk_s.nid),
	    nid, sizeof(int))) {
		ERRMSG("Can't get node_memblk_s.nid.\n");
		return FALSE;
	}
	return TRUE;
}

int
get_num_mm_discontigmem(void)
{
	int i, nid;
	unsigned long start_paddr, size;

	if ((SYMBOL(node_memblk) == NOT_FOUND_SYMBOL)
	    || (ARRAY_LENGTH(node_memblk) == NOT_FOUND_STRUCTURE)
	    || (SIZE(node_memblk_s) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(node_memblk_s.start_paddr) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(node_memblk_s.size) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(node_memblk_s.nid) == NOT_FOUND_STRUCTURE)) {
		return vt.numnodes;
	} else {
		for (i = 0; i < ARRAY_LENGTH(node_memblk); i++) {
			if (!get_node_memblk(i, &start_paddr, &size, &nid)) {
				ERRMSG("Can't get the node_memblk (%d)\n", i);
				return 0;
			}
			if (!start_paddr && !size &&!nid)
				break;

			DEBUG_MSG("nid : %d\n", nid);
			DEBUG_MSG("  start_paddr: %lx\n", start_paddr);
			DEBUG_MSG("  size       : %lx\n", size);
		}
		if (i == 0) {
			/*
			 * On non-NUMA systems, node_memblk_s is not set.
			 */
			return vt.numnodes;
		} else {
			return i;
		}
	}
}

int
separate_mem_map(struct mem_map_data *mmd, int *id_mm, int nid_pgdat,
    unsigned long mem_map_pgdat, unsigned long pfn_start_pgdat)
{
	int i, nid;
	unsigned long start_paddr, size, pfn_start, pfn_end, mem_map;

	for (i = 0; i < ARRAY_LENGTH(node_memblk); i++) {
		if (!get_node_memblk(i, &start_paddr, &size, &nid)) {
			ERRMSG("Can't get the node_memblk (%d)\n", i);
			return FALSE;
		}
		if (!start_paddr && !size && !nid)
			break;

		/*
		 * Check pglist_data.node_id and node_memblk_s.nid match.
		 */
		if (nid_pgdat != nid)
			continue;

		pfn_start = paddr_to_pfn(start_paddr);
		pfn_end   = paddr_to_pfn(start_paddr + size);

		if (pfn_start < pfn_start_pgdat) {
			ERRMSG("node_memblk_s.start_paddr of node (%d) is invalid.\n", nid);
			return FALSE;
		}
		if (info->max_mapnr < pfn_end) {
			DEBUG_MSG("pfn_end of node (%d) is over max_mapnr.\n",
			    nid);
			DEBUG_MSG("  pfn_start: %lx\n", pfn_start);
			DEBUG_MSG("  pfn_end  : %lx\n", pfn_end);
			DEBUG_MSG("  max_mapnr: %llx\n", info->max_mapnr);

			pfn_end = info->max_mapnr;
		}

		mem_map = mem_map_pgdat+SIZE(page)*(pfn_start-pfn_start_pgdat);

		mmd->pfn_start = pfn_start;
		mmd->pfn_end   = pfn_end;
		mmd->mem_map   = mem_map;

		mmd++;
		(*id_mm)++;
	}
	return TRUE;
}

int
get_mm_discontigmem(void)
{
	int i, j, id_mm, node, num_mem_map, separate_mm = FALSE;
	unsigned long pgdat, mem_map, pfn_start, pfn_end, node_spanned_pages;
	unsigned long vmem_map;
	struct mem_map_data temp_mmd;

	num_mem_map = get_num_mm_discontigmem();
	if (num_mem_map < vt.numnodes) {
		ERRMSG("Can't get the number of mem_map.\n");
		return FALSE;
	}
	struct mem_map_data mmd[num_mem_map];
	if (vt.numnodes < num_mem_map) {
		separate_mm = TRUE;
	}

	/*
	 * Note:
	 *  This note is only for ia64 discontigmem kernel.
	 *  It is better to take mem_map information from a symbol vmem_map
	 *  instead of pglist_data.node_mem_map, because some node_mem_map
	 *  sometimes does not have mem_map information corresponding to its
	 *  node_start_pfn.
	 */
	if (SYMBOL(vmem_map) != NOT_FOUND_SYMBOL) {
		if (!readmem(VADDR, SYMBOL(vmem_map), &vmem_map, sizeof vmem_map)) {
			ERRMSG("Can't get vmem_map.\n");
			return FALSE;
		}
	}

	/*
	 * Get the first node_id.
	 */
	if ((node = next_online_node(0)) < 0) {
		ERRMSG("Can't get next online node.\n");
		return FALSE;
	}
	if (!(pgdat = next_online_pgdat(node))) {
		ERRMSG("Can't get pgdat list.\n");
		return FALSE;
	}
	id_mm = 0;
	for (i = 0; i < vt.numnodes; i++) {
		if (!readmem(VADDR, pgdat + OFFSET(pglist_data.node_start_pfn),
		    &pfn_start, sizeof pfn_start)) {
			ERRMSG("Can't get node_start_pfn.\n");
			return FALSE;
		}
		if (!readmem(VADDR,pgdat+OFFSET(pglist_data.node_spanned_pages),
		    &node_spanned_pages, sizeof node_spanned_pages)) {
			ERRMSG("Can't get node_spanned_pages.\n");
			return FALSE;
		}
		pfn_end = pfn_start + node_spanned_pages;

		if (SYMBOL(vmem_map) == NOT_FOUND_SYMBOL) {
			if (!readmem(VADDR, pgdat + OFFSET(pglist_data.node_mem_map),
			    &mem_map, sizeof mem_map)) {
				ERRMSG("Can't get mem_map.\n");
				return FALSE;
			}
		} else
			mem_map = vmem_map + (SIZE(page) * pfn_start);

		if (separate_mm) {
			/*
			 * For some ia64 NUMA systems.
			 * On some systems, a node has the separated memory.
			 * And pglist_data(s) have the duplicated memory range
			 * like following:
			 *
			 * Nid:      Physical address
			 *  0 : 0x1000000000 - 0x2000000000
			 *  1 : 0x2000000000 - 0x3000000000
			 *  2 : 0x0000000000 - 0x6020000000 <- Overlapping
			 *  3 : 0x3000000000 - 0x4000000000
			 *  4 : 0x4000000000 - 0x5000000000
			 *  5 : 0x5000000000 - 0x6000000000
			 *
			 * Then, mem_map(s) should be separated by
			 * node_memblk_s info.
			 */
			if (!separate_mem_map(&mmd[id_mm], &id_mm, node,
			    mem_map, pfn_start)) {
				ERRMSG("Can't separate mem_map.\n");
				return FALSE;
			}
		} else {
			if (info->max_mapnr < pfn_end) {
				DEBUG_MSG("pfn_end of node (%d) is over max_mapnr.\n",
				    node);
				DEBUG_MSG("  pfn_start: %lx\n", pfn_start);
				DEBUG_MSG("  pfn_end  : %lx\n", pfn_end);
				DEBUG_MSG("  max_mapnr: %llx\n", info->max_mapnr);

				pfn_end = info->max_mapnr;
			}

			/*
			 * The number of mem_map is the same as the number
			 * of nodes.
			 */
			mmd[id_mm].pfn_start = pfn_start;
			mmd[id_mm].pfn_end   = pfn_end;
			mmd[id_mm].mem_map   = mem_map;
			id_mm++;
		}

		/*
		 * Get pglist_data of the next node.
		 */
		if (i < (vt.numnodes - 1)) {
			if ((node = next_online_node(node + 1)) < 0) {
				ERRMSG("Can't get next online node.\n");
				return FALSE;
			} else if (!(pgdat = next_online_pgdat(node))) {
				ERRMSG("Can't determine pgdat list (node %d).\n",
				    node);
				return FALSE;
			}
		}
	}

	/*
	 * Sort mem_map by pfn_start.
	 */
	for (i = 0; i < (num_mem_map - 1); i++) {
		for (j = i + 1; j < num_mem_map; j++) {
			if (mmd[j].pfn_start < mmd[i].pfn_start) {
				temp_mmd = mmd[j];
				mmd[j] = mmd[i];
				mmd[i] = temp_mmd;
			}
		}
	}

	/*
	 * Calculate the number of mem_map.
	 */
	info->num_mem_map = num_mem_map;
	if (mmd[0].pfn_start != 0)
		info->num_mem_map++;

	for (i = 0; i < num_mem_map - 1; i++) {
		if (mmd[i].pfn_end > mmd[i + 1].pfn_start) {
			ERRMSG("The mem_map is overlapped with the next one.\n");
			ERRMSG("mmd[%d].pfn_end   = %llx\n", i, mmd[i].pfn_end);
			ERRMSG("mmd[%d].pfn_start = %llx\n", i + 1, mmd[i + 1].pfn_start);
			return FALSE;
		} else if (mmd[i].pfn_end == mmd[i + 1].pfn_start)
			/*
			 * Continuous mem_map
			 */
			continue;

		/*
		 * Discontinuous mem_map
		 */
		info->num_mem_map++;
	}
	if (mmd[num_mem_map - 1].pfn_end < info->max_mapnr)
		info->num_mem_map++;

	if ((info->mem_map_data = (struct mem_map_data *)
	    malloc(sizeof(struct mem_map_data)*info->num_mem_map)) == NULL) {
		ERRMSG("Can't allocate memory for the mem_map_data. %s\n",
		    strerror(errno));
		return FALSE;
	}

	/*
	 * Create mem_map data.
	 */
	id_mm = 0;
	if (mmd[0].pfn_start != 0) {
		dump_mem_map(0, mmd[0].pfn_start, NOT_MEMMAP_ADDR, id_mm);
		id_mm++;
	}
	for (i = 0; i < num_mem_map; i++) {
		dump_mem_map(mmd[i].pfn_start, mmd[i].pfn_end,
		    mmd[i].mem_map, id_mm);
		id_mm++;
		if ((i < num_mem_map - 1)
		    && (mmd[i].pfn_end != mmd[i + 1].pfn_start)) {
			dump_mem_map(mmd[i].pfn_end, mmd[i +1].pfn_start,
			    NOT_MEMMAP_ADDR, id_mm);
			id_mm++;
		}
	}
	i = num_mem_map - 1;
	if (is_xen_memory()) {
		if (mmd[i].pfn_end < info->dom0_mapnr)
			dump_mem_map(mmd[i].pfn_end, info->dom0_mapnr,
			    NOT_MEMMAP_ADDR, id_mm);
	} else {
		if (mmd[i].pfn_end < info->max_mapnr)
			dump_mem_map(mmd[i].pfn_end, info->max_mapnr,
			    NOT_MEMMAP_ADDR, id_mm);
	}
	return TRUE;
}

unsigned long
nr_to_section(unsigned long nr, unsigned long *mem_sec)
{
	unsigned long addr;

	if (is_sparsemem_extreme()) {
		if (mem_sec[SECTION_NR_TO_ROOT(nr)] == 0)
			return NOT_KV_ADDR;
		addr = mem_sec[SECTION_NR_TO_ROOT(nr)] +
		    (nr & SECTION_ROOT_MASK()) * SIZE(mem_section);
	} else {
		addr = SYMBOL(mem_section) + (nr * SIZE(mem_section));
	}

	if (!is_kvaddr(addr))
		return NOT_KV_ADDR;

	return addr;
}

unsigned long
section_mem_map_addr(unsigned long addr)
{
	char *mem_section;
	unsigned long map;

	if (!is_kvaddr(addr))
		return NOT_KV_ADDR;

	if ((mem_section = malloc(SIZE(mem_section))) == NULL) {
		ERRMSG("Can't allocate memory for a struct mem_section. %s\n",
		    strerror(errno));
		return NOT_KV_ADDR;
	}
	if (!readmem(VADDR, addr, mem_section, SIZE(mem_section))) {
		ERRMSG("Can't get a struct mem_section(%lx).\n", addr);
		free(mem_section);
		return NOT_KV_ADDR;
	}
	map = ULONG(mem_section + OFFSET(mem_section.section_mem_map));
	map &= SECTION_MAP_MASK;
	free(mem_section);

	return map;
}

unsigned long
sparse_decode_mem_map(unsigned long coded_mem_map, unsigned long section_nr)
{
	unsigned long mem_map;

	mem_map =  coded_mem_map +
	    (SECTION_NR_TO_PFN(section_nr) * SIZE(page));

	if (!is_kvaddr(mem_map))
		return NOT_KV_ADDR;
	return mem_map;
}
int
get_mm_sparsemem(void)
{
	unsigned int section_nr, mem_section_size, num_section;
	mdf_pfn_t pfn_start, pfn_end;
	unsigned long section, mem_map;
	unsigned long *mem_sec = NULL;

	int ret = FALSE;

	/*
	 * Get the address of the symbol "mem_section".
	 */
	num_section = divideup(info->max_mapnr, PAGES_PER_SECTION());
	if (is_sparsemem_extreme()) {
		info->sections_per_root = _SECTIONS_PER_ROOT_EXTREME();
		mem_section_size = sizeof(void *) * NR_SECTION_ROOTS();
	} else {
		info->sections_per_root = _SECTIONS_PER_ROOT();
		mem_section_size = SIZE(mem_section) * NR_SECTION_ROOTS();
	}
	if ((mem_sec = malloc(mem_section_size)) == NULL) {
		ERRMSG("Can't allocate memory for the mem_section. %s\n",
		    strerror(errno));
		return FALSE;
	}
	if (!readmem(VADDR, SYMBOL(mem_section), mem_sec,
	    mem_section_size)) {
		ERRMSG("Can't get the address of mem_section.\n");
		goto out;
	}
	info->num_mem_map = num_section;
	if ((info->mem_map_data = (struct mem_map_data *)
	    malloc(sizeof(struct mem_map_data)*info->num_mem_map)) == NULL) {
		ERRMSG("Can't allocate memory for the mem_map_data. %s\n",
		    strerror(errno));
		goto out;
	}
	for (section_nr = 0; section_nr < num_section; section_nr++) {
		section = nr_to_section(section_nr, mem_sec);
		if (section == NOT_KV_ADDR) {
			mem_map = NOT_MEMMAP_ADDR;
		} else {
			mem_map = section_mem_map_addr(section);
			if (mem_map == 0) {
				mem_map = NOT_MEMMAP_ADDR;
			} else {
				mem_map = sparse_decode_mem_map(mem_map,
								section_nr);
				if (!is_kvaddr(mem_map))
					mem_map = NOT_MEMMAP_ADDR;
			}
		}
		pfn_start = section_nr * PAGES_PER_SECTION();
		pfn_end   = pfn_start + PAGES_PER_SECTION();
		if (info->max_mapnr < pfn_end)
			pfn_end = info->max_mapnr;
		dump_mem_map(pfn_start, pfn_end, mem_map, section_nr);
	}
	ret = TRUE;
out:
	if (mem_sec != NULL)
		free(mem_sec);

	return ret;
}

int
get_mem_map_without_mm(void)
{
	info->num_mem_map = 1;
	if ((info->mem_map_data = (struct mem_map_data *)
	    malloc(sizeof(struct mem_map_data)*info->num_mem_map)) == NULL) {
		ERRMSG("Can't allocate memory for the mem_map_data. %s\n",
		    strerror(errno));
		return FALSE;
	}
	if (is_xen_memory())
		dump_mem_map(0, info->dom0_mapnr, NOT_MEMMAP_ADDR, 0);
	else
		dump_mem_map(0, info->max_mapnr, NOT_MEMMAP_ADDR, 0);

	return TRUE;
}

int
get_mem_map(void)
{
	mdf_pfn_t max_pfn = 0;
	unsigned int i;
	int ret;

	switch (get_mem_type()) {
	case SPARSEMEM:
		DEBUG_MSG("\n");
		DEBUG_MSG("Memory type  : SPARSEMEM\n");
		DEBUG_MSG("\n");
		ret = get_mm_sparsemem();
		break;
	case SPARSEMEM_EX:
		DEBUG_MSG("\n");
		DEBUG_MSG("Memory type  : SPARSEMEM_EX\n");
		DEBUG_MSG("\n");
		ret = get_mm_sparsemem();
		break;
	case DISCONTIGMEM:
		DEBUG_MSG("\n");
		DEBUG_MSG("Memory type  : DISCONTIGMEM\n");
		DEBUG_MSG("\n");
		ret = get_mm_discontigmem();
		break;
	case FLATMEM:
		DEBUG_MSG("\n");
		DEBUG_MSG("Memory type  : FLATMEM\n");
		DEBUG_MSG("\n");
		ret = get_mm_flatmem();
		break;
	default:
		ERRMSG("Can't distinguish the memory type.\n");
		ret = FALSE;
		break;
	}
	/*
	 * Adjust "max_mapnr" for the case that Linux uses less memory
	 * than is dumped. For example when "mem=" has been used for the
	 * dumped system.
	 */
	if (!is_xen_memory()) {
		unsigned int valid_memmap = 0;
		for (i = 0; i < info->num_mem_map; i++) {
			if (info->mem_map_data[i].mem_map == NOT_MEMMAP_ADDR)
				continue;
			max_pfn = MAX(max_pfn, info->mem_map_data[i].pfn_end);
			valid_memmap++;
		}
		if (valid_memmap) {
			info->max_mapnr = MIN(info->max_mapnr, max_pfn);
		}
	}
	return ret;
}

int
initialize_bitmap_memory(void)
{
	struct disk_dump_header	*dh;
	struct kdump_sub_header *kh;
	struct dump_bitmap *bmp;
	off_t bitmap_offset;
	off_t bitmap_len, max_sect_len;
	mdf_pfn_t pfn;
	int i, j;
	long block_size;

	dh = info->dh_memory;
	kh = info->kh_memory;
	block_size = dh->block_size;

	bitmap_offset
	    = (DISKDUMP_HEADER_BLOCKS + dh->sub_hdr_size) * block_size;
	bitmap_len = block_size * dh->bitmap_blocks;

	bmp = malloc(sizeof(struct dump_bitmap));
	if (bmp == NULL) {
		ERRMSG("Can't allocate memory for the memory-bitmap. %s\n",
		    strerror(errno));
		return FALSE;
	}
	bmp->fd        = info->fd_memory;
	bmp->file_name = info->name_memory;
	bmp->no_block  = -1;
	memset(bmp->buf, 0, BUFSIZE_BITMAP);
	bmp->offset = bitmap_offset + bitmap_len / 2;
	info->bitmap_memory = bmp;

	if (dh->header_version >= 6)
		max_sect_len = divideup(kh->max_mapnr_64, BITMAP_SECT_LEN);
	else
		max_sect_len = divideup(dh->max_mapnr, BITMAP_SECT_LEN);
	info->valid_pages = calloc(sizeof(ulong), max_sect_len);
	if (info->valid_pages == NULL) {
		ERRMSG("Can't allocate memory for the valid_pages. %s\n",
		    strerror(errno));
		free(bmp);
		return FALSE;
	}
	for (i = 1, pfn = 0; i < max_sect_len; i++) {
		info->valid_pages[i] = info->valid_pages[i - 1];
		for (j = 0; j < BITMAP_SECT_LEN; j++, pfn++)
			if (is_dumpable(info->bitmap_memory, pfn))
				info->valid_pages[i]++;
	}

	return TRUE;
}

int
calibrate_machdep_info(void)
{
	if (NUMBER(MAX_PHYSMEM_BITS) > 0)
		info->max_physmem_bits = NUMBER(MAX_PHYSMEM_BITS);

	if (NUMBER(SECTION_SIZE_BITS) > 0)
		info->section_size_bits = NUMBER(SECTION_SIZE_BITS);

	return TRUE;
}

int
initial(void)
{
	off_t offset;
	unsigned long size;
	int debug_info = FALSE;

	if (is_xen_memory() && !initial_xen())
		return FALSE;

#ifdef USELZO
	if (lzo_init() == LZO_E_OK)
		info->flag_lzo_support = TRUE;
#else
	if (info->flag_compress == DUMP_DH_COMPRESSED_LZO) {
		MSG("'-l' option is disabled, ");
		MSG("because this binary doesn't support lzo compression.\n");
		MSG("Try `make USELZO=on` when building.\n");
	}
#endif

#ifndef USESNAPPY
	if (info->flag_compress == DUMP_DH_COMPRESSED_SNAPPY) {
		MSG("'-p' option is disabled, ");
		MSG("because this binary doesn't support snappy "
		    "compression.\n");
		MSG("Try `make USESNAPPY=on` when building.\n");
	}
#endif

	if (info->flag_exclude_xen_dom && !is_xen_memory()) {
		MSG("'-X' option is disable,");
		MSG("because %s is not Xen's memory core image.\n", info->name_memory);
		MSG("Commandline parameter is invalid.\n");
		MSG("Try `makedumpfile --help' for more information.\n");
		return FALSE;
	}

	if (info->flag_refiltering) {
		if (info->flag_elf_dumpfile) {
			MSG("'-E' option is disable, ");
			MSG("because %s is kdump compressed format.\n",
							info->name_memory);
			return FALSE;
		}

		if(info->flag_cyclic) {
			info->flag_cyclic = FALSE;
			MSG("Switched running mode from cyclic to non-cyclic,\n");
			MSG("because the cyclic mode doesn't support refiltering\n");
			MSG("kdump compressed format.\n");
		}

		info->phys_base = info->kh_memory->phys_base;
		info->max_dump_level |= info->kh_memory->dump_level;

		if (!initialize_bitmap_memory())
			return FALSE;

	} else if (info->flag_sadump) {
		if (info->flag_elf_dumpfile) {
			MSG("'-E' option is disable, ");
			MSG("because %s is sadump %s format.\n",
			    info->name_memory, sadump_format_type_name());
			return FALSE;
		}

		if(info->flag_cyclic) {
			info->flag_cyclic = FALSE;
			MSG("Switched running mode from cyclic to non-cyclic,\n");
			MSG("because the cyclic mode doesn't support sadump format.\n");
		}

		set_page_size(sadump_page_size());

		if (!sadump_initialize_bitmap_memory())
			return FALSE;

		(void) sadump_set_timestamp(&info->timestamp);

		/*
		 * NOTE: phys_base is never saved by sadump and so
		 * must be computed in some way. We here choose the
		 * way of looking at linux_banner. See
		 * sadump_virt_phys_base(). The processing is
		 * postponed until debug information becomes
		 * available.
		 */

	} else if (!get_phys_base())
		return FALSE;

	/*
	 * Get the debug information for analysis from the vmcoreinfo file
	 */
	if (info->flag_read_vmcoreinfo) {
		if (!read_vmcoreinfo())
			return FALSE;
		close_vmcoreinfo();
		debug_info = TRUE;
	/*
	 * Get the debug information for analysis from the kernel file
	 */
	} else if (info->name_vmlinux) {
		set_dwarf_debuginfo("vmlinux", NULL,
					info->name_vmlinux, info->fd_vmlinux);

		if (!get_symbol_info())
			return FALSE;

		if (!get_structure_info())
			return FALSE;

		if (!get_srcfile_info())
			return FALSE;

		debug_info = TRUE;
	} else {
		/*
		 * Check whether /proc/vmcore contains vmcoreinfo,
		 * and get both the offset and the size.
		 */
		if (!has_vmcoreinfo()) {
			if (info->max_dump_level <= DL_EXCLUDE_ZERO)
				goto out;

			MSG("%s doesn't contain vmcoreinfo.\n",
			    info->name_memory);
			MSG("Specify '-x' option or '-i' option.\n");
			MSG("Commandline parameter is invalid.\n");
			MSG("Try `makedumpfile --help' for more information.\n");
			return FALSE;
		}
	}

	/*
	 * Get the debug information from /proc/vmcore.
	 * NOTE: Don't move this code to the above, because the debugging
	 *       information token by -x/-i option is overwritten by vmcoreinfo
	 *       in /proc/vmcore. vmcoreinfo in /proc/vmcore is more reliable
	 *       than -x/-i option.
	 */
	if (has_vmcoreinfo()) {
		get_vmcoreinfo(&offset, &size);
		if (!read_vmcoreinfo_from_vmcore(offset, size, FALSE))
			return FALSE;
		debug_info = TRUE;
	}

out:
	if (!info->page_size) {
		/*
		 * If we cannot get page_size from a vmcoreinfo file,
		 * fall back to the current kernel page size.
		 */
		if (!fallback_to_current_page_size())
			return FALSE;
	}
	if (!get_max_mapnr())
		return FALSE;

	if (info->flag_cyclic) {
		if (info->bufsize_cyclic == 0) {
			if (!calculate_cyclic_buffer_size())
				return FALSE;
		} else {
			unsigned long long free_memory;

			/*
                        * The buffer size is specified as Kbyte with
                        * --cyclic-buffer <size> option.
                        */
			info->bufsize_cyclic <<= 10;

			/*
			 * Truncate the buffer size to free memory size.
			 */
			free_memory = get_free_memory_size();
			if (info->num_dumpfile > 1)
				free_memory /= info->num_dumpfile;
			if (info->bufsize_cyclic > free_memory) {
				MSG("Specified buffer size is larger than free memory.\n");
				MSG("The buffer size for the cyclic mode will ");
				MSG("be truncated to %lld byte.\n", free_memory);
				info->bufsize_cyclic = free_memory;
			}
		}

		info->pfn_cyclic = info->bufsize_cyclic * BITPERBYTE;

		DEBUG_MSG("\n");
		DEBUG_MSG("Buffer size for the cyclic mode: %ld\n", info->bufsize_cyclic);
	}

	if (!is_xen_memory() && !cache_init())
		return FALSE;

	if (debug_info && !get_machdep_info())
		return FALSE;

	if (debug_info && !calibrate_machdep_info())
		return FALSE;

	if (is_xen_memory() && !get_dom0_mapnr())
		return FALSE;

	if (debug_info) {
		if (info->flag_sadump)
			(void) sadump_virt_phys_base();

		if (info->flag_sadump) {
			int online_cpus;

			online_cpus = sadump_num_online_cpus();
			if (!online_cpus)
				return FALSE;

			set_nr_cpus(online_cpus);
		}

		if (!check_release())
			return FALSE;

		if (!get_versiondep_info())
			return FALSE;

		/*
		 * NOTE: This must be done before refering to
		 * VMALLOC'ed memory. The first 640kB contains data
		 * necessary for paging, like PTE. The absence of the
		 * region affects reading VMALLOC'ed memory such as
		 * module data.
		 */
		if (info->flag_sadump)
			sadump_kdump_backup_region_init();

		if (!get_numnodes())
			return FALSE;

		if (!get_mem_map())
			return FALSE;

		if (!info->flag_dmesg && info->flag_sadump &&
		    sadump_check_debug_info() &&
		    !sadump_generate_elf_note_from_dumpfile())
			return FALSE;

	} else {
		if (!get_mem_map_without_mm())
			return FALSE;
	}

	if (!get_value_for_old_linux())
		return FALSE;

	/* use buddy identification of free pages whether cyclic or not */
	/* (this can reduce pages scan of 1TB memory from 60sec to 30sec) */
	if (info->dump_level & DL_EXCLUDE_FREE)
		setup_page_is_buddy();

	if (info->flag_usemmap == MMAP_TRY && initialize_mmap()) {
		DEBUG_MSG("mmap() is available on the kernel.\n");
		info->flag_usemmap = MMAP_ENABLE;
	} else {
		DEBUG_MSG("The kernel doesn't support mmap(),");
		DEBUG_MSG("read() will be used instead.\n");
		info->flag_usemmap = MMAP_DISABLE;
        }

	return TRUE;
}

void
initialize_bitmap(struct dump_bitmap *bitmap)
{
	bitmap->fd        = info->fd_bitmap;
	bitmap->file_name = info->name_bitmap;
	bitmap->no_block  = -1;
	memset(bitmap->buf, 0, BUFSIZE_BITMAP);
}

void
initialize_bitmap_cyclic(char *bitmap)
{
	memset(bitmap, 0, info->bufsize_cyclic);
}

void
initialize_1st_bitmap(struct dump_bitmap *bitmap)
{
	initialize_bitmap(bitmap);
	bitmap->offset = 0;
}

void
initialize_2nd_bitmap(struct dump_bitmap *bitmap)
{
	initialize_bitmap(bitmap);
	bitmap->offset = info->len_bitmap / 2;
}

int
set_bitmap(struct dump_bitmap *bitmap, mdf_pfn_t pfn, int val)
{
	int byte, bit;
	off_t old_offset, new_offset;
	old_offset = bitmap->offset + BUFSIZE_BITMAP * bitmap->no_block;
	new_offset = bitmap->offset + BUFSIZE_BITMAP * (pfn / PFN_BUFBITMAP);

	if (0 <= bitmap->no_block && old_offset != new_offset) {
		if (lseek(bitmap->fd, old_offset, SEEK_SET) < 0 ) {
			ERRMSG("Can't seek the bitmap(%s). %s\n",
			    bitmap->file_name, strerror(errno));
			return FALSE;
		}
		if (write(bitmap->fd, bitmap->buf, BUFSIZE_BITMAP)
		    != BUFSIZE_BITMAP) {
			ERRMSG("Can't write the bitmap(%s). %s\n",
			    bitmap->file_name, strerror(errno));
			return FALSE;
		}
	}
	if (old_offset != new_offset) {
		if (lseek(bitmap->fd, new_offset, SEEK_SET) < 0 ) {
			ERRMSG("Can't seek the bitmap(%s). %s\n",
			    bitmap->file_name, strerror(errno));
			return FALSE;
		}
		if (read(bitmap->fd, bitmap->buf, BUFSIZE_BITMAP)
		    != BUFSIZE_BITMAP) {
			ERRMSG("Can't read the bitmap(%s). %s\n",
			    bitmap->file_name, strerror(errno));
			return FALSE;
		}
		bitmap->no_block = pfn / PFN_BUFBITMAP;
	}
	/*
	 * If val is 0, clear bit on the bitmap.
	 */
	byte = (pfn%PFN_BUFBITMAP)>>3;
	bit  = (pfn%PFN_BUFBITMAP) & 7;
	if (val)
		bitmap->buf[byte] |= 1<<bit;
	else
		bitmap->buf[byte] &= ~(1<<bit);

	return TRUE;
}

int
set_bitmap_cyclic(char *bitmap, mdf_pfn_t pfn, int val, struct cycle *cycle)
{
	int byte, bit;
	static int warning = 0;

        if (pfn < cycle->start_pfn || cycle->end_pfn <= pfn) {
		if (warning == 0) {
			MSG("WARNING: PFN out of cycle range. (pfn:%llx, ", pfn);
			MSG("cycle:[%llx-%llx])\n", cycle->start_pfn, cycle->end_pfn);
			warning = 1;
		}
                return FALSE;
	}

	/*
	 * If val is 0, clear bit on the bitmap.
	 */
	byte = (pfn - cycle->start_pfn)>>3;
	bit  = (pfn - cycle->start_pfn) & 7;
	if (val)
		bitmap[byte] |= 1<<bit;
	else
		bitmap[byte] &= ~(1<<bit);

	return TRUE;
}

int
sync_bitmap(struct dump_bitmap *bitmap)
{
	off_t offset;
	offset = bitmap->offset + BUFSIZE_BITMAP * bitmap->no_block;

	/*
	 * The bitmap buffer is not dirty, and it is not necessary
	 * to write out it.
	 */
	if (bitmap->no_block < 0)
		return TRUE;

	if (lseek(bitmap->fd, offset, SEEK_SET) < 0 ) {
		ERRMSG("Can't seek the bitmap(%s). %s\n",
		    bitmap->file_name, strerror(errno));
		return FALSE;
	}
	if (write(bitmap->fd, bitmap->buf, BUFSIZE_BITMAP)
	    != BUFSIZE_BITMAP) {
		ERRMSG("Can't write the bitmap(%s). %s\n",
		    bitmap->file_name, strerror(errno));
		return FALSE;
	}
	return TRUE;
}

int
sync_1st_bitmap(void)
{
	return sync_bitmap(info->bitmap1);
}

int
sync_2nd_bitmap(void)
{
	return sync_bitmap(info->bitmap2);
}

int
set_bit_on_1st_bitmap(mdf_pfn_t pfn, struct cycle *cycle)
{
	if (info->flag_cyclic) {
		return set_bitmap_cyclic(info->partial_bitmap1, pfn, 1, cycle);
	} else {
		return set_bitmap(info->bitmap1, pfn, 1);
	}
}

int
clear_bit_on_1st_bitmap(mdf_pfn_t pfn, struct cycle *cycle)
{
	if (info->flag_cyclic) {
		return set_bitmap_cyclic(info->partial_bitmap1, pfn, 0, cycle);
	} else {
		return set_bitmap(info->bitmap1, pfn, 0);
	}
}

int
clear_bit_on_2nd_bitmap(mdf_pfn_t pfn, struct cycle *cycle)
{
	if (info->flag_cyclic) {
		return set_bitmap_cyclic(info->partial_bitmap2, pfn, 0, cycle);
	} else {
		return set_bitmap(info->bitmap2, pfn, 0);
	}
}

int
clear_bit_on_2nd_bitmap_for_kernel(mdf_pfn_t pfn, struct cycle *cycle)
{
	unsigned long long maddr;

	if (is_xen_memory()) {
		maddr = ptom_xen(pfn_to_paddr(pfn));
		if (maddr == NOT_PADDR) {
			ERRMSG("Can't convert a physical address(%llx) to machine address.\n",
			    pfn_to_paddr(pfn));
			return FALSE;
		}
		pfn = paddr_to_pfn(maddr);
	}
	return clear_bit_on_2nd_bitmap(pfn, cycle);
}

int
set_bit_on_2nd_bitmap(mdf_pfn_t pfn, struct cycle *cycle)
{
	if (info->flag_cyclic) {
		return set_bitmap_cyclic(info->partial_bitmap2, pfn, 1, cycle);
	} else {
		return set_bitmap(info->bitmap2, pfn, 1);
	}
}

int
set_bit_on_2nd_bitmap_for_kernel(mdf_pfn_t pfn, struct cycle *cycle)
{
	unsigned long long maddr;

	if (is_xen_memory()) {
		maddr = ptom_xen(pfn_to_paddr(pfn));
		if (maddr == NOT_PADDR) {
			ERRMSG("Can't convert a physical address(%llx) to machine address.\n",
			    pfn_to_paddr(pfn));
			return FALSE;
		}
		pfn = paddr_to_pfn(maddr);
	}
	return set_bit_on_2nd_bitmap(pfn, cycle);
}

static inline int
is_in_segs(unsigned long long paddr)
{
	if (info->flag_refiltering || info->flag_sadump) {
		static struct dump_bitmap bitmap1 = {0};

		if (bitmap1.fd == 0)
			initialize_1st_bitmap(&bitmap1);

		return is_dumpable(&bitmap1, paddr_to_pfn(paddr));
	}

	if (paddr_to_offset(paddr))
		return TRUE;
	else
		return FALSE;
}

int
read_cache(struct cache_data *cd)
{
	const off_t failed = (off_t)-1;

	if (lseek(cd->fd, cd->offset, SEEK_SET) == failed) {
		ERRMSG("Can't seek the dump file(%s). %s\n",
		    cd->file_name, strerror(errno));
		return FALSE;
	}
	if (read(cd->fd, cd->buf, cd->cache_size) != cd->cache_size) {
		ERRMSG("Can't read the dump file(%s). %s\n",
		    cd->file_name, strerror(errno));
		return FALSE;
	}
	cd->offset += cd->cache_size;
	return TRUE;
}

int
is_bigendian(void)
{
	int i = 0x12345678;

	if (*(char *)&i == 0x12)
		return TRUE;
	else
		return FALSE;
}

int
write_and_check_space(int fd, void *buf, size_t buf_size, char *file_name)
{
	int status, written_size = 0;

	while (written_size < buf_size) {
		status = write(fd, buf + written_size,
				   buf_size - written_size);
		if (0 < status) {
			written_size += status;
			continue;
		}
		if (errno == ENOSPC)
			info->flag_nospace = TRUE;
		MSG("\nCan't write the dump file(%s). %s\n",
		    file_name, strerror(errno));
		return FALSE;
	}
	return TRUE;
}

int
write_buffer(int fd, off_t offset, void *buf, size_t buf_size, char *file_name)
{
	struct makedumpfile_data_header fdh;
	const off_t failed = (off_t)-1;

	if (fd == STDOUT_FILENO) {
		/*
		 * Output a header of flattened format instead of
		 * lseek(). For sending dump data to a different
		 * architecture, change the values to big endian.
		 */
		if (is_bigendian()){
			fdh.offset   = offset;
			fdh.buf_size = buf_size;
		} else {
			fdh.offset   = bswap_64(offset);
			fdh.buf_size = bswap_64(buf_size);
		}
		if (!write_and_check_space(fd, &fdh, sizeof(fdh), file_name))
			return FALSE;
	} else {
		if (lseek(fd, offset, SEEK_SET) == failed) {
			ERRMSG("Can't seek the dump file(%s). %s\n",
			    file_name, strerror(errno));
			return FALSE;
		}
	}
	if (!write_and_check_space(fd, buf, buf_size, file_name))
		return FALSE;

	return TRUE;
}

int
write_cache(struct cache_data *cd, void *buf, size_t size)
{
	memcpy(cd->buf + cd->buf_size, buf, size);
	cd->buf_size += size;

	if (cd->buf_size < cd->cache_size)
		return TRUE;

	if (!write_buffer(cd->fd, cd->offset, cd->buf, cd->cache_size,
	    cd->file_name))
		return FALSE;

	cd->buf_size -= cd->cache_size;
	memcpy(cd->buf, cd->buf + cd->cache_size, cd->buf_size);
	cd->offset += cd->cache_size;
	return TRUE;
}

int
write_cache_bufsz(struct cache_data *cd)
{
	if (!cd->buf_size)
		return TRUE;

	if (!write_buffer(cd->fd, cd->offset, cd->buf, cd->buf_size,
	    cd->file_name))
		return FALSE;

	cd->offset  += cd->buf_size;
	cd->buf_size = 0;
	return TRUE;
}

int
write_cache_zero(struct cache_data *cd, size_t size)
{
	if (!write_cache_bufsz(cd))
		return FALSE;

	memset(cd->buf + cd->buf_size, 0, size);
	cd->buf_size += size;

	return write_cache_bufsz(cd);
}

int
read_buf_from_stdin(void *buf, int buf_size)
{
	int read_size = 0, tmp_read_size = 0;
	time_t last_time, tm;

	last_time = time(NULL);

	while (read_size != buf_size) {

		tmp_read_size = read(STDIN_FILENO, buf + read_size,
		    buf_size - read_size);

		if (tmp_read_size < 0) {
			ERRMSG("Can't read STDIN. %s\n", strerror(errno));
			return FALSE;

		} else if (0 == tmp_read_size) {
			/*
			 * If it cannot get any data from a standard input
			 * for a long time, break this loop.
			 */
			tm = time(NULL);
			if (TIMEOUT_STDIN < (tm - last_time)) {
				ERRMSG("Can't get any data from STDIN.\n");
				return FALSE;
			}
		} else {
			read_size += tmp_read_size;
			last_time = time(NULL);
		}
	}
	return TRUE;
}

int
read_start_flat_header(void)
{
	char buf[MAX_SIZE_MDF_HEADER];
	struct makedumpfile_header fh;

	/*
	 * Get flat header.
	 */
	if (!read_buf_from_stdin(buf, MAX_SIZE_MDF_HEADER)) {
		ERRMSG("Can't get header of flattened format.\n");
		return FALSE;
	}
	memcpy(&fh, buf, sizeof(fh));

	if (!is_bigendian()){
		fh.type    = bswap_64(fh.type);
		fh.version = bswap_64(fh.version);
	}

	/*
	 * Check flat header.
	 */
	if (strcmp(fh.signature, MAKEDUMPFILE_SIGNATURE)) {
		ERRMSG("Can't get signature of flattened format.\n");
		return FALSE;
	}
	if (fh.type != TYPE_FLAT_HEADER) {
		ERRMSG("Can't get type of flattened format.\n");
		return FALSE;
	}

	return TRUE;
}

int
read_flat_data_header(struct makedumpfile_data_header *fdh)
{
	if (!read_buf_from_stdin(fdh,
	    sizeof(struct makedumpfile_data_header))) {
		ERRMSG("Can't get header of flattened format.\n");
		return FALSE;
	}
	if (!is_bigendian()){
		fdh->offset   = bswap_64(fdh->offset);
		fdh->buf_size = bswap_64(fdh->buf_size);
	}
	return TRUE;
}

int
rearrange_dumpdata(void)
{
	int read_size, tmp_read_size;
	char buf[SIZE_BUF_STDIN];
	struct makedumpfile_data_header fdh;

	/*
	 * Get flat header.
	 */
	if (!read_start_flat_header()) {
		ERRMSG("Can't get header of flattened format.\n");
		return FALSE;
	}

	/*
	 * Read the first data header.
	 */
	if (!read_flat_data_header(&fdh)) {
		ERRMSG("Can't get header of flattened format.\n");
		return FALSE;
	}

	do {
		read_size = 0;
		while (read_size < fdh.buf_size) {
			if (sizeof(buf) < (fdh.buf_size - read_size))
				tmp_read_size = sizeof(buf);
			else
				tmp_read_size = fdh.buf_size - read_size;

			if (!read_buf_from_stdin(buf, tmp_read_size)) {
				ERRMSG("Can't get data of flattened format.\n");
				return FALSE;
			}
			if (!write_buffer(info->fd_dumpfile,
			    fdh.offset + read_size, buf, tmp_read_size,
			    info->name_dumpfile))
				return FALSE;

			read_size += tmp_read_size;
		}
		/*
		 * Read the next header.
		 */
		if (!read_flat_data_header(&fdh)) {
			ERRMSG("Can't get data header of flattened format.\n");
			return FALSE;
		}

	} while ((0 <= fdh.offset) && (0 < fdh.buf_size));

	if ((fdh.offset != END_FLAG_FLAT_HEADER)
	    || (fdh.buf_size != END_FLAG_FLAT_HEADER)) {
		ERRMSG("Can't get valid end header of flattened format.\n");
		return FALSE;
	}

	return TRUE;
}

mdf_pfn_t
page_to_pfn(unsigned long page)
{
	unsigned int num;
	mdf_pfn_t pfn = ULONGLONG_MAX;
	unsigned long long index = 0;
	struct mem_map_data *mmd;

	mmd = info->mem_map_data;
	for (num = 0; num < info->num_mem_map; num++, mmd++) {
		if (mmd->mem_map == NOT_MEMMAP_ADDR)
			continue;
		if (page < mmd->mem_map)
			continue;
		index = (page - mmd->mem_map) / SIZE(page);
		if (index >= mmd->pfn_end - mmd->pfn_start)
			continue;
		pfn = mmd->pfn_start + index;
		break;
	}
	if (pfn == ULONGLONG_MAX) {
		ERRMSG("Can't convert the address of page descriptor (%lx) to pfn.\n", page);
		return ULONGLONG_MAX;
	}
	return pfn;
}

int
reset_bitmap_of_free_pages(unsigned long node_zones, struct cycle *cycle)
{

	int order, i, migrate_type, migrate_types;
	unsigned long curr, previous, head, curr_page, curr_prev;
	unsigned long addr_free_pages, free_pages = 0, found_free_pages = 0;
	mdf_pfn_t pfn, start_pfn;

	/*
	 * On linux-2.6.24 or later, free_list is divided into the array.
	 */
	migrate_types = ARRAY_LENGTH(free_area.free_list);
	if (migrate_types == NOT_FOUND_STRUCTURE)
		migrate_types = 1;

	for (order = (ARRAY_LENGTH(zone.free_area) - 1); order >= 0; --order) {
		for (migrate_type = 0; migrate_type < migrate_types;
		     migrate_type++) {
			head = node_zones + OFFSET(zone.free_area)
				+ SIZE(free_area) * order
				+ OFFSET(free_area.free_list)
				+ SIZE(list_head) * migrate_type;
			previous = head;
			if (!readmem(VADDR, head + OFFSET(list_head.next),
				     &curr, sizeof curr)) {
				ERRMSG("Can't get next list_head.\n");
				return FALSE;
			}
			for (;curr != head;) {
				curr_page = curr - OFFSET(page.lru);
				start_pfn = page_to_pfn(curr_page);
				if (start_pfn == ULONGLONG_MAX)
					return FALSE;

				if (!readmem(VADDR, curr+OFFSET(list_head.prev),
					     &curr_prev, sizeof curr_prev)) {
					ERRMSG("Can't get prev list_head.\n");
					return FALSE;
				}
				if (previous != curr_prev) {
					ERRMSG("The free list is broken.\n");
					retcd = ANALYSIS_FAILED;
					return FALSE;
				}
				for (i = 0; i < (1<<order); i++) {
					pfn = start_pfn + i;
					if (clear_bit_on_2nd_bitmap_for_kernel(pfn, cycle))
						found_free_pages++;
				}

				previous = curr;
				if (!readmem(VADDR, curr+OFFSET(list_head.next),
					     &curr, sizeof curr)) {
					ERRMSG("Can't get next list_head.\n");
					return FALSE;
				}
			}
		}
	}

	/*
	 * Check the number of free pages.
	 */
	if (OFFSET(zone.free_pages) != NOT_FOUND_STRUCTURE) {
		addr_free_pages = node_zones + OFFSET(zone.free_pages);

	} else if (OFFSET(zone.vm_stat) != NOT_FOUND_STRUCTURE) {
		/*
		 * On linux-2.6.21 or later, the number of free_pages is
		 * in vm_stat[NR_FREE_PAGES].
		 */
		addr_free_pages = node_zones + OFFSET(zone.vm_stat)
		    + sizeof(long) * NUMBER(NR_FREE_PAGES);

	} else {
		ERRMSG("Can't get addr_free_pages.\n");
		return FALSE;
	}
	if (!readmem(VADDR, addr_free_pages, &free_pages, sizeof free_pages)) {
		ERRMSG("Can't get free_pages.\n");
		return FALSE;
	}
	if (free_pages != found_free_pages && !info->flag_cyclic) {
		/*
		 * On linux-2.6.21 or later, the number of free_pages is
		 * sometimes different from the one of the list "free_area",
		 * because the former is flushed asynchronously.
		 */
		DEBUG_MSG("The number of free_pages is invalid.\n");
		DEBUG_MSG("  free_pages       = %ld\n", free_pages);
		DEBUG_MSG("  found_free_pages = %ld\n", found_free_pages);
	}
	pfn_free += found_free_pages;

	return TRUE;
}

static int
dump_log_entry(char *logptr, int fp)
{
	char *msg, *p, *bufp;
	unsigned int i, text_len, indent_len, buf_need;
	unsigned long long ts_nsec;
	char buf[BUFSIZE];
	ulonglong nanos;
	ulong rem;

	text_len = USHORT(logptr + OFFSET(printk_log.text_len));
	ts_nsec = ULONGLONG(logptr + OFFSET(printk_log.ts_nsec));

	nanos = (ulonglong)ts_nsec / (ulonglong)1000000000;
	rem = (ulonglong)ts_nsec % (ulonglong)1000000000;

	msg = logptr + SIZE(printk_log);

	bufp = buf;
	bufp += sprintf(buf, "[%5lld.%06ld] ", nanos, rem/1000);
	indent_len = strlen(buf);

	/* How much buffer space is needed in the worst case */
	buf_need = MAX(sizeof("\\xXX\n"), sizeof("\n") + indent_len);

	for (i = 0, p = msg; i < text_len; i++, p++) {
		if (bufp - buf >= sizeof(buf) - buf_need) {
			if (write(info->fd_dumpfile, buf, bufp - buf) < 0)
				return FALSE;
			bufp = buf;
		}

		if (*p == '\n')
			bufp += sprintf(bufp, "\n%-*s", indent_len, "");
		else if (isprint(*p) || isspace(*p))
			*bufp++ = *p;
		else
			bufp += sprintf(bufp, "\\x%02x", *p);
	}

	*bufp++ = '\n';

	if (write(info->fd_dumpfile, buf, bufp - buf) < 0)
		return FALSE;
	else
		return TRUE;
}

/*
 * get log record by index; idx must point to valid message.
 */
static char *
log_from_idx(unsigned int idx, char *logbuf)
{
	char *logptr;
	unsigned int msglen;

	logptr = logbuf + idx;

	/*
	 * A length == 0 record is the end of buffer marker.
	 * Wrap around and return the message at the start of
	 * the buffer.
	 */

	msglen = USHORT(logptr + OFFSET(printk_log.len));
	if (!msglen)
		logptr = logbuf;

	return logptr;
}

static long
log_next(unsigned int idx, char *logbuf)
{
	char *logptr;
	unsigned int msglen;

	logptr = logbuf + idx;

	/*
	 * A length == 0 record is the end of buffer marker. Wrap around and
	 * read the message at the start of the buffer as *this* one, and
	 * return the one after that.
	 */

	msglen = USHORT(logptr + OFFSET(printk_log.len));
	if (!msglen) {
		msglen = USHORT(logbuf + OFFSET(printk_log.len));
		return msglen;
	}

	return idx + msglen;
}

int
dump_dmesg()
{
	int log_buf_len, length_log, length_oldlog, ret = FALSE;
	unsigned long index, log_buf, log_end;
	unsigned int idx, log_first_idx, log_next_idx;
	unsigned long log_end_2_6_24;
	unsigned      log_end_2_6_25;
	char *log_buffer = NULL, *log_ptr = NULL;

	/*
	 * log_end has been changed to "unsigned" since linux-2.6.25.
	 *   2.6.24 or former: static unsigned long log_end;
	 *   2.6.25 or later : static unsigned log_end;
	 */
	if (!open_files_for_creating_dumpfile())
		return FALSE;

	if (!info->flag_refiltering && !info->flag_sadump) {
		if (!get_elf_info(info->fd_memory, info->name_memory))
			return FALSE;
	}
	if (!initial())
		return FALSE;

	if ((SYMBOL(log_buf) == NOT_FOUND_SYMBOL)
	    || (SYMBOL(log_buf_len) == NOT_FOUND_SYMBOL)) {
		ERRMSG("Can't find some symbols for log_buf.\n");
		return FALSE;
	}
	/*
	 * kernel 3.5 variable-length record buffer structure
	 */
	if (SYMBOL(log_end) == NOT_FOUND_SYMBOL) {
		if ((SYMBOL(log_first_idx) == NOT_FOUND_SYMBOL)
		    || (SYMBOL(log_next_idx) == NOT_FOUND_SYMBOL)) {
			ERRMSG("Can't find variable-length record symbols");
			return FALSE;
		} else {
			if (!readmem(VADDR, SYMBOL(log_first_idx), &log_first_idx,
			    sizeof(log_first_idx))) {
				ERRMSG("Can't get log_first_idx.\n");
				return FALSE;
			}
			if (!readmem(VADDR, SYMBOL(log_next_idx), &log_next_idx,
			    sizeof(log_next_idx))) {
				ERRMSG("Can't get log_next_idx.\n");
				return FALSE;
			}
		}
	}
	if (!readmem(VADDR, SYMBOL(log_buf), &log_buf, sizeof(log_buf))) {
		ERRMSG("Can't get log_buf.\n");
		return FALSE;
	}
	if (info->kernel_version < KERNEL_VERSION(3, 5, 0)) {
		if (info->kernel_version >= KERNEL_VERSION(2, 6, 25)) {
			if (!readmem(VADDR, SYMBOL(log_end), &log_end_2_6_25,
			    sizeof(log_end_2_6_25))) {
				ERRMSG("Can't to get log_end.\n");
				return FALSE;
			}
			log_end = log_end_2_6_25;
		} else {
			if (!readmem(VADDR, SYMBOL(log_end), &log_end_2_6_24,
			    sizeof(log_end_2_6_24))) {
				ERRMSG("Can't to get log_end.\n");
				return FALSE;
			}
			log_end = log_end_2_6_24;
		}
	} else
		log_end = 0;

	if (!readmem(VADDR, SYMBOL(log_buf_len), &log_buf_len,
	    sizeof(log_buf_len))) {
		ERRMSG("Can't get log_buf_len.\n");
		return FALSE;
	}
	DEBUG_MSG("\n");
	DEBUG_MSG("log_buf       : %lx\n", log_buf);
	DEBUG_MSG("log_end       : %lx\n", log_end);
	DEBUG_MSG("log_buf_len   : %d\n", log_buf_len);
	DEBUG_MSG("log_first_idx : %u\n", log_first_idx);
	DEBUG_MSG("log_next_idx  : %u\n", log_next_idx);

	if ((log_buffer = malloc(log_buf_len)) == NULL) {
		ERRMSG("Can't allocate memory for log_buf. %s\n",
			   strerror(errno));
		return FALSE;
	}

	if (info->kernel_version < KERNEL_VERSION(3, 5, 0)) {
		if (log_end < log_buf_len) {
			length_log = log_end;
			if (!readmem(VADDR, log_buf, log_buffer, length_log)) {
				ERRMSG("Can't read dmesg log.\n");
				goto out;
			}
		} else {
			index = log_end & (log_buf_len - 1);
			DEBUG_MSG("index        : %lx\n", index);
			length_log = log_buf_len;
			length_oldlog = log_buf_len - index;
			if (!readmem(VADDR, log_buf + index, log_buffer, length_oldlog)) {
				ERRMSG("Can't read old dmesg log.\n");
				goto out;
			}
			if (!readmem(VADDR, log_buf, log_buffer + length_oldlog, index)) {
				ERRMSG("Can't read new dmesg log.\n");
				goto out;
			}
		}
		DEBUG_MSG("length_log   : %d\n", length_log);

		if (!open_dump_file()) {
			ERRMSG("Can't open output file.\n");
			goto out;
		}
		if (write(info->fd_dumpfile, log_buffer, length_log) < 0)
			goto out;

		if (!close_files_for_creating_dumpfile())
			goto out;
	} else {
		if (SIZE(printk_log) == NOT_FOUND_STRUCTURE ||
		    OFFSET(printk_log.len) == NOT_FOUND_STRUCTURE ||
		    OFFSET(printk_log.text_len) == NOT_FOUND_STRUCTURE ||
		    OFFSET(printk_log.ts_nsec) == NOT_FOUND_STRUCTURE) {
			ERRMSG("Can't get necessary structures for extracting dmesg log.\n");
			goto out;
		}

		if (!readmem(VADDR, log_buf, log_buffer, log_buf_len)) {
			ERRMSG("Can't read indexed dmesg log.\n");
			goto out;
		}
		if (!open_dump_file()) {
			ERRMSG("Can't open output file.\n");
			goto out;
		}
		idx = log_first_idx;
		while (idx != log_next_idx) {
			log_ptr = log_from_idx(idx, log_buffer);
			if (!dump_log_entry(log_ptr, info->fd_dumpfile))
				goto out;
			idx = log_next(idx, log_buffer);
		}
		if (!close_files_for_creating_dumpfile())
			goto out;
	}

	ret = TRUE;
out:
	if (log_buffer)
		free(log_buffer);

	return ret;
}


int
_exclude_free_page(struct cycle *cycle)
{
	int i, nr_zones, num_nodes, node;
	unsigned long node_zones, zone, spanned_pages, pgdat;
	struct timeval tv_start;

	if ((node = next_online_node(0)) < 0) {
		ERRMSG("Can't get next online node.\n");
		return FALSE;
	}
	if (!(pgdat = next_online_pgdat(node))) {
		ERRMSG("Can't get pgdat list.\n");
		return FALSE;
	}
	gettimeofday(&tv_start, NULL);

	for (num_nodes = 1; num_nodes <= vt.numnodes; num_nodes++) {

		print_progress(PROGRESS_FREE_PAGES, num_nodes - 1, vt.numnodes);

		node_zones = pgdat + OFFSET(pglist_data.node_zones);

		if (!readmem(VADDR, pgdat + OFFSET(pglist_data.nr_zones),
		    &nr_zones, sizeof(nr_zones))) {
			ERRMSG("Can't get nr_zones.\n");
			return FALSE;
		}

		for (i = 0; i < nr_zones; i++) {

			print_progress(PROGRESS_FREE_PAGES, i + nr_zones * (num_nodes - 1),
					nr_zones * vt.numnodes);

			zone = node_zones + (i * SIZE(zone));
			if (!readmem(VADDR, zone + OFFSET(zone.spanned_pages),
			    &spanned_pages, sizeof spanned_pages)) {
				ERRMSG("Can't get spanned_pages.\n");
				return FALSE;
			}
			if (!spanned_pages)
				continue;
			if (!reset_bitmap_of_free_pages(zone, cycle))
				return FALSE;
		}
		if (num_nodes < vt.numnodes) {
			if ((node = next_online_node(node + 1)) < 0) {
				ERRMSG("Can't get next online node.\n");
				return FALSE;
			} else if (!(pgdat = next_online_pgdat(node))) {
				ERRMSG("Can't determine pgdat list (node %d).\n",
				    node);
				return FALSE;
			}
		}
	}

	/*
	 * print [100 %]
	 */
	print_progress(PROGRESS_FREE_PAGES, vt.numnodes, vt.numnodes);
	print_execution_time(PROGRESS_FREE_PAGES, &tv_start);

	return TRUE;
}

int
exclude_free_page(struct cycle *cycle)
{
	/*
	 * Check having necessary information.
	 */
	if ((SYMBOL(node_data) == NOT_FOUND_SYMBOL)
	    && (SYMBOL(pgdat_list) == NOT_FOUND_SYMBOL)
	    && (SYMBOL(contig_page_data) == NOT_FOUND_SYMBOL)) {
		ERRMSG("Can't get necessary symbols for excluding free pages.\n");
		return FALSE;
	}
	if ((SIZE(zone) == NOT_FOUND_STRUCTURE)
	    || ((OFFSET(zone.free_pages) == NOT_FOUND_STRUCTURE)
	        && (OFFSET(zone.vm_stat) == NOT_FOUND_STRUCTURE))
	    || (OFFSET(zone.free_area) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(zone.spanned_pages) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(pglist_data.node_zones) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(pglist_data.nr_zones) == NOT_FOUND_STRUCTURE)
	    || (SIZE(free_area) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(free_area.free_list) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(list_head.next) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(list_head.prev) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(page.lru) == NOT_FOUND_STRUCTURE)
	    || (ARRAY_LENGTH(zone.free_area) == NOT_FOUND_STRUCTURE)) {
		ERRMSG("Can't get necessary structures for excluding free pages.\n");
		return FALSE;
	}
	if (is_xen_memory() && !info->dom0_mapnr) {
		ERRMSG("Can't get max domain-0 PFN for excluding free pages.\n");
		return FALSE;
	}

	/*
	 * Detect free pages and update 2nd-bitmap.
	 */
	if (!_exclude_free_page(cycle))
		return FALSE;

	return TRUE;
}

/*
 * For the kernel versions from v2.6.17 to v2.6.37.
 */
static int
page_is_buddy_v2(unsigned long flags, unsigned int _mapcount,
			unsigned long private, unsigned int _count)
{
	if (flags & (1UL << NUMBER(PG_buddy)))
		return TRUE;

	return FALSE;
}

/*
 * For v2.6.38 and later kernel versions.
 */
static int
page_is_buddy_v3(unsigned long flags, unsigned int _mapcount,
			unsigned long private, unsigned int _count)
{
	if (flags & (1UL << NUMBER(PG_slab)))
		return FALSE;

	if (_mapcount == (int)NUMBER(PAGE_BUDDY_MAPCOUNT_VALUE))
		return TRUE;

	return FALSE;
}

static void
setup_page_is_buddy(void)
{
	if (OFFSET(page.private) == NOT_FOUND_STRUCTURE)
		goto out;

	if (NUMBER(PG_buddy) == NOT_FOUND_NUMBER) {
		if (NUMBER(PAGE_BUDDY_MAPCOUNT_VALUE) != NOT_FOUND_NUMBER) {
			if (OFFSET(page._mapcount) != NOT_FOUND_STRUCTURE)
				info->page_is_buddy = page_is_buddy_v3;
		}
	} else
		info->page_is_buddy = page_is_buddy_v2;

out:
	if (!info->page_is_buddy)
		DEBUG_MSG("Can't select page_is_buddy handler; "
			  "follow free lists instead of mem_map array.\n");
}

/*
 * If using a dumpfile in kdump-compressed format as a source file
 * instead of /proc/vmcore, 1st-bitmap of a new dumpfile must be
 * the same as the one of a source file.
 */
int
copy_1st_bitmap_from_memory(void)
{
	char buf[info->dh_memory->block_size];
	off_t offset_page;
	off_t bitmap_offset;
	struct disk_dump_header *dh = info->dh_memory;

	bitmap_offset = (DISKDUMP_HEADER_BLOCKS + dh->sub_hdr_size)
			 * dh->block_size;

	if (lseek(info->fd_memory, bitmap_offset, SEEK_SET) < 0) {
		ERRMSG("Can't seek %s. %s\n",
				info->name_memory, strerror(errno));
		return FALSE;
	}
	if (lseek(info->bitmap1->fd, info->bitmap1->offset, SEEK_SET) < 0) {
		ERRMSG("Can't seek the bitmap(%s). %s\n",
		    info->bitmap1->file_name, strerror(errno));
		return FALSE;
	}
	offset_page = 0;
	while (offset_page < (info->len_bitmap / 2)) {
		if (read(info->fd_memory, buf, sizeof(buf)) != sizeof(buf)) {
			ERRMSG("Can't read %s. %s\n",
					info->name_memory, strerror(errno));
			return FALSE;
		}
		if (write(info->bitmap1->fd, buf, sizeof(buf)) != sizeof(buf)) {
			ERRMSG("Can't write the bitmap(%s). %s\n",
			    info->bitmap1->file_name, strerror(errno));
			return FALSE;
		}
		offset_page += sizeof(buf);
	}
	return TRUE;
}

int
create_1st_bitmap(void)
{
	int i;
	unsigned int num_pt_loads = get_num_pt_loads();
 	char buf[info->page_size];
	mdf_pfn_t pfn, pfn_start, pfn_end, pfn_bitmap1;
	unsigned long long phys_start, phys_end;
	struct timeval tv_start;
	off_t offset_page;

	if (info->flag_refiltering)
		return copy_1st_bitmap_from_memory();

	if (info->flag_sadump)
		return sadump_copy_1st_bitmap_from_memory();

	/*
	 * At first, clear all the bits on the 1st-bitmap.
	 */
	memset(buf, 0, sizeof(buf));

	if (lseek(info->bitmap1->fd, info->bitmap1->offset, SEEK_SET) < 0) {
		ERRMSG("Can't seek the bitmap(%s). %s\n",
		    info->bitmap1->file_name, strerror(errno));
		return FALSE;
	}
	offset_page = 0;
	while (offset_page < (info->len_bitmap / 2)) {
		if (write(info->bitmap1->fd, buf, info->page_size)
		    != info->page_size) {
			ERRMSG("Can't write the bitmap(%s). %s\n",
			    info->bitmap1->file_name, strerror(errno));
			return FALSE;
		}
		offset_page += info->page_size;
	}

	gettimeofday(&tv_start, NULL);

	/*
	 * If page is on memory hole, set bit on the 1st-bitmap.
	 */
	pfn_bitmap1 = 0;
	for (i = 0; get_pt_load(i, &phys_start, &phys_end, NULL, NULL); i++) {

		print_progress(PROGRESS_HOLES, i, num_pt_loads);

		pfn_start = paddr_to_pfn(phys_start);
		pfn_end   = paddr_to_pfn(phys_end);
		if (pfn_start > info->max_mapnr)
			continue;
		pfn_end = MIN(pfn_end, info->max_mapnr);

		for (pfn = pfn_start; pfn < pfn_end; pfn++) {
			set_bit_on_1st_bitmap(pfn, NULL);
			pfn_bitmap1++;
		}
	}
	pfn_memhole = info->max_mapnr - pfn_bitmap1;

	/*
	 * print 100 %
	 */
	print_progress(PROGRESS_HOLES, info->max_mapnr, info->max_mapnr);
	print_execution_time(PROGRESS_HOLES, &tv_start);

	if (!sync_1st_bitmap())
		return FALSE;

	return TRUE;
}


int
create_1st_bitmap_cyclic(struct cycle *cycle)
{
	int i;
	mdf_pfn_t pfn;
	unsigned long long phys_start, phys_end;
	mdf_pfn_t pfn_start, pfn_end;
	mdf_pfn_t pfn_start_roundup, pfn_end_round;
	unsigned long pfn_start_byte, pfn_end_byte;

	/*
	 * At first, clear all the bits on the 1st-bitmap.
	 */
	initialize_bitmap_cyclic(info->partial_bitmap1);

	/*
	 * If page is on memory hole, set bit on the 1st-bitmap.
	 * (note that this is not done in cyclic mode)
	 */
	for (i = 0; get_pt_load(i, &phys_start, &phys_end, NULL, NULL); i++) {
		pfn_start = MAX(paddr_to_pfn(phys_start), cycle->start_pfn);
		pfn_end   = MIN(paddr_to_pfn(phys_end), cycle->end_pfn);

		if (pfn_start >= pfn_end)
			continue;

		pfn_start_roundup = MIN(roundup(pfn_start, BITPERBYTE),
					pfn_end);
		pfn_end_round = MAX(round(pfn_end, BITPERBYTE), pfn_start);

		for (pfn = pfn_start; pfn < pfn_start_roundup; pfn++) {
			set_bit_on_1st_bitmap(pfn, cycle);
		}

		pfn_start_byte = (pfn_start_roundup - cycle->start_pfn) >> 3;
		pfn_end_byte = (pfn_end_round - cycle->start_pfn) >> 3;

		if (pfn_start_byte < pfn_end_byte) {
			memset(info->partial_bitmap1 + pfn_start_byte,
			       0xff,
			       pfn_end_byte - pfn_start_byte);
		}

		if (pfn_end_round >= pfn_start) {
			for (pfn = pfn_end_round; pfn < pfn_end; pfn++) {
				set_bit_on_1st_bitmap(pfn, cycle);
			}
		}
	}

	return TRUE;
}

/*
 * Exclude the page filled with zero in case of creating an elf dumpfile.
 */
int
exclude_zero_pages(void)
{
	mdf_pfn_t pfn;
	unsigned long long paddr;
	struct dump_bitmap bitmap2;
	struct timeval tv_start;
	unsigned char buf[info->page_size];

	initialize_2nd_bitmap(&bitmap2);

	gettimeofday(&tv_start, NULL);

	for (pfn = 0, paddr = pfn_to_paddr(pfn); pfn < info->max_mapnr;
	    pfn++, paddr += info->page_size) {

		print_progress(PROGRESS_ZERO_PAGES, pfn, info->max_mapnr);

		if (!is_in_segs(paddr))
			continue;

		if (!is_dumpable(&bitmap2, pfn))
			continue;

		if (is_xen_memory()) {
			if (!readmem(MADDR_XEN, paddr, buf, info->page_size)) {
				ERRMSG("Can't get the page data(pfn:%llx, max_mapnr:%llx).\n",
				    pfn, info->max_mapnr);
				return FALSE;
			}
		} else {
			if (!readmem(PADDR, paddr, buf, info->page_size)) {
				ERRMSG("Can't get the page data(pfn:%llx, max_mapnr:%llx).\n",
				    pfn, info->max_mapnr);
				return FALSE;
			}
		}
		if (is_zero_page(buf, info->page_size)) {
			if (clear_bit_on_2nd_bitmap(pfn, NULL))
				pfn_zero++;
		}
	}

	/*
	 * print [100 %]
	 */
	print_progress(PROGRESS_ZERO_PAGES, info->max_mapnr, info->max_mapnr);
	print_execution_time(PROGRESS_ZERO_PAGES, &tv_start);

	return TRUE;
}

int
exclude_zero_pages_cyclic(struct cycle *cycle)
{
	mdf_pfn_t pfn;
	unsigned long long paddr;
	unsigned char buf[info->page_size];

	for (pfn = cycle->start_pfn, paddr = pfn_to_paddr(pfn); pfn < cycle->end_pfn;
	    pfn++, paddr += info->page_size) {

		if (!is_in_segs(paddr))
			continue;

		if (!is_dumpable_cyclic(info->partial_bitmap2, pfn, cycle))
			continue;

		if (is_xen_memory()) {
			if (!readmem(MADDR_XEN, paddr, buf, info->page_size)) {
				ERRMSG("Can't get the page data(pfn:%llx, max_mapnr:%llx).\n",
				    pfn, info->max_mapnr);
				return FALSE;
			}
		} else {
			if (!readmem(PADDR, paddr, buf, info->page_size)) {
				ERRMSG("Can't get the page data(pfn:%llx, max_mapnr:%llx).\n",
				    pfn, info->max_mapnr);
				return FALSE;
			}
		}
		if (is_zero_page(buf, info->page_size)) {
			if (clear_bit_on_2nd_bitmap(pfn, cycle))
				pfn_zero++;
		}
	}

	return TRUE;
}


static int
initialize_2nd_bitmap_cyclic(struct cycle *cycle)
{
	int i;
	mdf_pfn_t pfn;
	unsigned long long phys_start, phys_end;
	mdf_pfn_t pfn_start, pfn_end;
	mdf_pfn_t pfn_start_roundup, pfn_end_round;
	unsigned long pfn_start_byte, pfn_end_byte;

	/*
	 * At first, clear all the bits on the 2nd-bitmap.
	 */
	initialize_bitmap_cyclic(info->partial_bitmap2);

	/*
	 * If page is on memory hole, set bit on the 2nd-bitmap.
	 */
	for (i = 0; get_pt_load(i, &phys_start, &phys_end, NULL, NULL); i++) {
		pfn_start = MAX(paddr_to_pfn(phys_start), cycle->start_pfn);
		pfn_end = MIN(paddr_to_pfn(phys_end), cycle->end_pfn);

		if (pfn_start >= pfn_end)
			continue;

		pfn_start_roundup = MIN(roundup(pfn_start, BITPERBYTE),
					pfn_end);
		pfn_end_round = MAX(round(pfn_end, BITPERBYTE), pfn_start);

		for (pfn = pfn_start; pfn < pfn_start_roundup; ++pfn) {
			if (!set_bit_on_2nd_bitmap_for_kernel(pfn, cycle))
				return FALSE;
			pfn_memhole--;
		}

		pfn_start_byte = (pfn_start_roundup - cycle->start_pfn) >> 3;
		pfn_end_byte = (pfn_end_round - cycle->start_pfn) >> 3;

		if (pfn_start_byte < pfn_end_byte) {
			memset(info->partial_bitmap2 + pfn_start_byte,
			       0xff,
			       pfn_end_byte - pfn_start_byte);
			pfn_memhole -= (pfn_end_byte - pfn_start_byte) << 3;
		}

		if (pfn_end_round >= pfn_start) {
			for (pfn = pfn_end_round; pfn < pfn_end; ++pfn) {
				if (!set_bit_on_2nd_bitmap_for_kernel(pfn, cycle))
					return FALSE;
				pfn_memhole--;
			}
		}
	}

	return TRUE;
}

static void
exclude_range(mdf_pfn_t *counter, mdf_pfn_t pfn, mdf_pfn_t endpfn,
	      struct cycle *cycle)
{
	if (cycle) {
		cycle->exclude_pfn_start = cycle->end_pfn;
		cycle->exclude_pfn_end = endpfn;
		cycle->exclude_pfn_counter = counter;

		if (cycle->end_pfn < endpfn)
			endpfn = cycle->end_pfn;
	}

	while (pfn < endpfn) {
		if (clear_bit_on_2nd_bitmap_for_kernel(pfn, cycle))
			(*counter)++;
		++pfn;
	}
}

int
__exclude_unnecessary_pages(unsigned long mem_map,
    mdf_pfn_t pfn_start, mdf_pfn_t pfn_end, struct cycle *cycle)
{
	mdf_pfn_t pfn;
	mdf_pfn_t *pfn_counter;
	mdf_pfn_t nr_pages;
	unsigned long index_pg, pfn_mm;
	unsigned long long maddr;
	mdf_pfn_t pfn_read_start, pfn_read_end;
	unsigned char page_cache[SIZE(page) * PGMM_CACHED];
	unsigned char *pcache;
	unsigned int _count, _mapcount = 0, compound_order = 0;
	unsigned long flags, mapping, private = 0;
	unsigned long compound_dtor;

	/*
	 * If a multi-page exclusion is pending, do it first
	 */
	if (cycle && cycle->exclude_pfn_start < cycle->exclude_pfn_end) {
		exclude_range(cycle->exclude_pfn_counter,
			cycle->exclude_pfn_start, cycle->exclude_pfn_end,
			cycle);

		mem_map += (cycle->exclude_pfn_end - pfn_start) * SIZE(page);
		pfn_start = cycle->exclude_pfn_end;
	}

	/*
	 * Refresh the buffer of struct page, when changing mem_map.
	 */
	pfn_read_start = ULONGLONG_MAX;
	pfn_read_end   = 0;

	for (pfn = pfn_start; pfn < pfn_end; pfn++, mem_map += SIZE(page)) {

		/*
		 * If this pfn doesn't belong to target region, skip this pfn.
		 */
		if (info->flag_cyclic && !is_cyclic_region(pfn, cycle))
			continue;

		/*
		 * Exclude the memory hole.
		 */
		if (is_xen_memory()) {
			maddr = ptom_xen(pfn_to_paddr(pfn));
			if (maddr == NOT_PADDR) {
				ERRMSG("Can't convert a physical address(%llx) to machine address.\n",
				    pfn_to_paddr(pfn));
				return FALSE;
			}
			if (!is_in_segs(maddr))
				continue;
		} else {
			if (!is_in_segs(pfn_to_paddr(pfn)))
				continue;
		}

		index_pg = pfn % PGMM_CACHED;
		if (pfn < pfn_read_start || pfn_read_end < pfn) {
			if (roundup(pfn + 1, PGMM_CACHED) < pfn_end)
				pfn_mm = PGMM_CACHED - index_pg;
			else
				pfn_mm = pfn_end - pfn;

			if (!readmem(VADDR, mem_map,
			    page_cache + (index_pg * SIZE(page)),
			    SIZE(page) * pfn_mm)) {
				ERRMSG("Can't read the buffer of struct page.\n");
				return FALSE;
			}
			pfn_read_start = pfn;
			pfn_read_end   = pfn + pfn_mm - 1;
		}
		pcache  = page_cache + (index_pg * SIZE(page));

		flags   = ULONG(pcache + OFFSET(page.flags));
		_count  = UINT(pcache + OFFSET(page._count));
		mapping = ULONG(pcache + OFFSET(page.mapping));

		if ((index_pg < PGMM_CACHED - 1) &&
		    isCompoundHead(flags)) {
			compound_order = ULONG(pcache + SIZE(page) + OFFSET(page.lru)
					       + OFFSET(list_head.prev));
			compound_dtor = ULONG(pcache + SIZE(page) + OFFSET(page.lru)
					     + OFFSET(list_head.next));

			if ((compound_order >= sizeof(unsigned long) * 8)
			    || ((pfn & ((1UL << compound_order) - 1)) != 0)) {
				/* Invalid order */
				compound_order = 0;
			}
		} else {
			/*
			 * The last pfn of the mem_map cache must not be compound page
			 * since all compound pages are aligned to its page order and
			 * PGMM_CACHED is a power of 2.
			 */
			compound_order = 0;
			compound_dtor = 0;
		}

		if (OFFSET(page._mapcount) != NOT_FOUND_STRUCTURE)
			_mapcount = UINT(pcache + OFFSET(page._mapcount));
		if (OFFSET(page.private) != NOT_FOUND_STRUCTURE)
			private = ULONG(pcache + OFFSET(page.private));

		nr_pages = 1 << compound_order;
		pfn_counter = NULL;
		/*
		 * Exclude the free page managed by a buddy
		 * Use buddy identification of free pages whether cyclic or not.
		 */
		if ((info->dump_level & DL_EXCLUDE_FREE)
		    && info->page_is_buddy
		    && info->page_is_buddy(flags, _mapcount, private, _count)) {
			nr_pages = 1 << private;
			pfn_counter = &pfn_free;
		}
		/*
		 * Exclude the cache page without the private page.
		 */
		else if ((info->dump_level & DL_EXCLUDE_CACHE)
		    && (isLRU(flags) || isSwapCache(flags))
		    && !isPrivate(flags) && !isAnon(mapping)) {
			pfn_counter = &pfn_cache;
		}
		/*
		 * Exclude the cache page with the private page.
		 */
		else if ((info->dump_level & DL_EXCLUDE_CACHE_PRI)
		    && (isLRU(flags) || isSwapCache(flags))
		    && !isAnon(mapping)) {
			pfn_counter = &pfn_cache_private;
		}
		/*
		 * Exclude the data page of the user process.
		 *  - anonymous pages
		 *  - hugetlbfs pages
		 */
		else if ((info->dump_level & DL_EXCLUDE_USER_DATA)
			 && (isAnon(mapping) || isHugetlb(compound_dtor))) {
			pfn_counter = &pfn_user;
		}
		/*
		 * Exclude the hwpoison page.
		 */
		else if (isHWPOISON(flags)) {
			pfn_counter = &pfn_hwpoison;
		}
		/*
		 * Unexcludable page
		 */
		else
			continue;

		/*
		 * Execute exclusion
		 */
		if (nr_pages == 1) {
			if (clear_bit_on_2nd_bitmap_for_kernel(pfn, cycle))
				(*pfn_counter)++;
		} else {
			exclude_range(pfn_counter, pfn, pfn + nr_pages, cycle);
			pfn += nr_pages - 1;
			mem_map += (nr_pages - 1) * SIZE(page);
		}
	}
	return TRUE;
}

int
exclude_unnecessary_pages(void)
{
	unsigned int mm;
	struct mem_map_data *mmd;
	struct timeval tv_start;

	if (is_xen_memory() && !info->dom0_mapnr) {
		ERRMSG("Can't get max domain-0 PFN for excluding pages.\n");
		return FALSE;
	}

	gettimeofday(&tv_start, NULL);

	for (mm = 0; mm < info->num_mem_map; mm++) {
		print_progress(PROGRESS_UNN_PAGES, mm, info->num_mem_map);

		mmd = &info->mem_map_data[mm];

		if (mmd->mem_map == NOT_MEMMAP_ADDR)
			continue;

		if (!__exclude_unnecessary_pages(mmd->mem_map,
						 mmd->pfn_start, mmd->pfn_end, NULL))
			return FALSE;
	}

	/*
	 * print [100 %]
	 */
	print_progress(PROGRESS_UNN_PAGES, info->num_mem_map, info->num_mem_map);
	print_execution_time(PROGRESS_UNN_PAGES, &tv_start);

	return TRUE;
}

int
exclude_unnecessary_pages_cyclic(struct cycle *cycle)
{
	unsigned int mm;
	struct mem_map_data *mmd;
	struct timeval tv_start;

	if (!initialize_2nd_bitmap_cyclic(cycle))
		return FALSE;

	if ((info->dump_level & DL_EXCLUDE_FREE) && !info->page_is_buddy)
		if (!exclude_free_page(cycle))
			return FALSE;

	/*
	 * Exclude cache pages, cache private pages, user data pages,
	 * free pages and hwpoison pages.
	 */
	if (info->dump_level & DL_EXCLUDE_CACHE ||
	    info->dump_level & DL_EXCLUDE_CACHE_PRI ||
	    info->dump_level & DL_EXCLUDE_USER_DATA ||
	    NUMBER(PG_hwpoison) != NOT_FOUND_NUMBER ||
	    ((info->dump_level & DL_EXCLUDE_FREE) && info->page_is_buddy)) {

		gettimeofday(&tv_start, NULL);

		for (mm = 0; mm < info->num_mem_map; mm++) {

			if (!info->flag_mem_usage)
				print_progress(PROGRESS_UNN_PAGES, mm, info->num_mem_map);

			mmd = &info->mem_map_data[mm];

			if (mmd->mem_map == NOT_MEMMAP_ADDR)
				continue;

			if (mmd->pfn_end >= cycle->start_pfn &&
			    mmd->pfn_start <= cycle->end_pfn) {
				if (!__exclude_unnecessary_pages(mmd->mem_map,
								 mmd->pfn_start, mmd->pfn_end, cycle))
					return FALSE;
			}
		}

		/*
		 * print [100 %]
		 */
		if (!info->flag_mem_usage) {
			print_progress(PROGRESS_UNN_PAGES, info->num_mem_map, info->num_mem_map);
			print_execution_time(PROGRESS_UNN_PAGES, &tv_start);
		}
	}

	return TRUE;
}

int
copy_bitmap(void)
{
	off_t offset;
	unsigned char buf[info->page_size];
 	const off_t failed = (off_t)-1;

	offset = 0;
	while (offset < (info->len_bitmap / 2)) {
		if (lseek(info->bitmap1->fd, info->bitmap1->offset + offset,
		    SEEK_SET) == failed) {
			ERRMSG("Can't seek the bitmap(%s). %s\n",
			    info->name_bitmap, strerror(errno));
			return FALSE;
		}
		if (read(info->bitmap1->fd, buf, sizeof(buf)) != sizeof(buf)) {
			ERRMSG("Can't read the dump memory(%s). %s\n",
			    info->name_memory, strerror(errno));
			return FALSE;
		}
		if (lseek(info->bitmap2->fd, info->bitmap2->offset + offset,
		    SEEK_SET) == failed) {
			ERRMSG("Can't seek the bitmap(%s). %s\n",
			    info->name_bitmap, strerror(errno));
			return FALSE;
		}
		if (write(info->bitmap2->fd, buf, sizeof(buf)) != sizeof(buf)) {
			ERRMSG("Can't write the bitmap(%s). %s\n",
		    	info->name_bitmap, strerror(errno));
			return FALSE;
		}
		offset += sizeof(buf);
	}

	return TRUE;
}

int
create_2nd_bitmap(void)
{
	/*
	 * Copy 1st-bitmap to 2nd-bitmap.
	 */
	if (!copy_bitmap()) {
		ERRMSG("Can't copy 1st-bitmap to 2nd-bitmap.\n");
		return FALSE;
	}

	/*
	 * Exclude cache pages, cache private pages, user data pages,
	 * and hwpoison pages.
	 */
	if (info->dump_level & DL_EXCLUDE_CACHE ||
	    info->dump_level & DL_EXCLUDE_CACHE_PRI ||
	    info->dump_level & DL_EXCLUDE_USER_DATA ||
	    NUMBER(PG_hwpoison) != NOT_FOUND_NUMBER) {
		if (!exclude_unnecessary_pages()) {
			ERRMSG("Can't exclude unnecessary pages.\n");
			return FALSE;
		}
	}

	/*
	 * Exclude free pages.
	 */
	if ((info->dump_level & DL_EXCLUDE_FREE) && !info->page_is_buddy)
		if (!exclude_free_page(NULL))
			return FALSE;

	/*
	 * Exclude Xen user domain.
	 */
	if (info->flag_exclude_xen_dom) {
		if (!exclude_xen_user_domain()) {
			ERRMSG("Can't exclude xen user domain.\n");
			return FALSE;
		}
	}

	/*
	 * Exclude pages filled with zero for creating an ELF dumpfile.
	 *
	 * Note: If creating a kdump-compressed dumpfile, makedumpfile
	 *	 checks zero-pages while copying dumpable pages to a
	 *	 dumpfile from /proc/vmcore. That is valuable for the
	 *	 speed, because each page is read one time only.
	 *	 Otherwise (if creating an ELF dumpfile), makedumpfile
	 *	 should check zero-pages at this time because 2nd-bitmap
	 *	 should be fixed for creating an ELF header. That is slow
	 *	 due to reading each page two times, but it is necessary.
	 */
	if ((info->dump_level & DL_EXCLUDE_ZERO) && info->flag_elf_dumpfile) {
		/*
		 * 2nd-bitmap should be flushed at this time, because
		 * exclude_zero_pages() checks 2nd-bitmap.
		 */
		if (!sync_2nd_bitmap())
			return FALSE;

		if (!exclude_zero_pages()) {
			ERRMSG("Can't exclude pages filled with zero for creating an ELF dumpfile.\n");
			return FALSE;
		}
	}

	if (!sync_2nd_bitmap())
		return FALSE;

	return TRUE;
}

int
prepare_bitmap_buffer(void)
{
	unsigned long long tmp;

	/*
	 * Create 2 bitmaps (1st-bitmap & 2nd-bitmap) on block_size boundary.
	 * The crash utility requires both of them to be aligned to block_size
	 * boundary.
	 */
	tmp = divideup(divideup(info->max_mapnr, BITPERBYTE), info->page_size);
	info->len_bitmap = tmp*info->page_size*2;

	/*
	 * Prepare bitmap buffers for creating dump bitmap.
	 */
	if ((info->bitmap1 = malloc(sizeof(struct dump_bitmap))) == NULL) {
		ERRMSG("Can't allocate memory for the 1st-bitmap. %s\n",
		    strerror(errno));
		return FALSE;
	}
	if ((info->bitmap2 = malloc(sizeof(struct dump_bitmap))) == NULL) {
		ERRMSG("Can't allocate memory for the 2nd-bitmap. %s\n",
		    strerror(errno));
		return FALSE;
	}
	initialize_1st_bitmap(info->bitmap1);
	initialize_2nd_bitmap(info->bitmap2);

	return TRUE;
}

int
prepare_bitmap1_buffer_cyclic(void)
{
	/*
	 * Prepare partial bitmap buffers for cyclic processing.
	 */
	if ((info->partial_bitmap1 = (char *)malloc(info->bufsize_cyclic)) == NULL) {
		ERRMSG("Can't allocate memory for the 1st bitmaps. %s\n",
		       strerror(errno));
		return FALSE;
	}
	initialize_bitmap_cyclic(info->partial_bitmap1);

	return TRUE;
}

int
prepare_bitmap2_buffer_cyclic(void)
{
	unsigned long tmp;

	/*
	 * Create 2 bitmaps (1st-bitmap & 2nd-bitmap) on block_size
	 * boundary. The crash utility requires both of them to be
	 * aligned to block_size boundary.
	 */
	tmp = divideup(divideup(info->max_mapnr, BITPERBYTE), info->page_size);
	info->len_bitmap = tmp * info->page_size * 2;

	/*
	 * Prepare partial bitmap buffers for cyclic processing.
	 */
	if ((info->partial_bitmap2 = (char *)malloc(info->bufsize_cyclic)) == NULL) {
		ERRMSG("Can't allocate memory for the 2nd bitmaps. %s\n",
		       strerror(errno));
		return FALSE;
	}
	initialize_bitmap_cyclic(info->partial_bitmap2);

	return TRUE;
}

void
free_bitmap1_buffer(void)
{
	if (info->bitmap1) {
		free(info->bitmap1);
		info->bitmap1 = NULL;
	}
}

void
free_bitmap2_buffer(void)
{
	if (info->bitmap2) {
		free(info->bitmap2);
		info->bitmap2 = NULL;
	}
}

void
free_bitmap_buffer(void)
{
	free_bitmap1_buffer();
	free_bitmap2_buffer();
}

void
free_bitmap1_buffer_cyclic()
{
	if (info->partial_bitmap1 != NULL){
		free(info->partial_bitmap1);
		info->partial_bitmap1 = NULL;
	}
}

void
free_bitmap2_buffer_cyclic()
{
	if (info->partial_bitmap2 != NULL){
		free(info->partial_bitmap2);
		info->partial_bitmap2 = NULL;
	}
}

int
create_dump_bitmap(void)
{
	int ret = FALSE;

	if (info->flag_cyclic) {
		if (!prepare_bitmap2_buffer_cyclic())
			goto out;
		info->num_dumpable = get_num_dumpable_cyclic();

		if (!info->flag_elf_dumpfile)
			free_bitmap2_buffer_cyclic();

	} else {
		if (!prepare_bitmap_buffer())
			goto out;

		if (!create_1st_bitmap())
			goto out;

		if (!create_2nd_bitmap())
			goto out;
	}

	ret = TRUE;
out:
	free_bitmap_buffer();

	return ret;
}

int
get_loads_dumpfile(void)
{
	int i, phnum, num_new_load = 0;
	long page_size = info->page_size;
	mdf_pfn_t pfn, pfn_start, pfn_end, num_excluded;
	unsigned long frac_head, frac_tail;
	Elf64_Phdr load;
	struct dump_bitmap bitmap2;

	initialize_2nd_bitmap(&bitmap2);

	if (!(phnum = get_phnum_memory()))
		return FALSE;

	for (i = 0; i < phnum; i++) {
		if (!get_phdr_memory(i, &load))
			return FALSE;
		if (load.p_type != PT_LOAD)
			continue;

		pfn_start = paddr_to_pfn(load.p_paddr);
		pfn_end   = paddr_to_pfn(load.p_paddr + load.p_memsz);
		frac_head = page_size - (load.p_paddr % page_size);
		frac_tail = (load.p_paddr + load.p_memsz) % page_size;

		num_new_load++;
		num_excluded = 0;

		if (frac_head && (frac_head != page_size))
			pfn_start++;
		if (frac_tail)
			pfn_end++;

		for (pfn = pfn_start; pfn < pfn_end; pfn++) {
			if (!is_dumpable(&bitmap2, pfn)) {
				num_excluded++;
				continue;
			}

			/*
			 * If the number of the contiguous pages to be excluded
			 * is 256 or more, those pages are excluded really.
			 * And a new PT_LOAD segment is created.
			 */
			if (num_excluded >= PFN_EXCLUDED) {
				num_new_load++;
			}
			num_excluded = 0;
		}
	}
	return num_new_load;
}

int
prepare_cache_data(struct cache_data *cd)
{
	cd->fd         = info->fd_dumpfile;
	cd->file_name  = info->name_dumpfile;
	cd->cache_size = info->page_size << info->block_order;
	cd->buf_size   = 0;
	cd->buf        = NULL;

	if ((cd->buf = malloc(cd->cache_size + info->page_size)) == NULL) {
		ERRMSG("Can't allocate memory for the data buffer. %s\n",
		    strerror(errno));
		return FALSE;
	}
	return TRUE;
}

void
free_cache_data(struct cache_data *cd)
{
	free(cd->buf);
	cd->buf = NULL;
}

int
write_start_flat_header()
{
	char buf[MAX_SIZE_MDF_HEADER];
	struct makedumpfile_header fh;

	if (!info->flag_flatten)
		return FALSE;

	strcpy(fh.signature, MAKEDUMPFILE_SIGNATURE);

	/*
	 * For sending dump data to a different architecture, change the values
	 * to big endian.
	 */
	if (is_bigendian()){
		fh.type    = TYPE_FLAT_HEADER;
		fh.version = VERSION_FLAT_HEADER;
	} else {
		fh.type    = bswap_64(TYPE_FLAT_HEADER);
		fh.version = bswap_64(VERSION_FLAT_HEADER);
	}

	memset(buf, 0, sizeof(buf));
	memcpy(buf, &fh, sizeof(fh));

	if (!write_and_check_space(info->fd_dumpfile, buf, MAX_SIZE_MDF_HEADER,
	    info->name_dumpfile))
		return FALSE;

	return TRUE;
}

int
write_end_flat_header(void)
{
	struct makedumpfile_data_header fdh;

	if (!info->flag_flatten)
		return FALSE;

	fdh.offset   = END_FLAG_FLAT_HEADER;
	fdh.buf_size = END_FLAG_FLAT_HEADER;

	if (!write_and_check_space(info->fd_dumpfile, &fdh, sizeof(fdh),
	    info->name_dumpfile))
		return FALSE;

	return TRUE;
}

int
write_elf_phdr(struct cache_data *cd_hdr, Elf64_Phdr *load)
{
	Elf32_Phdr load32;

	if (is_elf64_memory()) { /* ELF64 */
		if (!write_cache(cd_hdr, load, sizeof(Elf64_Phdr)))
			return FALSE;

	} else {
		memset(&load32, 0, sizeof(Elf32_Phdr));
		load32.p_type   = load->p_type;
		load32.p_flags  = load->p_flags;
		load32.p_offset = load->p_offset;
		load32.p_vaddr  = load->p_vaddr;
		load32.p_paddr  = load->p_paddr;
		load32.p_filesz = load->p_filesz;
		load32.p_memsz  = load->p_memsz;
		load32.p_align  = load->p_align;

		if (!write_cache(cd_hdr, &load32, sizeof(Elf32_Phdr)))
			return FALSE;
	}
	return TRUE;
}

int
write_elf_header(struct cache_data *cd_header)
{
	int i, num_loads_dumpfile, phnum;
	off_t offset_note_memory, offset_note_dumpfile;
	size_t size_note, size_eraseinfo = 0;
	Elf64_Ehdr ehdr64;
	Elf32_Ehdr ehdr32;
	Elf64_Phdr note;

	char *buf = NULL;
	const off_t failed = (off_t)-1;

	int ret = FALSE;

	if (!info->flag_elf_dumpfile)
		return FALSE;

	/*
	 * Get the PT_LOAD number of the dumpfile.
	 */
	if (info->flag_cyclic) {
		if (!(num_loads_dumpfile = get_loads_dumpfile_cyclic())) {
			ERRMSG("Can't get a number of PT_LOAD.\n");
			goto out;
		}
	} else {
		if (!(num_loads_dumpfile = get_loads_dumpfile())) {
			ERRMSG("Can't get a number of PT_LOAD.\n");
			goto out;
		}
	}

	if (is_elf64_memory()) { /* ELF64 */
		if (!get_elf64_ehdr(info->fd_memory,
				    info->name_memory, &ehdr64)) {
			ERRMSG("Can't get ehdr64.\n");
			goto out;
		}
		/*
		 * PT_NOTE(1) + PT_LOAD(1+)
		 */
		ehdr64.e_phnum = 1 + num_loads_dumpfile;
	} else {                /* ELF32 */
		if (!get_elf32_ehdr(info->fd_memory,
				    info->name_memory, &ehdr32)) {
			ERRMSG("Can't get ehdr32.\n");
			goto out;
		}
		/*
		 * PT_NOTE(1) + PT_LOAD(1+)
		 */
		ehdr32.e_phnum = 1 + num_loads_dumpfile;
	}

	/*
	 * Write an ELF header.
	 */
	if (is_elf64_memory()) { /* ELF64 */
		if (!write_buffer(info->fd_dumpfile, 0, &ehdr64, sizeof(ehdr64),
		    info->name_dumpfile))
			goto out;

	} else {                /* ELF32 */
		if (!write_buffer(info->fd_dumpfile, 0, &ehdr32, sizeof(ehdr32),
		    info->name_dumpfile))
			goto out;
	}

	/*
	 * Pre-calculate the required size to store eraseinfo in ELF note
	 * section so that we can add enough space in ELF notes section and
	 * adjust the PT_LOAD offset accordingly.
	 */
	size_eraseinfo = get_size_eraseinfo();

	/*
	 * Store the size_eraseinfo for later use in write_elf_eraseinfo()
	 * function.
	 */
	info->size_elf_eraseinfo = size_eraseinfo;

	/*
	 * Write a PT_NOTE header.
	 */
	if (!(phnum = get_phnum_memory()))
		goto out;

	for (i = 0; i < phnum; i++) {
		if (!get_phdr_memory(i, &note))
			return FALSE;
		if (note.p_type == PT_NOTE)
			break;
	}
	if (note.p_type != PT_NOTE) {
		ERRMSG("Can't get a PT_NOTE header.\n");
		goto out;
	}

	if (is_elf64_memory()) { /* ELF64 */
		cd_header->offset    = sizeof(ehdr64);
		offset_note_dumpfile = sizeof(ehdr64)
		    + sizeof(Elf64_Phdr) * ehdr64.e_phnum;
	} else {
		cd_header->offset    = sizeof(ehdr32);
		offset_note_dumpfile = sizeof(ehdr32)
		    + sizeof(Elf32_Phdr) * ehdr32.e_phnum;
	}
	offset_note_memory = note.p_offset;
	note.p_offset      = offset_note_dumpfile;
	size_note          = note.p_filesz;

	/*
	 * Modify the note size in PT_NOTE header to accomodate eraseinfo data.
	 * Eraseinfo will be written later.
	 */
	if (info->size_elf_eraseinfo) {
		if (is_elf64_memory())
			note.p_filesz += sizeof(Elf64_Nhdr);
		else
			note.p_filesz += sizeof(Elf32_Nhdr);
		note.p_filesz += roundup(ERASEINFO_NOTE_NAME_BYTES, 4) +
					roundup(size_eraseinfo, 4);
	}

	if (!write_elf_phdr(cd_header, &note))
		goto out;

	/*
	 * Write a PT_NOTE segment.
	 * PT_LOAD header will be written later.
	 */
	if ((buf = malloc(size_note)) == NULL) {
		ERRMSG("Can't allocate memory for PT_NOTE segment. %s\n",
		    strerror(errno));
		goto out;
	}
	if (lseek(info->fd_memory, offset_note_memory, SEEK_SET) == failed) {
		ERRMSG("Can't seek the dump memory(%s). %s\n",
		    info->name_memory, strerror(errno));
		goto out;
	}
	if (read(info->fd_memory, buf, size_note) != size_note) {
		ERRMSG("Can't read the dump memory(%s). %s\n",
		    info->name_memory, strerror(errno));
		goto out;
	}
	if (!write_buffer(info->fd_dumpfile, offset_note_dumpfile, buf,
	    size_note, info->name_dumpfile))
		goto out;

	/* Set the size_note with new size. */
	size_note          = note.p_filesz;

	/*
	 * Set an offset of PT_LOAD segment.
	 */
	info->offset_load_dumpfile = offset_note_dumpfile + size_note;
	info->offset_note_dumpfile = offset_note_dumpfile;

	ret = TRUE;
out:
	if (buf != NULL)
		free(buf);

	return ret;
}

int
write_kdump_header(void)
{
	int ret = FALSE;
	size_t size;
	off_t offset_note, offset_vmcoreinfo;
	unsigned long size_note, size_vmcoreinfo;
	struct disk_dump_header *dh = info->dump_header;
	struct kdump_sub_header kh;
	char *buf = NULL;

	if (info->flag_elf_dumpfile)
		return FALSE;

	get_pt_note(&offset_note, &size_note);

	/*
	 * Write common header
	 */
	strncpy(dh->signature, KDUMP_SIGNATURE, strlen(KDUMP_SIGNATURE));
	dh->header_version = 6;
	dh->block_size     = info->page_size;
	dh->sub_hdr_size   = sizeof(kh) + size_note;
	dh->sub_hdr_size   = divideup(dh->sub_hdr_size, dh->block_size);
	/* dh->max_mapnr may be truncated, full 64bit in kh.max_mapnr_64 */
	dh->max_mapnr      = MIN(info->max_mapnr, UINT_MAX);
	dh->nr_cpus        = get_nr_cpus();
	dh->bitmap_blocks  = divideup(info->len_bitmap, dh->block_size);
	memcpy(&dh->timestamp, &info->timestamp, sizeof(dh->timestamp));
	memcpy(&dh->utsname, &info->system_utsname, sizeof(dh->utsname));
	if (info->flag_compress & DUMP_DH_COMPRESSED_ZLIB)
		dh->status |= DUMP_DH_COMPRESSED_ZLIB;
#ifdef USELZO
	else if (info->flag_compress & DUMP_DH_COMPRESSED_LZO)
		dh->status |= DUMP_DH_COMPRESSED_LZO;
#endif
#ifdef USESNAPPY
	else if (info->flag_compress & DUMP_DH_COMPRESSED_SNAPPY)
		dh->status |= DUMP_DH_COMPRESSED_SNAPPY;
#endif

	size = sizeof(struct disk_dump_header);
	if (!write_buffer(info->fd_dumpfile, 0, dh, size, info->name_dumpfile))
		return FALSE;

	/*
	 * Write sub header
	 */
	size = sizeof(struct kdump_sub_header);
	memset(&kh, 0, size);
	/* 64bit max_mapnr_64 */
	kh.max_mapnr_64 = info->max_mapnr;
	kh.phys_base  = info->phys_base;
	kh.dump_level = info->dump_level;
	if (info->flag_split) {
		kh.split = 1;
		/*
		 * start_pfn and end_pfn may be truncated,
		 * only for compatibility purpose
		 */
		kh.start_pfn = MIN(info->split_start_pfn, UINT_MAX);
		kh.end_pfn   = MIN(info->split_end_pfn, UINT_MAX);

		/* 64bit start_pfn_64 and end_pfn_64 */
		kh.start_pfn_64 = info->split_start_pfn;
		kh.end_pfn_64   = info->split_end_pfn;
	}
	if (has_pt_note()) {
		/*
		 * Write ELF note section
		 */
		kh.offset_note
			= DISKDUMP_HEADER_BLOCKS * dh->block_size + sizeof(kh);
		kh.size_note = size_note;

		buf = malloc(size_note);
		if (buf == NULL) {
			ERRMSG("Can't allocate memory for ELF note section. %s\n",
			    strerror(errno));
			return FALSE;
		}

		if (!info->flag_sadump) {
			if (lseek(info->fd_memory, offset_note, SEEK_SET) < 0) {
				ERRMSG("Can't seek the dump memory(%s). %s\n",
				       info->name_memory, strerror(errno));
				goto out;
			}
			if (read(info->fd_memory, buf, size_note) != size_note) {
				ERRMSG("Can't read the dump memory(%s). %s\n",
				       info->name_memory, strerror(errno));
				goto out;
			}
		} else {
			if (!sadump_read_elf_note(buf, size_note))
				goto out;
		}

		if (!write_buffer(info->fd_dumpfile, kh.offset_note, buf,
		    kh.size_note, info->name_dumpfile))
			goto out;

		if (has_vmcoreinfo()) {
			get_vmcoreinfo(&offset_vmcoreinfo, &size_vmcoreinfo);
			/*
			 * Set vmcoreinfo data
			 *
			 * NOTE: ELF note section contains vmcoreinfo data, and
			 *       kh.offset_vmcoreinfo points the vmcoreinfo data.
			 */
			kh.offset_vmcoreinfo
			    = offset_vmcoreinfo - offset_note
			      + kh.offset_note;
			kh.size_vmcoreinfo = size_vmcoreinfo;
		}
	}
	if (!write_buffer(info->fd_dumpfile, dh->block_size, &kh,
	    size, info->name_dumpfile))
		goto out;

	info->sub_header = kh;
	info->offset_bitmap1
	    = (DISKDUMP_HEADER_BLOCKS + dh->sub_hdr_size) * dh->block_size;

	ret = TRUE;
out:
	if (buf)
		free(buf);

	return ret;
}

mdf_pfn_t
get_num_dumpable(void)
{
	mdf_pfn_t pfn, num_dumpable;
	struct dump_bitmap bitmap2;

	initialize_2nd_bitmap(&bitmap2);

	for (pfn = 0, num_dumpable = 0; pfn < info->max_mapnr; pfn++) {
		if (is_dumpable(&bitmap2, pfn))
			num_dumpable++;
	}
	return num_dumpable;
}

mdf_pfn_t
get_num_dumpable_cyclic(void)
{
	mdf_pfn_t pfn, num_dumpable=0;
	struct cycle cycle = {0};

	pfn_memhole = info->max_mapnr;

	for_each_cycle(0, info->max_mapnr, &cycle)
	{
		if (!exclude_unnecessary_pages_cyclic(&cycle))
			return FALSE;

		if (info->flag_mem_usage)
			exclude_zero_pages_cyclic(&cycle);

		for(pfn=cycle.start_pfn; pfn<cycle.end_pfn; pfn++)
			if (is_dumpable_cyclic(info->partial_bitmap2, pfn, &cycle))
				num_dumpable++;
	}

	return num_dumpable;
}

int
write_elf_load_segment(struct cache_data *cd_page, unsigned long long paddr,
		       off_t off_memory, long long size)
{
	long page_size = info->page_size;
	long long bufsz_write;
	char buf[info->page_size];

	off_memory = paddr_to_offset2(paddr, off_memory);
	if (!off_memory) {
		ERRMSG("Can't convert physaddr(%llx) to an offset.\n",
		    paddr);
		return FALSE;
	}
	if (lseek(info->fd_memory, off_memory, SEEK_SET) < 0) {
		ERRMSG("Can't seek the dump memory(%s). %s\n",
		    info->name_memory, strerror(errno));
		return FALSE;
	}

	while (size > 0) {
		if (size >= page_size)
			bufsz_write = page_size;
		else
			bufsz_write = size;

		if (read(info->fd_memory, buf, bufsz_write) != bufsz_write) {
			ERRMSG("Can't read the dump memory(%s). %s\n",
			    info->name_memory, strerror(errno));
			return FALSE;
		}
		filter_data_buffer((unsigned char *)buf, paddr, bufsz_write);
		paddr += bufsz_write;
		if (!write_cache(cd_page, buf, bufsz_write))
			return FALSE;

		size -= page_size;
	}
	return TRUE;
}

int
write_elf_pages(struct cache_data *cd_header, struct cache_data *cd_page)
{
	int i, phnum;
	long page_size = info->page_size;
	mdf_pfn_t pfn, pfn_start, pfn_end, num_excluded;
	unsigned long long paddr;
	mdf_pfn_t num_dumpable, per;
	unsigned long long memsz, filesz;
	unsigned long frac_head, frac_tail;
	off_t off_seg_load, off_memory;
	Elf64_Phdr load;
	struct dump_bitmap bitmap2;
	struct timeval tv_start;

	if (!info->flag_elf_dumpfile)
		return FALSE;

	initialize_2nd_bitmap(&bitmap2);

	num_dumpable = get_num_dumpable();
	per = num_dumpable / 10000;
	per = per ? per : 1;

	off_seg_load    = info->offset_load_dumpfile;
	cd_page->offset = info->offset_load_dumpfile;

	if (!(phnum = get_phnum_memory()))
		return FALSE;

	gettimeofday(&tv_start, NULL);

	for (i = 0; i < phnum; i++) {
		if (!get_phdr_memory(i, &load))
			return FALSE;

		if (load.p_type != PT_LOAD)
			continue;

		off_memory= load.p_offset;
		paddr     = load.p_paddr;
		pfn_start = paddr_to_pfn(load.p_paddr);
		pfn_end   = paddr_to_pfn(load.p_paddr + load.p_memsz);
		frac_head = page_size - (load.p_paddr % page_size);
		frac_tail = (load.p_paddr + load.p_memsz)%page_size;

		num_excluded = 0;
		memsz  = 0;
		filesz = 0;
		if (frac_head && (frac_head != page_size)) {
			memsz  = frac_head;
			filesz = frac_head;
			pfn_start++;
		}

		if (frac_tail)
			pfn_end++;

		for (pfn = pfn_start; pfn < pfn_end; pfn++) {
			if (!is_dumpable(&bitmap2, pfn)) {
				num_excluded++;
				if ((pfn == pfn_end - 1) && frac_tail)
					memsz += frac_tail;
				else
					memsz += page_size;
				continue;
			}

			if ((num_dumped % per) == 0)
				print_progress(PROGRESS_COPY, num_dumped, num_dumpable);

			num_dumped++;

			/*
			 * The dumpable pages are continuous.
			 */
			if (!num_excluded) {
				if ((pfn == pfn_end - 1) && frac_tail) {
					memsz  += frac_tail;
					filesz += frac_tail;
				} else {
					memsz  += page_size;
					filesz += page_size;
				}
				continue;
			/*
			 * If the number of the contiguous pages to be excluded
			 * is 255 or less, those pages are not excluded.
			 */
			} else if (num_excluded < PFN_EXCLUDED) {
				if ((pfn == pfn_end - 1) && frac_tail) {
					memsz  += frac_tail;
					filesz += (page_size*num_excluded
					    + frac_tail);
				}else {
					memsz  += page_size;
					filesz += (page_size*num_excluded
					    + page_size);
				}
				num_excluded = 0;
				continue;
			}

			/*
			 * If the number of the contiguous pages to be excluded
			 * is 256 or more, those pages are excluded really.
			 * And a new PT_LOAD segment is created.
			 */
			load.p_memsz  = memsz;
			load.p_filesz = filesz;
			if (load.p_filesz)
				load.p_offset = off_seg_load;
			else
				/*
				 * If PT_LOAD segment does not have real data
				 * due to the all excluded pages, the file
				 * offset is not effective and it should be 0.
				 */
				load.p_offset = 0;

			/*
			 * Write a PT_LOAD header.
			 */
			if (!write_elf_phdr(cd_header, &load))
				return FALSE;

			/*
			 * Write a PT_LOAD segment.
			 */
			if (load.p_filesz)
				if (!write_elf_load_segment(cd_page, paddr,
				    off_memory, load.p_filesz))
					return FALSE;

			load.p_paddr += load.p_memsz;
#ifdef __x86__
			/*
			 * FIXME:
			 *  (x86) Fill PT_LOAD headers with appropriate
			 *        virtual addresses.
			 */
			if (load.p_paddr < MAXMEM)
				load.p_vaddr += load.p_memsz;
#else
			load.p_vaddr += load.p_memsz;
#endif /* x86 */
			paddr  = load.p_paddr;
			off_seg_load += load.p_filesz;

			num_excluded = 0;
			memsz  = page_size;
			filesz = page_size;
		}
		/*
		 * Write the last PT_LOAD.
		 */
		load.p_memsz  = memsz;
		load.p_filesz = filesz;
		load.p_offset = off_seg_load;

		/*
		 * Write a PT_LOAD header.
		 */
		if (!write_elf_phdr(cd_header, &load))
			return FALSE;

		/*
		 * Write a PT_LOAD segment.
		 */
		if (load.p_filesz)
			if (!write_elf_load_segment(cd_page, paddr,
						    off_memory, load.p_filesz))
				return FALSE;

		off_seg_load += load.p_filesz;
	}
	if (!write_cache_bufsz(cd_header))
		return FALSE;
	if (!write_cache_bufsz(cd_page))
		return FALSE;

	/*
	 * print [100 %]
	 */
	print_progress(PROGRESS_COPY, num_dumpable, num_dumpable);
	print_execution_time(PROGRESS_COPY, &tv_start);
	PROGRESS_MSG("\n");

	return TRUE;
}

int
read_pfn(mdf_pfn_t pfn, unsigned char *buf)
{
	unsigned long long paddr;

	paddr = pfn_to_paddr(pfn);
	if (!readmem(PADDR, paddr, buf, info->page_size)) {
		ERRMSG("Can't get the page data.\n");
		return FALSE;
	}

	return TRUE;
}

int
get_loads_dumpfile_cyclic(void)
{
	int i, phnum, num_new_load = 0;
	long page_size = info->page_size;
	unsigned char buf[info->page_size];
	mdf_pfn_t pfn, pfn_start, pfn_end, num_excluded;
	unsigned long frac_head, frac_tail;
	Elf64_Phdr load;
	struct cycle cycle = {0};

	if (!(phnum = get_phnum_memory()))
		return FALSE;

	for (i = 0; i < phnum; i++) {
		if (!get_phdr_memory(i, &load))
			return FALSE;
		if (load.p_type != PT_LOAD)
			continue;

		pfn_start = paddr_to_pfn(load.p_paddr);
		pfn_end = paddr_to_pfn(load.p_paddr + load.p_memsz);
		frac_head = page_size - (load.p_paddr % page_size);
		frac_tail = (load.p_paddr + load.p_memsz) % page_size;

		num_new_load++;
		num_excluded = 0;

		if (frac_head && (frac_head != page_size))
			pfn_start++;
		if (frac_tail)
			pfn_end++;

		for_each_cycle(pfn_start, pfn_end, &cycle) {
			if (!exclude_unnecessary_pages_cyclic(&cycle))
				return FALSE;
			for (pfn = MAX(pfn_start, cycle.start_pfn); pfn < cycle.end_pfn; pfn++) {
				if (!is_dumpable_cyclic(info->partial_bitmap2, pfn, &cycle)) {
					num_excluded++;
					continue;
				}

				/*
				 * Exclude zero pages.
				 */
				if (info->dump_level & DL_EXCLUDE_ZERO) {
					if (!read_pfn(pfn, buf))
						return FALSE;
					if (is_zero_page(buf, page_size)) {
						num_excluded++;
						continue;
					}
				}

				info->num_dumpable++;

				/*
				 * If the number of the contiguous pages to be excluded
				 * is 256 or more, those pages are excluded really.
				 * And a new PT_LOAD segment is created.
				 */
				if (num_excluded >= PFN_EXCLUDED) {
					num_new_load++;
				}
				num_excluded = 0;
			}
		}

	}
	return num_new_load;
}

int
write_elf_pages_cyclic(struct cache_data *cd_header, struct cache_data *cd_page)
{
	int i, phnum;
	long page_size = info->page_size;
	unsigned char buf[info->page_size];
	mdf_pfn_t pfn, pfn_start, pfn_end, num_excluded, num_dumpable, per;
	unsigned long long paddr;
	unsigned long long memsz, filesz;
	unsigned long frac_head, frac_tail;
	off_t off_seg_load, off_memory;
	Elf64_Phdr load;
	struct timeval tv_start;
	struct cycle cycle = {0};

	if (!info->flag_elf_dumpfile)
		return FALSE;

	num_dumpable = info->num_dumpable;
	per = num_dumpable / 10000;
	per = per ? per : 1;

	off_seg_load   = info->offset_load_dumpfile;
	cd_page->offset = info->offset_load_dumpfile;

	/*
	 * Reset counter for debug message.
	 */
	pfn_zero = pfn_cache = pfn_cache_private = 0;
	pfn_user = pfn_free = pfn_hwpoison = 0;
	pfn_memhole = info->max_mapnr;

	if (!(phnum = get_phnum_memory()))
		return FALSE;

	gettimeofday(&tv_start, NULL);

	for (i = 0; i < phnum; i++) {
		if (!get_phdr_memory(i, &load))
			return FALSE;

		if (load.p_type != PT_LOAD)
			continue;

		off_memory= load.p_offset;
		paddr = load.p_paddr;
		pfn_start = paddr_to_pfn(load.p_paddr);
		pfn_end = paddr_to_pfn(load.p_paddr + load.p_memsz);
		frac_head = page_size - (load.p_paddr % page_size);
		frac_tail = (load.p_paddr + load.p_memsz)%page_size;

		num_excluded = 0;
		memsz  = 0;
		filesz = 0;
		if (frac_head && (frac_head != page_size)) {
			memsz  = frac_head;
			filesz = frac_head;
			pfn_start++;
		}

		if (frac_tail)
			pfn_end++;

		for_each_cycle(pfn_start, pfn_end, &cycle) {
			/*
			 * Update target region and partial bitmap if necessary.
			 */
			if (!exclude_unnecessary_pages_cyclic(&cycle))
				return FALSE;

			for (pfn = MAX(pfn_start, cycle.start_pfn); pfn < cycle.end_pfn; pfn++) {
				if (!is_dumpable_cyclic(info->partial_bitmap2, pfn, &cycle)) {
					num_excluded++;
					if ((pfn == pfn_end - 1) && frac_tail)
						memsz += frac_tail;
					else
						memsz += page_size;
					continue;
				}

				/*
				 * Exclude zero pages.
				 */
				if (info->dump_level & DL_EXCLUDE_ZERO) {
					if (!read_pfn(pfn, buf))
						return FALSE;
					if (is_zero_page(buf, page_size)) {
						pfn_zero++;
						num_excluded++;
						if ((pfn == pfn_end - 1) && frac_tail)
							memsz += frac_tail;
						else
							memsz += page_size;
						continue;
					}
				}

				if ((num_dumped % per) == 0)
					print_progress(PROGRESS_COPY, num_dumped, num_dumpable);

				num_dumped++;

				/*
				 * The dumpable pages are continuous.
				 */
				if (!num_excluded) {
					if ((pfn == pfn_end - 1) && frac_tail) {
						memsz  += frac_tail;
						filesz += frac_tail;
					} else {
						memsz  += page_size;
						filesz += page_size;
					}
					continue;
					/*
					 * If the number of the contiguous pages to be excluded
					 * is 255 or less, those pages are not excluded.
					 */
				} else if (num_excluded < PFN_EXCLUDED) {
					if ((pfn == pfn_end - 1) && frac_tail) {
						memsz  += frac_tail;
						filesz += (page_size*num_excluded
							   + frac_tail);
					}else {
						memsz  += page_size;
						filesz += (page_size*num_excluded
							   + page_size);
					}
					num_excluded = 0;
					continue;
				}

				/*
				 * If the number of the contiguous pages to be excluded
				 * is 256 or more, those pages are excluded really.
				 * And a new PT_LOAD segment is created.
				 */
				load.p_memsz = memsz;
				load.p_filesz = filesz;
				if (load.p_filesz)
					load.p_offset = off_seg_load;
				else
					/*
					 * If PT_LOAD segment does not have real data
					 * due to the all excluded pages, the file
					 * offset is not effective and it should be 0.
					 */
					load.p_offset = 0;

				/*
				 * Write a PT_LOAD header.
				 */
				if (!write_elf_phdr(cd_header, &load))
					return FALSE;

				/*
				 * Write a PT_LOAD segment.
				 */
				if (load.p_filesz)
					if (!write_elf_load_segment(cd_page, paddr,
								    off_memory, load.p_filesz))
						return FALSE;

				load.p_paddr += load.p_memsz;
#ifdef __x86__
				/*
				 * FIXME:
				 *  (x86) Fill PT_LOAD headers with appropriate
				 * virtual addresses.
				 */
				if (load.p_paddr < MAXMEM)
					load.p_vaddr += load.p_memsz;
#else
				load.p_vaddr += load.p_memsz;
#endif /* x86 */
				paddr  = load.p_paddr;
				off_seg_load += load.p_filesz;

				num_excluded = 0;
				memsz  = page_size;
				filesz = page_size;
			}
		}

		/*
		 * Write the last PT_LOAD.
		 */
		load.p_memsz = memsz;
		load.p_filesz = filesz;
		load.p_offset = off_seg_load;

		/*
		 * Write a PT_LOAD header.
		 */
		if (!write_elf_phdr(cd_header, &load))
			return FALSE;

		/*
		 * Write a PT_LOAD segment.
		 */
		if (load.p_filesz)
			if (!write_elf_load_segment(cd_page, paddr,
						    off_memory, load.p_filesz))
				return FALSE;

		off_seg_load += load.p_filesz;
	}
	if (!write_cache_bufsz(cd_header))
		return FALSE;
	if (!write_cache_bufsz(cd_page))
		return FALSE;

	free_bitmap2_buffer_cyclic();

	/*
	 * print [100 %]
	 */
	print_progress(PROGRESS_COPY, num_dumpable, num_dumpable);
	print_execution_time(PROGRESS_COPY, &tv_start);
	PROGRESS_MSG("\n");

	return TRUE;
}

int
write_kdump_pages(struct cache_data *cd_header, struct cache_data *cd_page)
{
	mdf_pfn_t pfn, per, num_dumpable;
	mdf_pfn_t start_pfn, end_pfn;
	unsigned long size_out;
	struct page_desc pd, pd_zero;
	off_t offset_data = 0;
	struct disk_dump_header *dh = info->dump_header;
	unsigned char buf[info->page_size], *buf_out = NULL;
	unsigned long len_buf_out;
	struct dump_bitmap bitmap2;
	struct timeval tv_start;
	const off_t failed = (off_t)-1;
	unsigned long len_buf_out_zlib, len_buf_out_lzo, len_buf_out_snappy;

	int ret = FALSE;

	if (info->flag_elf_dumpfile)
		return FALSE;

	initialize_2nd_bitmap(&bitmap2);

	len_buf_out_zlib = len_buf_out_lzo = len_buf_out_snappy = 0;

#ifdef USELZO
	lzo_bytep wrkmem;

	if ((wrkmem = malloc(LZO1X_1_MEM_COMPRESS)) == NULL) {
		ERRMSG("Can't allocate memory for the working memory. %s\n",
		       strerror(errno));
		goto out;
	}

	len_buf_out_lzo = info->page_size + info->page_size / 16 + 64 + 3;
#endif

#ifdef USESNAPPY
	len_buf_out_snappy = snappy_max_compressed_length(info->page_size);
#endif

	len_buf_out_zlib = compressBound(info->page_size);
	
	len_buf_out = MAX(len_buf_out_zlib,
			  MAX(len_buf_out_lzo,
			      len_buf_out_snappy));

	if ((buf_out = malloc(len_buf_out)) == NULL) {
		ERRMSG("Can't allocate memory for the compression buffer. %s\n",
		    strerror(errno));
		goto out;
	}

	num_dumpable = get_num_dumpable();
	per = num_dumpable / 10000;
	per = per ? per : 1;

	/*
	 * Calculate the offset of the page data.
	 */
	cd_header->offset
	    = (DISKDUMP_HEADER_BLOCKS + dh->sub_hdr_size + dh->bitmap_blocks)
		* dh->block_size;
	cd_page->offset = cd_header->offset + sizeof(page_desc_t)*num_dumpable;
	offset_data  = cd_page->offset;

	/*
	 * Set a fileoffset of Physical Address 0x0.
	 */
	if (lseek(info->fd_memory, get_offset_pt_load_memory(), SEEK_SET)
	    == failed) {
		ERRMSG("Can't seek the dump memory(%s). %s\n",
		    info->name_memory, strerror(errno));
		goto out;
	}

	/*
	 * Write the data of zero-filled page.
	 */
	gettimeofday(&tv_start, NULL);
	if (info->dump_level & DL_EXCLUDE_ZERO) {
		pd_zero.size = info->page_size;
		pd_zero.flags = 0;
		pd_zero.offset = offset_data;
		pd_zero.page_flags = 0;
		memset(buf, 0, pd_zero.size);
		if (!write_cache(cd_page, buf, pd_zero.size))
			goto out;
		offset_data  += pd_zero.size;
	}
	if (info->flag_split) {
		start_pfn = info->split_start_pfn;
		end_pfn   = info->split_end_pfn;
	}
	else {
		start_pfn = 0;
		end_pfn   = info->max_mapnr;
	}

	for (pfn = start_pfn; pfn < end_pfn; pfn++) {

		if ((num_dumped % per) == 0)
			print_progress(PROGRESS_COPY, num_dumped, num_dumpable);

		/*
		 * Check the excluded page.
		 */
		if (!is_dumpable(&bitmap2, pfn))
			continue;

		num_dumped++;

		if (!read_pfn(pfn, buf))
			goto out;
		filter_data_buffer(buf, pfn_to_paddr(pfn), info->page_size);

		/*
		 * Exclude the page filled with zeros.
		 */
		if ((info->dump_level & DL_EXCLUDE_ZERO)
		    && is_zero_page(buf, info->page_size)) {
			if (!write_cache(cd_header, &pd_zero, sizeof(page_desc_t)))
				goto out;
			pfn_zero++;
			continue;
		}
		/*
		 * Compress the page data.
		 */
		size_out = len_buf_out;
		if ((info->flag_compress & DUMP_DH_COMPRESSED_ZLIB)
		    && ((size_out = len_buf_out),
			compress2(buf_out, &size_out, buf, info->page_size,
				  Z_BEST_SPEED) == Z_OK)
		    && (size_out < info->page_size)) {
			pd.flags = DUMP_DH_COMPRESSED_ZLIB;
			pd.size  = size_out;
#ifdef USELZO
		} else if (info->flag_lzo_support
			   && (info->flag_compress & DUMP_DH_COMPRESSED_LZO)
			   && ((size_out = info->page_size),
			       lzo1x_1_compress(buf, info->page_size, buf_out,
						&size_out, wrkmem) == LZO_E_OK)
			   && (size_out < info->page_size)) {
			pd.flags = DUMP_DH_COMPRESSED_LZO;
			pd.size  = size_out;
#endif
#ifdef USESNAPPY
		} else if ((info->flag_compress & DUMP_DH_COMPRESSED_SNAPPY)
			   && ((size_out = len_buf_out_snappy),
			       snappy_compress((char *)buf, info->page_size,
					       (char *)buf_out,
					       (size_t *)&size_out)
			       == SNAPPY_OK)
			   && (size_out < info->page_size)) {
			pd.flags = DUMP_DH_COMPRESSED_SNAPPY;
			pd.size  = size_out;
#endif
		} else {
			pd.flags = 0;
			pd.size  = info->page_size;
		}
		pd.page_flags = 0;
		pd.offset     = offset_data;
		offset_data  += pd.size;

		/*
		 * Write the page header.
		 */
		if (!write_cache(cd_header, &pd, sizeof(page_desc_t)))
			goto out;

		/*
		 * Write the page data.
		 */
		if (!write_cache(cd_page, pd.flags ? buf_out : buf, pd.size))
			goto out;
	}

	/*
	 * Write the remainder.
	 */
	if (!write_cache_bufsz(cd_page))
		goto out;
	if (!write_cache_bufsz(cd_header))
		goto out;

	/*
	 * print [100 %]
	 */
	print_progress(PROGRESS_COPY, num_dumpable, num_dumpable);
	print_execution_time(PROGRESS_COPY, &tv_start);
	PROGRESS_MSG("\n");

	ret = TRUE;
out:
	if (buf_out != NULL)
		free(buf_out);
#ifdef USELZO
	if (wrkmem != NULL)
		free(wrkmem);
#endif

	return ret;
}

int
write_kdump_pages_cyclic(struct cache_data *cd_header, struct cache_data *cd_page,
			 struct page_desc *pd_zero, off_t *offset_data, struct cycle *cycle)
{
	mdf_pfn_t pfn, per;
	mdf_pfn_t start_pfn, end_pfn;
	unsigned long size_out;
	struct page_desc pd;
	unsigned char buf[info->page_size], *buf_out = NULL;
	unsigned long len_buf_out;
	struct timeval tv_start;
	const off_t failed = (off_t)-1;
	unsigned long len_buf_out_zlib, len_buf_out_lzo, len_buf_out_snappy;

	int ret = FALSE;

	if (info->flag_elf_dumpfile)
		return FALSE;

	len_buf_out_zlib = len_buf_out_lzo = len_buf_out_snappy = 0;

#ifdef USELZO
	lzo_bytep wrkmem;

	if ((wrkmem = malloc(LZO1X_1_MEM_COMPRESS)) == NULL) {
		ERRMSG("Can't allocate memory for the working memory. %s\n",
		       strerror(errno));
		goto out;
	}

	len_buf_out_lzo = info->page_size + info->page_size / 16 + 64 + 3;
#endif
#ifdef USESNAPPY
	len_buf_out_snappy = snappy_max_compressed_length(info->page_size);
#endif

	len_buf_out_zlib = compressBound(info->page_size);

	len_buf_out = MAX(len_buf_out_zlib,
			  MAX(len_buf_out_lzo,
			      len_buf_out_snappy));

	if ((buf_out = malloc(len_buf_out)) == NULL) {
		ERRMSG("Can't allocate memory for the compression buffer. %s\n",
		       strerror(errno));
		goto out;
	}

	per = info->num_dumpable / 10000;
	per = per ? per : 1;

	/*
	 * Set a fileoffset of Physical Address 0x0.
	 */
	if (lseek(info->fd_memory, get_offset_pt_load_memory(), SEEK_SET)
	    == failed) {
		ERRMSG("Can't seek the dump memory(%s). %s\n",
		       info->name_memory, strerror(errno));
		goto out;
	}

	start_pfn = cycle->start_pfn;
	end_pfn   = cycle->end_pfn;

	if (info->flag_split) {
		if (start_pfn < info->split_start_pfn)
			start_pfn = info->split_start_pfn;
		if (end_pfn > info->split_end_pfn)
			end_pfn = info->split_end_pfn;
	}

	gettimeofday(&tv_start, NULL);

	for (pfn = start_pfn; pfn < end_pfn; pfn++) {

		if ((num_dumped % per) == 0)
			print_progress(PROGRESS_COPY, num_dumped, info->num_dumpable);

		/*
		 * Check the excluded page.
		 */
		if (!is_on(info->partial_bitmap2, pfn - cycle->start_pfn))
			continue;

		num_dumped++;

		if (!read_pfn(pfn, buf))
			goto out;
		filter_data_buffer(buf, pfn_to_paddr(pfn), info->page_size);

		/*
		 * Exclude the page filled with zeros.
		 */
		if ((info->dump_level & DL_EXCLUDE_ZERO)
		    && is_zero_page(buf, info->page_size)) {
			if (!write_cache(cd_header, pd_zero, sizeof(page_desc_t)))
				goto out;
			pfn_zero++;
			continue;
		}
		/*
		 * Compress the page data.
		 */
		size_out = len_buf_out;
		if ((info->flag_compress & DUMP_DH_COMPRESSED_ZLIB)
		    && ((size_out = len_buf_out),
			compress2(buf_out, &size_out, buf, info->page_size,
				  Z_BEST_SPEED) == Z_OK)
		    && (size_out < info->page_size)) {
			pd.flags = DUMP_DH_COMPRESSED_ZLIB;
			pd.size  = size_out;
#ifdef USELZO
		} else if (info->flag_lzo_support
			   && (info->flag_compress & DUMP_DH_COMPRESSED_LZO)
			   && ((size_out = info->page_size),
			       lzo1x_1_compress(buf, info->page_size, buf_out,
						&size_out, wrkmem) == LZO_E_OK)
			   && (size_out < info->page_size)) {
			pd.flags = DUMP_DH_COMPRESSED_LZO;
			pd.size  = size_out;
#endif
#ifdef USESNAPPY
		} else if ((info->flag_compress & DUMP_DH_COMPRESSED_SNAPPY)
			   && ((size_out = len_buf_out_snappy),
			       snappy_compress((char *)buf, info->page_size,
					       (char *)buf_out,
					       (size_t *)&size_out)
			       == SNAPPY_OK)
			   && (size_out < info->page_size)) {
			pd.flags = DUMP_DH_COMPRESSED_SNAPPY;
			pd.size  = size_out;
#endif
		} else {
			pd.flags = 0;
			pd.size  = info->page_size;
		}
		pd.page_flags = 0;
		pd.offset     = *offset_data;
		*offset_data  += pd.size;

                /*
                 * Write the page header.
                 */
                if (!write_cache(cd_header, &pd, sizeof(page_desc_t)))
                        goto out;

                /*
                 * Write the page data.
                 */
		if (!write_cache(cd_page, pd.flags ? buf_out : buf, pd.size))
                        goto out;
        }

	ret = TRUE;
out:
	if (buf_out != NULL)
		free(buf_out);
#ifdef USELZO
	if (wrkmem != NULL)
		free(wrkmem);
#endif

	print_progress(PROGRESS_COPY, num_dumped, info->num_dumpable);
	print_execution_time(PROGRESS_COPY, &tv_start);

	return ret;
}

/*
 * Copy eraseinfo from input dumpfile/vmcore to output dumpfile.
 */
static int
copy_eraseinfo(struct cache_data *cd_eraseinfo)
{
	char *buf = NULL;
	off_t offset;
	unsigned long size;
	int ret = FALSE;

	get_eraseinfo(&offset, &size);
	buf = malloc(size);
	if (buf == NULL) {
		ERRMSG("Can't allocate memory for erase info section. %s\n",
		    strerror(errno));
		return FALSE;
	}
	if (lseek(info->fd_memory, offset, SEEK_SET) < 0) {
		ERRMSG("Can't seek the dump memory(%s). %s\n",
		    info->name_memory, strerror(errno));
		goto out;
	}
	if (read(info->fd_memory, buf, size) != size) {
		ERRMSG("Can't read the dump memory(%s). %s\n",
		    info->name_memory, strerror(errno));
		goto out;
	}
	if (!write_cache(cd_eraseinfo, buf, size))
		goto out;
	ret = TRUE;
out:
	if (buf)
		free(buf);
	return ret;
}

static int
update_eraseinfo_of_sub_header(off_t offset_eraseinfo,
			       unsigned long size_eraseinfo)
{
	off_t offset;

	/* seek to kdump sub header offset */
	offset = DISKDUMP_HEADER_BLOCKS * info->page_size;

	info->sub_header.offset_eraseinfo = offset_eraseinfo;
	info->sub_header.size_eraseinfo   = size_eraseinfo;

	if (!write_buffer(info->fd_dumpfile, offset, &info->sub_header,
			sizeof(struct kdump_sub_header), info->name_dumpfile))
		return FALSE;

	return TRUE;
}

/*
 * Traverse through eraseinfo nodes and write it to the o/p dumpfile if the
 * node has erased flag set.
 */
int
write_eraseinfo(struct cache_data *cd_page, unsigned long *size_out)
{
	int i, j, obuf_size = 0, ei_size = 0;
	int ret = FALSE;
	unsigned long size_eraseinfo = 0;
	char *obuf = NULL;
	char size_str[MAX_SIZE_STR_LEN];

	for (i = 1; i < num_erase_info; i++) {
		if (!erase_info[i].erased)
			continue;
		for (j = 0; j < erase_info[i].num_sizes; j++) {
			if (erase_info[i].sizes[j] > 0)
				sprintf(size_str, "size %ld\n",
						erase_info[i].sizes[j]);
			else if (erase_info[i].sizes[j] == -1)
				sprintf(size_str, "nullify\n");

			/* Calculate the required buffer size. */
			ei_size = strlen("erase ") +
					strlen(erase_info[i].symbol_expr) + 1 +
					strlen(size_str) +
					1;
			/*
			 * If obuf is allocated in the previous run and is
			 * big enough to hold current erase info string then
			 * reuse it otherwise realloc.
			 */
			if (ei_size > obuf_size) {
				obuf_size = ei_size;
				obuf = realloc(obuf, obuf_size);
				if (!obuf) {
					ERRMSG("Can't allocate memory for"
						" output buffer\n");
					return FALSE;
				}
			}
			sprintf(obuf, "erase %s %s", erase_info[i].symbol_expr,
							size_str);
			DEBUG_MSG(obuf);
			if (!write_cache(cd_page, obuf, strlen(obuf)))
				goto out;
			size_eraseinfo += strlen(obuf);
		}
	}
	/*
	 * Write the remainder.
	 */
	if (!write_cache_bufsz(cd_page))
		goto out;

	*size_out = size_eraseinfo;
	ret = TRUE;
out:
	if (obuf)
		free(obuf);

	return ret;
}

int
write_elf_eraseinfo(struct cache_data *cd_header)
{
	char note[MAX_SIZE_NHDR];
	char buf[ERASEINFO_NOTE_NAME_BYTES + 4];
	off_t offset_eraseinfo;
	unsigned long note_header_size, size_written, size_note;

	DEBUG_MSG("erase info size: %lu\n", info->size_elf_eraseinfo);

	if (!info->size_elf_eraseinfo)
		return TRUE;

	DEBUG_MSG("Writing erase info...\n");

	/* calculate the eraseinfo ELF note offset */
	get_pt_note(NULL, &size_note);
	cd_header->offset = info->offset_note_dumpfile +
				roundup(size_note, 4);

	/* Write eraseinfo ELF note header. */
	memset(note, 0, sizeof(note));
	if (is_elf64_memory()) {
		Elf64_Nhdr *nh = (Elf64_Nhdr *)note;

		note_header_size = sizeof(Elf64_Nhdr);
		nh->n_namesz = ERASEINFO_NOTE_NAME_BYTES;
		nh->n_descsz = info->size_elf_eraseinfo;
		nh->n_type = 0;
	} else {
		Elf32_Nhdr *nh = (Elf32_Nhdr *)note;

		note_header_size = sizeof(Elf32_Nhdr);
		nh->n_namesz = ERASEINFO_NOTE_NAME_BYTES;
		nh->n_descsz = info->size_elf_eraseinfo;
		nh->n_type = 0;
	}
	if (!write_cache(cd_header, note, note_header_size))
		return FALSE;

	/* Write eraseinfo Note name */
	memset(buf, 0, sizeof(buf));
	memcpy(buf, ERASEINFO_NOTE_NAME, ERASEINFO_NOTE_NAME_BYTES);
	if (!write_cache(cd_header, buf,
				roundup(ERASEINFO_NOTE_NAME_BYTES, 4)))
		return FALSE;

	offset_eraseinfo = cd_header->offset;
	if (!write_eraseinfo(cd_header, &size_written))
		return FALSE;

	/*
	 * The actual eraseinfo written may be less than pre-calculated size.
	 * Hence fill up the rest of size with zero's.
	 */
	if (size_written < info->size_elf_eraseinfo)
		write_cache_zero(cd_header,
				info->size_elf_eraseinfo - size_written);

	DEBUG_MSG("offset_eraseinfo: %llx, size_eraseinfo: %ld\n",
		(unsigned long long)offset_eraseinfo, info->size_elf_eraseinfo);

	return TRUE;
}

int
write_kdump_eraseinfo(struct cache_data *cd_page)
{
	off_t offset_eraseinfo;
	unsigned long size_eraseinfo, size_written;

	DEBUG_MSG("Writing erase info...\n");
	offset_eraseinfo = cd_page->offset;

	/*
	 * In case of refiltering copy the existing eraseinfo from input
	 * dumpfile to o/p dumpfile.
	 */
	if (has_eraseinfo()) {
		get_eraseinfo(NULL, &size_eraseinfo);
		if (!copy_eraseinfo(cd_page))
			return FALSE;
	} else
		size_eraseinfo = 0;

	if (!write_eraseinfo(cd_page, &size_written))
		return FALSE;

	size_eraseinfo += size_written;
	DEBUG_MSG("offset_eraseinfo: %llx, size_eraseinfo: %ld\n",
		(unsigned long long)offset_eraseinfo, size_eraseinfo);

	if (size_eraseinfo)
		/* Update the erase info offset and size in kdump sub header */
		if (!update_eraseinfo_of_sub_header(offset_eraseinfo,
						    size_eraseinfo))
			return FALSE;

	return TRUE;
}

int
write_kdump_bitmap(void)
{
	struct cache_data bm;
	long long buf_size;
	off_t offset;

	int ret = FALSE;

	if (info->flag_elf_dumpfile)
		return FALSE;

	bm.fd        = info->fd_bitmap;
	bm.file_name = info->name_bitmap;
	bm.offset    = 0;
	bm.buf       = NULL;

	if ((bm.buf = calloc(1, BUFSIZE_BITMAP)) == NULL) {
		ERRMSG("Can't allocate memory for dump bitmap buffer. %s\n",
		    strerror(errno));
		goto out;
	}
	offset = info->offset_bitmap1;
	buf_size = info->len_bitmap;

	while (buf_size > 0) {
		if (buf_size >= BUFSIZE_BITMAP)
			bm.cache_size = BUFSIZE_BITMAP;
		else
			bm.cache_size = buf_size;

		if(!read_cache(&bm))
			goto out;

		if (!write_buffer(info->fd_dumpfile, offset,
		    bm.buf, bm.cache_size, info->name_dumpfile))
			goto out;

		offset += bm.cache_size;
		buf_size -= BUFSIZE_BITMAP;
	}
	ret = TRUE;
out:
	if (bm.buf != NULL)
		free(bm.buf);

	return ret;
}

int
write_kdump_bitmap1_cyclic(struct cycle *cycle)
{
	off_t offset;
        int increment;
	int ret = FALSE;

	increment = divideup(cycle->end_pfn - cycle->start_pfn, BITPERBYTE);

	if (info->flag_elf_dumpfile)
		return FALSE;

	offset = info->offset_bitmap1;
	if (!write_buffer(info->fd_dumpfile, offset + info->bufsize_cyclic *
			  (cycle->start_pfn / info->pfn_cyclic),
			  info->partial_bitmap1, increment, info->name_dumpfile))
		goto out;

	ret = TRUE;
out:
	return ret;
}

int
write_kdump_bitmap2_cyclic(struct cycle *cycle)
{
	off_t offset;
	int increment;
	int ret = FALSE;

	increment = divideup(cycle->end_pfn - cycle->start_pfn,
			     BITPERBYTE);

	if (info->flag_elf_dumpfile)
		return FALSE;

	offset = info->offset_bitmap1;
	offset += info->len_bitmap / 2;
	if (!write_buffer(info->fd_dumpfile, offset,
			  info->partial_bitmap2, increment, info->name_dumpfile))
		goto out;

	info->offset_bitmap1 += increment;

	ret = TRUE;
out:

	return ret;
}

int
write_kdump_pages_and_bitmap_cyclic(struct cache_data *cd_header, struct cache_data *cd_page)
{
	struct page_desc pd_zero;
	off_t offset_data=0;
	struct disk_dump_header *dh = info->dump_header;
	unsigned char buf[info->page_size];
	struct timeval tv_start;

	/*
	 * Reset counter for debug message.
	 */
	pfn_zero = pfn_cache = pfn_cache_private = 0;
	pfn_user = pfn_free = pfn_hwpoison = 0;
	pfn_memhole = info->max_mapnr;

	cd_header->offset
		= (DISKDUMP_HEADER_BLOCKS + dh->sub_hdr_size + dh->bitmap_blocks)
		* dh->block_size;
	cd_page->offset = cd_header->offset + sizeof(page_desc_t)*info->num_dumpable;
	offset_data = cd_page->offset;
	
	/*
	 * Write the data of zero-filled page.
	 */
	if (info->dump_level & DL_EXCLUDE_ZERO) {
		pd_zero.size = info->page_size;
		pd_zero.flags = 0;
		pd_zero.offset = offset_data;
		pd_zero.page_flags = 0;
		memset(buf, 0, pd_zero.size);
		if (!write_cache(cd_page, buf, pd_zero.size))
			return FALSE;
		offset_data += pd_zero.size;
	}

	/*
	 * Write the 1st bitmap
	 */
	if (!prepare_bitmap1_buffer_cyclic())
		return FALSE;

	struct cycle cycle = {0};
	for_each_cycle(0, info->max_mapnr, &cycle)
	{
		if (!create_1st_bitmap_cyclic(&cycle))
			return FALSE;
		if (!write_kdump_bitmap1_cyclic(&cycle))
			return FALSE;
	}


	free_bitmap1_buffer_cyclic();

	if (!prepare_bitmap2_buffer_cyclic())
		return FALSE;

	/*
	 * Write pages and bitmap cyclically.
	 */
	//cycle = {0, 0};
	memset(&cycle, 0, sizeof(struct cycle));
	for_each_cycle(0, info->max_mapnr, &cycle)
	{
		if (!exclude_unnecessary_pages_cyclic(&cycle))
			return FALSE;

		if (!write_kdump_pages_cyclic(cd_header, cd_page, &pd_zero,
					&offset_data, &cycle))
			return FALSE;

		if (!write_kdump_bitmap2_cyclic(&cycle))
			return FALSE;
	}

	free_bitmap2_buffer_cyclic();

	gettimeofday(&tv_start, NULL);

	/*
	 * Write the remainder.
	 */
	if (!write_cache_bufsz(cd_page))
		return FALSE;
	if (!write_cache_bufsz(cd_header))
		return FALSE;

	/*
	 * print [100 %]
	 */
	print_progress(PROGRESS_COPY, num_dumped, info->num_dumpable);
	print_execution_time(PROGRESS_COPY, &tv_start);
	PROGRESS_MSG("\n");

	return TRUE;
}
	
void
close_vmcoreinfo(void)
{
	if(fclose(info->file_vmcoreinfo) < 0)
		ERRMSG("Can't close the vmcoreinfo file(%s). %s\n",
		    info->name_vmcoreinfo, strerror(errno));
}

void
close_dump_memory(void)
{
	if ((info->fd_memory = close(info->fd_memory)) < 0)
		ERRMSG("Can't close the dump memory(%s). %s\n",
		    info->name_memory, strerror(errno));
}

void
close_dump_file(void)
{
	if (info->flag_flatten)
		return;

	if ((info->fd_dumpfile = close(info->fd_dumpfile)) < 0)
		ERRMSG("Can't close the dump file(%s). %s\n",
		    info->name_dumpfile, strerror(errno));
}

void
close_dump_bitmap(void)
{
	if ((info->fd_bitmap = close(info->fd_bitmap)) < 0)
		ERRMSG("Can't close the bitmap file(%s). %s\n",
		    info->name_bitmap, strerror(errno));
	free(info->name_bitmap);
	info->name_bitmap = NULL;
}

void
close_kernel_file(void)
{
	if (info->name_vmlinux) {
		if ((info->fd_vmlinux = close(info->fd_vmlinux)) < 0) {
			ERRMSG("Can't close the kernel file(%s). %s\n",
			    info->name_vmlinux, strerror(errno));
		}
	}
	if (info->name_xen_syms) {
		if ((info->fd_xen_syms = close(info->fd_xen_syms)) < 0) {
			ERRMSG("Can't close the kernel file(%s). %s\n",
			    info->name_xen_syms, strerror(errno));
		}
	}
}

/*
 * Close the following files when it generates the vmcoreinfo file.
 * - vmlinux
 * - vmcoreinfo file
 */
int
close_files_for_generating_vmcoreinfo(void)
{
	close_kernel_file();

	close_vmcoreinfo();

	return TRUE;
}

/*
 * Close the following file when it rearranges the dump data.
 * - dump file
 */
int
close_files_for_rearranging_dumpdata(void)
{
	close_dump_file();

	return TRUE;
}

/*
 * Close the following files when it creates the dump file.
 * - dump mem
 * - bit map
 * if it reads the vmcoreinfo file
 *   - vmcoreinfo file
 * else
 *   - vmlinux
 */
int
close_files_for_creating_dumpfile(void)
{
	if (info->max_dump_level > DL_EXCLUDE_ZERO)
		close_kernel_file();

	/* free name for vmcoreinfo */
	if (has_vmcoreinfo()) {
		free(info->name_vmcoreinfo);
		info->name_vmcoreinfo = NULL;
	}
	close_dump_memory();

	close_dump_bitmap();

	return TRUE;
}

/*
 * for Xen extraction
 */
int
get_symbol_info_xen(void)
{
	/*
	 * Common symbol
	 */
	SYMBOL_INIT(dom_xen, "dom_xen");
	SYMBOL_INIT(dom_io, "dom_io");
	SYMBOL_INIT(domain_list, "domain_list");
	SYMBOL_INIT(frame_table, "frame_table");
	SYMBOL_INIT(alloc_bitmap, "alloc_bitmap");
	SYMBOL_INIT(max_page, "max_page");
	SYMBOL_INIT(xenheap_phys_end, "xenheap_phys_end");

	/*
	 * Architecture specific
	 */
	SYMBOL_INIT(pgd_l2, "idle_pg_table_l2");	/* x86 */
	SYMBOL_INIT(pgd_l3, "idle_pg_table_l3");	/* x86-PAE */
	if (SYMBOL(pgd_l3) == NOT_FOUND_SYMBOL)
		SYMBOL_INIT(pgd_l3, "idle_pg_table");	/* x86-PAE */
	SYMBOL_INIT(pgd_l4, "idle_pg_table_4");		/* x86_64 */
	if (SYMBOL(pgd_l4) == NOT_FOUND_SYMBOL)
		SYMBOL_INIT(pgd_l4, "idle_pg_table");		/* x86_64 */

	SYMBOL_INIT(xen_heap_start, "xen_heap_start");	/* ia64 */
	SYMBOL_INIT(xen_pstart, "xen_pstart");		/* ia64 */
	SYMBOL_INIT(frametable_pg_dir, "frametable_pg_dir");	/* ia64 */

	return TRUE;
}

int
get_structure_info_xen(void)
{
	SIZE_INIT(page_info, "page_info");
	OFFSET_INIT(page_info.count_info, "page_info", "count_info");
	OFFSET_INIT(page_info._domain, "page_info", "_domain");

	SIZE_INIT(domain, "domain");
	OFFSET_INIT(domain.domain_id, "domain", "domain_id");
	OFFSET_INIT(domain.next_in_list, "domain", "next_in_list");

	return TRUE;
}

int
init_xen_crash_info(void)
{
	off_t		offset_xen_crash_info;
	unsigned long	size_xen_crash_info;
	void		*buf;

	get_xen_crash_info(&offset_xen_crash_info, &size_xen_crash_info);
	if (!size_xen_crash_info) {
		info->xen_crash_info_v = -1;
		return TRUE;		/* missing info is non-fatal */
	}

	if (size_xen_crash_info < sizeof(xen_crash_info_com_t)) {
		ERRMSG("Xen crash info too small (%lu bytes).\n",
		       size_xen_crash_info);
		return FALSE;
	}

	buf = malloc(size_xen_crash_info);
	if (!buf) {
		ERRMSG("Can't allocate note (%lu bytes). %s\n",
		       size_xen_crash_info, strerror(errno));
		return FALSE;
	}

	if (lseek(info->fd_memory, offset_xen_crash_info, SEEK_SET) < 0) {
		ERRMSG("Can't seek the dump memory(%s). %s\n",
		       info->name_memory, strerror(errno));
		return FALSE;
	}
	if (read(info->fd_memory, buf, size_xen_crash_info)
	    != size_xen_crash_info) {
		ERRMSG("Can't read the dump memory(%s). %s\n",
		       info->name_memory, strerror(errno));
		return FALSE;
	}

	info->xen_crash_info.com = buf;
	if (size_xen_crash_info >= sizeof(xen_crash_info_v2_t))
		info->xen_crash_info_v = 2;
	else if (size_xen_crash_info >= sizeof(xen_crash_info_t))
		info->xen_crash_info_v = 1;
	else
		info->xen_crash_info_v = 0;

	return TRUE;
}

int
get_xen_info(void)
{
	unsigned long domain;
	unsigned int domain_id;
	int num_domain;

	/*
	 * Get architecture specific basic data
	 */
	if (!get_xen_basic_info_arch())
		return FALSE;

	if (!info->xen_crash_info.com ||
	    info->xen_crash_info.com->xen_major_version < 4) {
		if (SYMBOL(alloc_bitmap) == NOT_FOUND_SYMBOL) {
			ERRMSG("Can't get the symbol of alloc_bitmap.\n");
			return FALSE;
		}
		if (!readmem(VADDR_XEN, SYMBOL(alloc_bitmap), &info->alloc_bitmap,
		      sizeof(info->alloc_bitmap))) {
			ERRMSG("Can't get the value of alloc_bitmap.\n");
			return FALSE;
		}
		if (SYMBOL(max_page) == NOT_FOUND_SYMBOL) {
			ERRMSG("Can't get the symbol of max_page.\n");
			return FALSE;
		}
		if (!readmem(VADDR_XEN, SYMBOL(max_page), &info->max_page,
		    sizeof(info->max_page))) {
			ERRMSG("Can't get the value of max_page.\n");
			return FALSE;
		}
	}

	/*
	 * Walk through domain_list
	 */
	if (SYMBOL(domain_list) == NOT_FOUND_SYMBOL) {
		ERRMSG("Can't get the symbol of domain_list.\n");
		return FALSE;
	}
	if (!readmem(VADDR_XEN, SYMBOL(domain_list), &domain, sizeof(domain))){
		ERRMSG("Can't get the value of domain_list.\n");
		return FALSE;
	}

	/*
	 * Get numbers of domain first
	 */
	num_domain = 0;
	while (domain) {
		num_domain++;
		if (!readmem(VADDR_XEN, domain + OFFSET(domain.next_in_list),
		    &domain, sizeof(domain))) {
			ERRMSG("Can't get through the domain_list.\n");
			return FALSE;
		}
	}

	if ((info->domain_list = (struct domain_list *)
	      malloc(sizeof(struct domain_list) * (num_domain + 2))) == NULL) {
		ERRMSG("Can't allocate memory for domain_list.\n");
		return FALSE;
	}

	info->num_domain = num_domain + 2;

	if (!readmem(VADDR_XEN, SYMBOL(domain_list), &domain, sizeof(domain))) {
		ERRMSG("Can't get the value of domain_list.\n");
		return FALSE;
	}
	num_domain = 0;
	while (domain) {
		if (!readmem(VADDR_XEN, domain + OFFSET(domain.domain_id),
		      &domain_id, sizeof(domain_id))) {
			ERRMSG("Can't get the domain_id.\n");
			return FALSE;
		}
		info->domain_list[num_domain].domain_addr = domain;
		info->domain_list[num_domain].domain_id = domain_id;
		/*
		 * pickled_id is set by architecture specific
		 */
		num_domain++;

		if (!readmem(VADDR_XEN, domain + OFFSET(domain.next_in_list),
		     &domain, sizeof(domain))) {
			ERRMSG("Can't get through the domain_list.\n");
			return FALSE;
		}
	}

	/*
	 * special domains
	 */
	if (SYMBOL(dom_xen) == NOT_FOUND_SYMBOL) {
		ERRMSG("Can't get the symbol of dom_xen.\n");
		return FALSE;
	}
	if (!readmem(VADDR_XEN, SYMBOL(dom_xen), &domain, sizeof(domain))) {
		ERRMSG("Can't get the value of dom_xen.\n");
		return FALSE;
	}
	if (!readmem(VADDR_XEN, domain + OFFSET(domain.domain_id), &domain_id,
	    sizeof(domain_id))) {
		ERRMSG( "Can't get the value of dom_xen domain_id.\n");
		return FALSE;
	}
	info->domain_list[num_domain].domain_addr = domain;
	info->domain_list[num_domain].domain_id = domain_id;
	num_domain++;

	if (SYMBOL(dom_io) == NOT_FOUND_SYMBOL) {
		ERRMSG("Can't get the symbol of dom_io.\n");
		return FALSE;
	}
	if (!readmem(VADDR_XEN, SYMBOL(dom_io), &domain, sizeof(domain))) {
		ERRMSG("Can't get the value of dom_io.\n");
		return FALSE;
	}
	if (!readmem(VADDR_XEN, domain + OFFSET(domain.domain_id), &domain_id,
	    sizeof(domain_id))) {
		ERRMSG( "Can't get the value of dom_io domain_id.\n");
		return FALSE;
	}
	info->domain_list[num_domain].domain_addr = domain;
	info->domain_list[num_domain].domain_id = domain_id;

	/*
	 * Get architecture specific data
	 */
	if (!get_xen_info_arch())
		return FALSE;

	return TRUE;
}

void
show_data_xen(void)
{
	int i;

	/*
	 * Show data for debug
	 */
	MSG("\n");
	MSG("SYMBOL(dom_xen): %llx\n", SYMBOL(dom_xen));
	MSG("SYMBOL(dom_io): %llx\n", SYMBOL(dom_io));
	MSG("SYMBOL(domain_list): %llx\n", SYMBOL(domain_list));
	MSG("SYMBOL(xen_heap_start): %llx\n", SYMBOL(xen_heap_start));
	MSG("SYMBOL(frame_table): %llx\n", SYMBOL(frame_table));
	MSG("SYMBOL(alloc_bitmap): %llx\n", SYMBOL(alloc_bitmap));
	MSG("SYMBOL(max_page): %llx\n", SYMBOL(max_page));
	MSG("SYMBOL(pgd_l2): %llx\n", SYMBOL(pgd_l2));
	MSG("SYMBOL(pgd_l3): %llx\n", SYMBOL(pgd_l3));
	MSG("SYMBOL(pgd_l4): %llx\n", SYMBOL(pgd_l4));
	MSG("SYMBOL(xenheap_phys_end): %llx\n", SYMBOL(xenheap_phys_end));
	MSG("SYMBOL(xen_pstart): %llx\n", SYMBOL(xen_pstart));
	MSG("SYMBOL(frametable_pg_dir): %llx\n", SYMBOL(frametable_pg_dir));

	MSG("SIZE(page_info): %ld\n", SIZE(page_info));
	MSG("OFFSET(page_info.count_info): %ld\n", OFFSET(page_info.count_info));
	MSG("OFFSET(page_info._domain): %ld\n", OFFSET(page_info._domain));
	MSG("SIZE(domain): %ld\n", SIZE(domain));
	MSG("OFFSET(domain.domain_id): %ld\n", OFFSET(domain.domain_id));
	MSG("OFFSET(domain.next_in_list): %ld\n", OFFSET(domain.next_in_list));

	MSG("\n");
	if (info->xen_crash_info.com) {
		MSG("xen_major_version: %lx\n",
		    info->xen_crash_info.com->xen_major_version);
		MSG("xen_minor_version: %lx\n",
		    info->xen_crash_info.com->xen_minor_version);
	}
	MSG("xen_phys_start: %lx\n", info->xen_phys_start);
	MSG("frame_table_vaddr: %lx\n", info->frame_table_vaddr);
	MSG("xen_heap_start: %lx\n", info->xen_heap_start);
	MSG("xen_heap_end:%lx\n", info->xen_heap_end);
	MSG("alloc_bitmap: %lx\n", info->alloc_bitmap);
	MSG("max_page: %lx\n", info->max_page);
	MSG("num_domain: %d\n", info->num_domain);
	for (i = 0; i < info->num_domain; i++) {
		MSG(" %u: %x: %lx\n", info->domain_list[i].domain_id,
			info->domain_list[i].pickled_id,
			info->domain_list[i].domain_addr);
	}
}

int
generate_vmcoreinfo_xen(void)
{
	if ((info->page_size = sysconf(_SC_PAGE_SIZE)) <= 0) {
		ERRMSG("Can't get the size of page.\n");
		return FALSE;
	}
	set_dwarf_debuginfo("xen-syms", NULL,
			    info->name_xen_syms, info->fd_xen_syms);

	if (!get_symbol_info_xen())
		return FALSE;

	if (!get_structure_info_xen())
		return FALSE;

	/*
	 * write 1st kernel's PAGESIZE
	 */
	fprintf(info->file_vmcoreinfo, "%s%ld\n", STR_PAGESIZE,
	    info->page_size);

	/*
	 * write the symbol of 1st kernel
	 */
	WRITE_SYMBOL("dom_xen", dom_xen);
	WRITE_SYMBOL("dom_io", dom_io);
	WRITE_SYMBOL("domain_list", domain_list);
	WRITE_SYMBOL("xen_heap_start", xen_heap_start);
	WRITE_SYMBOL("frame_table", frame_table);
	WRITE_SYMBOL("alloc_bitmap", alloc_bitmap);
	WRITE_SYMBOL("max_page", max_page);
	WRITE_SYMBOL("pgd_l2", pgd_l2);
	WRITE_SYMBOL("pgd_l3", pgd_l3);
	WRITE_SYMBOL("pgd_l4", pgd_l4);
	WRITE_SYMBOL("xenheap_phys_end", xenheap_phys_end);
	WRITE_SYMBOL("xen_pstart", xen_pstart);
	WRITE_SYMBOL("frametable_pg_dir", frametable_pg_dir);

	/*
	 * write the structure size of 1st kernel
	 */
	WRITE_STRUCTURE_SIZE("page_info", page_info);
	WRITE_STRUCTURE_SIZE("domain", domain);

	/*
	 * write the member offset of 1st kernel
	 */
	WRITE_MEMBER_OFFSET("page_info.count_info", page_info.count_info);
	WRITE_MEMBER_OFFSET("page_info._domain", page_info._domain);
	WRITE_MEMBER_OFFSET("domain.domain_id", domain.domain_id);
	WRITE_MEMBER_OFFSET("domain.next_in_list", domain.next_in_list);

	return TRUE;
}

int
read_vmcoreinfo_basic_info_xen(void)
{
	long page_size = FALSE;
	char buf[BUFSIZE_FGETS], *endp;
	unsigned int i;

	if (fseek(info->file_vmcoreinfo, 0, SEEK_SET) < 0) {
		ERRMSG("Can't seek the vmcoreinfo file(%s). %s\n",
		    info->name_vmcoreinfo, strerror(errno));
		return FALSE;
	}

	while (fgets(buf, BUFSIZE_FGETS, info->file_vmcoreinfo)) {
		i = strlen(buf);
		if (!i)
			break;
		if (buf[i - 1] == '\n')
			buf[i - 1] = '\0';
		if (strncmp(buf, STR_PAGESIZE, strlen(STR_PAGESIZE)) == 0) {
			page_size = strtol(buf+strlen(STR_PAGESIZE),&endp,10);
			if ((!page_size || page_size == LONG_MAX)
			    || strlen(endp) != 0) {
				ERRMSG("Invalid data in %s: %s",
				    info->name_vmcoreinfo, buf);
				return FALSE;
			}
			if (!set_page_size(page_size)) {
				ERRMSG("Invalid data in %s: %s",
				    info->name_vmcoreinfo, buf);
				return FALSE;
			}
			break;
		}
	}
	if (!info->page_size) {
		ERRMSG("Invalid format in %s", info->name_vmcoreinfo);
		return FALSE;
	}
	return TRUE;
}

int
read_vmcoreinfo_xen(void)
{
	if (!read_vmcoreinfo_basic_info_xen())
		return FALSE;

	READ_SYMBOL("dom_xen", dom_xen);
	READ_SYMBOL("dom_io", dom_io);
	READ_SYMBOL("domain_list", domain_list);
	READ_SYMBOL("xen_heap_start", xen_heap_start);
	READ_SYMBOL("frame_table", frame_table);
	READ_SYMBOL("alloc_bitmap", alloc_bitmap);
	READ_SYMBOL("max_page", max_page);
	READ_SYMBOL("pgd_l2", pgd_l2);
	READ_SYMBOL("pgd_l3", pgd_l3);
	READ_SYMBOL("pgd_l4", pgd_l4);
	READ_SYMBOL("xenheap_phys_end", xenheap_phys_end);
	READ_SYMBOL("xen_pstart", xen_pstart);
	READ_SYMBOL("frametable_pg_dir", frametable_pg_dir);

	READ_STRUCTURE_SIZE("page_info", page_info);
	READ_STRUCTURE_SIZE("domain", domain);

	READ_MEMBER_OFFSET("page_info.count_info", page_info.count_info);
	READ_MEMBER_OFFSET("page_info._domain", page_info._domain);
	READ_MEMBER_OFFSET("domain.domain_id", domain.domain_id);
	READ_MEMBER_OFFSET("domain.next_in_list", domain.next_in_list);

	return TRUE;
}

int
allocated_in_map(mdf_pfn_t pfn)
{
	static unsigned long long cur_idx = -1;
	static unsigned long cur_word;
	unsigned long long idx;

	idx = pfn / PAGES_PER_MAPWORD;
	if (idx != cur_idx) {
		if (!readmem(VADDR_XEN,
		    info->alloc_bitmap + idx * sizeof(unsigned long),
		    &cur_word, sizeof(cur_word))) {
			ERRMSG("Can't access alloc_bitmap.\n");
			return 0;
		}
		cur_idx = idx;
	}

	return !!(cur_word & (1UL << (pfn & (PAGES_PER_MAPWORD - 1))));
}

int
is_select_domain(unsigned int id)
{
	int i;

	/* selected domain is fix to dom0 only now !!
	   (yes... domain_list is not necessary right now,
		   it can get from "dom0" directly) */

	for (i = 0; i < info->num_domain; i++) {
		if (info->domain_list[i].domain_id == 0 &&
		    info->domain_list[i].pickled_id == id)
			return TRUE;
	}

	return FALSE;
}

int
exclude_xen3_user_domain(void)
{
	int i;
	unsigned int count_info, _domain;
	unsigned int num_pt_loads = get_num_pt_loads();
	unsigned long page_info_addr;
	unsigned long long phys_start, phys_end;
	mdf_pfn_t pfn, pfn_end;
	mdf_pfn_t j, size;

	/*
	 * NOTE: the first half of bitmap is not used for Xen extraction
	 */
	for (i = 0; get_pt_load(i, &phys_start, &phys_end, NULL, NULL); i++) {

		print_progress(PROGRESS_XEN_DOMAIN, i, num_pt_loads);

		pfn     = paddr_to_pfn(phys_start);
		pfn_end = paddr_to_pfn(phys_end);
		size    = pfn_end - pfn;

		for (j = 0; pfn < pfn_end; pfn++, j++) {
			print_progress(PROGRESS_XEN_DOMAIN, j + (size * i),
					size * num_pt_loads);

			if (!allocated_in_map(pfn)) {
				clear_bit_on_2nd_bitmap(pfn, NULL);
				continue;
			}

			page_info_addr = info->frame_table_vaddr + pfn * SIZE(page_info);
			if (!readmem(VADDR_XEN,
			      page_info_addr + OFFSET(page_info.count_info),
		 	      &count_info, sizeof(count_info))) {
				clear_bit_on_2nd_bitmap(pfn, NULL);
				continue;	/* page_info may not exist */
			}
			if (!readmem(VADDR_XEN,
			      page_info_addr + OFFSET(page_info._domain),
			      &_domain, sizeof(_domain))) {
				ERRMSG("Can't get page_info._domain.\n");
				return FALSE;
			}
			/*
			 * select:
			 *  - anonymous (_domain == 0), or
			 *  - xen heap area, or
			 *  - selected domain page
			 */
			if (_domain == 0)
				continue;
			if (info->xen_heap_start <= pfn && pfn < info->xen_heap_end)
				continue;
			if ((count_info & 0xffff) && is_select_domain(_domain))
				continue;
			clear_bit_on_2nd_bitmap(pfn, NULL);
		}
	}

	return TRUE;
}

int
exclude_xen4_user_domain(void)
{
	int i;
	unsigned long count_info;
	unsigned int  _domain;
	unsigned int num_pt_loads = get_num_pt_loads();
	unsigned long page_info_addr;
	unsigned long long phys_start, phys_end;
	mdf_pfn_t pfn, pfn_end;
	mdf_pfn_t j, size;

	/*
	 * NOTE: the first half of bitmap is not used for Xen extraction
	 */
	for (i = 0; get_pt_load(i, &phys_start, &phys_end, NULL, NULL); i++) {

		print_progress(PROGRESS_XEN_DOMAIN, i, num_pt_loads);

		pfn     = paddr_to_pfn(phys_start);
		pfn_end = paddr_to_pfn(phys_end);
		size    = pfn_end - pfn;

		for (j = 0; pfn < pfn_end; pfn++, j++) {
			print_progress(PROGRESS_XEN_DOMAIN, j + (size * i),
					size * num_pt_loads);

			page_info_addr = info->frame_table_vaddr + pfn * SIZE(page_info);
			if (!readmem(VADDR_XEN,
			      page_info_addr + OFFSET(page_info.count_info),
		 	      &count_info, sizeof(count_info))) {
				clear_bit_on_2nd_bitmap(pfn, NULL);
				continue;	/* page_info may not exist */
			}

			/* always keep Xen heap pages */
			if (count_info & PGC_xen_heap)
				continue;

			/* delete free, offlined and broken pages */
			if (page_state_is(count_info, free) ||
			    page_state_is(count_info, offlined) ||
			    count_info & PGC_broken) {
				clear_bit_on_2nd_bitmap(pfn, NULL);
				continue;
			}

			/* keep inuse pages not allocated to any domain
			 * this covers e.g. Xen static data
			 */
			if (! (count_info & PGC_allocated))
				continue;

			/* Need to check the domain
			 * keep:
			 *  - anonymous (_domain == 0), or
			 *  - selected domain page
			 */
			if (!readmem(VADDR_XEN,
			      page_info_addr + OFFSET(page_info._domain),
			      &_domain, sizeof(_domain))) {
				ERRMSG("Can't get page_info._domain.\n");
				return FALSE;
			}

			if (_domain == 0)
				continue;
			if (is_select_domain(_domain))
				continue;
			clear_bit_on_2nd_bitmap(pfn, NULL);
		}
	}

	return TRUE;
}

int
exclude_xen_user_domain(void)
{
	struct timeval tv_start;
	int ret;

	gettimeofday(&tv_start, NULL);

	if (info->xen_crash_info.com &&
	    info->xen_crash_info.com->xen_major_version >= 4)
		ret = exclude_xen4_user_domain();
	else
		ret = exclude_xen3_user_domain();

	/*
	 * print [100 %]
	 */
	print_progress(PROGRESS_XEN_DOMAIN, 1, 1);
	print_execution_time(PROGRESS_XEN_DOMAIN, &tv_start);

	return ret;
}

int
initial_xen(void)
{
	int xen_info_required = TRUE;
	off_t offset;
	unsigned long size;

#if defined(__powerpc64__) || defined(__powerpc32__)
	MSG("\n");
	MSG("Xen is not supported on powerpc.\n");
	return FALSE;
#else
	if(!info->flag_elf_dumpfile && !info->flag_dmesg) {
		MSG("Specify '-E' option for Xen.\n");
		MSG("Commandline parameter is invalid.\n");
		MSG("Try `makedumpfile --help' for more information.\n");
		return FALSE;
	}
#ifndef __x86_64__
	if (DL_EXCLUDE_ZERO < info->max_dump_level) {
		MSG("Dump_level is invalid. It should be 0 or 1.\n");
		MSG("Commandline parameter is invalid.\n");
		MSG("Try `makedumpfile --help' for more information.\n");
		return FALSE;
	}
#endif
	if (is_xen_memory()) {
		if(info->flag_cyclic) {
			info->flag_cyclic = FALSE;
			MSG("Switched running mode from cyclic to non-cyclic,\n");
			MSG("because the cyclic mode doesn't support Xen.\n");
		}
	}

	if (!init_xen_crash_info())
		return FALSE;
	/*
	 * Get the debug information for analysis from the vmcoreinfo file
	 */
	if (info->flag_read_vmcoreinfo) {
		if (!read_vmcoreinfo_xen())
			return FALSE;
		close_vmcoreinfo();
	/*
	 * Get the debug information for analysis from the xen-syms file
	 */
	} else if (info->name_xen_syms) {
		set_dwarf_debuginfo("xen-syms", NULL,
				    info->name_xen_syms, info->fd_xen_syms);

		if (!get_symbol_info_xen())
			return FALSE;
		if (!get_structure_info_xen())
			return FALSE;
	/*
	 * Get the debug information for analysis from /proc/vmcore
	 */
	} else {
		/*
		 * Check whether /proc/vmcore contains vmcoreinfo,
		 * and get both the offset and the size.
		 */
		if (!has_vmcoreinfo_xen()){
			if (!info->flag_exclude_xen_dom) {
				xen_info_required = FALSE;
				goto out;
			}

			MSG("%s doesn't contain a vmcoreinfo for Xen.\n",
			    info->name_memory);
			MSG("Specify '--xen-syms' option or '--xen-vmcoreinfo' option.\n");
			MSG("Commandline parameter is invalid.\n");
			MSG("Try `makedumpfile --help' for more information.\n");
			return FALSE;
		}
		/*
		 * Get the debug information from /proc/vmcore
		 */
		get_vmcoreinfo_xen(&offset, &size);
		if (!read_vmcoreinfo_from_vmcore(offset, size, TRUE))
			return FALSE;
	}

out:
	if (!info->page_size) {
		/*
		 * If we cannot get page_size from a vmcoreinfo file,
		 * fall back to the current kernel page size.
		 */
		if (!fallback_to_current_page_size())
			return FALSE;
	}

	if (!cache_init())
		return FALSE;

	if (xen_info_required == TRUE) {
		if (!get_xen_info())
			return FALSE;

		if (message_level & ML_PRINT_DEBUG_MSG)
			show_data_xen();
	}

	if (!get_max_mapnr())
		return FALSE;

	return TRUE;
#endif
}

void
print_vtop(void)
{
	unsigned long long paddr;

	if (!info->vaddr_for_vtop)
		return;

	MSG("\n");
	MSG("Translating virtual address %lx to physical address.\n", info->vaddr_for_vtop);

	paddr = vaddr_to_paddr(info->vaddr_for_vtop);

	MSG("VIRTUAL           PHYSICAL\n");
	MSG("%16lx  %llx\n", info->vaddr_for_vtop, paddr);
	MSG("\n");

	info->vaddr_for_vtop = 0;

	return;
}

void
print_report(void)
{
	mdf_pfn_t pfn_original, pfn_excluded, shrinking;

	/*
	 * /proc/vmcore doesn't contain the memory hole area.
	 */
	pfn_original = info->max_mapnr - pfn_memhole;

	pfn_excluded = pfn_zero + pfn_cache + pfn_cache_private
	    + pfn_user + pfn_free + pfn_hwpoison;
	shrinking = (pfn_original - pfn_excluded) * 100;
	shrinking = shrinking / pfn_original;

	REPORT_MSG("\n");
	REPORT_MSG("Original pages  : 0x%016llx\n", pfn_original);
	REPORT_MSG("  Excluded pages   : 0x%016llx\n", pfn_excluded);
	REPORT_MSG("    Pages filled with zero  : 0x%016llx\n", pfn_zero);
	REPORT_MSG("    Cache pages             : 0x%016llx\n", pfn_cache);
	REPORT_MSG("    Cache pages + private   : 0x%016llx\n",
	    pfn_cache_private);
	REPORT_MSG("    User process data pages : 0x%016llx\n", pfn_user);
	REPORT_MSG("    Free pages              : 0x%016llx\n", pfn_free);
	REPORT_MSG("    Hwpoison pages          : 0x%016llx\n", pfn_hwpoison);
	REPORT_MSG("  Remaining pages  : 0x%016llx\n",
	    pfn_original - pfn_excluded);
	REPORT_MSG("  (The number of pages is reduced to %lld%%.)\n",
	    shrinking);
	REPORT_MSG("Memory Hole     : 0x%016llx\n", pfn_memhole);
	REPORT_MSG("--------------------------------------------------\n");
	REPORT_MSG("Total pages     : 0x%016llx\n", info->max_mapnr);
	REPORT_MSG("\n");
}

static void
print_mem_usage(void)
{
	mdf_pfn_t pfn_original, pfn_excluded, shrinking;
	unsigned long long total_size;

	/*
	* /proc/vmcore doesn't contain the memory hole area.
	*/
	pfn_original = info->max_mapnr - pfn_memhole;

	pfn_excluded = pfn_zero + pfn_cache + pfn_cache_private
	    + pfn_user + pfn_free + pfn_hwpoison;
	shrinking = (pfn_original - pfn_excluded) * 100;
	shrinking = shrinking / pfn_original;
	total_size = info->page_size * pfn_original;

	MSG("\n");
	MSG("TYPE		PAGES			EXCLUDABLE	DESCRIPTION\n");
	MSG("----------------------------------------------------------------------\n");

	MSG("ZERO		%-16llu	yes		Pages filled with zero\n", pfn_zero);
	MSG("CACHE		%-16llu	yes		Cache pages\n", pfn_cache);
	MSG("CACHE_PRIVATE	%-16llu	yes		Cache pages + private\n",
	    pfn_cache_private);
	MSG("USER		%-16llu	yes		User process pages\n", pfn_user);
	MSG("FREE		%-16llu	yes		Free pages\n", pfn_free);
	MSG("KERN_DATA	%-16llu	no		Dumpable kernel data \n",
	    pfn_original - pfn_excluded);

	MSG("\n");

	MSG("page size:		%-16ld\n", info->page_size);
	MSG("Total pages on system:	%-16llu\n", pfn_original);
	MSG("Total size on system:	%-16llu Byte\n", total_size);
}

int
writeout_dumpfile(void)
{
	int ret = FALSE;
	struct cache_data cd_header, cd_page;

	info->flag_nospace = FALSE;

	if (!open_dump_file())
		return FALSE;

	if (info->flag_flatten) {
		if (!write_start_flat_header())
			return FALSE;
	}
	if (!prepare_cache_data(&cd_header))
		return FALSE;

	if (!prepare_cache_data(&cd_page)) {
		free_cache_data(&cd_header);
		return FALSE;
	}
	if (info->flag_elf_dumpfile) {
		if (!write_elf_header(&cd_header))
			goto out;
		if (info->flag_cyclic) {
			if (!write_elf_pages_cyclic(&cd_header, &cd_page))
				goto out;
		} else {
			if (!write_elf_pages(&cd_header, &cd_page))
				goto out;
		}
		if (!write_elf_eraseinfo(&cd_header))
			goto out;
	} else if (info->flag_cyclic) {
		if (!write_kdump_header())
			goto out;
		if (!write_kdump_pages_and_bitmap_cyclic(&cd_header, &cd_page))
			goto out;
		if (!write_kdump_eraseinfo(&cd_page))
			goto out;
	} else {
		if (!write_kdump_header())
			goto out;
		if (!write_kdump_pages(&cd_header, &cd_page))
			goto out;
		if (!write_kdump_eraseinfo(&cd_page))
			goto out;
		if (!write_kdump_bitmap())
			goto out;
	}
	if (info->flag_flatten) {
		if (!write_end_flat_header())
			goto out;
	}

	ret = TRUE;
out:
	free_cache_data(&cd_header);
	free_cache_data(&cd_page);

	close_dump_file();

	if ((ret == FALSE) && info->flag_nospace)
		return NOSPACE;
	else
		return ret;
}

int
setup_splitting(void)
{
	int i;
	mdf_pfn_t j, pfn_per_dumpfile;
	mdf_pfn_t start_pfn, end_pfn;
	mdf_pfn_t num_dumpable = get_num_dumpable();
	struct dump_bitmap bitmap2;

	if (info->num_dumpfile <= 1)
		return FALSE;

	if (info->flag_cyclic) {
		for (i = 0; i < info->num_dumpfile; i++) {
			SPLITTING_START_PFN(i) = divideup(info->max_mapnr, info->num_dumpfile) * i;
			SPLITTING_END_PFN(i)   = divideup(info->max_mapnr, info->num_dumpfile) * (i + 1);
		}
		if (SPLITTING_END_PFN(i-1) > info->max_mapnr)
			SPLITTING_END_PFN(i-1) = info->max_mapnr;
        } else {
		initialize_2nd_bitmap(&bitmap2);

		pfn_per_dumpfile = num_dumpable / info->num_dumpfile;
		start_pfn = end_pfn = 0;
		for (i = 0; i < info->num_dumpfile; i++) {
			start_pfn = end_pfn;
			if (i == (info->num_dumpfile - 1)) {
				end_pfn  = info->max_mapnr;
			} else {
				for (j = 0; j < pfn_per_dumpfile; end_pfn++) {
					if (is_dumpable(&bitmap2, end_pfn))
						j++;
				}
			}
			SPLITTING_START_PFN(i) = start_pfn;
			SPLITTING_END_PFN(i)   = end_pfn;
		}
	}

	return TRUE;
}

/*
 * This function is for creating split dumpfiles by multiple
 * processes. Each child process should re-open a /proc/vmcore
 * file, because it prevents each other from affectting the file
 * offset due to read(2) call.
 */
int
reopen_dump_memory()
{
	close_dump_memory();

	if ((info->fd_memory = open(info->name_memory, O_RDONLY)) < 0) {
		ERRMSG("Can't open the dump memory(%s). %s\n",
		    info->name_memory, strerror(errno));
		return FALSE;
	}
	return TRUE;
}

int
get_next_dump_level(int index)
{
	if (info->num_dump_level <= index)
		return -1;

	return info->array_dump_level[index];
}

int
delete_dumpfile(void)
{
	int i;

	if (info->flag_flatten)
		return TRUE;

	if (info->flag_split) {
		for (i = 0; i < info->num_dumpfile; i++)
			unlink(SPLITTING_DUMPFILE(i));
	} else {
		unlink(info->name_dumpfile);
	}
	return TRUE;
}

int
writeout_multiple_dumpfiles(void)
{
	int i, status, ret = TRUE;
	pid_t pid;
	pid_t array_pid[info->num_dumpfile];

	if (!setup_splitting())
		return FALSE;

	for (i = 0; i < info->num_dumpfile; i++) {
		if ((pid = fork()) < 0) {
			return FALSE;

		} else if (pid == 0) { /* Child */
			info->name_dumpfile   = SPLITTING_DUMPFILE(i);
			info->fd_bitmap       = SPLITTING_FD_BITMAP(i);
			info->split_start_pfn = SPLITTING_START_PFN(i);
			info->split_end_pfn   = SPLITTING_END_PFN(i);

			if (!reopen_dump_memory())
				exit(1);
			if ((status = writeout_dumpfile()) == FALSE)
				exit(1);
			else if (status == NOSPACE)
				exit(2);
			exit(0);
		}
		array_pid[i] = pid;
	}
	for (i = 0; i < info->num_dumpfile; i++) {
		waitpid(array_pid[i], &status, WUNTRACED);
		if (!WIFEXITED(status) || WEXITSTATUS(status) == 1) {
			ERRMSG("Child process(%d) finished imcompletely.(%d)\n",
			    array_pid[i], status);
			ret = FALSE;
		} else if ((ret == TRUE) && (WEXITSTATUS(status) == 2))
			ret = NOSPACE;
	}
	return ret;
}

int
create_dumpfile(void)
{
	int num_retry, status, new_level;

	if (!open_files_for_creating_dumpfile())
		return FALSE;

	if (!info->flag_refiltering && !info->flag_sadump) {
		if (!get_elf_info(info->fd_memory, info->name_memory))
			return FALSE;
	}
	if (!initial())
		return FALSE;

	print_vtop();

	num_retry = 0;
retry:
	if (info->flag_refiltering) {
		/* Change dump level */
		new_level = info->dump_level | info->kh_memory->dump_level;
		if (new_level != info->dump_level) {
			info->dump_level = new_level;
			MSG("dump_level is changed to %d, " \
				"because %s was created by dump_level(%d).",
				new_level, info->name_memory,
				info->kh_memory->dump_level);
		}
	}

	if ((info->name_filterconfig || info->name_eppic_config)
			&& !gather_filter_info())
		return FALSE;

	if (!create_dump_bitmap())
		return FALSE;

	if (info->flag_split) {
		if ((status = writeout_multiple_dumpfiles()) == FALSE)
			return FALSE;
	} else {
		if ((status = writeout_dumpfile()) == FALSE)
			return FALSE;
	}
	if (status == NOSPACE) {
		/*
		 * If specifying the other dump_level, makedumpfile tries
		 * to create a dumpfile with it again.
		 */
		num_retry++;
		if ((info->dump_level = get_next_dump_level(num_retry)) < 0)
 			return FALSE;
		MSG("Retry to create a dumpfile by dump_level(%d).\n",
		    info->dump_level);
		if (!delete_dumpfile())
 			return FALSE;
		goto retry;
	}
	print_report();

	clear_filter_info();
	if (!close_files_for_creating_dumpfile())
		return FALSE;

	return TRUE;
}

int
__read_disk_dump_header(struct disk_dump_header *dh, char *filename)
{
	int fd, ret = FALSE;

	if ((fd = open(filename, O_RDONLY)) < 0) {
		ERRMSG("Can't open a file(%s). %s\n",
		    filename, strerror(errno));
		return FALSE;
	}
	if (lseek(fd, 0x0, SEEK_SET) < 0) {
		ERRMSG("Can't seek a file(%s). %s\n",
		    filename, strerror(errno));
		goto out;
	}
	if (read(fd, dh, sizeof(struct disk_dump_header))
	    != sizeof(struct disk_dump_header)) {
		ERRMSG("Can't read a file(%s). %s\n",
		    filename, strerror(errno));
		goto out;
	}
	ret = TRUE;
out:
	close(fd);

	return ret;
}

int
read_disk_dump_header(struct disk_dump_header *dh, char *filename)
{
	if (!__read_disk_dump_header(dh, filename))
		return FALSE;

	if (strncmp(dh->signature, KDUMP_SIGNATURE, strlen(KDUMP_SIGNATURE))) {
		ERRMSG("%s is not the kdump-compressed format.\n",
		    filename);
		return FALSE;
	}
	return TRUE;
}

int
read_kdump_sub_header(struct kdump_sub_header *kh, char *filename)
{
	int fd, ret = FALSE;
	struct disk_dump_header dh;
	off_t offset;

	if (!read_disk_dump_header(&dh, filename))
		return FALSE;

	offset = DISKDUMP_HEADER_BLOCKS * dh.block_size;

	if ((fd = open(filename, O_RDONLY)) < 0) {
		ERRMSG("Can't open a file(%s). %s\n",
		    filename, strerror(errno));
		return FALSE;
	}
	if (lseek(fd, offset, SEEK_SET) < 0) {
		ERRMSG("Can't seek a file(%s). %s\n",
		    filename, strerror(errno));
		goto out;
	}
	if (read(fd, kh, sizeof(struct kdump_sub_header))
	     != sizeof(struct kdump_sub_header)) {
		ERRMSG("Can't read a file(%s). %s\n",
		    filename, strerror(errno));
		goto out;
	}
	ret = TRUE;
out:
	close(fd);

	return ret;
}

int
store_splitting_info(void)
{
	int i;
	struct disk_dump_header dh, tmp_dh;
	struct kdump_sub_header kh;

	for (i = 0; i < info->num_dumpfile; i++) {
		if (!read_disk_dump_header(&tmp_dh, SPLITTING_DUMPFILE(i)))
			return FALSE;

		if (i == 0) {
			memcpy(&dh, &tmp_dh, sizeof(tmp_dh));
			if (!set_page_size(dh.block_size))
				return FALSE;
			DEBUG_MSG("page_size    : %ld\n", info->page_size);
		}

		/*
		 * Check whether multiple dumpfiles are parts of
		 * the same /proc/vmcore.
		 */
		if (memcmp(&dh, &tmp_dh, sizeof(tmp_dh))) {
			ERRMSG("Invalid dumpfile(%s).\n",
			    SPLITTING_DUMPFILE(i));
			return FALSE;
		}
		if (!read_kdump_sub_header(&kh, SPLITTING_DUMPFILE(i)))
			return FALSE;

		if (i == 0) {
			if (dh.header_version >= 6)
				info->max_mapnr = kh.max_mapnr_64;
			else
				info->max_mapnr = dh.max_mapnr;

			DEBUG_MSG("max_mapnr    : %llx\n", info->max_mapnr);

			info->dump_level = kh.dump_level;
			DEBUG_MSG("dump_level   : %d\n", info->dump_level);
		}

		if (dh.header_version >= 6) {
			SPLITTING_START_PFN(i) = kh.start_pfn_64;
			SPLITTING_END_PFN(i)   = kh.end_pfn_64;
		} else {
			SPLITTING_START_PFN(i) = kh.start_pfn;
			SPLITTING_END_PFN(i)   = kh.end_pfn;
		}
		SPLITTING_OFFSET_EI(i) = kh.offset_eraseinfo;
		SPLITTING_SIZE_EI(i)   = kh.size_eraseinfo;
	}
	return TRUE;
}

void
sort_splitting_info(void)
{
	int i, j;
	mdf_pfn_t start_pfn, end_pfn;
	char *name_dumpfile;

	/*
	 * Sort splitting_info by start_pfn.
	 */
	for (i = 0; i < (info->num_dumpfile - 1); i++) {
		for (j = i; j < info->num_dumpfile; j++) {
			if (SPLITTING_START_PFN(i) < SPLITTING_START_PFN(j))
				continue;
			start_pfn     = SPLITTING_START_PFN(i);
			end_pfn       = SPLITTING_END_PFN(i);
			name_dumpfile = SPLITTING_DUMPFILE(i);

			SPLITTING_START_PFN(i) = SPLITTING_START_PFN(j);
			SPLITTING_END_PFN(i)   = SPLITTING_END_PFN(j);
			SPLITTING_DUMPFILE(i)  = SPLITTING_DUMPFILE(j);

			SPLITTING_START_PFN(j) = start_pfn;
			SPLITTING_END_PFN(j)   = end_pfn;
			SPLITTING_DUMPFILE(j)  = name_dumpfile;
		}
	}

	DEBUG_MSG("num_dumpfile : %d\n", info->num_dumpfile);
	for (i = 0; i < info->num_dumpfile; i++) {
		DEBUG_MSG("dumpfile (%s)\n", SPLITTING_DUMPFILE(i));
		DEBUG_MSG("  start_pfn  : %llx\n", SPLITTING_START_PFN(i));
		DEBUG_MSG("  end_pfn    : %llx\n", SPLITTING_END_PFN(i));
	}
}

int
check_splitting_info(void)
{
	int i;
	mdf_pfn_t end_pfn;

	/*
	 * Check whether there are not lack of /proc/vmcore.
	 */
	if (SPLITTING_START_PFN(0) != 0) {
		ERRMSG("There is not dumpfile corresponding to pfn 0x%x - 0x%llx.\n",
		    0x0, SPLITTING_START_PFN(0));
		return FALSE;
	}
	end_pfn = SPLITTING_END_PFN(0);

	for (i = 1; i < info->num_dumpfile; i++) {
		if (end_pfn != SPLITTING_START_PFN(i)) {
			ERRMSG("There is not dumpfile corresponding to pfn 0x%llx - 0x%llx.\n",
			    end_pfn, SPLITTING_START_PFN(i));
			return FALSE;
		}
		end_pfn = SPLITTING_END_PFN(i);
	}
	if (end_pfn != info->max_mapnr) {
		ERRMSG("There is not dumpfile corresponding to pfn 0x%llx - 0x%llx.\n",
		    end_pfn, info->max_mapnr);
		return FALSE;
	}

	return TRUE;
}

int
get_splitting_info(void)
{
	if (!store_splitting_info())
		return FALSE;

	sort_splitting_info();

	if (!check_splitting_info())
		return FALSE;

	if (!get_kdump_compressed_header_info(SPLITTING_DUMPFILE(0)))
		return FALSE;

	return TRUE;
}

int
copy_same_data(int src_fd, int dst_fd, off_t offset, unsigned long size)
{
	int ret = FALSE;
	char *buf = NULL;

	if ((buf = malloc(size)) == NULL) {
		ERRMSG("Can't allocate memory.\n");
		return FALSE;
	}
	if (lseek(src_fd, offset, SEEK_SET) < 0) {
		ERRMSG("Can't seek a source file. %s\n", strerror(errno));
		goto out;
	}
	if (read(src_fd, buf, size) != size) {
		ERRMSG("Can't read a source file. %s\n", strerror(errno));
		goto out;
	}
	if (lseek(dst_fd, offset, SEEK_SET) < 0) {
		ERRMSG("Can't seek a destination file. %s\n", strerror(errno));
		goto out;
	}
	if (write(dst_fd, buf, size) != size) {
		ERRMSG("Can't write a destination file. %s\n", strerror(errno));
		goto out;
	}
	ret = TRUE;
out:
	free(buf);
	return ret;
}

int
reassemble_kdump_header(void)
{
	int fd = -1, ret = FALSE;
	off_t offset;
	unsigned long size;
	struct disk_dump_header dh;
	struct kdump_sub_header kh;
	char *buf_bitmap = NULL;
	ssize_t status, read_size, written_size;

	/*
	 * Write common header.
	 */
	if (!read_disk_dump_header(&dh, SPLITTING_DUMPFILE(0)))
		return FALSE;

	if (lseek(info->fd_dumpfile, 0x0, SEEK_SET) < 0) {
		ERRMSG("Can't seek a file(%s). %s\n",
		    info->name_dumpfile, strerror(errno));
		return FALSE;
	}
	if (write(info->fd_dumpfile, &dh, sizeof(dh)) != sizeof(dh)) {
		ERRMSG("Can't write a file(%s). %s\n",
		    info->name_dumpfile, strerror(errno));
		return FALSE;
	}

	/*
	 * Write sub header.
	 */
	if (!read_kdump_sub_header(&kh, SPLITTING_DUMPFILE(0)))
		return FALSE;

	kh.split = 0;
	kh.start_pfn = 0;
	kh.end_pfn   = 0;
	kh.start_pfn_64 = 0;
	kh.end_pfn_64 = 0;

	if (lseek(info->fd_dumpfile, info->page_size, SEEK_SET) < 0) {
		ERRMSG("Can't seek a file(%s). %s\n",
		    info->name_dumpfile, strerror(errno));
		return FALSE;
	}
	if (write(info->fd_dumpfile, &kh, sizeof(kh)) != sizeof(kh)) {
		ERRMSG("Can't write a file(%s). %s\n",
		    info->name_dumpfile, strerror(errno));
		return FALSE;
	}
	memcpy(&info->sub_header, &kh, sizeof(kh));

	if ((fd = open(SPLITTING_DUMPFILE(0), O_RDONLY)) < 0) {
		ERRMSG("Can't open a file(%s). %s\n",
		    SPLITTING_DUMPFILE(0), strerror(errno));
		return FALSE;
	}
	if (has_pt_note()) {
		get_pt_note(&offset, &size);
		if (!copy_same_data(fd, info->fd_dumpfile, offset, size)) {
			ERRMSG("Can't copy pt_note data to %s.\n",
			    info->name_dumpfile);
			goto out;
		}
	}
	if (has_vmcoreinfo()) {
		get_vmcoreinfo(&offset, &size);
		if (!copy_same_data(fd, info->fd_dumpfile, offset, size)) {
			ERRMSG("Can't copy vmcoreinfo data to %s.\n",
			    info->name_dumpfile);
			goto out;
		}
	}

	/*
	 * Write dump bitmap to both a dumpfile and a bitmap file.
	 */
	offset = (DISKDUMP_HEADER_BLOCKS + dh.sub_hdr_size) * dh.block_size;
	info->len_bitmap = dh.bitmap_blocks * dh.block_size;
	if ((buf_bitmap = malloc(info->len_bitmap)) == NULL) {
		ERRMSG("Can't allocate memory for bitmap.\n");
		goto out;
	}
	if (lseek(fd, offset, SEEK_SET) < 0) {
		ERRMSG("Can't seek a file(%s). %s\n",
		    SPLITTING_DUMPFILE(0), strerror(errno));
		goto out;
	}
	read_size = 0;
	while (read_size < info->len_bitmap) {
		status = read(fd, buf_bitmap + read_size, info->len_bitmap
			- read_size);
		if (status < 0) {
			ERRMSG("Can't read a file(%s). %s\n",
				SPLITTING_DUMPFILE(0), strerror(errno));
			goto out;
		}
		read_size += status;
	}

	if (lseek(info->fd_dumpfile, offset, SEEK_SET) < 0) {
		ERRMSG("Can't seek a file(%s). %s\n",
		    info->name_dumpfile, strerror(errno));
		goto out;
	}
	written_size = 0;
	while (written_size < info->len_bitmap) {
		status = write(info->fd_dumpfile, buf_bitmap + written_size,
			info->len_bitmap - written_size);
		if (status < 0) {
			ERRMSG("Can't write a file(%s). %s\n",
			    info->name_dumpfile, strerror(errno));
			goto out;
		}
		written_size += status;
	}

	if (lseek(info->fd_bitmap, 0x0, SEEK_SET) < 0) {
		ERRMSG("Can't seek a file(%s). %s\n",
		    info->name_bitmap, strerror(errno));
		goto out;
	}
	written_size = 0;
	while (written_size < info->len_bitmap) {
		status = write(info->fd_bitmap, buf_bitmap + written_size,
			info->len_bitmap - written_size);
		if (status < 0) {
			ERRMSG("Can't write a file(%s). %s\n",
			    info->name_bitmap, strerror(errno));
			goto out;
		}
		written_size += status;
	}

	ret = TRUE;
out:
	if (fd > 0)
		close(fd);
	free(buf_bitmap);

	return ret;
}

int
reassemble_kdump_pages(void)
{
	int i, fd = 0, ret = FALSE;
	off_t offset_first_ph, offset_ph_org, offset_eraseinfo;
	off_t offset_data_new, offset_zero_page = 0;
	mdf_pfn_t pfn, start_pfn, end_pfn;
	mdf_pfn_t num_dumpable;
	unsigned long size_eraseinfo;
	struct dump_bitmap bitmap2;
	struct disk_dump_header dh;
	struct page_desc pd, pd_zero;
	struct cache_data cd_pd, cd_data;
	struct timeval tv_start;
	char *data = NULL;
	unsigned long data_buf_size = info->page_size;

	initialize_2nd_bitmap(&bitmap2);

	if (!read_disk_dump_header(&dh, SPLITTING_DUMPFILE(0)))
		return FALSE;

	if (!prepare_cache_data(&cd_pd))
		return FALSE;

	if (!prepare_cache_data(&cd_data)) {
		free_cache_data(&cd_pd);
		return FALSE;
	}
	if ((data = malloc(data_buf_size)) == NULL) {
		ERRMSG("Can't allocate memory for page data.\n");
		free_cache_data(&cd_pd);
		free_cache_data(&cd_data);
		return FALSE;
	}
	num_dumpable = get_num_dumpable();
	num_dumped = 0;

	offset_first_ph
	    = (DISKDUMP_HEADER_BLOCKS + dh.sub_hdr_size + dh.bitmap_blocks)
		* dh.block_size;
	cd_pd.offset    = offset_first_ph;
	offset_data_new = offset_first_ph + sizeof(page_desc_t) * num_dumpable;
	cd_data.offset  = offset_data_new;

	/*
	 * Write page header of zero-filled page.
	 */
	gettimeofday(&tv_start, NULL);
	if (info->dump_level & DL_EXCLUDE_ZERO) {
		/*
		 * makedumpfile outputs the data of zero-filled page at first
		 * if excluding zero-filled page, so the offset of first data
		 * is for zero-filled page in all dumpfiles.
		 */
		offset_zero_page = offset_data_new;

		pd_zero.size = info->page_size;
		pd_zero.flags = 0;
		pd_zero.offset = offset_data_new;
		pd_zero.page_flags = 0;
		memset(data, 0, pd_zero.size);
		if (!write_cache(&cd_data, data, pd_zero.size))
			goto out;
		offset_data_new  += pd_zero.size;
	}

	for (i = 0; i < info->num_dumpfile; i++) {
		if ((fd = open(SPLITTING_DUMPFILE(i), O_RDONLY)) < 0) {
			ERRMSG("Can't open a file(%s). %s\n",
			    SPLITTING_DUMPFILE(i), strerror(errno));
			goto out;
		}
		start_pfn = SPLITTING_START_PFN(i);
		end_pfn   = SPLITTING_END_PFN(i);

		offset_ph_org = offset_first_ph;
		for (pfn = start_pfn; pfn < end_pfn; pfn++) {
			if (!is_dumpable(&bitmap2, pfn))
				continue;

			num_dumped++;

			print_progress(PROGRESS_COPY, num_dumped, num_dumpable);

			if (lseek(fd, offset_ph_org, SEEK_SET) < 0) {
				ERRMSG("Can't seek a file(%s). %s\n",
				    SPLITTING_DUMPFILE(i), strerror(errno));
				goto out;
			}
			if (read(fd, &pd, sizeof(pd)) != sizeof(pd)) {
				ERRMSG("Can't read a file(%s). %s\n",
				    SPLITTING_DUMPFILE(i), strerror(errno));
				goto out;
			}
			if (lseek(fd, pd.offset, SEEK_SET) < 0) {
				ERRMSG("Can't seek a file(%s). %s\n",
				    SPLITTING_DUMPFILE(i), strerror(errno));
				goto out;
			}
			if (read(fd, data, pd.size) != pd.size) {
				ERRMSG("Can't read a file(%s). %s\n",
				    SPLITTING_DUMPFILE(i), strerror(errno));
				goto out;
			}
			if ((info->dump_level & DL_EXCLUDE_ZERO)
			    && (pd.offset == offset_zero_page)) {
				/*
			 	 * Handle the data of zero-filled page.
				 */
				if (!write_cache(&cd_pd, &pd_zero,
				    sizeof(pd_zero)))
					goto out;
				offset_ph_org += sizeof(pd);
				continue;
			}
			pd.offset = offset_data_new;
			if (!write_cache(&cd_pd, &pd, sizeof(pd)))
				goto out;
			offset_ph_org += sizeof(pd);

			if (!write_cache(&cd_data, data, pd.size))
				goto out;

			offset_data_new += pd.size;
		}
		close(fd);
		fd = 0;
	}
	if (!write_cache_bufsz(&cd_pd))
		goto out;
	if (!write_cache_bufsz(&cd_data))
		goto out;

	offset_eraseinfo = cd_data.offset;
	size_eraseinfo   = 0;
	/* Copy eraseinfo from split dumpfiles to o/p dumpfile */
	for (i = 0; i < info->num_dumpfile; i++) {
		if (!SPLITTING_SIZE_EI(i))
			continue;

		if (SPLITTING_SIZE_EI(i) > data_buf_size) {
			data_buf_size = SPLITTING_SIZE_EI(i);
			if ((data = realloc(data, data_buf_size)) == NULL) {
				ERRMSG("Can't allocate memory for eraseinfo"
					" data.\n");
				goto out;
			}
		}
		if ((fd = open(SPLITTING_DUMPFILE(i), O_RDONLY)) < 0) {
			ERRMSG("Can't open a file(%s). %s\n",
			    SPLITTING_DUMPFILE(i), strerror(errno));
			goto out;
		}
		if (lseek(fd, SPLITTING_OFFSET_EI(i), SEEK_SET) < 0) {
			ERRMSG("Can't seek a file(%s). %s\n",
			    SPLITTING_DUMPFILE(i), strerror(errno));
			goto out;
		}
		if (read(fd, data, SPLITTING_SIZE_EI(i)) !=
						SPLITTING_SIZE_EI(i)) {
			ERRMSG("Can't read a file(%s). %s\n",
			    SPLITTING_DUMPFILE(i), strerror(errno));
			goto out;
		}
		if (!write_cache(&cd_data, data, SPLITTING_SIZE_EI(i)))
			goto out;
		size_eraseinfo += SPLITTING_SIZE_EI(i);

		close(fd);
		fd = 0;
	}
	if (size_eraseinfo) {
		if (!write_cache_bufsz(&cd_data))
			goto out;

		if (!update_eraseinfo_of_sub_header(offset_eraseinfo,
						    size_eraseinfo))
			goto out;
	}
	print_progress(PROGRESS_COPY, num_dumpable, num_dumpable);
	print_execution_time(PROGRESS_COPY, &tv_start);

	ret = TRUE;
out:
	free_cache_data(&cd_pd);
	free_cache_data(&cd_data);

	if (data)
		free(data);
	if (fd > 0)
		close(fd);

	return ret;
}

int
reassemble_dumpfile(void)
{
	if (!get_splitting_info())
		return FALSE;

	if (!open_dump_bitmap())
		return FALSE;

	if (!open_dump_file())
		return FALSE;

	if (!reassemble_kdump_header())
		return FALSE;

	if (!reassemble_kdump_pages())
		return FALSE;

	close_dump_file();
	close_dump_bitmap();

	return TRUE;
}

int
check_param_for_generating_vmcoreinfo(int argc, char *argv[])
{
	if (argc != optind)
		return FALSE;

	if (info->flag_compress        || info->dump_level
	    || info->flag_elf_dumpfile || info->flag_read_vmcoreinfo
	    || info->flag_flatten      || info->flag_rearrange
	    || info->flag_exclude_xen_dom
	    || (!info->name_vmlinux && !info->name_xen_syms))

		return FALSE;

	return TRUE;
}

/*
 * Parameters for creating dumpfile from the dump data
 * of flattened format by rearranging the dump data.
 */
int
check_param_for_rearranging_dumpdata(int argc, char *argv[])
{
	if (argc != optind + 1)
		return FALSE;

	if (info->flag_compress        || info->dump_level
	    || info->flag_elf_dumpfile || info->flag_read_vmcoreinfo
	    || info->name_vmlinux      || info->name_xen_syms
	    || info->flag_flatten      || info->flag_generate_vmcoreinfo
	    || info->flag_exclude_xen_dom)
		return FALSE;

	info->name_dumpfile = argv[optind];
	return TRUE;
}

/*
 * Parameters for reassembling multiple dumpfiles into one dumpfile.
 */
int
check_param_for_reassembling_dumpfile(int argc, char *argv[])
{
	int i;

	info->num_dumpfile  = argc - optind - 1;
	info->name_dumpfile = argv[argc - 1];

	DEBUG_MSG("num_dumpfile : %d\n", info->num_dumpfile);

	if (info->flag_compress        || info->dump_level
	    || info->flag_elf_dumpfile || info->flag_read_vmcoreinfo
	    || info->name_vmlinux      || info->name_xen_syms
	    || info->flag_flatten      || info->flag_generate_vmcoreinfo
	    || info->flag_exclude_xen_dom || info->flag_split)
		return FALSE;

	if ((info->splitting_info
	    = malloc(sizeof(splitting_info_t) * info->num_dumpfile))
	    == NULL) {
		MSG("Can't allocate memory for splitting_info.\n");
		return FALSE;
	}
	for (i = 0; i < info->num_dumpfile; i++)
		SPLITTING_DUMPFILE(i) = argv[optind + i];

	return TRUE;
}

/*
 * Check parameters to create the dump file.
 */
int
check_param_for_creating_dumpfile(int argc, char *argv[])
{
	int i;

	if (info->flag_generate_vmcoreinfo || info->flag_rearrange)
		return FALSE;

	if ((message_level < MIN_MSG_LEVEL)
	    || (MAX_MSG_LEVEL < message_level)) {
		message_level = DEFAULT_MSG_LEVEL;
		MSG("Message_level is invalid.\n");
		return FALSE;
	}
	if ((info->flag_compress && info->flag_elf_dumpfile)
	    || (info->flag_read_vmcoreinfo && info->name_vmlinux)
	    || (info->flag_read_vmcoreinfo && info->name_xen_syms))
		return FALSE;

	if (info->flag_flatten && info->flag_split)
		return FALSE;

	if (info->name_filterconfig && !info->name_vmlinux)
		return FALSE;

	if (info->flag_sadump_diskset && !sadump_is_supported_arch())
		return FALSE;

	if ((argc == optind + 2) && !info->flag_flatten
				 && !info->flag_split
				 && !info->flag_sadump_diskset) {
		/*
		 * Parameters for creating the dumpfile from vmcore.
		 */
		info->name_memory   = argv[optind];
		info->name_dumpfile = argv[optind+1];

	} else if (info->flag_split && (info->flag_sadump_diskset
					? (argc >= optind + 2)
					: (argc > optind + 2))) {
		int num_vmcore;

		/*
		 * Parameters for creating multiple dumpfiles from vmcore.
		 */
		if (info->flag_sadump_diskset) {
			num_vmcore = 0;
			info->name_memory = sadump_head_disk_name_memory();
		} else {
			num_vmcore = 1;
			info->name_memory = argv[optind];
		}
		info->num_dumpfile = argc - optind - num_vmcore;

		if (info->flag_elf_dumpfile) {
			MSG("Options for splitting dumpfile cannot be used with Elf format.\n");
			return FALSE;
		}
		if ((info->splitting_info
		    = malloc(sizeof(splitting_info_t) * info->num_dumpfile))
		    == NULL) {
			MSG("Can't allocate memory for splitting_info.\n");
			return FALSE;
		}
		for (i = 0; i < info->num_dumpfile; i++)
			SPLITTING_DUMPFILE(i) = argv[optind + num_vmcore + i];

	} else if ((argc == optind + 1) && !info->flag_split
					&& info->flag_sadump_diskset) {
		info->name_dumpfile = argv[optind];
		info->name_memory = sadump_head_disk_name_memory();

		DEBUG_MSG("name_dumpfile: %s\n", info->name_dumpfile);
		DEBUG_MSG("name_memory: %s\n", info->name_memory);

	} else if ((argc == optind + 1) && info->flag_flatten) {
		/*
		 * Parameters for outputting the dump data of the
		 * flattened format to STDOUT.
		 */
		info->name_memory   = argv[optind];

	} else if ((argc == optind + 1) && info->flag_mem_usage) {
		/*
		* Parameter for showing the page number of memory
		* in different use from.
		*/
		info->name_memory   = argv[optind];

	} else
		return FALSE;

	return TRUE;
}

int
parse_dump_level(char *str_dump_level)
{
	int i, ret = FALSE;
	char *buf, *ptr;

	if (!(buf = strdup(str_dump_level))) {
		MSG("Can't duplicate strings(%s).\n", str_dump_level);
		return FALSE;
	}
	info->max_dump_level = 0;
	info->num_dump_level = 0;
	ptr = buf;
	while(TRUE) {
		ptr = strtok(ptr, ",");
		if (!ptr)
			break;

		i = atoi(ptr);
		if ((i < MIN_DUMP_LEVEL) || (MAX_DUMP_LEVEL < i)) {
			MSG("Dump_level(%d) is invalid.\n", i);
			goto out;
		}
		if (NUM_ARRAY_DUMP_LEVEL <= info->num_dump_level) {
			MSG("Dump_level is invalid.\n");
			goto out;
		}
		if (info->max_dump_level < i)
			info->max_dump_level = i;
		if (info->num_dump_level == 0)
			info->dump_level = i;
		info->array_dump_level[info->num_dump_level] = i;
		info->num_dump_level++;
		ptr = NULL;
	}
	ret = TRUE;
out:
	free(buf);

	return ret;
}

/*
 * Get the amount of free memory from /proc/meminfo.
 */
unsigned long long
get_free_memory_size(void) {
	char buf[BUFSIZE_FGETS];
	char unit[4];
	unsigned long long free_size = 0;
	char *name_meminfo = "/proc/meminfo";
	FILE *file_meminfo;

	if ((file_meminfo = fopen(name_meminfo, "r")) == NULL) {
		ERRMSG("Can't open the %s. %s\n", name_meminfo, strerror(errno));
		return FALSE;
	}

	while (fgets(buf, BUFSIZE_FGETS, file_meminfo) != NULL) {
		if (sscanf(buf, "MemFree: %llu %s", &free_size, unit) == 2) {
			if (strcmp(unit, "kB") == 0) {
				free_size *= 1024;
				goto out;
			}
		}
	}

	ERRMSG("Can't get free memory size.\n");
	free_size = 0;
out:
	if (fclose(file_meminfo) < 0)
		ERRMSG("Can't close the %s. %s\n", name_meminfo, strerror(errno));

	return free_size;
}


/*
 * Choose the lesser value of the two below as the size of cyclic buffer.
 *  - the size enough for storing the 1st/2nd bitmap for the whole of vmcore
 *  - 80% of free memory (as safety limit)
 */
int
calculate_cyclic_buffer_size(void) {
	unsigned long long limit_size, bitmap_size;

	if (info->max_mapnr <= 0) {
		ERRMSG("Invalid max_mapnr(%llu).\n", info->max_mapnr);
		return FALSE;
	}

	/*
	 *  We should keep the size of cyclic buffer within 80% of free memory
	 *  for safety.
	 */
	limit_size = get_free_memory_size() * 0.8;
	bitmap_size = info->max_mapnr / BITPERBYTE;

	/* if --split was specified cyclic buffer allocated per dump file */
	if (info->num_dumpfile > 1)
		bitmap_size /= info->num_dumpfile;

	info->bufsize_cyclic = MIN(limit_size, bitmap_size);

	return TRUE;
}



/* #define CRASH_RESERVED_MEM_NR   8 */
struct memory_range crash_reserved_mem[CRASH_RESERVED_MEM_NR];
int crash_reserved_mem_nr;

/*
 * iomem_for_each_line()
 *
 * Iterate over each line in the file returned by proc_iomem(). If match is
 * NULL or if the line matches with our match-pattern then call the
 * callback if non-NULL.
 *
 * Return the number of lines matched.
 */
int iomem_for_each_line(char *match,
			      int (*callback)(void *data,
					      int nr,
					      char *str,
					      unsigned long base,
					      unsigned long length),
			      void *data)
{
	const char iomem[] = "/proc/iomem";
	char line[BUFSIZE_FGETS];
	FILE *fp;
	unsigned long long start, end, size;
	char *str;
	int consumed;
	int count;
	int nr = 0;

	fp = fopen(iomem, "r");
	if (!fp) {
		ERRMSG("Cannot open %s\n", iomem);
		return nr;
	}

	while (fgets(line, sizeof(line), fp) != 0) {
		count = sscanf(line, "%Lx-%Lx : %n", &start, &end, &consumed);
		if (count != 2)
			continue;
		str = line + consumed;
		size = end - start + 1;
		if (!match || memcmp(str, match, strlen(match)) == 0) {
			if (callback
			    && callback(data, nr, str, start, size) < 0) {
				break;
			}
			nr++;
		}
	}

	fclose(fp);

	return nr;
}

static int crashkernel_mem_callback(void *data, int nr,
					  char *str,
					  unsigned long base,
					  unsigned long length)
{
	if (nr >= CRASH_RESERVED_MEM_NR)
		return 1;

	crash_reserved_mem[nr].start = base;
	crash_reserved_mem[nr].end   = base + length - 1;
	return 0;
}

int is_crashkernel_mem_reserved(void)
{
	int ret;

	ret = iomem_for_each_line("Crash kernel\n",
					crashkernel_mem_callback, NULL);
	crash_reserved_mem_nr = ret;

	return !!crash_reserved_mem_nr;
}

int get_page_offset(void)
{
	struct utsname utsname;
	if (uname(&utsname)) {
		ERRMSG("Cannot get name and information about current kernel : %s",
		       strerror(errno));
		return FALSE;
	}

	info->kernel_version = get_kernel_version(utsname.release);
	get_versiondep_info();

	return TRUE;
}


/* Returns the physical address of start of crash notes buffer for a kernel. */
int get_sys_kernel_vmcoreinfo(uint64_t *addr, uint64_t *len)
{
	char line[BUFSIZE_FGETS];
	int count;
	FILE *fp;
	unsigned long long temp, temp2;

	*addr = 0;
	*len = 0;

	if (!(fp = fopen("/sys/kernel/vmcoreinfo", "r")))
		return FALSE;

	if (!fgets(line, sizeof(line), fp)) {
		ERRMSG("Cannot parse %s: %s, fgets failed.\n",
		       "/sys/kernel/vmcoreinfo", strerror(errno));
		return FALSE;
	}
	count = sscanf(line, "%Lx %Lx", &temp, &temp2);
	if (count != 2) {
		ERRMSG("Cannot parse %s: %s, sscanf failed.\n",
		       "/sys/kernel/vmcoreinfo", strerror(errno));
		return FALSE;
	}

	*addr = (uint64_t) temp;
	*len = (uint64_t) temp2;

	fclose(fp);
	return TRUE;
}

int show_mem_usage(void)
{
	uint64_t vmcoreinfo_addr, vmcoreinfo_len;

	if (!is_crashkernel_mem_reserved()) {
		ERRMSG("No memory is reserved for crashkenrel!\n");
		return FALSE;
	}


	if (!info->flag_cyclic)
		info->flag_cyclic = TRUE;

	info->dump_level = MAX_DUMP_LEVEL;

	if (!get_page_offset())
		return FALSE;

	if (!open_dump_memory())
		return FALSE;

	if (!get_elf_loads(info->fd_memory, info->name_memory))
		return FALSE;

	if (!get_sys_kernel_vmcoreinfo(&vmcoreinfo_addr, &vmcoreinfo_len))
		return FALSE;

	if (!set_kcore_vmcoreinfo(vmcoreinfo_addr, vmcoreinfo_len))
		return FALSE;

	if (!get_kcore_dump_loads())
		return FALSE;

	if (!initial())
		return FALSE;


	if (!prepare_bitmap2_buffer_cyclic())
		return FALSE;

	info->num_dumpable = get_num_dumpable_cyclic();

	free_bitmap2_buffer_cyclic();

	print_mem_usage();

	if (!close_files_for_creating_dumpfile())
		return FALSE;

	return TRUE;
}
