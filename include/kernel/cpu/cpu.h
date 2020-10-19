//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#ifndef KERNEL_CPU_CPU_H
#define KERNEL_CPU_CPU_H

#include <base.h>

void cli();
void sti();

uint64_t read_tsc();

uint64_t read_msr(uint32_t msr);
void write_msr(uint32_t msr, uint64_t value);

uint64_t read_gsbase();
void write_gsbase(uint64_t value);
uint64_t read_kernel_gsbase();
void write_kernel_gsbase(uint64_t value);
void swapgs();

void load_gdt(void *gdtr);
void load_idt(void *idtr);
void flush_gdt();

void tlb_invlpg(uint64_t addr);
void tlb_flush();

void enable_sse();

#endif
