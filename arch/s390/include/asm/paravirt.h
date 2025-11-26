#ifndef _ASM_S390_PARAVIRT_H
#define _ASM_S390_PARAVIRT_H

extern struct static_key paravirt_steal_enabled;

static inline u64 paravirt_steal_clock(int cpu)
{
	return 0;
}

#endif
