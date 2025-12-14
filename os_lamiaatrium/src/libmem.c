/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

 /* LamiaAtrium release
  * Source Code License Grant: The authors hereby grant to Licensee
  * personal permission to use and modify the Licensed Source Code
  * for the sole purpose of studying while attending the course CO2018.
  */

  /*
   * System Library
   * Memory Module Library libmem.c  (64-bit, port từ bản 32-bit đã làm)
   */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include "mem.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;
int enable_mem_log = 1;



/* Forward decl cho pg_setval dùng trong __free */
int pg_setval(struct mm_struct* mm, int addr, BYTE value, struct pcb_t* caller);

/* --------------------------------------------------------------------- */
/*  QUẢN LÝ DANH SÁCH VÙNG TRỐNG                                         */
/* --------------------------------------------------------------------- */

/* enlist_vm_freerg_list - add new rg to freerg_list
 * @mm:  memory region
 * @rg_elmt: new region
 */
int enlist_vm_freerg_list(struct mm_struct* mm, struct vm_rg_struct* rg_elmt)
{
    struct vm_rg_struct* rg_node;

    if (!mm || !mm->mmap)
        return -1;

    rg_node = mm->mmap->vm_freerg_list;

    if (rg_elmt->rg_start >= rg_elmt->rg_end)
        return -1;

    if (rg_node != NULL)
        rg_elmt->rg_next = rg_node;
    else
        rg_elmt->rg_next = NULL;

    /* Enlist the new region */
    mm->mmap->vm_freerg_list = rg_elmt;

    return 0;
}

/* get_symrg_byid - get mem region by region ID (symbol index)
 */
struct vm_rg_struct* get_symrg_byid(struct mm_struct* mm, int rgid)
{
    if (!mm)
        return NULL;
    if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
        return NULL;

    return &mm->symrgtbl[rgid];
}

/* --------------------------------------------------------------------- */
/*  CẤP PHÁT / THU HỒI VÙNG BỘ NHỚ (SYMRG)                               */
/* --------------------------------------------------------------------- */

/* __alloc - allocate a region memory (theo phong cách 32-bit của bạn)
 * @caller : process của user (nhưng mm/mem nằm trong caller->krnl)
 * @vmaid  : vm area id (lab này dùng 0)
 * @rgid   : symbol-region id (index vào symrgtbl + register index)
 * @size   : requested size (byte)
 * @alloc_addr : OUT – địa chỉ ảo đầu vùng
 */
int __alloc(struct pcb_t* caller, int vmaid, int rgid, addr_t size, addr_t* alloc_addr)
{
    if (!caller || !caller->krnl || !caller->krnl->mm)
        return -1;

    struct mm_struct* mm = caller->krnl->mm;
    struct vm_rg_struct* oldrg = &mm->symrgtbl[rgid];

    /* ====== BƯỚC 1: nếu đã có region cũ → free TRƯỚC, KHÔNG giữ lock ====== */
    if (!(oldrg->rg_start == 0 && oldrg->rg_end == 0)) {
        __free(caller, vmaid, rgid);
    }
    else {
        oldrg->rg_start = oldrg->rg_end = 0; // slot rác -> reset
    }

    /* ====== BƯỚC 2: bắt đầu alloc thật sự ====== */
    pthread_mutex_lock(&mmvm_lock);

    struct vm_area_struct* cur_vma = get_vma_by_num(mm, vmaid);
    struct vm_rg_struct rgnode;

    if (!cur_vma) {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }

    /* 1. Thử free list */
    if (get_free_vmrg_area(caller, vmaid, (int)size, &rgnode) == 0) {
        mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
        mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
        *alloc_addr = rgnode.rg_start;
        caller->regs[rgid] = (uint64_t)rgnode.rg_start;

        pthread_mutex_unlock(&mmvm_lock);
        return 0;
    }

    /* 2. Gọi syscall tăng vma */
    addr_t old_sbrk = cur_vma->sbrk;

    struct sc_regs regs;
    regs.a1 = SYSMEM_INC_OP;
    regs.a2 = vmaid;
    regs.a3 = (uint64_t)size;

    if (syscall(caller->krnl, caller->pid, 17, &regs) < 0) {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }

    if (get_free_vmrg_area(caller, vmaid, (int)size, &rgnode) == 0) {
        mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
        mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
        *alloc_addr = rgnode.rg_start;
        caller->regs[rgid] = (uint64_t)rgnode.rg_start;

        pthread_mutex_unlock(&mmvm_lock);
        return 0;
    }

    /* fallback */
    mm->symrgtbl[rgid].rg_start = old_sbrk;
    mm->symrgtbl[rgid].rg_end = old_sbrk + size;
    *alloc_addr = old_sbrk;
    caller->regs[rgid] = (uint64_t)old_sbrk;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
}


