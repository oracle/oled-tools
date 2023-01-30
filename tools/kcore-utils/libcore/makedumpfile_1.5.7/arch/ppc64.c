/*
 * ppc64.c
 *
 * Created by: Sachin Sant (sachinp@in.ibm.com)
 * Copyright (C) IBM Corporation, 2006. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation (version 2 of the License).
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef __powerpc64__

#include "../print_info.h"
#include "../elf_info.h"
#include "../makedumpfile.h"

/*
 * This function traverses vmemmap list to get the count of vmemmap regions
 * and populates the regions' info in info->vmemmap_list[]
 */
static int
get_vmemmap_list_info(ulong head)
{
	int   i, cnt;
	long  backing_size, virt_addr_offset, phys_offset, list_offset;
	ulong curr, next;
	char  *vmemmap_buf = NULL;

	backing_size		= SIZE(vmemmap_backing);
	virt_addr_offset	= OFFSET(vmemmap_backing.virt_addr);
	phys_offset		= OFFSET(vmemmap_backing.phys);
	list_offset		= OFFSET(vmemmap_backing.list);
	info->vmemmap_list = NULL;

	/*
	 * Get list count by traversing the vmemmap list
	 */
	cnt = 0;
	curr = head;
	next = 0;
	do {
		if (!readmem(VADDR, (curr + list_offset), &next,
			     sizeof(next))) {
			ERRMSG("Can't get vmemmap region addresses\n");
			goto err;
		}
		curr = next;
		cnt++;
	} while ((next != 0) && (next != head));

	/*
	 * Using temporary buffer to save vmemmap region information
	 */
	vmemmap_buf = calloc(1, backing_size);
	if (vmemmap_buf == NULL) {
		ERRMSG("Can't allocate memory for vmemmap_buf. %s\n",
		       strerror(errno));
		goto err;
	}

	info->vmemmap_list = calloc(1, cnt * sizeof(struct ppc64_vmemmap));
	if (info->vmemmap_list == NULL) {
		ERRMSG("Can't allocate memory for vmemmap_list. %s\n",
		       strerror(errno));
		goto err;
	}

	curr = head;
	for (i = 0; i < cnt; i++) {
		if (!readmem(VADDR, curr, vmemmap_buf, backing_size)) {
			ERRMSG("Can't get vmemmap region info\n");
			goto err;
		}

		info->vmemmap_list[i].phys = ULONG(vmemmap_buf + phys_offset);
		info->vmemmap_list[i].virt = ULONG(vmemmap_buf +
						   virt_addr_offset);
		curr = ULONG(vmemmap_buf + list_offset);

		if (info->vmemmap_list[i].virt < info->vmemmap_start)
			info->vmemmap_start = info->vmemmap_list[i].virt;

		if ((info->vmemmap_list[i].virt + info->vmemmap_psize) >
		    info->vmemmap_end)
			info->vmemmap_end = (info->vmemmap_list[i].virt +
					     info->vmemmap_psize);
	}

	free(vmemmap_buf);
	return cnt;
err:
	free(vmemmap_buf);
	free(info->vmemmap_list);
	return 0;
}

/*
 *  Verify that the kernel has made the vmemmap list available,
 *  and if so, stash the relevant data required to make vtop
 *  translations.
 */
static int
ppc64_vmemmap_init(void)
{
	int psize, shift;
	ulong head;

	if ((SYMBOL(vmemmap_list) == NOT_FOUND_SYMBOL)
	    || (SYMBOL(mmu_psize_defs) == NOT_FOUND_SYMBOL)
	    || (SYMBOL(mmu_vmemmap_psize) == NOT_FOUND_SYMBOL)
	    || (SIZE(vmemmap_backing) == NOT_FOUND_STRUCTURE)
	    || (SIZE(mmu_psize_def) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(mmu_psize_def.shift) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(vmemmap_backing.phys) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(vmemmap_backing.virt_addr) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(vmemmap_backing.list) == NOT_FOUND_STRUCTURE))
		return FALSE;

	if (!readmem(VADDR, SYMBOL(mmu_vmemmap_psize), &psize, sizeof(int)))
		return FALSE;

	if (!readmem(VADDR, SYMBOL(mmu_psize_defs) +
		     (SIZE(mmu_psize_def) * psize) +
		     OFFSET(mmu_psize_def.shift), &shift, sizeof(int)))
		return FALSE;
	info->vmemmap_psize = 1 << shift;

	if (!readmem(VADDR, SYMBOL(vmemmap_list), &head, sizeof(unsigned long)))
		return FALSE;

	/*
	 * Get vmemmap list count and populate vmemmap regions info
	 */
	info->vmemmap_cnt = get_vmemmap_list_info(head);
	if (info->vmemmap_cnt == 0)
		return FALSE;

	info->flag_vmemmap = TRUE;
	return TRUE;
}

