#ifndef _ARCH_PAGE_H_
#define _ARCH_PAGE_H_

#include <const.h>

#define CONFIG_ARM64_VA_BITS    39
#define CONFIG_PGTABLE_LEVELS   3
#define CONFIG_ARM64_PAGE_SHIFT 12

#define VA_BITS          (CONFIG_ARM64_VA_BITS)
#define _PAGE_OFFSET(va) (-(UL(1) << (va)))
#define PAGE_OFFSET      (_PAGE_OFFSET(VA_BITS))

#define KERNEL_VMA  (_PAGE_END(VA_BITS_MIN))
#define FIXADDR_TOP (UL(1) << (VA_BITS - 1))
#define PKMAP_SIZE  (128 << 20)
#define PKMAP_START FIXADDR_TOP
#define PKMAP_END   (FIXADDR_TOP + PKMAP_SIZE)

#define MM_PAGE_OFFSET 0x1000000000UL
#define VMALLOC_START  0x2000000000UL
#define VMALLOC_END    0x3000000000UL

#define VM_USER_START (-(UL(1) << (VA_BITS)))
#define VM_STACK_TOP  (-ARCH_PMD_SIZE)

#if VA_BITS > 48
#define VA_BITS_MIN (48)
#else
#define VA_BITS_MIN (VA_BITS)
#endif

#define _PAGE_END(va) (-(UL(1) << ((va)-1)))
#define PAGE_END      (_PAGE_END(VA_BITS_MIN))

/* Memory types. */
#define MT_NORMAL        0
#define MT_NORMAL_TAGGED 1
#define MT_NORMAL_NC     2
#define MT_DEVICE_nGnRnE 3
#define MT_DEVICE_nGnRE  4

#define IDMAP_PGTABLE_LEVELS (CONFIG_PGTABLE_LEVELS - 1)
#define INIT_PGTABLE_LEVELS  (CONFIG_PGTABLE_LEVELS - 1)

#define IDMAP_DIR_SIZE (IDMAP_PGTABLE_LEVELS * ARCH_PG_SIZE)
#define INIT_DIR_SIZE  (ARCH_PG_SIZE)
#define MM_DIR_SIZE    (ARCH_PG_SIZE)

#define ARM64_HW_PGTABLE_LEVEL_SHIFT(n) ((ARCH_PG_SHIFT - 3) * (4 - (n)) + 3)

#define ARM64_PG_SHIFT      CONFIG_ARM64_PAGE_SHIFT
#define ARM64_PG_SIZE       (_AC(1, UL) << ARM64_PG_SHIFT)
#define ARM64_PG_MASK       (~(ARM64_PG_SIZE - 1))
#define ARM64_VM_PT_ENTRIES (1 << (ARM64_PG_SHIFT - 3))

#ifndef __ASSEMBLY__

typedef unsigned long pteval_t;
typedef unsigned long pmdval_t;
typedef unsigned long pudval_t;
typedef unsigned long pdeval_t;

typedef struct {
    pdeval_t pde;
} pde_t;

#if CONFIG_PGTABLE_LEVELS > 3
typedef struct {
    pudval_t pud;
} pud_t;

#define pud_val(x) ((x).pud)
#define __pud(x)   ((pud_t){(x)})
#endif

#if CONFIG_PGTABLE_LEVELS > 2
typedef struct {
    pmdval_t pmd;
} pmd_t;

#define pmd_val(x) ((x).pmd)
#define __pmd(x)   ((pmd_t){(x)})
#endif

typedef struct {
    pteval_t pte;
} pte_t;

typedef struct {
    unsigned long pgprot;
} pgprot_t;

#define pde_val(x) ((x).pde)
#define __pde(x)   ((pde_t){(x)})

#define pte_val(x) ((x).pte)
#define __pte(x)   ((pte_t){(x)})

#define pgprot_val(x) ((x).pgprot)
#define __pgprot(x)   ((pgprot_t){(x)})

/* struct page_directory */
typedef struct {
    /* physical address of page dir */
    phys_addr_t phys_addr;
    /* virtual address of page dir */
    pde_t* vir_addr;
} pgdir_t;

#endif

#define ARCH_PG_SIZE       ARM64_PG_SIZE
#define ARCH_PG_SHIFT      ARM64_PG_SHIFT
#define ARCH_PG_MASK       ARM64_PG_MASK
#define ARCH_VM_PT_ENTRIES ARM64_VM_PT_ENTRIES