/* __free - remove a region memory (port từ bản 32-bit, có merge free region)
 */
int __free(struct pcb_t* caller, int vmaid, int rgid)
{
    if (!caller || !caller->krnl || !caller->krnl->mm)
        return -1;

    pthread_mutex_lock(&mmvm_lock);

    struct mm_struct* mm = caller->krnl->mm;
    struct vm_rg_struct* rgnode = get_symrg_byid(mm, rgid);

    if (!rgnode) {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }
    if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ) {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }

    /* Xóa dữ liệu trong vùng bằng 0 (giống code 32-bit dùng pg_setval) */
    for (int i = (int)rgnode->rg_start; i <= (int)rgnode->rg_end; i++) {
        pg_setval(mm, i, 0, caller);
    }

    struct vm_area_struct* cur_vma = get_vma_by_num(mm, vmaid);
    if (!cur_vma) {
        pthread_mutex_unlock(&mmvm_lock);
        return -1;
    }

    /* Tạo node free mới bằng chính vùng vừa free */
    struct vm_rg_struct* nrgnode = (struct vm_rg_struct*)malloc(sizeof(struct vm_rg_struct));
    nrgnode->rg_start = rgnode->rg_start;
    nrgnode->rg_end = rgnode->rg_end;
    nrgnode->rg_next = NULL;

    /* Gộp với các free region kề nhau (nếu có) – giống logic 32-bit */
    struct vm_rg_struct* rgit = cur_vma->vm_freerg_list;
    struct vm_rg_struct* prev_rgit = NULL;

    while (rgit != NULL) {
        if (nrgnode->rg_start == rgit->rg_end) {           /* [rgit] ... [nrgnode] dính đầu/cuối */
            nrgnode->rg_start = rgit->rg_start;
            if (prev_rgit)
                prev_rgit->rg_next = rgit->rg_next;
            else
                cur_vma->vm_freerg_list = rgit->rg_next;
        }
        else if (nrgnode->rg_end == rgit->rg_start) {    /* [nrgnode] ... [rgit] dính đầu/cuối */
            nrgnode->rg_end = rgit->rg_end;
            if (prev_rgit)
                prev_rgit->rg_next = rgit->rg_next;
            else
                cur_vma->vm_freerg_list = rgit->rg_next;
        }
        else {
            prev_rgit = rgit;
        }
        rgit = rgit->rg_next;
    }

    /* Đưa region đã gộp vào free list */
    enlist_vm_freerg_list(mm, nrgnode);

    /* Reset symrgtbl slot */
    rgnode->rg_start = 0;
    rgnode->rg_end = 0;
    rgnode->rg_next = NULL;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
}

/* --------------------------------------------------------------------- */
/*  API LIB CHO LỆNH ALLOC/FREE                                          */
/* --------------------------------------------------------------------- */

int liballoc(struct pcb_t* proc, addr_t size, uint32_t reg_index)
{

    addr_t addr = 0;

    int ret = __alloc(proc, 0, (int)reg_index, size, &addr);

#ifdef IODUMP
    if (enable_mem_log) {
        printf("liballoc:%d\n", __LINE__);
        print_pgtbl(proc, 0, -1);
}
#endif

    return ret;
}

int libfree(struct pcb_t* proc, uint32_t reg_index)
{
    if (!proc || reg_index >= PAGING_MAX_SYMTBL_SZ)
        return -1;

    int ret = __free(proc, 0, (int)reg_index);

#ifdef IODUMP
    if (enable_mem_log) {
        printf("libfree:%d\n", __LINE__);
        print_pgtbl(proc, 0, -1);
}
#endif

    return ret;
}

/* --------------------------------------------------------------------- */
/*  TRUY XUẤT / GHI GIÁ TRỊ QUA TRANG – SWAPIN/SWAPOUT                   */
/* --------------------------------------------------------------------- */

/* pg_getpage - get the page in ram
 *  nếu page không online thì thực hiện thay thế trang (victim + swap)
 */
