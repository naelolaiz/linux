/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/linkage.h>

#include <asm/asm.h>
#include <asm/csr.h>
#include <asm/asm-offsets.h>

#ifdef CONFIG_FPU_F_ONLY
/* F without D: keep each register in the low 32 bits of its 64-bit slot. */
#define FP_SAVE		fsw
#define FP_RESTORE	flw
#else
#define FP_SAVE		fsd
#define FP_RESTORE	fld
#endif

SYM_FUNC_START(__fstate_save)
	li  a2,  TASK_THREAD_F0
	add a0, a0, a2
	li t1, SR_FS
	csrs CSR_STATUS, t1
	frcsr t0
	FP_SAVE f0,  TASK_THREAD_F0_F0(a0)
	FP_SAVE f1,  TASK_THREAD_F1_F0(a0)
	FP_SAVE f2,  TASK_THREAD_F2_F0(a0)
	FP_SAVE f3,  TASK_THREAD_F3_F0(a0)
	FP_SAVE f4,  TASK_THREAD_F4_F0(a0)
	FP_SAVE f5,  TASK_THREAD_F5_F0(a0)
	FP_SAVE f6,  TASK_THREAD_F6_F0(a0)
	FP_SAVE f7,  TASK_THREAD_F7_F0(a0)
	FP_SAVE f8,  TASK_THREAD_F8_F0(a0)
	FP_SAVE f9,  TASK_THREAD_F9_F0(a0)
	FP_SAVE f10, TASK_THREAD_F10_F0(a0)
	FP_SAVE f11, TASK_THREAD_F11_F0(a0)
	FP_SAVE f12, TASK_THREAD_F12_F0(a0)
	FP_SAVE f13, TASK_THREAD_F13_F0(a0)
	FP_SAVE f14, TASK_THREAD_F14_F0(a0)
	FP_SAVE f15, TASK_THREAD_F15_F0(a0)
	FP_SAVE f16, TASK_THREAD_F16_F0(a0)
	FP_SAVE f17, TASK_THREAD_F17_F0(a0)
	FP_SAVE f18, TASK_THREAD_F18_F0(a0)
	FP_SAVE f19, TASK_THREAD_F19_F0(a0)
	FP_SAVE f20, TASK_THREAD_F20_F0(a0)
	FP_SAVE f21, TASK_THREAD_F21_F0(a0)
	FP_SAVE f22, TASK_THREAD_F22_F0(a0)
	FP_SAVE f23, TASK_THREAD_F23_F0(a0)
	FP_SAVE f24, TASK_THREAD_F24_F0(a0)
	FP_SAVE f25, TASK_THREAD_F25_F0(a0)
	FP_SAVE f26, TASK_THREAD_F26_F0(a0)
	FP_SAVE f27, TASK_THREAD_F27_F0(a0)
	FP_SAVE f28, TASK_THREAD_F28_F0(a0)
	FP_SAVE f29, TASK_THREAD_F29_F0(a0)
	FP_SAVE f30, TASK_THREAD_F30_F0(a0)
	FP_SAVE f31, TASK_THREAD_F31_F0(a0)
	sw t0, TASK_THREAD_FCSR_F0(a0)
	csrc CSR_STATUS, t1
	ret
SYM_FUNC_END(__fstate_save)

SYM_FUNC_START(__fstate_restore)
	li  a2,  TASK_THREAD_F0
	add a0, a0, a2
	li t1, SR_FS
	lw t0, TASK_THREAD_FCSR_F0(a0)
	csrs CSR_STATUS, t1
	FP_RESTORE f0,  TASK_THREAD_F0_F0(a0)
	FP_RESTORE f1,  TASK_THREAD_F1_F0(a0)
	FP_RESTORE f2,  TASK_THREAD_F2_F0(a0)
	FP_RESTORE f3,  TASK_THREAD_F3_F0(a0)
	FP_RESTORE f4,  TASK_THREAD_F4_F0(a0)
	FP_RESTORE f5,  TASK_THREAD_F5_F0(a0)
	FP_RESTORE f6,  TASK_THREAD_F6_F0(a0)
	FP_RESTORE f7,  TASK_THREAD_F7_F0(a0)
	FP_RESTORE f8,  TASK_THREAD_F8_F0(a0)
	FP_RESTORE f9,  TASK_THREAD_F9_F0(a0)
	FP_RESTORE f10, TASK_THREAD_F10_F0(a0)
	FP_RESTORE f11, TASK_THREAD_F11_F0(a0)
	FP_RESTORE f12, TASK_THREAD_F12_F0(a0)
	FP_RESTORE f13, TASK_THREAD_F13_F0(a0)
	FP_RESTORE f14, TASK_THREAD_F14_F0(a0)
	FP_RESTORE f15, TASK_THREAD_F15_F0(a0)
	FP_RESTORE f16, TASK_THREAD_F16_F0(a0)
	FP_RESTORE f17, TASK_THREAD_F17_F0(a0)
	FP_RESTORE f18, TASK_THREAD_F18_F0(a0)
	FP_RESTORE f19, TASK_THREAD_F19_F0(a0)
	FP_RESTORE f20, TASK_THREAD_F20_F0(a0)
	FP_RESTORE f21, TASK_THREAD_F21_F0(a0)
	FP_RESTORE f22, TASK_THREAD_F22_F0(a0)
	FP_RESTORE f23, TASK_THREAD_F23_F0(a0)
	FP_RESTORE f24, TASK_THREAD_F24_F0(a0)
	FP_RESTORE f25, TASK_THREAD_F25_F0(a0)
	FP_RESTORE f26, TASK_THREAD_F26_F0(a0)
	FP_RESTORE f27, TASK_THREAD_F27_F0(a0)
	FP_RESTORE f28, TASK_THREAD_F28_F0(a0)
	FP_RESTORE f29, TASK_THREAD_F29_F0(a0)
	FP_RESTORE f30, TASK_THREAD_F30_F0(a0)
	FP_RESTORE f31, TASK_THREAD_F31_F0(a0)
	fscsr t0
	csrc CSR_STATUS, t1
	ret