#if CONFIG_PGTABLE_LEVELS > 2
#define ARCH_PMD_SHIFT      ARM64_HW_PGTABLE_LEVEL_SHIFT(2)
#define ARCH_PMD_SIZE       (_AC(1, UL) << ARCH_PMD_SHIFT)
#define ARCH_PMD_MASK       (~(ARCH_PMD_SIZE - 1))
#define ARCH_VM_PMD_ENTRIES ARCH_VM_PT_ENTRIES
#endif

#if CONFIG_PGTABLE_LEVELS > 3
#define ARCH_PUD_SHIFT      ARM64_HW_PGTABLE_LEVEL_SHIFT(1)
#define ARCH_PUD_SIZE       (_AC(1, UL) << ARCH_PUD_SHIFT)
#define ARCH_PUD_MASK       (~(ARCH_PUD_SIZE - 1))
#define ARCH_VM_PUD_ENTRIES ARCH_VM_PT_ENTRIES
#endif

#define ARCH_PGD_SHIFT      ARM64_HW_PGTABLE_LEVEL_SHIFT(4 - CONFIG_PGTABLE_LEVELS)
#define ARCH_PGD_SIZE       (_AC(1, UL) << ARCH_PGD_SHIFT)
#define ARCH_PGD_MASK       (~(ARCH_PGD_SIZE - 1))
#define ARCH_VM_DIR_ENTRIES (1 << (VA_BITS - ARCH_PGD_SHIFT))

#define ARCH_BIG_PAGE_SIZE ARCH_PMD_SIZE

#define ARCH_PTE(v) \
    (((unsigned long)(v) >> ARCH_PG_SHIFT) & (ARCH_VM_PT_ENTRIES - 1))
#define ARCH_PMDE(v) \
    (((unsigned long)(v) >> ARCH_PMD_SHIFT) & (ARCH_VM_PMD_ENTRIES - 1))
#define ARCH_PDE(v) \
    (((unsigned long)(v) >> ARCH_PGD_SHIFT) & (ARCH_VM_DIR_ENTRIES - 1))

#define _ARM64_PGD_TYPE_TABLE (_AT(pdeval_t, 3) << 0)
#define _ARM64_PGD_TYPE_SECT  (_AT(pdeval_t, 1) << 0)

#define _ARM64_PUD_TYPE_TABLE (_AT(pudval_t, 3) << 0)
#define _ARM64_PUD_TYPE_MASK  (_AT(pudval_t, 3) << 0)
#define _ARM64_PUD_TABLE_BIT  (_AT(pudval_t, 1) << 1)
#define _ARM64_PUD_TYPE_SECT  (_AT(pudval_t, 1) << 0)

#define _ARM64_PMD_TYPE_TABLE (_AT(pmdval_t, 3) << 0)
#define _ARM64_PMD_TYPE_MASK  (_AT(pmdval_t, 3) << 0)
#define _ARM64_PMD_TABLE_BIT  (_AT(pmdval_t, 1) << 1)
#define _ARM64_PMD_TYPE_SECT  (_AT(pmdval_t, 1) << 0)

#define _ARM64_SECT_VALID  (_AT(pmdval_t, 1) << 0)
#define _ARM64_SECT_USER   (_AT(pmdval_t, 1) << 6)
#define _ARM64_SECT_RDONLY (_AT(pmdval_t, 1) << 7)
#define _ARM64_SECT_S      (_AT(pmdval_t, 3) << 8)
#define _ARM64_SECT_AF     (_AT(pmdval_t, 1) << 10)
#define _ARM64_SECT_NG     (_AT(pmdval_t, 1) << 11)
#define _ARM64_SECT_CONT   (_AT(pmdval_t, 1) << 52)
#define _ARM64_SECT_PXN    (_AT(pmdval_t, 1) << 53)
#define _ARM64_SECT_UXN    (_AT(pmdval_t, 1) << 54)

#define _ARM64_PTE_VALID     (_AT(pteval_t, 1) << 0)
#define _ARM64_PTE_TYPE_MASK (_AT(pteval_t, 3) << 0)
#define _ARM64_PTE_TYPE_PAGE (_AT(pteval_t, 3) << 0)
#define _ARM64_PTE_TABLE_BIT (_AT(pteval_t, 1) << 1)
#define _ARM64_PTE_USER      (_AT(pteval_t, 1) << 6)  /* AP[1] */
#define _ARM64_PTE_RDONLY    (_AT(pteval_t, 1) << 7)  /* AP[2] */
#define _ARM64_PTE_SHARED    (_AT(pteval_t, 3) << 8)  /* SH[1:0] */
#define _ARM64_PTE_AF        (_AT(pteval_t, 1) << 10) /* Access Flag */
#define _ARM64_PTE_NG        (_AT(pteval_t, 1) << 11) /* nG */
#define _ARM64_PTE_GP        (_AT(pteval_t, 1) << 50) /* BTI guarded */
#define _ARM64_PTE_DBM       (_AT(pteval_t, 1) << 51) /* Dirty Bit Management */
#define _ARM64_PTE_CONT      (_AT(pteval_t, 1) << 52) /* Contiguous range */
#define _ARM64_PTE_PXN       (_AT(pteval_t, 1) << 53) /* Privileged XN */
#define _ARM64_PTE_UXN       (_AT(pteval_t, 1) << 54) /* User XN */