int pg_getpage(struct mm_struct* mm, int pgn, int* fpn, struct pcb_t* caller)
{
    if (!mm || !fpn || !caller || !caller->krnl) return -1;

    uint32_t pte = pte_get_entry(caller, (addr_t)pgn);

    /* Case 1: page is present in RAM */
    if (PAGING_PAGE_PRESENT(pte)) {
        *fpn = (int)PAGING_FPN(pte);
        return 0;
    }

    /* Case 2: page NOT present => must swap in (victim + swap) */
    addr_t vicpgn;
    if (find_victim_page(mm, &vicpgn) < 0) {
        /* Nếu FIFO trống: tuỳ đề có thể coi là lỗi */
        return -1;
    }

    uint32_t vicpte = pte_get_entry(caller, vicpgn);

    /* victim phải đang present thì mới có fpn để đẩy ra swap */
    if (!PAGING_PAGE_PRESENT(vicpte)) {
        return -1;
    }

    addr_t vicfpn = (addr_t)PAGING_FPN(vicpte);

    /* lấy frame trống trong swap */
    addr_t swpfpn;
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) != 0) {
        return -1;
    }

    /* RAM(victim) -> SWAP(swpfpn) */
    {
        struct sc_regs regs;
        regs.a1 = SYSMEM_SWP_OP;
        regs.a2 = (uint64_t)vicfpn;
        regs.a3 = (uint64_t)swpfpn;
        if (syscall(caller->krnl, caller->pid, 17, &regs) < 0) return -1;
    }

    /* update victim PTE => swapped */
    if (pte_set_swap(caller, vicpgn, 0, swpfpn) < 0) return -1;

    /* lấy “swap slot” của trang target đang cần vào RAM
       Nếu target chưa từng swapped (pte=0) thì bạn phải có policy riêng.
       Ở bài LamiaAtrium thường target sẽ có swapped info khi bị đẩy ra trước đó. */
    if (!(pte & PAGING_PTE_SWAPPED_MASK)) {
        /* target chưa có swap backing => không swap-in được */
        return -1;
    }

    addr_t tgt_swpfpn = (addr_t)PAGING_SWP(pte);

    /* SWAP(target) -> RAM(vicfpn) */
    {
        struct sc_regs regs;
        regs.a1 = SYSMEM_SWP_OP;
        regs.a2 = (uint64_t)tgt_swpfpn;
        regs.a3 = (uint64_t)vicfpn;
        if (syscall(caller->krnl, caller->pid, 17, &regs) < 0) return -1;
    }

    /* update target PTE => present at vicfpn */
    if (pte_set_fpn(caller, (addr_t)pgn, vicfpn) < 0) return -1;

    /* push target pgn vào FIFO */
    if (enlist_pgn_node(&mm->fifo_pgn, (addr_t)pgn) < 0) return -1;

    *fpn = (int)vicfpn;
    return 0;
}


    /* ===== CASE 3: page đã có trong RAM ===== */
   




/* pg_getval - đọc giá trị tại địa chỉ ảo addr */
int pg_getval(struct mm_struct* mm, int addr, BYTE* data, struct pcb_t* caller)
{
    if (!caller || !caller->krnl || !caller->krnl->mram)
        return -1;

    int pgn = PAGING_PGN(addr);
    int off = PAGING_OFFST(addr);
    int fpn;

    if (pg_getpage(mm, pgn, &fpn, caller) != 0)
        return -1;

    int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

    struct sc_regs regs;
    regs.a1 = SYSMEM_IO_READ;
    regs.a2 = (uint64_t)phyaddr;
    regs.a3 = 0;

    if (syscall(caller->krnl, caller->pid, 17, &regs) < 0)
        return -1;

    *data = (BYTE)regs.a3;
    return 0;
}

/* pg_setval - ghi giá trị tại địa chỉ ảo addr */
int pg_setval(struct mm_struct* mm, int addr, BYTE value, struct pcb_t* caller)
{
    if (!caller || !caller->krnl || !caller->krnl->mram)
        return -1;

    int pgn = PAGING_PGN(addr);
    int off = PAGING_OFFST(addr);
    int fpn;

    if (pg_getpage(mm, pgn, &fpn, caller) != 0)
        return -1;

    int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

    struct sc_regs regs;
    regs.a1 = SYSMEM_IO_WRITE;
    regs.a2 = (uint64_t)phyaddr;
    regs.a3 = (uint64_t)value;

    if (syscall(caller->krnl, caller->pid, 17, &regs) < 0)
        return -1;

    return 0;
}

/* --------------------------------------------------------------------- */
/*  ĐỌC / GHI VÙNG BỘ NHỚ THEO SYMBOL REGION                             */
/* --------------------------------------------------------------------- */

