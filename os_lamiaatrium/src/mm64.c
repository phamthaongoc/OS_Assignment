#include "os-mm.h"
#include "mm64.h"
#include "mm.h"
#include "mem.h"     /* MEMPHY_read/write, MEMPHY_put_freefp... */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef MM64

/* ============================================================
 * Flat page table walk:
 * mm->pgd[pgn] = PTE (32-bit)
 * ============================================================ */
static inline pte_t*
mm64_walk_get_pte(struct mm_struct* mm, addr_t vaddr, int create)
{
    if (!mm) return NULL;

    if (!mm->pgd) {
        if (!create) return NULL;

        /* pgd là mảng PTE 32-bit, số trang dùng macro của đề (mm.h) */
        mm->pgd = (uint32_t*)calloc(PAGING_MAX_PGN, sizeof(uint32_t));
        if (!mm->pgd) return NULL;
    }

    addr_t pgn = (addr_t)(vaddr >> PAGING64_ADDR_PT_SHIFT);
    if (pgn >= (addr_t)PAGING_MAX_PGN) return NULL;

    return (pte_t*)&mm->pgd[pgn];
}

/* ============================================================
 * PTE helpers (PTE is 32-bit)
 * ============================================================ */
int init_pte(pte_t* pte, int pre, addr_t fpn,
    int drt, int swp, int swptyp, addr_t swpoff)
{
    if (!pte) return -1;

    *pte = 0;

    if (!pre) return 0;

    if (!swp) {
        /* present page */
        SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
        CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

        SETVAL(*pte, (uint32_t)fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
        if (drt) SETBIT(*pte, PAGING_PTE_DIRTY_MASK);
    }
    else {
        /* swapped page: present=0 */
        CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);
        SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

        SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
        SETVAL(*pte, (uint32_t)swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }

    return 0;
}

uint32_t pte_get_entry(struct pcb_t* caller, addr_t pgn)
{
    if (!caller || !caller->krnl || !caller->krnl->mm) return 0;

    pte_t* pte = mm64_walk_get_pte(
        caller->krnl->mm,
        ((addr_t)pgn << PAGING64_ADDR_PT_SHIFT),
        0
    );
    return pte ? (uint32_t)(*pte) : 0;
}

int pte_set_entry(struct pcb_t* caller, addr_t pgn, uint32_t val)
{
    if (!caller || !caller->krnl || !caller->krnl->mm) return -1;

    pte_t* pte = mm64_walk_get_pte(
        caller->krnl->mm,
        ((addr_t)pgn << PAGING64_ADDR_PT_SHIFT),
        1
    );
    if (!pte) return -1;

    *pte = (pte_t)val;
    return 0;
}

int pte_set_fpn(struct pcb_t* caller, addr_t pgn, addr_t fpn)
{
    uint32_t pte = 0;
    SETBIT(pte, PAGING_PTE_PRESENT_MASK);
    CLRBIT(pte, PAGING_PTE_SWAPPED_MASK);
    SETVAL(pte, (uint32_t)fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    return pte_set_entry(caller, pgn, pte);
}

int pte_set_swap(struct pcb_t* caller, addr_t pgn, int swptyp, addr_t swpoff)
{
    uint32_t pte = 0;
    CLRBIT(pte, PAGING_PTE_PRESENT_MASK);
    SETBIT(pte, PAGING_PTE_SWAPPED_MASK);
    SETVAL(pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
    SETVAL(pte, (uint32_t)swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    return pte_set_entry(caller, pgn, pte);
}

/* ============================================================
 * FIFO page replacement (enqueue tail, pop head)
 * ============================================================ */
int enlist_pgn_node(struct pgn_t** list, addr_t pgn)
{
    if (!list) return -1;

    struct pgn_t* n = (struct pgn_t*)malloc(sizeof(struct pgn_t));
    if (!n) return -1;
    n->pgn = pgn;
    n->pg_next = NULL;

    if (*list == NULL) {
        *list = n;
        return 0;
    }

    struct pgn_t* it = *list;
    while (it->pg_next) it = it->pg_next;
    it->pg_next = n;
    return 0;
}

int find_victim_page(struct mm_struct* mm, addr_t* retpgn)
{
    if (!mm || !mm->fifo_pgn || !retpgn) return -1;

    struct pgn_t* v = mm->fifo_pgn;
    *retpgn = v->pgn;
    mm->fifo_pgn = v->pg_next;
    free(v);
    return 0;
}

/* ============================================================
 * Swap copy (RAM <-> SWAP)
 * ============================================================ */
int __swap_cp_page(struct memphy_struct* src, addr_t srcfpn,
    struct memphy_struct* dst, addr_t dstfpn)
{
    if (!src || !dst) return -1;

    for (int i = 0; i < PAGING64_PAGESZ; i++) {
        BYTE data;
        MEMPHY_read(src, (int)(srcfpn * PAGING64_PAGESZ + i), &data);
        MEMPHY_write(dst, (int)(dstfpn * PAGING64_PAGESZ + i), data);
    }
    return 0;
}

/* ============================================================
 * init / free
 * ============================================================ */
int init_mm(struct mm_struct* mm, struct pcb_t* caller)
{
    (void)caller;
    if (!mm) return -1;

    mm->pgd = NULL;
    mm->mmap = NULL;                 // <-- FIX: tránh mmap = rác
    mm->fifo_pgn = NULL;
    memset(mm->symrgtbl, 0, sizeof(mm->symrgtbl));  // <-- FIX: tránh oldrg = rác

    return 0;
}

int free_pcb_memph(struct pcb_t* caller)
{
    if (!caller || !caller->krnl || !caller->krnl->mm) return -1;

    struct mm_struct* mm = caller->krnl->mm;
    if (!mm->pgd) return 0;

    for (addr_t pgn = 0; pgn < (addr_t)PAGING_MAX_PGN; pgn++) {
        uint32_t pte = mm->pgd[pgn];
        if (pte == 0) continue;

        if (PAGING_PAGE_PRESENT(pte)) {
            addr_t fpn = (addr_t)PAGING_FPN(pte);
            MEMPHY_put_freefp(caller->krnl->mram, fpn);
        }
        else if (pte & PAGING_PTE_SWAPPED_MASK) {
            addr_t swp = (addr_t)PAGING_SWP(pte);
            MEMPHY_put_freefp(caller->krnl->active_mswp, swp);
        }

        mm->pgd[pgn] = 0;
    }

    return 0;
}

/* ============================================================
 * sys_mem.c needs this symbol
 * ============================================================ */
int vmap_pgd_memset(struct pcb_t* caller, addr_t addr, int pgnum)
{
    if (!caller || !caller->krnl || !caller->krnl->mm) return -1;

    for (int i = 0; i < pgnum; i++) {
        addr_t va = addr + (addr_t)i * PAGING64_PAGESZ;
        addr_t pgn = (va >> PAGING64_ADDR_PT_SHIFT);
        pte_set_entry(caller, pgn, 0);
    }
    return 0;
}

/* Nếu build đòi mấy hàm này mà bạn chưa dùng sâu -> stub an toàn */
addr_t vmap_page_range(struct pcb_t* caller, addr_t addr, int pgnum,
    struct framephy_struct* frames, struct vm_rg_struct* ret_rg)
{
    (void)frames;
    if (!caller || !ret_rg) return 0;

    ret_rg->rg_start = addr;
    ret_rg->rg_end = addr + (addr_t)pgnum * PAGING64_PAGESZ - 1;

    vmap_pgd_memset(caller, addr, pgnum);
    return ret_rg->rg_start;
}

addr_t alloc_pages_range(struct pcb_t* caller, int req_pgnum, struct framephy_struct** frm_lst)
{
    (void)caller; (void)req_pgnum;
    if (frm_lst) *frm_lst = NULL;
    return 0;
}

addr_t vm_map_ram(struct pcb_t* caller, addr_t astart, addr_t aend, addr_t mapstart,
    int incpgnum, struct vm_rg_struct* ret_rg)
{
    (void)caller; (void)astart; (void)aend; (void)mapstart; (void)incpgnum; (void)ret_rg;
    return 0;
}

/* ============================================================
 * Debug print
 * ============================================================ */
int print_pgtbl(struct pcb_t* proc, addr_t start, addr_t end)
{
    if (!proc || !proc->krnl || !proc->krnl->mm) return -1;
    struct mm_struct* mm = proc->krnl->mm;

    printf("print_pgtbl:\n");
    printf(" PDG=%p P4g=%p PUD=%p PMD=%p\n",
        (void*)mm->pgd, (void*)mm->pgd, (void*)mm->pgd, (void*)mm->pgd);

    if (!mm->pgd) return 0;

    addr_t spgn, epgn;
    if (end == (addr_t)-1) {
        spgn = 0;
        epgn = (addr_t)PAGING_MAX_PGN - 1;
    }
    else {
        spgn = (start >> PAGING64_ADDR_PT_SHIFT);
        epgn = (end >> PAGING64_ADDR_PT_SHIFT);
        if (epgn >= (addr_t)PAGING_MAX_PGN) epgn = (addr_t)PAGING_MAX_PGN - 1;
    }

    for (addr_t pgn = spgn; pgn <= epgn; pgn++) {
        uint32_t pte = pte_get_entry(proc, pgn);
        if (pte == 0) continue;

        printf("  [pgn=%lu] PTE=0x%08x",
            (unsigned long)pgn, pte);

        if (PAGING_PAGE_PRESENT(pte))
            printf("  fpn=%u", (unsigned)PAGING_FPN(pte));
        else if (pte & PAGING_PTE_SWAPPED_MASK)
            printf("  swap=%u", (unsigned)PAGING_SWP(pte));

        printf("\n");
    }
    return 0;
}



/* helpers (nếu chỗ khác gọi) */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
    if (pgd) *pgd = PAGING64_ADDR_PGD(addr);
    if (p4d) *p4d = PAGING64_ADDR_P4D(addr);
    if (pud) *pud = PAGING64_ADDR_PUD(addr);
    if (pmd) *pmd = PAGING64_ADDR_PMD(addr);
    if (pt)  *pt = PAGING64_ADDR_PT(addr);
    return 0;
}

int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
    return get_pd_from_address((pgn << PAGING64_ADDR_PT_SHIFT), pgd, p4d, pud, pmd, pt);
}

#endif /* MM64 */
