#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <kernel/cpu.h>

static cpu_info_t g_cpu_info = {0};
static bool cpu_info_initialized = false;

void cpu_detect(cpu_info_t* info) {
	uint32_t eax, ebx, ecx, edx;
	
	// Get vendor string
	cpuid(0, &eax, &ebx, &ecx, &edx);
	uint32_t max_basic = eax;
	
	// Vendor ID is EBX, EDX, ECX
	memcpy(info->vendor, &ebx, 4);
	memcpy(info->vendor + 4, &edx, 4);
	memcpy(info->vendor + 8, &ecx, 4);
	info->vendor[12] = '\0';
	
	// Get feature flags
	if (max_basic >= 1) {
		cpuid(1, &eax, &ebx, &ecx, &edx);
		info->features_ecx = ecx;
		info->features_edx = edx;
		
		// Detect specific features
		info->has_fpu = (edx & CPUID_FEAT_EDX_FPU) != 0;
		info->has_tsc = (edx & CPUID_FEAT_EDX_TSC) != 0;
		info->has_msr = (edx & CPUID_FEAT_EDX_MSR) != 0;
		info->has_apic = (edx & CPUID_FEAT_EDX_APIC) != 0;
		info->has_sse = (edx & CPUID_FEAT_EDX_SSE) != 0;
		info->has_sse2 = (edx & CPUID_FEAT_EDX_SSE2) != 0;
		info->has_sse3 = (ecx & CPUID_FEAT_ECX_SSE3) != 0;
		info->has_sse41 = (ecx & CPUID_FEAT_ECX_SSE41) != 0;
		info->has_sse42 = (ecx & CPUID_FEAT_ECX_SSE42) != 0;
		info->has_avx = (ecx & CPUID_FEAT_ECX_AVX) != 0;
	}
	
	// Get brand string (requires extended CPUID)
	cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
	uint32_t max_extended = eax;
	
	if (max_extended >= 0x80000004) {
		// Brand string is in EAX, EBX, ECX, EDX for functions 0x80000002-0x80000004
		cpuid(0x80000002, &eax, &ebx, &ecx, &edx);
		memcpy(info->brand, &eax, 4);
		memcpy(info->brand + 4, &ebx, 4);
		memcpy(info->brand + 8, &ecx, 4);
		memcpy(info->brand + 12, &edx, 4);
		
		cpuid(0x80000003, &eax, &ebx, &ecx, &edx);
		memcpy(info->brand + 16, &eax, 4);
		memcpy(info->brand + 20, &ebx, 4);
		memcpy(info->brand + 24, &ecx, 4);
		memcpy(info->brand + 28, &edx, 4);
		
		cpuid(0x80000004, &eax, &ebx, &ecx, &edx);
		memcpy(info->brand + 32, &eax, 4);
		memcpy(info->brand + 36, &ebx, 4);
		memcpy(info->brand + 40, &ecx, 4);
		memcpy(info->brand + 44, &edx, 4);
		info->brand[48] = '\0';
		
		// Trim leading spaces from brand string
		char* start = info->brand;
		while (*start == ' ') start++;
		if (start != info->brand) {
			memmove(info->brand, start, strlen(start) + 1);
		}
	} else {
		strcpy(info->brand, "Unknown CPU");
	}
	
	g_cpu_info = *info;
	cpu_info_initialized = true;
}

void cpu_print_info(const cpu_info_t* info) {
	printf("CPU Information:\n");
	printf("  Vendor: %s\n", info->vendor);
	printf("  Brand:  %s\n", info->brand);
	printf("\nFeatures:\n");
	printf("  FPU:   %s\n", info->has_fpu ? "Yes" : "No");
	printf("  TSC:   %s\n", info->has_tsc ? "Yes" : "No");
	printf("  MSR:   %s\n", info->has_msr ? "Yes" : "No");
	printf("  APIC:  %s\n", info->has_apic ? "Yes" : "No");
	printf("  SSE:   %s\n", info->has_sse ? "Yes" : "No");
	printf("  SSE2:  %s\n", info->has_sse2 ? "Yes" : "No");
	printf("  SSE3:  %s\n", info->has_sse3 ? "Yes" : "No");
	printf("  SSE4.1: %s\n", info->has_sse41 ? "Yes" : "No");
	printf("  SSE4.2: %s\n", info->has_sse42 ? "Yes" : "No");
	printf("  AVX:   %s\n", info->has_avx ? "Yes" : "No");
}

bool cpu_has_feature(uint32_t feature) {
	if (!cpu_info_initialized) {
		cpu_detect(&g_cpu_info);
	}
	
	// Check if feature is in ECX or EDX
	if (feature & 0xFFFF0000) {
		return (g_cpu_info.features_ecx & feature) != 0;
	} else {
		return (g_cpu_info.features_edx & feature) != 0;
	}
}