int __read(struct pcb_t* caller, int vmaid, int rgid, addr_t offset, BYTE* data)
{
    if (!caller || !caller->krnl || !caller->krnl->mm)
        return -1;

    struct vm_rg_struct* currg = get_symrg_byid(caller->krnl->mm, rgid);
    if (!currg)
        return -1;

    addr_t addr = currg->rg_start + offset;
    return pg_getval(caller->krnl->mm, (int)addr, data, caller);
}

int libread(
    struct pcb_t* proc,
    uint32_t source,   /* region id = index vào symrgtbl */
    addr_t offset,
    uint32_t* destination)
{
    BYTE data;
    int ret = __read(proc, 0, (int)source, offset, &data);
    if (ret != 0)
        return ret;

    *destination = (uint32_t)data;

#ifdef IODUMP
    if (enable_mem_log) {
        printf("libread:%d\n", __LINE__);
        print_pgtbl(proc, 0, -1);
}
#endif

    return 0;
}

int __write(struct pcb_t* caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
    if (!caller || !caller->krnl || !caller->krnl->mm)
        return -1;

    struct vm_rg_struct* currg = get_symrg_byid(caller->krnl->mm, rgid);
    struct vm_area_struct* cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

    if (!currg || !cur_vma)
        return -1;

    addr_t addr = currg->rg_start + offset;
    return pg_setval(caller->krnl->mm, (int)addr, value, caller);
}

int libwrite(
    struct pcb_t* proc,
    BYTE data,
    uint32_t destination, /* region id */
    addr_t offset)
{
    int ret = __write(proc, 0, (int)destination, offset, data);

#ifdef IODUMP
    if (enable_mem_log) {
        printf("libwrite:%d\n", __LINE__);
        print_pgtbl(proc, 0, -1);
}
#endif

    return ret;
}

/* --------------------------------------------------------------------- */
/*  FREE TẤT CẢ FRAME THUỘC VỀ MỘT PCB                                   */
/* --------------------------------------------------------------------- */
#if 0
int free_pcb_memph(struct pcb_t* caller)
{
    if (!caller || !caller->krnl || !caller->krnl->mm)
        return -1;

    struct mm_struct* mm = caller->krnl->mm;

    for (int pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++) {
        uint32_t pte = mm->pgd[pagenum];

        if (PAGING_PAGE_PRESENT(pte)) {
            int fpn = PAGING_FPN(pte);
            MEMPHY_put_freefp(caller->krnl->mram, fpn);
        }
        else {
            int fpn = PAGING_SWP(pte);
            MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
        }
    }

    return 0;
}

/* --------------------------------------------------------------------- */
/*  CHỌN TRANG NẠN NHÂN (FIFO)                                           */
/* --------------------------------------------------------------------- */

int find_victim_page(struct mm_struct* mm, addr_t* retpgn)
{
    if (!mm || !mm->fifo_pgn)
        return -1;

    /* FIFO: lấy node đầu danh sách */
    struct pgn_t* pg = mm->fifo_pgn;
    *retpgn = pg->pgn;

    mm->fifo_pgn = pg->pg_next;
    free(pg);

    return 0;
}
#endif
/* --------------------------------------------------------------------- */
/*  TÌM VÙNG FREE ĐỦ CHỖ                                                */
/* --------------------------------------------------------------------- */

int get_free_vmrg_area(struct pcb_t* caller, int vmaid, int size, struct vm_rg_struct* newrg)
{
    if (!caller || !caller->krnl || !caller->krnl->mm)
        return -1;

    struct vm_area_struct* cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
    if (!cur_vma)
        return -1;

    struct vm_rg_struct* rgit = cur_vma->vm_freerg_list;
    struct vm_rg_struct* prev_rgit = NULL;

    if (!rgit)
        return -1;

    while (rgit != NULL) {
        int gap = (int)(rgit->rg_end - rgit->rg_start + 1);
        if (size <= gap) {
            /* Lấy [rg_start, rg_start + size - 1] */
            newrg->rg_start = rgit->rg_start;
            newrg->rg_end = rgit->rg_start + size - 1;
            newrg->rg_next = NULL;

            if (newrg->rg_end == rgit->rg_end) {
                /* Dùng hết region hiện tại */
                if (prev_rgit)
                    prev_rgit->rg_next = rgit->rg_next;
                else
                    cur_vma->vm_freerg_list = rgit->rg_next;
            }
            else {
                /* Thu nhỏ region free lại từ phía đầu */
                rgit->rg_start += size;
            }

            return 0;
        }
        prev_rgit = rgit;
        rgit = rgit->rg_next;
    }

    return -1;
}
