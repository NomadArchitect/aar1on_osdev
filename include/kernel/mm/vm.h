//
// Created by Aaron Gill-Braun on 2020-10-03.
//

#ifndef KERNEL_MM_VM_H
#define KERNEL_MM_VM_H

#include <mm/mm.h>

#define entry_to_table(entry) \
  ((uint64_t *) phys_to_virt((entry) & 0xFFFFFFFFFF000))

#define entry_to_addr(entry) \
  ((entry) & (~0xFFF))

#define R_ENTRY 510ULL
#define TEMP_ENTRY 511L
#define TEMP_PAGE 0xFFFFFFFFFFFFF000

#define LOW_HALF_START 0x0000000000000000
#define LOW_HALF_END 0x00007FFFFFFFFFFF
#define HIGH_HALF_START 0xFFFF800000000000
#define HIGH_HALF_END 0xFFFFFFFFFFFFFFFF

typedef struct vm_area {
  uintptr_t base;
  size_t size;
  page_t *pages;
} vm_area_t;

typedef enum {
  AT_ADDRESS,
  ABOVE_ADDRESS
} vm_search_type_t;

void vm_init();
void *vm_map_page(page_t *page);
void *vm_map_addr(uintptr_t phys_addr, size_t len, uint16_t flags);
void *vm_map_vaddr(uintptr_t virt_addr, uintptr_t phys_addr, size_t len, uint16_t flags);

vm_area_t *vm_get_vm_area(uintptr_t address);
bool vm_find_free_area(vm_search_type_t search_type, uintptr_t *addr, size_t len);

#endif
