#ifndef _KERNEL_CPU_H
#define _KERNEL_CPU_H

#include <stdint.h>
#include <stdbool.h>

// CPUID feature flags
#define CPUID_FEAT_ECX_SSE3         (1 << 0)
#define CPUID_FEAT_ECX_PCLMUL       (1 << 1)
#define CPUID_FEAT_ECX_SSSE3        (1 << 9)
#define CPUID_FEAT_ECX_FMA          (1 << 12)
#define CPUID_FEAT_ECX_SSE41        (1 << 19)
#define CPUID_FEAT_ECX_SSE42        (1 << 20)
#define CPUID_FEAT_ECX_AVX          (1 << 28)

#define CPUID_FEAT_EDX_FPU          (1 << 0)
#define CPUID_FEAT_EDX_PSE          (1 << 3)
#define CPUID_FEAT_EDX_TSC          (1 << 4)
#define CPUID_FEAT_EDX_MSR          (1 << 5)
#define CPUID_FEAT_EDX_PAE          (1 << 6)
#define CPUID_FEAT_EDX_APIC         (1 << 9)
#define CPUID_FEAT_EDX_SEP          (1 << 11)
#define CPUID_FEAT_EDX_PGE          (1 << 13)
#define CPUID_FEAT_EDX_CMOV         (1 << 15)
#define CPUID_FEAT_EDX_PSE36        (1 << 17)
#define CPUID_FEAT_EDX_MMX          (1 << 23)
#define CPUID_FEAT_EDX_FXSR         (1 << 24)
#define CPUID_FEAT_EDX_SSE          (1 << 25)
#define CPUID_FEAT_EDX_SSE2         (1 << 26)

// CR0 bits
#define CR0_PE  (1 << 0)   // Protected Mode Enable
#define CR0_MP  (1 << 1)   // Monitor co-processor
#define CR0_EM  (1 << 2)   // Emulation
#define CR0_TS  (1 << 3)   // Task switched
#define CR0_ET  (1 << 4)   // Extension type
#define CR0_NE  (1 << 5)   // Numeric error
#define CR0_WP  (1 << 16)  // Write protect
#define CR0_AM  (1 << 18)  // Alignment mask
#define CR0_NW  (1 << 29)  // Not-write through
#define CR0_CD  (1 << 30)  // Cache disable
#define CR0_PG  (1 << 31)  // Paging

// CR4 bits
#define CR4_VME        (1 << 0)   // Virtual 8086 Mode Extensions
#define CR4_PVI        (1 << 1)   // Protected-mode Virtual Interrupts
#define CR4_TSD        (1 << 2)   // Time Stamp Disable
#define CR4_DE         (1 << 3)   // Debugging Extensions
#define CR4_PSE        (1 << 4)   // Page Size Extension
#define CR4_PAE        (1 << 5)   // Physical Address Extension
#define CR4_MCE        (1 << 6)   // Machine Check Exception
#define CR4_PGE        (1 << 7)   // Page Global Enabled
#define CR4_PCE        (1 << 8)   // Performance-Monitoring Counter enable
#define CR4_OSFXSR     (1 << 9)   // Operating system support for FXSAVE and FXRSTOR
#define CR4_OSXMMEXCPT (1 << 10)  // Operating System Support for Unmasked SIMD Floating-Point Exceptions

// CPU vendor strings
typedef struct {
	char vendor[13];
	char brand[49];
	uint32_t features_ecx;
	uint32_t features_edx;
	bool has_fpu;
	bool has_tsc;
	bool has_msr;
	bool has_apic;
	bool has_sse;
	bool has_sse2;
	bool has_sse3;
	bool has_sse41;
	bool has_sse42;
	bool has_avx;
} cpu_info_t;

// Assembly functions
extern void cpuid(uint32_t code, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx);
extern uint64_t rdtsc(void);
extern uint64_t rdmsr(uint32_t msr);
extern void wrmsr(uint32_t msr, uint64_t value);

extern uint32_t read_cr0(void);
extern void write_cr0(uint32_t val);
extern uint32_t read_cr2(void);
extern uint32_t read_cr3(void);
extern void write_cr3(uint32_t val);
extern uint32_t read_cr4(void);
extern void write_cr4(uint32_t val);

extern void invlpg(uint32_t addr);
extern void cpu_hlt(void);
extern void cpu_halt_forever(void);
extern void cpu_sti(void);
extern void cpu_cli(void);

extern uint32_t read_eflags(void);
extern void write_eflags(uint32_t flags);

extern int atomic_cmpxchg(int* ptr, int old_val, int new_val);
extern void atomic_inc(int* ptr);
extern void atomic_dec(int* ptr);
extern void memory_barrier(void);

// High-level functions
void cpu_detect(cpu_info_t* info);
void cpu_print_info(const cpu_info_t* info);
bool cpu_has_feature(uint32_t feature);

#endif
