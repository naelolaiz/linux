// SPDX-License-Identifier: GPL-2.0
/*
 * Non-coherent DMA cache maintenance for Espressif ESP32-S31.
 *
 * The S31 has no Zicbom and its external-memory (flash/PSRAM) L1 D-cache is
 * not coherent with bus masters such as the EMAC. CPU-issued CMO isn't
 * possible from S-mode, so cache maintenance is delegated to OpenSBI (M-mode)
 * via a vendor SBI ecall that calls the chip ROM's Cache_WriteBack_Addr /
 * Cache_Invalidate_Addr helpers, plugged into the RISC-V non-standard
 * cache-ops hooks so arch_sync_dma_for_{device,cpu}() keep streaming DMA
 * buffers coherent for any device marked "dma-noncoherent" in the DT.
 *
 * Copyright (C) 2026, Espressif Systems (Shanghai) CO LTD
 */

#define pr_fmt(fmt) "esp32s31-cache: " fmt

#include <linux/cacheflush.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/spinlock.h>
#include <asm/dma-noncoherent.h>
#include <asm/sbi.h>

/* Vendor ecall funcids — must match platform.c::s31_vendor_ext_provider. */
#define S31_SBI_CACHE_WBACK		0
#define S31_SBI_CACHE_INVAL		1
#define S31_SBI_CACHE_WBACK_INVAL	2
#define S31_SBI_ICACHE_SYNC		3
#define S31_SBI_ICACHE_SYNC_RANGE	4

/* The vendor extension ID is derived from mvendorid, matching OpenSBI's
 * sbi_ecall_vendor_id(). Cached on first use (one SBI call).
 */
static unsigned long esp32s31_cache_extid(void)
{
	static unsigned long extid;
	unsigned long id = READ_ONCE(extid);

	if (unlikely(!id)) {
		id = SBI_EXT_VENDOR_START +
			(sbi_get_mvendorid() &
			 (SBI_EXT_VENDOR_END - SBI_EXT_VENDOR_START));
		WRITE_ONCE(extid, id);
	}
	return id;
}

/*
 * OpenSBI runs each call in the ROM's Cache_* helpers, which drive the one
 * cache controller of the chip and take no lock. With two harts, DMA syncs
 * and the I-cache syncs of remote fence.i IPIs can run on both at once:
 * one call at a time.
 */
static DEFINE_RAW_SPINLOCK(esp32s31_cache_lock);

static struct sbiret esp32s31_cache_op(unsigned long func, phys_addr_t paddr,
				       size_t size)
{
	unsigned long extid = esp32s31_cache_extid();
	unsigned long flags;
	struct sbiret ret;

	raw_spin_lock_irqsave(&esp32s31_cache_lock, flags);
	ret = sbi_ecall(extid, func, (unsigned long)paddr, (unsigned long)size,
			0, 0, 0, 0);
	raw_spin_unlock_irqrestore(&esp32s31_cache_lock, flags);

	return ret;
}

/*
 * I-cache sync for local_flush_icache_all(): fence.i cannot reach this chip's
 * SoC-external, split, non-coherent ICache/DCache (there is no cache inside the
 * CPU IP), so making CPU-written code visible to instruction fetch is delegated
 * to OpenSBI (whole-cache writeback-D + invalidate-I). If OpenSBI predates the
 * funcid it returns -ENOTSUPP and this is a no-op.
 */
void esp32s31_flush_icache(void)
{
	esp32s31_cache_op(S31_SBI_ICACHE_SYNC, 0, 0);
}
EXPORT_SYMBOL_GPL(esp32s31_flush_icache);

/*
 * Range variant: sync only the lines backing one exec page (flash-XIP or PSRAM,
 * phys == cache vaddr). flush_icache_pte() drives this per exec page, so the
 * flash XIP cramfs (CPython) pays a small per-page op instead of a whole-cache
 * flush per fault.
 */
void esp32s31_flush_icache_range(phys_addr_t paddr, size_t size)
{
	esp32s31_cache_op(S31_SBI_ICACHE_SYNC_RANGE, paddr, size);
}

static void esp32s31_dma_cache_wback(phys_addr_t paddr, size_t size)
{
	esp32s31_cache_op(S31_SBI_CACHE_WBACK, paddr, size);
}

static void esp32s31_dma_cache_inv(phys_addr_t paddr, size_t size)
{
	esp32s31_cache_op(S31_SBI_CACHE_INVAL, paddr, size);
}

static void esp32s31_dma_cache_wback_inv(phys_addr_t paddr, size_t size)
{
	esp32s31_cache_op(S31_SBI_CACHE_WBACK_INVAL, paddr, size);
}

static const struct riscv_nonstd_cache_ops esp32s31_cmo_ops __initconst = {
	.wback		= &esp32s31_dma_cache_wback,
	.inv		= &esp32s31_dma_cache_inv,
	.wback_inv	= &esp32s31_dma_cache_wback_inv,
};

static int __init esp32s31_cache_init(void)
{
	struct sbiret ret;

	/* Probe the cache ecall with a REAL (non-zero) writeback over mapped
	 * PSRAM, so this actually exercises the M-mode ROM Cache_WriteBack_Addr
	 * call. A zero-size probe is a no-op and would hide an M-mode PMP/ROM
	 * fault until the first real DMA sync.
	 */
	ret = esp32s31_cache_op(S31_SBI_CACHE_WBACK, 0x50000000UL, 64);
	if (ret.error) {
		pr_err("cache SBI ecall unavailable (err %ld); DMA will be incoherent\n",
		       ret.error);
		return -ENODEV;
	}

	riscv_noncoherent_register_cache_ops(&esp32s31_cmo_ops);
	riscv_noncoherent_supported();
	pr_info("DMA cache ops registered via vendor SBI ecall\n");
	return 0;
}
arch_initcall(esp32s31_cache_init);
