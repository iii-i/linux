/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_DWARF_H
#define _ASM_S390_DWARF_H

#ifdef __ASSEMBLER__

#define CFI_STARTPROC		.cfi_startproc
#define CFI_ENDPROC		.cfi_endproc
#define CFI_DEF_CFA_OFFSET	.cfi_def_cfa_offset
#define CFI_ADJUST_CFA_OFFSET	.cfi_adjust_cfa_offset
#define CFI_RESTORE		.cfi_restore
#define CFI_REL_OFFSET		.cfi_rel_offset

#ifdef CONFIG_AS_CFI_VAL_OFFSET
#define CFI_VAL_OFFSET		.cfi_val_offset
#else
#define CFI_VAL_OFFSET		#
#endif

#ifndef BUILD_VDSO
	/*
	 * Emit CFI data in .debug_frame sections and not in .eh_frame
	 * sections.  The .eh_frame CFI is used for runtime unwind
	 * information that is not being used.  Hence, vmlinux.lds.S
	 * can discard the .eh_frame sections.
	 */
	.cfi_sections .debug_frame
#else
	/*
	 * For vDSO, emit CFI data in both, .eh_frame and .debug_frame
	 * sections.
	 */
	.cfi_sections .eh_frame, .debug_frame
#endif

#define DW_CFA_restore_extended 0x06
#define DW_CFA_expression 0x10
#define DW_CFA_offset_extended_sf 0x11

#define DW_OP_const8u 0x0e

.macro CFI_GLOBAL reg, addr
#if defined(CONFIG_AS_CFI_ESCAPE_LEB128) && defined(CONFIG_AS_CFI_ESCAPE_DATA)
.cfi_escape DW_CFA_expression, uleb128(\reg), 9, DW_OP_const8u, data8(\addr)
#else
.cfi_undefined \reg
#endif
.endm

.macro CFI_GLOBAL_MULTIPLE reg1, reg2, addr
.set .Lreg, (\reg1)
.rept (\reg2)-(\reg1)+1
	CFI_GLOBAL .Lreg, (\addr)+(.Lreg-(\reg1))*8
	.set .Lreg, .Lreg+1
.endr
.endm

.macro CFI_OFFSET_MULTIPLE reg1, reg2, off
.set .Lreg, (\reg1)
.rept (\reg2)-(\reg1)+1
	.cfi_escape DW_CFA_offset_extended_sf, uleb128(.Lreg), \
		    sleb128(-(\off)/8-(.Lreg-(\reg1)))
	.set .Lreg, .Lreg+1
.endr
.endm

.macro CFI_RESTORE_MULTIPLE reg1, reg2
.set .Lreg, (\reg1)
.rept (\reg2)-(\reg1)+1
	.cfi_escape DW_CFA_restore_extended, uleb128(.Lreg)
	.set .Lreg, .Lreg+1
.endr
.endm

#endif	/* __ASSEMBLER__ */

#endif	/* _ASM_S390_DWARF_H */