static int
ppc64_vmalloc_init(void)
{
	if (info->page_size == 65536) {
		/*
		 * 64K pagesize
		 */
		if (info->kernel_version >= KERNEL_VERSION(3, 10, 0)) {
			info->l1_index_size = PTE_INDEX_SIZE_L4_64K_3_10;
			info->l2_index_size = PMD_INDEX_SIZE_L4_64K_3_10;
			info->l3_index_size = PUD_INDEX_SIZE_L4_64K;
		} else {
			info->l1_index_size = PTE_INDEX_SIZE_L4_64K;
			info->l2_index_size = PMD_INDEX_SIZE_L4_64K;
			info->l3_index_size = PUD_INDEX_SIZE_L4_64K;
		}

		info->pte_shift = SYMBOL(demote_segment_4k) ?
			PTE_SHIFT_L4_64K_V2 : PTE_SHIFT_L4_64K_V1;
		info->l2_masked_bits = PMD_MASKED_BITS_64K;
	} else {
		/*
		 * 4K pagesize
		 */
		info->l1_index_size = PTE_INDEX_SIZE_L4_4K;
		info->l2_index_size = PMD_INDEX_SIZE_L4_4K;
		info->l3_index_size = PUD_INDEX_SIZE_L4_4K;

		info->pte_shift = PTE_SHIFT_L4_4K;
		info->l2_masked_bits = PMD_MASKED_BITS_4K;
	}

	/*
	 * Compute ptrs per each level
	 */
	info->l1_shift = info->page_shift;
	info->ptrs_per_l1 = (1 << info->l1_index_size);
	info->ptrs_per_l2 = (1 << info->l2_index_size);
	info->ptrs_per_l3 = (1 << info->l3_index_size);

	info->ptrs_per_pgd = info->ptrs_per_l3;

	/*
	 * Compute shifts
	 */
	info->l2_shift = info->l1_shift + info->l1_index_size;
	info->l3_shift = info->l2_shift + info->l2_index_size;
	info->l4_shift = info->l3_shift + info->l3_index_size;

	return TRUE;
}

/*
 *  If the vmemmap address translation information is stored in the kernel,
 *  make the translation.
 */
static unsigned long long
ppc64_vmemmap_to_phys(unsigned long vaddr)
{
	int	i;
	ulong	offset;
	unsigned long long paddr = NOT_PADDR;

	for (i = 0; i < info->vmemmap_cnt; i++) {
		if ((vaddr >= info->vmemmap_list[i].virt) && (vaddr <
		    (info->vmemmap_list[i].virt + info->vmemmap_psize))) {
			offset = vaddr - info->vmemmap_list[i].virt;
			paddr = info->vmemmap_list[i].phys + offset;
			break;
		}
	}

	return paddr;
}

