// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <asm/futex.h>

#define DEFINE_FUTEX_OP_FUNC(name)					\
__no_sanitize_memory int						\
__futex_atomic_##name(int oparg, int *old, u32 __user *uaddr)		\
{									\
	return __futex_atomic_##name##_inline(oparg, old, uaddr);	\
}									\
EXPORT_SYMBOL(__futex_atomic_##name)

DEFINE_FUTEX_OP_FUNC(set);
DEFINE_FUTEX_OP_FUNC(add);
DEFINE_FUTEX_OP_FUNC(or);
DEFINE_FUTEX_OP_FUNC(and);
DEFINE_FUTEX_OP_FUNC(xor);

__no_sanitize_memory int
futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr, u32 oldval, u32 newval)
{
	return __futex_atomic_cmpxchg_inatomic_inline(uval, uaddr, oldval, newval);
}
EXPORT_SYMBOL(futex_atomic_cmpxchg_inatomic);
