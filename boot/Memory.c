//
// Created by Aaron Gill-Braun on 2022-05-29.
//

#include <Memory.h>

#include <Library/UefiBootServicesTableLib.h>

#define OFFSET_PTR(ptr, offset) ((VOID *)((UINTN)(ptr) + (offset)))
#define TABLE_PTR(ptr, index) OFFSET_PTR(ptr, EFI_PAGES_TO_SIZE(index))

#define PT_OFFSET(a) (((a) >> 12) & 0x1FF)
#define PDT_OFFSET(a) (((a) >> 21) & 0x1FF)
#define PDPT_OFFSET(a) (((a) >> 30) & 0x1FF)
#define PML4_OFFSET(a) (((a) >> 39) & 0x1FF)

// page table entry flags
#define PE_P   0x001   // Present
#define PE_RW  0x002   // Read/Write
#define PE_US  0x004   // User/Supervisor
#define PE_PWT 0x008   // Write-Through
#define PE_PCD 0x010   // Cache-Disable
#define PE_S   0x080   // Page Size
#define PE_G   0x100   // Global
#define PE_NX  (1<<63) // No-Execute

#define PTE_FLAGS_MASK 0x7FFULL
#define PTE_ADDR(Addr) ((UINT64)(Addr) & ~(PTE_FLAGS_MASK))

#define TABLE_MAX_ENTRIES 512

extern UINT64 KernelPhysAddr;

PAGE_DESCRIPTOR *NewDescriptor(
  IN OUT PAGE_DESCRIPTOR *Prev,
  IN EFI_PHYSICAL_ADDRESS PhysAddr,
  IN EFI_VIRTUAL_ADDRESS VirtAddr,
  IN UINT64 NumPages,
  IN UINT64 Flags
) {
  ASSERT(PRE_EXIT_BOOT_SERVICES);
  PAGE_DESCRIPTOR *Descriptor = AllocateReservedPool(sizeof(PAGE_DESCRIPTOR));
  if (Descriptor == NULL) {
    PRINT_ERROR("Failed to allocate memory for memory descriptor");
    return NULL;
  }

  if (Prev != NULL) {
    Prev->Next = Descriptor;
  }

  Descriptor->PhysAddr = PhysAddr;
  Descriptor->VirtAddr = VirtAddr;
  Descriptor->NumPages = NumPages;
  Descriptor->Flags = Flags;
  Descriptor->Next = NULL;
  return Descriptor;
}

PAGE_DESCRIPTOR *AppendDescriptors(
  IN OUT PAGE_DESCRIPTOR *List,
  IN PAGE_DESCRIPTOR *Descriptors
) {
  PAGE_DESCRIPTOR *Last;
  if (List == NULL) {
    return Descriptors;
  }

  Last = List;
  while (Last->Next != NULL) {
    Last = Last->Next;
  }

  Last->Next = Descriptors;
  return List;
}

//