static unsigned long long
ppc64_vtop_level4(unsigned long vaddr)
{
	ulong *level4, *level4_dir;
	ulong *page_dir, *page_middle;
	ulong *page_table;
	unsigned long long level4_pte, pgd_pte;
	unsigned long long pmd_pte, pte;
	unsigned long long paddr = NOT_PADDR;

	if (info->page_buf == NULL) {
		/*
		 * This is the first vmalloc address translation request
		 */
		info->page_buf = (char *)calloc(1, PAGESIZE());
		if (info->page_buf == NULL) {
			ERRMSG("Can't allocate memory to read page tables. %s\n",
			       strerror(errno));
			return NOT_PADDR;
		}
	}

	level4 = (ulong *)info->kernel_pgd;
	level4_dir = (ulong *)((ulong *)level4 + L4_OFFSET(vaddr));
	if (!readmem(VADDR, PAGEBASE(level4), info->page_buf, PAGESIZE())) {
		ERRMSG("Can't read level4 page: 0x%llx\n", PAGEBASE(level4));
		return NOT_PADDR;
	}
	level4_pte = ULONG((info->page_buf + PAGEOFFSET(level4_dir)));
	if (!level4_pte)
		return NOT_PADDR;

	/*
	 * Sometimes we don't have level3 pagetable entries
	 */
	if (info->l3_index_size != 0) {
		page_dir = (ulong *)((ulong *)level4_pte + PGD_OFFSET_L4(vaddr));
		if (!readmem(VADDR, PAGEBASE(level4_pte), info->page_buf, PAGESIZE())) {
			ERRMSG("Can't read PGD page: 0x%llx\n", PAGEBASE(level4_pte));
			return NOT_PADDR;
		}
		pgd_pte = ULONG((info->page_buf + PAGEOFFSET(page_dir)));
		if (!pgd_pte)
			return NOT_PADDR;
	} else {
		pgd_pte = level4_pte;
	}

	page_middle = (ulong *)((ulong *)pgd_pte + PMD_OFFSET_L4(vaddr));
	if (!readmem(VADDR, PAGEBASE(pgd_pte), info->page_buf, PAGESIZE())) {
		ERRMSG("Can't read PMD page: 0x%llx\n", PAGEBASE(pgd_pte));
		return NOT_PADDR;
	}
	pmd_pte = ULONG((info->page_buf + PAGEOFFSET(page_middle)));
	if (!(pmd_pte))
		return NOT_PADDR;

	page_table = (ulong *)(pmd_pte & ~(info->l2_masked_bits))
			+ (BTOP(vaddr) & (info->ptrs_per_l1 - 1));
	if (!readmem(VADDR, PAGEBASE(pmd_pte), info->page_buf, PAGESIZE())) {
		ERRMSG("Can't read page table: 0x%llx\n", PAGEBASE(pmd_pte));
		return NOT_PADDR;
	}
	pte = ULONG((info->page_buf + PAGEOFFSET(page_table)));
	if (!(pte & _PAGE_PRESENT)) {
		ERRMSG("Page not present!\n");
		return NOT_PADDR;
	}

	if (!pte)
		return NOT_PADDR;

	paddr = PAGEBASE(PTOB(pte >> info->pte_shift)) + PAGEOFFSET(vaddr);

	return paddr;
}

int
set_ppc64_max_physmem_bits(void)
{
	long array_len = ARRAY_LENGTH(mem_section);
	/*
	 * The older ppc64 kernels uses _MAX_PHYSMEM_BITS as 42 and the
	 * newer kernels 3.7 onwards uses 46 bits.
	 */

	info->max_physmem_bits  = _MAX_PHYSMEM_BITS_ORIG ;
	if ((array_len == (NR_MEM_SECTIONS() / _SECTIONS_PER_ROOT_EXTREME()))
		|| (array_len == (NR_MEM_SECTIONS() / _SECTIONS_PER_ROOT())))
		return TRUE;

	info->max_physmem_bits  = _MAX_PHYSMEM_BITS_3_7;
	if ((array_len == (NR_MEM_SECTIONS() / _SECTIONS_PER_ROOT_EXTREME()))
		|| (array_len == (NR_MEM_SECTIONS() / _SECTIONS_PER_ROOT())))
		return TRUE;

	return FALSE;
}