#define _ARM64_PTE_ADDR_LOW \
    (((_AT(pteval_t, 1) << (48 - ARCH_PG_SHIFT)) - 1) << ARCH_PG_SHIFT)
#define _ARM64_PTE_ADDR_MASK _ARM64_PTE_ADDR_LOW

#define _ARM64_PTE_ATTRINDX(t)   (_AT(pteval_t, (t)) << 2)
#define _ARM64_PTE_ATTRINDX_MASK (_AT(pteval_t, 7) << 2)

#define SWAPPER_TABLE_SHIFT ARCH_PUD_SHIFT
#define SWAPPER_BLOCK_SHIFT ARCH_PMD_SHIFT
#define SWAPPER_BLOCK_SIZE  ARCH_PMD_SIZE

/*
 * TCR flags.
 */
#define TCR_T0SZ_OFFSET 0
#define TCR_T1SZ_OFFSET 16
#define TCR_T0SZ(x)     ((UL(64) - (x)) << TCR_T0SZ_OFFSET)
#define TCR_T1SZ(x)     ((UL(64) - (x)) << TCR_T1SZ_OFFSET)
#define TCR_TxSZ(x)     (TCR_T0SZ(x) | TCR_T1SZ(x))
#define TCR_TxSZ_WIDTH  6
#define TCR_T0SZ_MASK   (((UL(1) << TCR_TxSZ_WIDTH) - 1) << TCR_T0SZ_OFFSET)
#define TCR_T1SZ_MASK   (((UL(1) << TCR_TxSZ_WIDTH) - 1) << TCR_T1SZ_OFFSET)

#define TCR_EPD0_SHIFT  7
#define TCR_EPD0_MASK   (UL(1) << TCR_EPD0_SHIFT)
#define TCR_IRGN0_SHIFT 8
#define TCR_IRGN0_MASK  (UL(3) << TCR_IRGN0_SHIFT)
#define TCR_IRGN0_NC    (UL(0) << TCR_IRGN0_SHIFT)
#define TCR_IRGN0_WBWA  (UL(1) << TCR_IRGN0_SHIFT)
#define TCR_IRGN0_WT    (UL(2) << TCR_IRGN0_SHIFT)
#define TCR_IRGN0_WBnWA (UL(3) << TCR_IRGN0_SHIFT)

#define TCR_EPD1_SHIFT  23
#define TCR_EPD1_MASK   (UL(1) << TCR_EPD1_SHIFT)
#define TCR_IRGN1_SHIFT 24
#define TCR_IRGN1_MASK  (UL(3) << TCR_IRGN1_SHIFT)
#define TCR_IRGN1_NC    (UL(0) << TCR_IRGN1_SHIFT)
#define TCR_IRGN1_WBWA  (UL(1) << TCR_IRGN1_SHIFT)
#define TCR_IRGN1_WT    (UL(2) << TCR_IRGN1_SHIFT)
#define TCR_IRGN1_WBnWA (UL(3) << TCR_IRGN1_SHIFT)

#define TCR_IRGN_NC    (TCR_IRGN0_NC | TCR_IRGN1_NC)
#define TCR_IRGN_WBWA  (TCR_IRGN0_WBWA | TCR_IRGN1_WBWA)
#define TCR_IRGN_WT    (TCR_IRGN0_WT | TCR_IRGN1_WT)
#define TCR_IRGN_WBnWA (TCR_IRGN0_WBnWA | TCR_IRGN1_WBnWA)
#define TCR_IRGN_MASK  (TCR_IRGN0_MASK | TCR_IRGN1_MASK)

#define TCR_ORGN0_SHIFT 10
#define TCR_ORGN0_MASK  (UL(3) << TCR_ORGN0_SHIFT)
#define TCR_ORGN0_NC    (UL(0) << TCR_ORGN0_SHIFT)
#define TCR_ORGN0_WBWA  (UL(1) << TCR_ORGN0_SHIFT)
#define TCR_ORGN0_WT    (UL(2) << TCR_ORGN0_SHIFT)
#define TCR_ORGN0_WBnWA (UL(3) << TCR_ORGN0_SHIFT)