EFI_STATUS EFIAPI GetMemoryMap(OUT EFI_MEMORY_MAP *MemoryMap) {
  ASSERT(PRE_EXIT_BOOT_SERVICES);
  EFI_STATUS Status;
  if (MemoryMap == NULL) {
    return EFI_INVALID_PARAMETER;
  } else if (MemoryMap->Map != NULL) {
    FreePool(MemoryMap->Map);
  }

  MemoryMap->Map = NULL;
  MemoryMap->Size = 0;

RETRY:
  MemoryMap->Map = AllocateReservedPool(MemoryMap->Size);
  if (MemoryMap->Map == NULL) {
    PRINT_ERROR("Failed to allocate memory for memory map");
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->GetMemoryMap(
    &MemoryMap->Size,
    MemoryMap->Map,
    &MemoryMap->Key,
    &MemoryMap->DescSize,
    &MemoryMap->DescVersion
  );
  if (EFI_ERROR(Status)) {
    if (Status == EFI_BUFFER_TOO_SMALL) {
      FreePool(MemoryMap->Map);
      goto RETRY;
    }
    PRINT_ERROR("Failed to get memory map");
    return Status;
  }

  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI ExitBootServices(IN EFI_MEMORY_MAP *MemoryMap) {
  ASSERT(PRE_EXIT_BOOT_SERVICES);
  if (MemoryMap == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  EFI_STATUS Status = gBS->ExitBootServices(
    gImageHandle,
    MemoryMap->Key
  );
  if (EFI_ERROR(Status)) {
    PRINT_ERROR("Failed to exit boot services");
    return Status;
  }

  PostExitBootServices = TRUE;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI SetVirtualAddressMap(IN EFI_MEMORY_MAP *MemoryMap) {
  ASSERT(POST_EXIT_BOOT_SERVICES);
  if (MemoryMap == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  return gRT->SetVirtualAddressMap(
    MemoryMap->Size,
    MemoryMap->DescSize,
    MemoryMap->DescVersion,
    MemoryMap->Map
  );
}

//

BOOLEAN EFIAPI IsPageDescriptorInEfiMemoryRegion(IN PAGE_DESCRIPTOR *Desc, IN EFI_MEMORY_DESCRIPTOR *Region) {
  if (Desc == NULL || Region == NULL) {
    return FALSE;
  }

  UINTN DescSize = EFI_PAGES_TO_SIZE(Desc->NumPages);
  UINTN EfiRegionSize = EFI_PAGES_TO_SIZE(Region->NumberOfPages);

  return Desc->PhysAddr >= Region->PhysicalStart &&
    Desc->PhysAddr + DescSize <= Region->PhysicalStart + EfiRegionSize;
}

EFI_STATUS EFIAPI GetMemoryRegionType(IN EFI_MEMORY_TYPE Type) {
  switch (Type) {
    case EfiUnusableMemory:
      return MEMORY_UNUSABLE;
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiConventionalMemory:
      return MEMORY_USABLE;
    case EfiPalCode:
    case EfiMemoryMappedIOPortSpace:
    case EfiReservedMemoryType:
      return MEMORY_RESERVED;
    case EfiACPIReclaimMemory:
      return MEMORY_ACPI;
    case EfiACPIMemoryNVS:
      return MEMORY_ACPI_NVS;
    case EfiMemoryMappedIO:
      return MEMORY_MAPPED_IO;
    case EfiRuntimeServicesCode:
      return MEMORY_RUNTIME_CODE;
    case EfiRuntimeServicesData:
      return MEMORY_RUNTIME_DATA;
    case EfiPersistentMemory:
    default:
      return MEMORY_UNKNOWN;
  }
}

EFI_STATUS EFIAPI ConvertEfiMemoryMapToBootFormat(
  IN EFI_MEMORY_MAP *EfiMemoryMap,
  IN PAGE_DESCRIPTOR *KernelPages,
  OUT VOID *MapBuffer,
  IN OUT UINT32 *MapSize,
  OUT UINTN *TotalMem
) {
  ASSERT(PRE_EXIT_BOOT_SERVICES);
  if (EfiMemoryMap == NULL || MapBuffer == NULL || MapSize == NULL || TotalMem == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  UINTN BufferSize = *MapSize;
  UINTN MaxEntries = BufferSize / sizeof(memory_region_t);

  UINTN Total = 0;
  UINTN NumEntries = 0;
  memory_region_t *Map = MapBuffer;
  PAGE_DESCRIPTOR *KernelPage = KernelPages;
  EFI_MEMORY_DESCRIPTOR *Desc = EfiMemoryMap->Map;
  while (!AT_MEMORY_MAP_END(Desc, EfiMemoryMap)) {
    if (Desc->NumberOfPages == 0) {
      Desc = MEMORY_MAP_NEXT(Desc, EfiMemoryMap);
      continue;
    }

    uint32_t MemoryType = GetMemoryRegionType(Desc->Type);
    if (KernelPage && IsPageDescriptorInEfiMemoryRegion(KernelPage, Desc)) {
      // Add Entry for kernel pages
      if (NumEntries >= MaxEntries) {
        PRINT_ERROR("Not enough space for memory map");
        return EFI_BUFFER_TOO_SMALL;
      }

      if (KernelPage->Flags & PD_EXECUTE) {
        MemoryType = MEMORY_KERNEL_CODE;
      } else {
        MemoryType = MEMORY_KERNEL_DATA;
      }

      Map[NumEntries].type = MemoryType;
      Map[NumEntries].size = KernelPage->NumPages * EFI_PAGE_SIZE;
      Map[NumEntries].base = KernelPage->PhysAddr;
      NumEntries++;

      Total += KernelPage->NumPages * EFI_PAGE_SIZE;
      Desc->NumberOfPages -= KernelPage->NumPages;
      KernelPage = KernelPage->Next;
      continue;
    }

    if (NumEntries > 0 && Map[NumEntries - 1].type == MemoryType) {
      // merge adjacent regions of the same type
      Map[NumEntries - 1].size += Desc->NumberOfPages * EFI_PAGE_SIZE;
    } else {
      // add new region
      if (NumEntries >= MaxEntries) {
        PRINT_ERROR("Not enough space for memory map");
        return EFI_BUFFER_TOO_SMALL;
      }
      Map[NumEntries].type = MemoryType;
      Map[NumEntries].size = Desc->NumberOfPages * EFI_PAGE_SIZE;
      Map[NumEntries].base = Desc->PhysicalStart;
      NumEntries++;
    }

    Total += Desc->NumberOfPages * EFI_PAGE_SIZE;
    Desc = MEMORY_MAP_NEXT(Desc, EfiMemoryMap);
  }

  if (KernelPage != NULL) {
    PRINT_ERROR("Failed to add kernel pages to memory map");
    return EFI_INVALID_PARAMETER;
  }

  // write out actual size of memory map and total memory
  *MapSize = NumEntries * sizeof(memory_region_t);
  *TotalMem = Total;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI LocateFreeMemoryRegion(
  IN EFI_MEMORY_MAP *MemoryMap,
  IN UINTN NumPages,
  IN UINT64 AboveAddr,
  OUT UINT64 *Addr
) {
  if (ALIGN_VALUE(AboveAddr, EFI_PAGE_SIZE) != AboveAddr) {
    return EFI_INVALID_PARAMETER;
  }

  UINTN DesiredSize = EFI_PAGES_TO_SIZE(NumPages);
  EFI_MEMORY_DESCRIPTOR *Desc = MemoryMap->Map;
  while (!AT_MEMORY_MAP_END(Desc, MemoryMap)) {
    if (Desc->Type != EfiConventionalMemory) {
      goto NEXT;
    }

    UINT64 RangeStart = Desc->PhysicalStart;
    UINTN RangeSize = EFI_PAGES_TO_SIZE(Desc->NumberOfPages);
    if (RangeStart < AboveAddr) {
      if (RangeStart + RangeSize > (AboveAddr + DesiredSize)) {
        *Addr = AboveAddr;
        return EFI_SUCCESS;
      } else {
        goto NEXT;
      }
    }

    if (RangeSize >= DesiredSize) {
      *Addr = RangeStart;
      return EFI_SUCCESS;
    }

  NEXT:
    Desc = MEMORY_MAP_NEXT(Desc, MemoryMap);
  }

  return EFI_NOT_FOUND;
}

//
// Paging
//

UINT16 EFIAPI PageDescriptorFlagsToEntryFlags(IN UINT64 PDFlags) {
  UINT16 Flags = 0;
  if (PDFlags & PD_WRITE)
    Flags |= PE_RW;
  if (PDFlags & PD_NOCACHE)
    Flags |= PE_PCD;
  if (PDFlags & PD_WRTHRU)
    Flags |= PE_PWT;
  if (PDFlags & PD_SIZE_2MB || PDFlags & PD_SIZE_1GB)
    Flags |= PE_S;

  return Flags;
}

VOID EFIAPI FillTableWithEntries(
  IN UINT64 *Table,
  IN UINTN StartIndex,
  IN UINTN NumEntries,
  IN UINT64 StartPhysAddr,
  IN UINTN Stride,
  IN UINT16 Flags
) {
  ASSERT(Table != NULL);
  ASSERT(StartIndex < TABLE_MAX_ENTRIES);
  ASSERT(NumEntries < TABLE_MAX_ENTRIES - StartIndex);

  UINT64 PhysAddr = StartPhysAddr;
  for (UINTN Index = StartIndex; Index < StartIndex + NumEntries; Index++) {
    Table[Index] = PTE_ADDR(PhysAddr) | Flags;
    PhysAddr += Stride;
  }
}

EFI_STATUS EFIAPI SetupKernelPageTables(IN PAGE_DESCRIPTOR *Descriptors, OUT UINT64 *PML4Address) {
  ASSERT(PRE_EXIT_BOOT_SERVICES);
  ASSERT(Descriptors != NULL);

  CONST UINTN NumPageTables = 7;
  VOID *PageTablePages = AllocateReservedPages(NumPageTables);
  if (PageTablePages == NULL) {
    PRINT_ERROR("Failed to allocate pages for page tables");
    return EFI_OUT_OF_RESOURCES;
  }
  SetMem64(PageTablePages, EFI_PAGES_TO_SIZE(NumPageTables), 0);

  UINT64 *PML4 = TABLE_PTR(PageTablePages, 0);
  UINT64 *LowerPDPT = TABLE_PTR(PageTablePages, 1);
  UINT64 *LowerPDT = TABLE_PTR(PageTablePages, 2);
  UINT64 *LowerPT = TABLE_PTR(PageTablePages, 3);
  UINT64 *UpperPDPT = TABLE_PTR(PageTablePages, 4);
  UINT64 *UpperPDT = TABLE_PTR(PageTablePages, 5);
  UINT64 *UpperPT = TABLE_PTR(PageTablePages, 6);

  PML4[0] = PTE_ADDR(LowerPDPT) | PE_RW | PE_P;              // -> LowerPDPT
  PML4[511] = PTE_ADDR(UpperPDPT) | PE_RW |PE_P;             // -> UpperPDPT

  LowerPDPT[0] = PTE_ADDR(LowerPDT) | PE_RW | PE_P;          // -> LowerPDT
  LowerPDPT[1] = PTE_ADDR(0x40000000) | PE_S | PE_RW | PE_P; // -> 1-2GiB identity mapping
  LowerPDPT[2] = PTE_ADDR(0x80000000) | PE_S | PE_RW | PE_P; // -> 2-3GiB identity mapping
  LowerPDPT[3] = PTE_ADDR(0xC0000000) | PE_S | PE_RW | PE_P; // -> 3-4GiB identity mapping
  LowerPDT[0] = PTE_ADDR(LowerPT) | PE_RW | PE_P;            // -> LowerPT

  // Identity map lower 1GiB except for the first page
  FillTableWithEntries(LowerPT, 1, TABLE_MAX_ENTRIES - 1, 0x1000, SIZE_4KB, PE_RW | PE_P);
  FillTableWithEntries(LowerPDT, 1, TABLE_MAX_ENTRIES - 1, 0x200000, SIZE_2MB, PE_S | PE_RW | PE_P);

  UpperPDPT[0] = PTE_ADDR(UpperPDT) | PE_RW | PE_P;          // -> UpperPDT
  UpperPDT[0] = PTE_ADDR(UpperPT) | PE_RW | PE_P;            // -> UpperPT
  UpperPDT[1] = PTE_ADDR(0x200000) | PE_S | PE_RW | PE_P;    // -> 2MiB kernel reserved

  UINTN Index = PT_OFFSET(KernelPhysAddr);
  UINT64 PhysAddr = KernelPhysAddr;
  PAGE_DESCRIPTOR *KernelDescriptor = Descriptors;
  while (KernelDescriptor != NULL) {
    ASSERT(!(KernelDescriptor->Flags & PD_SIZE_2MB));
    ASSERT(!(KernelDescriptor->Flags & PD_SIZE_1GB));

    UINTN NumPages = KernelDescriptor->NumPages;
    UINT16 Flags = PageDescriptorFlagsToEntryFlags(KernelDescriptor->Flags) | PE_P;
    FillTableWithEntries(UpperPT, Index, NumPages, KernelDescriptor->PhysAddr, EFI_PAGE_SIZE, Flags);

    Index += NumPages;
    PhysAddr += EFI_PAGES_TO_SIZE(NumPages);
    KernelDescriptor = KernelDescriptor->Next;
  }

  FillTableWithEntries(UpperPT, 0, PT_OFFSET(KernelPhysAddr) - 1, 0, EFI_PAGE_SIZE, PE_RW | PE_P);
  FillTableWithEntries(UpperPT, Index, TABLE_MAX_ENTRIES - Index, PhysAddr, EFI_PAGE_SIZE, PE_RW | PE_P);

  *PML4Address = (UINT64)PML4;
  return EFI_SUCCESS;
}

//
// Debugging
//

CONST CHAR16 *MemoryTypeToString(EFI_MEMORY_TYPE MemoryType) {
  switch (MemoryType) {
  case EfiReservedMemoryType:
    return L"EfiReserved";
  case EfiLoaderCode:
    return L"EfiLoaderCode";
  case EfiLoaderData:
    return L"EfiLoaderData";
  case EfiBootServicesCode:
    return L"EfiBootServicesCode";
  case EfiBootServicesData:
    return L"EfiBootServicesData";
  case EfiRuntimeServicesCode:
    return L"EfiRuntimeServicesCode";
  case EfiRuntimeServicesData:
    return L"EfiRuntimeServicesData";
  case EfiConventionalMemory:
    return L"EfiConventionalMemory";
  case EfiUnusableMemory:
    return L"EfiUnusableMemory";
  case EfiACPIReclaimMemory:
    return L"EfiACPIReclaimMemory";
  case EfiACPIMemoryNVS:
    return L"EfiACPIMemoryNVS";
  case EfiMemoryMappedIO:
    return L"EfiMemoryMappedIO";
  case EfiMemoryMappedIOPortSpace:
    return L"EfiMemoryMappedIOPortSpace";
  case EfiPalCode:
    return L"EfiPALCode";
  case EfiPersistentMemory:
    return L"EfiPersistentMemory";
  default:
    return L"Unknown";
  }
}

void EFIAPI PrintEfiMemoryMap(IN EFI_MEMORY_MAP *MemoryMap) {
  EFI_STATUS Status;
  PRINT_INFO("MemoryMapSize: %u", MemoryMap->Size);
  PRINT_INFO("MapKey: %u", MemoryMap->Key);
  PRINT_INFO("DescriptorSize: %u", MemoryMap->DescSize);
  PRINT_INFO("DescriptorVersion: %u", MemoryMap->DescVersion);
  PRINT_INFO("------ Memory Map ------");

  EFI_MEMORY_DESCRIPTOR *Desc = MemoryMap->Map;
  while (!AT_MEMORY_MAP_END(Desc, MemoryMap)) {
    UINT32 CursX = gST->ConOut->Mode->CursorColumn;
    UINT32 CursY = gST->ConOut->Mode->CursorRow;

    PRINT_INFO("%s", MemoryTypeToString(Desc->Type));
    PRINT_INFO("    Physical start: 0x%p", Desc->PhysicalStart);
    PRINT_INFO("    Virtual start: 0x%p", Desc->VirtualStart);
    PRINT_INFO("    Number of pages: %u", Desc->NumberOfPages);
    PRINT_INFO("    Attribute: %u", Desc->Attribute);

    // PRINT_INFO("-- less --");
    // Status = gBS->WaitForEvent(1, &(gST->ConIn->WaitForKey), NULL);
    // if (EFI_ERROR(Status)) {
    //   PRINT_ERROR("Failed to wait for key");
    //   PRINT_STATUS(Status);
    //   return;
    // }
    //
    // EFI_INPUT_KEY Key;
    // gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
    // gST->ConOut->SetCursorPosition(gST->ConOut, CursX, CursY);
    Desc = MEMORY_MAP_NEXT(Desc, MemoryMap);
  }

  PRINT_INFO("------------------------");
}