int
get_machdep_info_ppc64(void)
{
	unsigned long vmlist, vmap_area_list, vmalloc_start;

	info->section_size_bits = _SECTION_SIZE_BITS;
	if (!set_ppc64_max_physmem_bits()) {
		ERRMSG("Can't detect max_physmem_bits.\n");
		return FALSE;
	}
	info->page_offset = __PAGE_OFFSET;

	if (SYMBOL(_stext) == NOT_FOUND_SYMBOL) {
		ERRMSG("Can't get the symbol of _stext.\n");
		return FALSE;
	}
	info->kernel_start = SYMBOL(_stext);
	DEBUG_MSG("kernel_start : %lx\n", info->kernel_start);

	/*
	 * Get vmalloc_start value from either vmap_area_list or vmlist.
	 */
	if ((SYMBOL(vmap_area_list) != NOT_FOUND_SYMBOL)
	    && (OFFSET(vmap_area.va_start) != NOT_FOUND_STRUCTURE)
	    && (OFFSET(vmap_area.list) != NOT_FOUND_STRUCTURE)) {
		if (!readmem(VADDR, SYMBOL(vmap_area_list) + OFFSET(list_head.next),
			     &vmap_area_list, sizeof(vmap_area_list))) {
			ERRMSG("Can't get vmap_area_list.\n");
			return FALSE;
		}
		if (!readmem(VADDR, vmap_area_list - OFFSET(vmap_area.list) +
			     OFFSET(vmap_area.va_start), &vmalloc_start,
			     sizeof(vmalloc_start))) {
			ERRMSG("Can't get vmalloc_start.\n");
			return FALSE;
		}
	} else if ((SYMBOL(vmlist) != NOT_FOUND_SYMBOL)
		   && (OFFSET(vm_struct.addr) != NOT_FOUND_STRUCTURE)) {
		if (!readmem(VADDR, SYMBOL(vmlist), &vmlist, sizeof(vmlist))) {
			ERRMSG("Can't get vmlist.\n");
			return FALSE;
		}
		if (!readmem(VADDR, vmlist + OFFSET(vm_struct.addr), &vmalloc_start,
			     sizeof(vmalloc_start))) {
			ERRMSG("Can't get vmalloc_start.\n");
			return FALSE;
		}
	} else {
		/*
		 * For the compatibility, makedumpfile should run without the symbol
		 * vmlist and the offset of vm_struct.addr if they are not necessary.
		 */
		return TRUE;
	}
	info->vmalloc_start = vmalloc_start;
	DEBUG_MSG("vmalloc_start: %lx\n", vmalloc_start);

	if (SYMBOL(swapper_pg_dir) != NOT_FOUND_SYMBOL) {
		info->kernel_pgd = SYMBOL(swapper_pg_dir);
	} else if (SYMBOL(cpu_pgd) != NOT_FOUND_SYMBOL) {
		info->kernel_pgd = SYMBOL(cpu_pgd);
	} else {
		ERRMSG("No swapper_pg_dir or cpu_pgd symbols exist\n");
		return FALSE;
	}

	if (SYMBOL(vmemmap_list) != NOT_FOUND_SYMBOL) {
		info->vmemmap_start = VMEMMAP_REGION_ID << REGION_SHIFT;
		info->vmemmap_end = info->vmemmap_start;
		if (ppc64_vmemmap_init() == FALSE) {
			ERRMSG("Can't get vmemmap list info.\n");
			return FALSE;
		}
		DEBUG_MSG("vmemmap_start: %lx\n", info->vmemmap_start);
	}

	return TRUE;
}

int
get_versiondep_info_ppc64()
{
	if (ppc64_vmalloc_init() == FALSE) {
		ERRMSG("Can't initialize for vmalloc translation\n");
		return FALSE;
	}

	return TRUE;
}

int
is_vmalloc_addr_ppc64(unsigned long vaddr)
{
	return (info->vmalloc_start && vaddr >= info->vmalloc_start);
}

unsigned long long
vaddr_to_paddr_ppc64(unsigned long vaddr)
{
	unsigned long long paddr;

	if ((info->flag_vmemmap)
	    && (vaddr >= info->vmemmap_start)) {
		return ppc64_vmemmap_to_phys(vaddr);
	}

	paddr = vaddr_to_paddr_general(vaddr);
	if (paddr != NOT_PADDR)
		return paddr;

	if (!is_vmalloc_addr_ppc64(vaddr))
		return (vaddr - info->kernel_start);

	if ((SYMBOL(vmap_area_list) == NOT_FOUND_SYMBOL)
	    || (OFFSET(vmap_area.va_start) == NOT_FOUND_STRUCTURE)
	    || (OFFSET(vmap_area.list) == NOT_FOUND_STRUCTURE)) {
		if ((SYMBOL(vmlist) == NOT_FOUND_SYMBOL)
		    || (OFFSET(vm_struct.addr) == NOT_FOUND_STRUCTURE)) {
			ERRMSG("Can't get info for vmalloc translation.\n");
			return NOT_PADDR;
		}
	}

	return ppc64_vtop_level4(vaddr);
}

#endif /* powerpc64 */