#define TCR_ORGN1_SHIFT 26
#define TCR_ORGN1_MASK  (UL(3) << TCR_ORGN1_SHIFT)
#define TCR_ORGN1_NC    (UL(0) << TCR_ORGN1_SHIFT)
#define TCR_ORGN1_WBWA  (UL(1) << TCR_ORGN1_SHIFT)
#define TCR_ORGN1_WT    (UL(2) << TCR_ORGN1_SHIFT)
#define TCR_ORGN1_WBnWA (UL(3) << TCR_ORGN1_SHIFT)

#define TCR_ORGN_NC    (TCR_ORGN0_NC | TCR_ORGN1_NC)
#define TCR_ORGN_WBWA  (TCR_ORGN0_WBWA | TCR_ORGN1_WBWA)
#define TCR_ORGN_WT    (TCR_ORGN0_WT | TCR_ORGN1_WT)
#define TCR_ORGN_WBnWA (TCR_ORGN0_WBnWA | TCR_ORGN1_WBnWA)
#define TCR_ORGN_MASK  (TCR_ORGN0_MASK | TCR_ORGN1_MASK)

#define TCR_SH0_SHIFT 12
#define TCR_SH0_MASK  (UL(3) << TCR_SH0_SHIFT)
#define TCR_SH0_INNER (UL(3) << TCR_SH0_SHIFT)

#define TCR_SH1_SHIFT 28
#define TCR_SH1_MASK  (UL(3) << TCR_SH1_SHIFT)
#define TCR_SH1_INNER (UL(3) << TCR_SH1_SHIFT)
#define TCR_SHARED    (TCR_SH0_INNER | TCR_SH1_INNER)

#define TCR_TG0_SHIFT 14
#define TCR_TG0_MASK  (UL(3) << TCR_TG0_SHIFT)
#define TCR_TG0_4K    (UL(0) << TCR_TG0_SHIFT)
#define TCR_TG0_64K   (UL(1) << TCR_TG0_SHIFT)
#define TCR_TG0_16K   (UL(2) << TCR_TG0_SHIFT)

#define TCR_TG1_SHIFT 30
#define TCR_TG1_MASK  (UL(3) << TCR_TG1_SHIFT)
#define TCR_TG1_16K   (UL(1) << TCR_TG1_SHIFT)
#define TCR_TG1_4K    (UL(2) << TCR_TG1_SHIFT)
#define TCR_TG1_64K   (UL(3) << TCR_TG1_SHIFT)

#define TCR_IPS_SHIFT 32
#define TCR_IPS_MASK  (UL(7) << TCR_IPS_SHIFT)
#define TCR_A1        (UL(1) << 22)
#define TCR_ASID16    (UL(1) << 36)
#define TCR_TBI0      (UL(1) << 37)
#define TCR_TBI1      (UL(1) << 38)
#define TCR_HA        (UL(1) << 39)
#define TCR_HD        (UL(1) << 40)
#define TCR_TBID1     (UL(1) << 52)
#define TCR_NFD0      (UL(1) << 53)
#define TCR_NFD1      (UL(1) << 54)
#define TCR_E0PD0     (UL(1) << 55)
#define TCR_E0PD1     (UL(1) << 56)
#define TCR_TCMA0     (UL(1) << 57)
#define TCR_TCMA1     (UL(1) << 58)

#define __va(x) ((void*)((unsigned long)(x) + va_pa_offset))
#define __pa(x) ((phys_addr_t)(x)-va_pa_offset)

#ifndef __ASSEMBLY__

#ifdef __UM__
#define va_pa_offset 0
#else
extern unsigned long va_pa_offset;
#endif

extern unsigned long kimage_voffset;

static inline unsigned long virt_to_phys(void* x) { return __pa(x); }
static inline void* phys_to_virt(unsigned long x) { return __va(x); }

void early_fixmap_init(void);

void paging_init(void);

void* ioremap_nc(unsigned long phys_addr, size_t size);

int pgd_new(pgdir_t* pgd);
int pgd_free(pgdir_t* pgd);
int pgd_writemap(pgdir_t* pgd, phys_addr_t phys_addr, unsigned long vir_addr,
                 size_t length, pgprot_t prot);
void pgd_unmap_memory(pgdir_t* pgd, unsigned long vir_addr, size_t length);

size_t pgd_va2pa_range(pgdir_t* pgd, unsigned long vir_addr,
                       phys_addr_t* phys_addr, size_t bytes);

void pgd_free_range(pgdir_t* pgd, unsigned long addr, unsigned long end,
                    unsigned long floor, unsigned long ceiling);

void switch_address_space(pgdir_t* pgd);

void* map_scratchpad(unsigned long offset, size_t* size);

#endif

#endif