SYM_FUNC_END(__fstate_restore)

#define get_f32(which) fmv.x.s a0, which; j 2f
#define put_f32(which) fmv.s.x which, a1; j 2f
#if __riscv_xlen == 64
# define get_f64(which) fmv.x.d a0, which; j 2f
# define put_f64(which) fmv.d.x which, a1; j 2f
#else
# define get_f64(which) fsd which, 0(a1); j 2f
# define put_f64(which) fld which, 0(a1); j 2f
#endif

.macro fp_access_prologue
	/*
	 * Compute jump offset to store the correct FP register since we don't
	 * have indirect FP register access
	 */
	sll t0, a0, 3
	la t2, 1f
	add t0, t0, t2
	li t1, SR_FS
	csrs CSR_STATUS, t1
	jr t0
1:
.endm

.macro fp_access_epilogue
2:
	csrc CSR_STATUS, t1
	ret
.endm

#define fp_access_body(__access_func) \
	__access_func(f0); \
	__access_func(f1); \
	__access_func(f2); \
	__access_func(f3); \
	__access_func(f4); \
	__access_func(f5); \
	__access_func(f6); \
	__access_func(f7); \
	__access_func(f8); \
	__access_func(f9); \
	__access_func(f10); \
	__access_func(f11); \
	__access_func(f12); \
	__access_func(f13); \
	__access_func(f14); \
	__access_func(f15); \
	__access_func(f16); \
	__access_func(f17); \
	__access_func(f18); \
	__access_func(f19); \
	__access_func(f20); \
	__access_func(f21); \
	__access_func(f22); \
	__access_func(f23); \
	__access_func(f24); \
	__access_func(f25); \
	__access_func(f26); \
	__access_func(f27); \
	__access_func(f28); \
	__access_func(f29); \
	__access_func(f30); \
	__access_func(f31)


#ifdef CONFIG_RISCV_SCALAR_MISALIGNED

/*
 * Disable compressed instructions set to keep a constant offset between FP
 * load/store/move instructions
 */
.option norvc
/*
 * put_f32_reg - Set a FP register from a register containing the value
 * a0 = FP register index to be set
 * a1 = value to be loaded in the FP register
 */
SYM_FUNC_START(put_f32_reg)
	fp_access_prologue
	fp_access_body(put_f32)
	fp_access_epilogue
SYM_FUNC_END(put_f32_reg)

/*
 * get_f32_reg - Get a FP register value and return it
 * a0 = FP register index to be retrieved
 */
SYM_FUNC_START(get_f32_reg)
	fp_access_prologue
	fp_access_body(get_f32)
	fp_access_epilogue
SYM_FUNC_END(get_f32_reg)

/*
 * put_f64_reg - Set a 64 bits FP register from a value or a pointer.
 * a0 = FP register index to be set
 * a1 = value/pointer to be loaded in the FP register (when xlen == 32 bits, we
 * load the value to a pointer).
 */
SYM_FUNC_START(put_f64_reg)
	fp_access_prologue
	fp_access_body(put_f64)
	fp_access_epilogue
SYM_FUNC_END(put_f64_reg)

/*
 * get_f64_reg - Get a 64 bits FP register value and returned it or store it to
 *	 	 a pointer.
 * a0 = FP register index to be retrieved
 * a1 = If xlen == 32, pointer which should be loaded with the FP register value
 *	or unused if xlen == 64. In which case the FP register value is returned
 *	through a0
 */
SYM_FUNC_START(get_f64_reg)
	fp_access_prologue
	fp_access_body(get_f64)
	fp_access_epilogue
SYM_FUNC_END(get_f64_reg)

#endif /* CONFIG_RISCV_SCALAR_MISALIGNED */
