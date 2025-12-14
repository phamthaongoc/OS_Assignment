#ifndef MM64_H
#define MM64_H

#include <stdint.h>
#include "os-mm.h"
#include "mm.h"

struct pcb_t;   /* ✅ thêm forward declare */

#define MM64_BITS_PER_LONG 64

/* 57-bit VA split:
 * [56:48]=PGD [47:39]=P4D [38:30]=PUD [29:21]=PMD [20:12]=PT [11:0]=OFF
 */
#define PAGING64_CPU_BUS_WIDTH 57
#define PAGING64_PAGESZ        4096
#define PAGING64_ADDR_PT_SHIFT 12

 /* ✅ thêm align macro để mm-vm.c thấy */
#define PAGING64_PAGE_ALIGNSZ(sz) (DIV_ROUND_UP((sz), PAGING64_PAGESZ) * PAGING64_PAGESZ)

/* ===== bit ranges ===== */
#define PAGING64_ADDR_OFFST_HIBIT 11
#define PAGING64_ADDR_OFFST_LOBIT 0

#define PAGING64_ADDR_PT_HIBIT 20
#define PAGING64_ADDR_PT_LOBIT 12

#define PAGING64_ADDR_PMD_HIBIT 29
#define PAGING64_ADDR_PMD_LOBIT 21

#define PAGING64_ADDR_PUD_HIBIT 38
#define PAGING64_ADDR_PUD_LOBIT 30

#define PAGING64_ADDR_P4D_HIBIT 47
#define PAGING64_ADDR_P4D_LOBIT 39

#define PAGING64_ADDR_PGD_HIBIT 56
#define PAGING64_ADDR_PGD_LOBIT 48

#define GENMASK64(h, l) \
  (((~0ULL) << (l)) & (~0ULL >> (MM64_BITS_PER_LONG - (h) - 1)))

#define PAGING64_ADDR_OFFST_MASK GENMASK64(PAGING64_ADDR_OFFST_HIBIT, PAGING64_ADDR_OFFST_LOBIT)
#define PAGING64_ADDR_PT_MASK    GENMASK64(PAGING64_ADDR_PT_HIBIT,    PAGING64_ADDR_PT_LOBIT)
#define PAGING64_ADDR_PMD_MASK   GENMASK64(PAGING64_ADDR_PMD_HIBIT,   PAGING64_ADDR_PMD_LOBIT)
#define PAGING64_ADDR_PUD_MASK   GENMASK64(PAGING64_ADDR_PUD_HIBIT,   PAGING64_ADDR_PUD_LOBIT)
#define PAGING64_ADDR_P4D_MASK   GENMASK64(PAGING64_ADDR_P4D_HIBIT,   PAGING64_ADDR_P4D_LOBIT)
#define PAGING64_ADDR_PGD_MASK   GENMASK64(PAGING64_ADDR_PGD_HIBIT,   PAGING64_ADDR_PGD_LOBIT)

#define PAGING64_ADDR_OFFST(addr) (((addr_t)(addr) & PAGING64_ADDR_OFFST_MASK) >> PAGING64_ADDR_OFFST_LOBIT)
#define PAGING64_ADDR_PT(addr)    (((addr_t)(addr) & PAGING64_ADDR_PT_MASK)    >> PAGING64_ADDR_PT_LOBIT)
#define PAGING64_ADDR_PMD(addr)   (((addr_t)(addr) & PAGING64_ADDR_PMD_MASK)   >> PAGING64_ADDR_PMD_LOBIT)
#define PAGING64_ADDR_PUD(addr)   (((addr_t)(addr) & PAGING64_ADDR_PUD_MASK)   >> PAGING64_ADDR_PUD_LOBIT)
#define PAGING64_ADDR_P4D(addr)   (((addr_t)(addr) & PAGING64_ADDR_P4D_MASK)   >> PAGING64_ADDR_P4D_LOBIT)
#define PAGING64_ADDR_PGD(addr)   (((addr_t)(addr) & PAGING64_ADDR_PGD_MASK)   >> PAGING64_ADDR_PGD_LOBIT)

#define LV_ENTRIES 512

typedef uint32_t pte_t;

typedef struct pt_t { pte_t  e[LV_ENTRIES]; } pt_t;
typedef struct pmd_t { pt_t* pt[LV_ENTRIES]; } pmd_t;
typedef struct pud_t { pmd_t* pmd[LV_ENTRIES]; } pud_t;
typedef struct p4d_t { pud_t* pud[LV_ENTRIES]; } p4d_t;
typedef struct pgd_t { p4d_t* p4d[LV_ENTRIES]; } pgd_t;

/* ===== prototypes ===== */
int init_pte(addr_t* pte, int pre, addr_t fpn, int drt, int swp, int swptyp, addr_t swpoff);

int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt);
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt);

uint32_t pte_get_entry(struct pcb_t* caller, addr_t pgn);
int      pte_set_entry(struct pcb_t* caller, addr_t pgn, uint32_t pte_val);
int      pte_set_fpn(struct pcb_t* caller, addr_t pgn, addr_t fpn);
int      pte_set_swap(struct pcb_t* caller, addr_t pgn, int swptyp, addr_t swpoff);

int    vmap_pgd_memset(struct pcb_t* caller, addr_t addr, int pgnum);
addr_t vmap_page_range(struct pcb_t* caller, addr_t addr, int pgnum,
    struct framephy_struct* frames, struct vm_rg_struct* ret_rg);
addr_t alloc_pages_range(struct pcb_t* caller, int req_pgnum, struct framephy_struct** frm_lst);
addr_t vm_map_ram(struct pcb_t* caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum,
    struct vm_rg_struct* ret_rg);

int __swap_cp_page(struct memphy_struct* mpsrc, addr_t srcfpn,
    struct memphy_struct* mpdst, addr_t dstfpn);

int init_mm(struct mm_struct* mm, struct pcb_t* caller);
int free_pcb_memph(struct pcb_t* caller);

int enlist_pgn_node(struct pgn_t** plist, addr_t pgn);
int find_victim_page(struct mm_struct* mm, addr_t* retpgn);

int print_pgtbl(struct pcb_t* proc, addr_t start, addr_t end);

#endif /* MM64_H */
