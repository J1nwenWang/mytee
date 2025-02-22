/*
 *  linux/arch/arm/mm/pgd.c
 *
 *  Copyright (C) 1998-2005 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/slab.h>

#include <asm/cp15.h>
#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/tlbflush.h>

#include "mm.h"

#ifdef CONFIG_MYTEE
#include <asm/mytee.h>
#endif

#ifdef CONFIG_ARM_LPAE
#ifdef CONFIG_MYTEE
static inline pgd_t* __pgd_alloc(void){
	pgd_t *new_pgd;
	new_pgd = (pgd_t *)mytee_ro_alloc();
        if(!new_pgd)
                new_pgd = kmalloc(PTRS_PER_PGD * sizeof(pgd_t), GFP_KERNEL);
        if (!new_pgd)
                return NULL;
	return new_pgd;
}

static inline void __pgd_free(pgd_t *pgd){
	if(is_mytee_ro_page((unsigned long)pgd)){
		mytee_ro_free(pgd);
		mytee_pgd_free(EMUL_PGD_FREE, pgd, 0, 0, 0, 0);
	}
	else
		kfree(pgd);
}
#else
#define __pgd_alloc()	kmalloc(PTRS_PER_PGD * sizeof(pgd_t), GFP_KERNEL)
#define __pgd_free(pgd)	kfree(pgd)
#endif
#else
#define __pgd_alloc()	(pgd_t *)__get_free_pages(GFP_KERNEL, 2)
#define __pgd_free(pgd)	free_pages((unsigned long)pgd, 2)
#endif

/*
 * need to get a 16k page for level 1
 */
pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *new_pgd, *init_pgd;
	pud_t *new_pud, *init_pud;
	pmd_t *new_pmd, *init_pmd;
	pte_t *new_pte, *init_pte;

	new_pgd = __pgd_alloc();
	if (!new_pgd)
		goto no_pgd;
	
#ifdef CONFIG_MYTEE
	mytee_ro_memset(EMUL_RO_MEMSET, new_pgd, 0, USER_PTRS_PER_PGD * sizeof(pgd_t), 0, 0);
#else	
	memset(new_pgd, 0, USER_PTRS_PER_PGD * sizeof(pgd_t));
#endif

	/*
	 * Copy over the kernel and IO PGD entries
	 */
	init_pgd = pgd_offset_k(0);
#ifdef CONFIG_MYTEE
// LPAE is enabled, so no need to copy
//	mytee_ro_memcpy(EMUL_RO_MEMCPY, new_pgd + USER_PTRS_PER_PGD, init_pgd + USER_PTRS_PER_PGD, (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t), 0, 0);
#else
	memcpy(new_pgd + USER_PTRS_PER_PGD, init_pgd + USER_PTRS_PER_PGD,
		       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
#endif
	clean_dcache_area(new_pgd, PTRS_PER_PGD * sizeof(pgd_t));

#ifdef CONFIG_ARM_LPAE
	/*
	 * Allocate PMD table for modules and pkmap mappings.
	 */
	new_pud = pud_alloc(mm, new_pgd + pgd_index(MODULES_VADDR),
			    MODULES_VADDR);
	if (!new_pud)
		goto no_pud;

	new_pmd = pmd_alloc(mm, new_pud, 0);
	if (!new_pmd)
		goto no_pmd;
#endif

	if (!vectors_high()) {
		/*
		 * On ARM, first page must always be allocated since it
		 * contains the machine vectors. The vectors are always high
		 * with LPAE.
		 */
		new_pud = pud_alloc(mm, new_pgd, 0);
		if (!new_pud)
			goto no_pud;

		new_pmd = pmd_alloc(mm, new_pud, 0);
		if (!new_pmd)
			goto no_pmd;

		new_pte = pte_alloc_map(mm, new_pmd, 0);
		if (!new_pte)
			goto no_pte;

#ifndef CONFIG_ARM_LPAE
		/*
		 * Modify the PTE pointer to have the correct domain.  This
		 * needs to be the vectors domain to avoid the low vectors
		 * being unmapped.
		 */
		pmd_val(*new_pmd) &= ~PMD_DOMAIN_MASK;
		pmd_val(*new_pmd) |= PMD_DOMAIN(DOMAIN_VECTORS);
#endif

		init_pud = pud_offset(init_pgd, 0);
		init_pmd = pmd_offset(init_pud, 0);
		init_pte = pte_offset_map(init_pmd, 0);
		set_pte_ext(new_pte + 0, init_pte[0], 0);
		set_pte_ext(new_pte + 1, init_pte[1], 0);
		pte_unmap(init_pte);
		pte_unmap(new_pte);
	}
// Call monitor to manage the bitmap for the allocated pgd
	mytee_new_pgd(EMUL_NEW_PGD, new_pgd, 0, 0, 0, 0);

	return new_pgd;

no_pte:
	pmd_free(mm, new_pmd);
	mm_dec_nr_pmds(mm);
no_pmd:
	pud_free(mm, new_pud);
no_pud:
	__pgd_free(new_pgd);
no_pgd:
	return NULL;
}

void pgd_free(struct mm_struct *mm, pgd_t *pgd_base)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pgtable_t pte;

#ifdef  CONFIG_MYTEE_F
	unsigned long cmd_id = EMUL_PGD_FREE;
    	__asm__ __volatile__(".arch_extension sec\n");
#endif


	if (!pgd_base)
		return;

	pgd = pgd_base + pgd_index(0);
	if (pgd_none_or_clear_bad(pgd))
		goto no_pgd;

	pud = pud_offset(pgd, 0);
	if (pud_none_or_clear_bad(pud))
		goto no_pud;

	pmd = pmd_offset(pud, 0);
	if (pmd_none_or_clear_bad(pmd))
		goto no_pmd;

	pte = pmd_pgtable(*pmd);
	pmd_clear(pmd);
	pte_free(mm, pte);
	atomic_long_dec(&mm->nr_ptes);
no_pmd:
	pud_clear(pud);
	pmd_free(mm, pmd);
	mm_dec_nr_pmds(mm);
no_pud:
	pgd_clear(pgd);
	pud_free(mm, pud);
no_pgd:
#ifdef CONFIG_ARM_LPAE
	/*
	 * Free modules/pkmap or identity pmd tables.
	 */
	for (pgd = pgd_base; pgd < pgd_base + PTRS_PER_PGD; pgd++) {
		if (pgd_none_or_clear_bad(pgd))
			continue;
		if (pgd_val(*pgd) & L_PGD_SWAPPER)
			continue;
		pud = pud_offset(pgd, 0);
		if (pud_none_or_clear_bad(pud))
			continue;
		pmd = pmd_offset(pud, 0);
		pud_clear(pud);
		pmd_free(mm, pmd);
		mm_dec_nr_pmds(mm);
		pgd_clear(pgd);
		pud_free(mm, pud);
	}
#endif
#ifdef  CONFIG_MYTEE_F
	if (mytee_is_pg_protected((unsigned long) pgd) != 0) {
	__asm__ __volatile__ (
    	"stmfd  sp!,{r0-r1}\n"
    	"mov	r0, %0\n"
    	"mov	r1, %1\n"
    	"smc	#0\n"
    	"mov	r0, #0\n"
    	"mcr	p15, 0, r0, c8, c3, 0\n"
        	"dsb\n"
        	"isb\n"
    	"pop	{r0-r1}\n"
    	::"r"(cmd_id),"r"(pgd):"r0","r1","cc");
	}
#endif
	__pgd_free(pgd_base);
}
