/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * LamiaAtrium release - for studying CO2018 only
 */

#ifndef OSMM_H
#define OSMM_H

#include <stdint.h>
#include <inttypes.h>   /* PRIu64 / PRIx64 */

#define MM_PAGING
#define PAGING_MAX_MMSWP     4
#define PAGING_MAX_SYMTBL_SZ 30

 /* Address type */
#ifdef MM64
typedef uint64_t addr_t;
#else
typedef uint32_t addr_t;
#endif

typedef char BYTE;

/* Print format for addr_t */
#ifdef MM64
#define FORMAT_ADDR  "%" PRIu64
#define FORMATX_ADDR "%016" PRIx64
#else
#define FORMAT_ADDR  "%u"
#define FORMATX_ADDR "%08x"
#endif

struct mm_struct;

/* ================== DATA STRUCTURES ================== */

struct pgn_t {
	addr_t pgn;
	struct pgn_t* pg_next;
};

struct vm_rg_struct {
	addr_t rg_start;
	addr_t rg_end;
	struct vm_rg_struct* rg_next;
};

struct vm_area_struct {
	unsigned long vm_id;
	addr_t vm_start;
	addr_t vm_end;
	addr_t sbrk;

	struct mm_struct* vm_mm;
	struct vm_rg_struct* vm_freerg_list;
	struct vm_area_struct* vm_next;
};

/*
 * IMPORTANT:
 * - Dù MM64, LamiaAtrium vẫn dùng PTE 32-bit và page table “phẳng”
 * - mm->pgd là mảng uint32_t[PAGING_MAX_PGN]
 */
struct mm_struct {
	uint32_t* pgd;                 /* flat page table: pgd[pgn] = PTE(32-bit) */
	struct vm_area_struct* mmap;
	struct vm_rg_struct symrgtbl[PAGING_MAX_SYMTBL_SZ];
	struct pgn_t* fifo_pgn;        /* FIFO list for replacement */
};

struct framephy_struct {
	addr_t fpn;
	struct framephy_struct* fp_next;
	struct mm_struct* owner;
};

struct memphy_struct {
	BYTE* storage;
	int maxsz;

	int rdmflg;
	int cursor;

	struct framephy_struct* free_fp_list;
	struct framephy_struct* used_fp_list;
};

#endif /* OSMM_H */
