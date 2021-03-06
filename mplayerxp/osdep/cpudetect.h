#ifndef CPUDETECT_H
#define CPUDETECT_H

namespace	usr {
#ifdef ARCH_X86_64
#  define REGa    rax
#  define REGb    rbx
#  define REGBP   rbp
#  define REGSP   rsp
#  define REG_a  "rax"
#  define REG_b  "rbx"
#  define REG_c  "rcx"
#  define REG_d  "rdx"
#  define REG_S  "rsi"
#  define REG_D  "rdi"
#  define REG_SP "rsp"
#  define REG_BP "rbp"
#else
#  define REGa    eax
#  define REGb    ebx
#  define REGBP   ebp
#  define REGSP   esp
#  define REG_a  "eax"
#  define REG_b  "ebx"
#  define REG_c  "ecx"
#  define REG_d  "edx"
#  define REG_S  "esi"
#  define REG_D  "edi"
#  define REG_SP "esp"
#  define REG_BP "ebp"
#endif

    enum {
	CPUTYPE_I386=3,
	CPUTYPE_I486=4,
	CPUTYPE_I586=5,
	CPUTYPE_I686=6
    };

    struct CpuCaps {
	int cpuType;
	int cpuStepping;
	int hasMMX;
	int hasMMX2;
	int has3DNow;
	int has3DNowExt;
	int hasSSE;
	int hasSSE2;
	int hasSSE3;
	int hasSSSE3;
	int hasFMA;
	int hasSSE41;
	int hasSSE42;
	int hasAES;
	int hasAVX;
	int isX86;
	unsigned cl_size; /* size of cache line */
    };

    extern CpuCaps gCpuCaps;

    void GetCpuCaps(CpuCaps *caps);

    /* returned value is mp_malloc()'ed so mp_free() it after use */
    std::string GetCpuFriendlyName(unsigned int regs[], unsigned int regs2[]);
} // namespace	usr
#endif /* !CPUDETECT_H */

