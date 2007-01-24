/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/t_lock.h>
#include <sys/memlist.h>
#include <sys/cpuvar.h>
#include <sys/vmem.h>
#include <sys/mman.h>
#include <sys/vm.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/vm_machparam.h>
#include <sys/tss.h>
#include <sys/vnode.h>
#include <vm/hat.h>
#include <vm/anon.h>
#include <vm/as.h>
#include <vm/page.h>
#include <vm/seg.h>
#include <vm/seg_kmem.h>
#include <vm/seg_map.h>
#include <vm/hat_i86.h>
#include <sys/promif.h>
#include <sys/x86_archext.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
#include <sys/sunddi.h>
#include <sys/ddidmareq.h>
#include <sys/controlregs.h>
#include <sys/reboot.h>
#include <sys/kdi.h>
#include <sys/bootconf.h>
#include <sys/bootsvcs.h>
#include <sys/bootinfo.h>
#include <vm/kboot_mmu.h>

caddr_t
i86devmap(pfn_t pf, pgcnt_t pgcnt, uint_t prot)
{
	caddr_t addr;
	caddr_t addr1;
	page_t *pp;

	addr1 = addr = vmem_alloc(heap_arena, mmu_ptob(pgcnt), VM_SLEEP);

	for (; pgcnt != 0; addr += MMU_PAGESIZE, ++pf, --pgcnt) {
		pp = page_numtopp_nolock(pf);
		if (pp == NULL) {
			hat_devload(kas.a_hat, addr, MMU_PAGESIZE, pf,
			    prot | HAT_NOSYNC, HAT_LOAD_LOCK);
		} else {
			hat_memload(kas.a_hat, addr, pp,
			    prot | HAT_NOSYNC, HAT_LOAD_LOCK);
		}
	}

	return (addr1);
}

/*
 * This routine is like page_numtopp, but accepts only free pages, which
 * it allocates (unfrees) and returns with the exclusive lock held.
 * It is used by machdep.c/dma_init() to find contiguous free pages.
 *
 * XXX this and some others should probably be in vm_machdep.c
 */
page_t *
page_numtopp_alloc(pfn_t pfnum)
{
	page_t *pp;

retry:
	pp = page_numtopp_nolock(pfnum);
	if (pp == NULL) {
		return (NULL);
	}

	if (!page_trylock(pp, SE_EXCL)) {
		return (NULL);
	}

	if (page_pptonum(pp) != pfnum) {
		page_unlock(pp);
		goto retry;
	}

	if (!PP_ISFREE(pp)) {
		page_unlock(pp);
		return (NULL);
	}
	if (pp->p_szc) {
		page_demote_free_pages(pp);
		page_unlock(pp);
		goto retry;
	}

	/* If associated with a vnode, destroy mappings */

	if (pp->p_vnode) {

		page_destroy_free(pp);

		if (!page_lock(pp, SE_EXCL, (kmutex_t *)NULL, P_NO_RECLAIM)) {
			return (NULL);
		}

		if (page_pptonum(pp) != pfnum) {
			page_unlock(pp);
			goto retry;
		}
	}

	if (!PP_ISFREE(pp) || !page_reclaim(pp, (kmutex_t *)NULL)) {
		page_unlock(pp);
		return (NULL);
	}

	return (pp);
}

/*
 * Flag is not set early in boot. Once it is set we are no longer
 * using boot's page tables.
 */
uint_t khat_running = 0;

/*
 * This procedure is callable only while the boot loader is in charge of the
 * MMU. It assumes that PA == VA for page table pointers.  It doesn't live in
 * kboot_mmu.c since it's used from common code.
 */
pfn_t
va_to_pfn(void *vaddr)
{
	uintptr_t	des_va = ALIGN2PAGE(vaddr);
	uintptr_t	va = des_va;
	size_t		len;
	uint_t		prot;
	pfn_t		pfn;

	if (khat_running)
		panic("va_to_pfn(): called too late\n");

	if (kbm_probe(&va, &len, &pfn, &prot) == 0)
		return (PFN_INVALID);
	if (va > des_va)
		return (PFN_INVALID);
	if (va < des_va)
		pfn += mmu_btop(des_va - va);
	return (pfn);
}

/*
 * Initialize a special area in the kernel that always holds some PTEs for
 * faster performance. This always holds segmap's PTEs.
 * In the 32 bit kernel this maps the kernel heap too.
 */
