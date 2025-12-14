/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

 /* LamiaAtrium release
  * Source Code License Grant: The authors hereby grant to Licensee
  * personal permission to use and modify the Licensed Source Code
  * for the sole purpose of studying while attending the course CO2018.
  */

  //#ifdef MM_PAGING
  /*
   * PAGING based Memory Management
   * Virtual memory module mm/mm-vm.c
   */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

   /*----------------------------------------------------------
    * 1. Tìm VMA theo vm_id
    *----------------------------------------------------------*/
struct vm_area_struct* get_vma_by_num(struct mm_struct* mm, int vmaid)
{
    if (mm == NULL || mm->mmap == NULL)
        return NULL;

    struct vm_area_struct* pvma = mm->mmap;

    while (pvma != NULL && pvma->vm_id < vmaid) {
        pvma = pvma->vm_next;
    }

    if (pvma != NULL && pvma->vm_id == vmaid)
        return pvma;

    return NULL;
}

/*----------------------------------------------------------
 * 2. Swap page giữa RAM và SWAP (kernel gọi qua syscall 17)
 *----------------------------------------------------------*/
int __mm_swap_page(struct pcb_t* caller, addr_t vicfpn, addr_t swpfpn)
{
    /* copy nội dung 1 page từ RAM sang SWAP hoặc ngược lại */
    __swap_cp_page(caller->krnl->mram,
        vicfpn,
        caller->krnl->active_mswp,
        swpfpn);
    return 0;
}

/*----------------------------------------------------------
 * 3. Lấy một vùng VM mới ở đỉnh (brk) của VMA
 *    - size      : số byte thực sự process yêu cầu
 *    - alignedsz : size đã được canh lề theo page size
 *    Trả về vm_rg_struct mô tả vùng mới [old_sbrk, old_sbrk+size)
 *    và cập nhật sbrk/vm_end của VMA.
 *----------------------------------------------------------*/
struct vm_rg_struct*
    get_vm_area_node_at_brk(struct pcb_t* caller,
        int vmaid,
        addr_t size,
        addr_t alignedsz)
{
    struct vm_area_struct* cur_vma =
        get_vma_by_num(caller->krnl->mm, vmaid);
    if (cur_vma == NULL)
        return NULL;

    struct vm_rg_struct* newrg =
        (struct vm_rg_struct*)malloc(sizeof(struct vm_rg_struct));
    if (newrg == NULL)
        return NULL;

    /* old_sbrk là nơi ta cấp phát cho biến mới */
    addr_t old_sbrk = cur_vma->sbrk;
    addr_t new_sbrk = old_sbrk + alignedsz;

    /* mở rộng vm_end nếu cần */
    if (new_sbrk > cur_vma->vm_end)
        cur_vma->vm_end = new_sbrk;

    /* vùng thực sự dùng cho biến (size có thể < alignedsz) */
    newrg->rg_start = old_sbrk;
    newrg->rg_end = old_sbrk + size;
    newrg->rg_next = NULL;

    /* cập nhật sbrk cho lần cấp phát sau */
    cur_vma->sbrk = new_sbrk;

    return newrg;
}

/*----------------------------------------------------------
 * 4. Kiểm tra vùng [vmastart, vmaend) có overlap với VMA khác không
 *    (ngoại trừ VMA có id = vmaid).
 *----------------------------------------------------------*/
int validate_overlap_vm_area(struct pcb_t* caller,
    int vmaid,
    addr_t vmastart,
    addr_t vmaend)
{
    if (caller == NULL || caller->krnl == NULL ||
        caller->krnl->mm == NULL)
        return -1;

    if (vmastart >= vmaend)
        return -1;

    struct mm_struct* mm = caller->krnl->mm;
    struct vm_area_struct* vma = mm->mmap;

    while (vma != NULL) {
        if (vma->vm_id != vmaid) {
            /* OVERLAP(a_start,a_end,b_start,b_end):
             * true nếu [a_start,a_end) giao [b_start,b_end)
             * Nếu macro OVERLAP đã có trong os-mm.h/common.h thì dùng luôn.
             * Nếu không, code thầy sẽ định nghĩa nó; mình chỉ dùng lại.
             */
#ifdef OVERLAP
            if (OVERLAP(vmastart, vmaend, vma->vm_start, vma->vm_end))
                return -1;
#else
            if (!(vmaend <= vma->vm_start || vma->vm_end <= vmastart))
                return -1;
#endif
        }
        vma = vma->vm_next;
    }

    return 0;
}

/*----------------------------------------------------------
 * 5. Tăng giới hạn VMA (sbrk/vm_end) để dành chỗ cho biến mới
 *    - caller : process gọi syscall
 *    - vmaid  : ID vm area (thường = 0)
 *    - inc_sz : số byte cần thêm (chưa căn theo page)
 *
 *    Ý tưởng:
 *      1) Căn inc_sz theo pagesize -> alignedsz
 *      2) Lấy vùng mới ở đỉnh heap bằng get_vm_area_node_at_brk
 *      3) Kiểm tra overlap với VMA khác
 *      4) (tuỳ chọn) map luôn RAM bằng vm_map_ram – ở đây mình
 *         để lazy alloc (mapping physical sẽ làm trong pg_getpage)
 *----------------------------------------------------------*/
int inc_vma_limit(struct pcb_t* caller, int vmaid, addr_t inc_sz)
{
    if (caller == NULL || caller->krnl == NULL ||
        caller->krnl->mm == NULL)
        return -1;

    if (inc_sz == 0)
        return 0;

    /* Căn kích thước theo page size */
    addr_t alignedsz;
#ifdef MM64
    alignedsz = PAGING64_PAGE_ALIGNSZ(inc_sz);
#else
    alignedsz = PAGING_PAGE_ALIGNSZ(inc_sz);
#endif

    if (alignedsz == 0)
        return -1;

    /* Lấy vùng mới ở đỉnh heap, đồng thời cập nhật sbrk/vm_end */
    struct vm_rg_struct* area =
        get_vm_area_node_at_brk(caller, vmaid, inc_sz, alignedsz);
    if (area == NULL)
        return -1;

    /* Kiểm tra không đè lên VMA khác */
    if (validate_overlap_vm_area(caller, vmaid,
        area->rg_start, area->rg_end) < 0) {
        free(area);
        return -1;
    }

    /* Ở mô hình lazy paging hiện tại, ta CHƯA cần map thật ra RAM ở đây.
     * Page sẽ được gán frame khi lần đầu đọc/ghi (pg_getpage trong libmem.c).
     *
     * Nếu thầy muốn chủ động map luôn, có thể bật đoạn code dưới
     * (cần implement vm_map_ram, alloc_pages_range, vmap_page_range):
     *
     *   addr_t old_end = area->rg_start;
     *   int incnumpage = alignedsz / PAGING_PAGESZ;
     *   if (vm_map_ram(caller,
     *                  area->rg_start,
     *                  area->rg_start + alignedsz,
     *                  old_end,
     *                  incnumpage,
     *                  area) < 0) {
     *       free(area);
     *       return -1;
     *   }
     */

     /* Vùng này dùng ngay cho biến (symrgtbl trong libmem),
      * nên không đưa vào vm_freerg_list tại đây.
      * Khi FREE thì __free() sẽ đẩy nó vào freerg_list sau.
      */

    free(area);
    return 0;
}

// #endif