void
hat_kmap_init(uintptr_t base, size_t len)
{
	uintptr_t map_addr;	/* base rounded down to large page size */
	uintptr_t map_eaddr;	/* base + len rounded up */
	size_t map_len;
	caddr_t ptes;		/* mapping area in kernel for kmap ptes */
	size_t window_size;	/* size of mapping area for ptes */
	ulong_t htable_cnt;	/* # of page tables to cover map_len */
	ulong_t i;
	htable_t *ht;
	uintptr_t va;

	/*
	 * We have to map in an area that matches an entire page table.
	 */
	map_addr = base & LEVEL_MASK(1);
	map_eaddr = (base + len + LEVEL_SIZE(1) - 1) & LEVEL_MASK(1);
	map_len = map_eaddr - map_addr;
	window_size = mmu_btop(map_len) * mmu.pte_size;
	window_size = (window_size + LEVEL_SIZE(1)) & LEVEL_MASK(1);
	htable_cnt = map_len >> LEVEL_SHIFT(1);

	/*
	 * allocate vmem for the kmap_ptes
	 */
	ptes = vmem_xalloc(heap_arena, window_size, LEVEL_SIZE(1), 0,
	    0, NULL, NULL, VM_SLEEP);
	mmu.kmap_htables =
	    kmem_alloc(htable_cnt * sizeof (htable_t *), KM_SLEEP);

	/*
	 * Map the page tables that cover kmap into the allocated range.
	 * Note we don't ever htable_release() the kmap page tables - they
	 * can't ever be stolen, freed, etc.
	 */
	for (va = map_addr, i = 0; i < htable_cnt; va += LEVEL_SIZE(1), ++i) {
		ht = htable_create(kas.a_hat, va, 0, NULL);
		if (ht == NULL)
			panic("hat_kmap_init: ht == NULL");
		mmu.kmap_htables[i] = ht;

		hat_devload(kas.a_hat, ptes + i * MMU_PAGESIZE,
		    MMU_PAGESIZE, ht->ht_pfn,
		    PROT_READ | PROT_WRITE | HAT_NOSYNC | HAT_UNORDERED_OK,
		    HAT_LOAD | HAT_LOAD_NOCONSIST);
	}

	/*
	 * set information in mmu to activate handling of kmap
	 */
	mmu.kmap_addr = map_addr;
	mmu.kmap_eaddr = map_eaddr;
	mmu.kmap_ptes = (x86pte_t *)ptes;
}

extern caddr_t	kpm_vbase;
extern size_t	kpm_size;

/*
 * Routine to pre-allocate data structures for hat_kern_setup(). It computes
 * how many pagetables it needs by walking the boot loader's page tables.
 */
/*ARGSUSED*/
void
hat_kern_alloc(
	caddr_t	segmap_base,
	size_t	segmap_size,
	caddr_t	ekernelheap)
{
	uintptr_t	last_va = (uintptr_t)-1;	/* catch 1st time */
	uintptr_t	va = 0;
	size_t		size;
	pfn_t		pfn;
	uint_t		prot;
	uint_t		table_cnt = 1;
	uint_t		mapping_cnt;
	level_t		start_level;
	level_t		l;
	struct memlist	*pmem;
	level_t		lpagel = mmu.max_page_level;
	uint64_t	paddr;
	int64_t		psize;


	if (kpm_size > 0) {
		/*
		 * Create the kpm page tables.
		 */
		for (pmem = phys_install; pmem; pmem = pmem->next) {
			paddr = pmem->address;
			psize = pmem->size;
			while (psize >= MMU_PAGESIZE) {
				if ((paddr & LEVEL_OFFSET(lpagel)) == 0 &&
				    psize > LEVEL_SIZE(lpagel))
					l = lpagel;
				else
					l = 0;
				kbm_map((uintptr_t)kpm_vbase + paddr, paddr,
				    l, 1);
				paddr += LEVEL_SIZE(l);
				psize -= LEVEL_SIZE(l);
			}
		}
	} else {
		/*
		 * Create the page windows and 1 page of VA in
		 * which we map the PTEs of those windows.
		 */
		mmu.pwin_base = vmem_xalloc(heap_arena, 2 * NCPU * MMU_PAGESIZE,
		    LEVEL_SIZE(1), 0, 0, NULL, NULL, VM_SLEEP);
		ASSERT(NCPU * 2 <= MMU_PAGESIZE / mmu.pte_size);
		mmu.pwin_pte_va = vmem_xalloc(heap_arena, MMU_PAGESIZE,
		    MMU_PAGESIZE, 0, 0, NULL, NULL, VM_SLEEP);

		/*
		 * Find/Create the page table window mappings.
		 */
		paddr = 0;
		(void) find_pte((uintptr_t)mmu.pwin_base, &paddr, 0, 0);
		ASSERT(paddr != 0);
		ASSERT((paddr & MMU_PAGEOFFSET) == 0);
		mmu.pwin_pte_pa = paddr;
		kbm_map((uintptr_t)mmu.pwin_pte_va, mmu.pwin_pte_pa, 0, 1);
	}

	/*
	 * Walk the boot loader's page tables and figure out
	 * how many tables and page mappings there will be.
	 */
	while (kbm_probe(&va, &size, &pfn, &prot) != 0) {
		/*
		 * At each level, if the last_va falls into a new htable,
		 * increment table_cnt. We can stop at the 1st level where
		 * they are in the same htable.
		 */
		if (size == MMU_PAGESIZE)
			start_level = 0;
		else
			start_level = 1;

		for (l = start_level; l < mmu.max_level; ++l) {
			if (va >> LEVEL_SHIFT(l + 1) ==
			    last_va >> LEVEL_SHIFT(l + 1))
				break;
			++table_cnt;
		}
		last_va = va;
		va = (va & LEVEL_MASK(1)) + LEVEL_SIZE(1);
	}

	/*
	 * Besides the boot loader mappings, we're going to fill in
	 * the entire top level page table for the kernel. Make sure there's
	 * enough reserve for that too.
	 */
	table_cnt += mmu.top_level_count - ((kernelbase >>
	    LEVEL_SHIFT(mmu.max_level)) & (mmu.top_level_count - 1));

#if defined(__i386)
	/*
	 * The 32 bit PAE hat allocates tables one level below the top when
	 * kernelbase isn't 1 Gig aligned. We'll just be sloppy and allocate
	 * a bunch more to the reserve. Any unused will be returned later.
	 * Note we've already counted these mappings, just not the extra
	 * pagetables.
	 */
	if (mmu.pae_hat != 0 && (kernelbase & LEVEL_OFFSET(mmu.max_level)) != 0)
		table_cnt += mmu.ptes_per_table -
		    ((kernelbase & LEVEL_OFFSET(mmu.max_level)) >>
		    LEVEL_SHIFT(mmu.max_level - 1));
#endif

	/*
	 * Add 1/4 more into table_cnt for extra slop.  The unused
	 * slop is freed back when we htable_adjust_reserve() later.
	 */
	table_cnt += table_cnt >> 2;

	/*
	 * We only need mapping entries (hments) for shared pages.
	 * This should be far, far fewer than the total possible,
	 * We'll allocate enough for 1/16 of all possible PTEs.
	 */
	mapping_cnt = (table_cnt * mmu.ptes_per_table) >> 4;

	/*
	 * Now create the initial htable/hment reserves
	 */
	htable_initial_reserve(table_cnt);
	hment_reserve(mapping_cnt);
	x86pte_cpu_init(CPU);
}


/*
 * This routine handles the work of creating the kernel's initial mappings
 * by deciphering the mappings in the page tables created by the boot program.
 *
 * We maintain large page mappings, but only to a level 1 pagesize.
 * The boot loader can only add new mappings once this function starts.
 * In particular it can not change the pagesize used for any existing
 * mappings or this code breaks!
 */

void
hat_kern_setup(void)
{
	/*
	 * Attach htables to the existing pagetables
	 */
	htable_attach(kas.a_hat, 0, mmu.max_level, NULL,
	    mmu_btop(getcr3()));

#if defined(__i386)
	CPU->cpu_tss->tss_cr3 = dftss0.tss_cr3 = getcr3();
#endif /* __i386 */

	/*
	 * The kernel HAT is now officially open for business.
	 */
	khat_running = 1;

	CPUSET_ATOMIC_ADD(kas.a_hat->hat_cpus, CPU->cpu_id);
	CPU->cpu_current_hat = kas.a_hat;
}