/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 */

#include <kubridge.h>
#include <so_util/so_util.h>
#include "utils/dialog.h"
#include "utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

extern so_module so_mod;

// Not exposed in so_util.h, but not static in so_util.c either -- reused here to place
// our SWP-replacement trampolines in the same code cave so_util itself uses for relocation
// trampolines (see trampoline_ldm() in so_util.c for the pattern this mirrors).
extern uintptr_t so_alloc_arena(so_module *so, uintptr_t range, uintptr_t dst, size_t sz);

#define SWP_PATCH_B_RANGE ((1 << 24) - 1)
#define ARM_COND_EQ 0x0u
#define ARM_COND_NE 0x1u
#define ARM_COND_AL 0xEu

static uint32_t arm_b_cond(uintptr_t from, uintptr_t to, uint32_t cond) {
    int32_t imm24 = ((int32_t) to - (int32_t) from - 8) >> 2;
    return (cond << 28) | 0x0A000000u | ((uint32_t) imm24 & 0x00FFFFFFu);
}

/*
 * The original Android .so (compiled ~2010-2011) uses boost::detail::spinlock_pool for
 * boost::shared_ptr refcounting, whose lock-acquire loop is inlined as the deprecated ARM
 * `SWP` instruction (atomic swap). SWP was deprecated in ARMv6 and is disabled/undefined by
 * default on ARMv7 cores (including the Vita's Cortex-A9) unless the kernel explicitly sets
 * SCTLR.SW -- which Sony's kernel does not. Executing it faults as an Undefined Instruction
 * exception (confirmed via crash dump: PC landed exactly on a `swp r3, r6, [r5]` inside
 * boost::detail::shared_count::shared_count(shared_count const&), reached from
 * pig::res::ResourceLoader::_AddPack's vector<shared_ptr<IStreamLoader>>::push_back).
 *
 * Fix: replace each `swp Rt, Rt2, [Rn]` with a small trampoline doing the equivalent
 * exchange via LDREX/STREX (natively supported on Cortex-A9), then branching back to
 * the instruction right after the original SWP -- the collateral `cmp Rt, #0` that always
 * follows it in this function is left untouched in place, since the patch only overwrites
 * the 4 bytes of the SWP instruction itself (a single relative `B`, same technique
 * so_util's own trampoline_ldm() uses for LDM patches).
 *
 * r12 (ip) is used as the STREX status scratch register: it does not appear anywhere in
 * this function's disassembly (it isn't in the prologue's push {r4-r9,sl,fp,lr} either),
 * so it is safe to clobber at all three call sites patched below.
 */
static void patch_swp_with_ldrex_strex(uintptr_t addr) {
    uint32_t instr;
    kuKernelCpuUnrestrictedMemcpy(&instr, (void *) addr, sizeof(instr));

    // SWP/SWPB encoding: cond 00010B00 Rn Rd 00001001 Rm (any cond, either B value)
    if ((instr & 0x0FB00FF0u) != 0x01000090u) {
        l_warn("patch_swp_with_ldrex_strex: no SWP at 0x%08X (found 0x%08X), skipping",
               (unsigned int) addr, instr);
        return;
    }

    uint32_t rn = (instr >> 16) & 0xF;  // base address register
    uint32_t rt = (instr >> 12) & 0xF;  // destination for the old value
    uint32_t rt2 = instr & 0xF;         // source of the new value

    if (rn != 12 && rt != 12 && rt2 != 12) {
        // Standard fast 5-instruction trampoline using r12 as scratch:
        uint32_t trampoline[5];
        trampoline[0] = 0xE1900F9Fu | (rn << 16) | (rt << 12);         // ldrex rt, [rn]
        trampoline[1] = 0xE1800F90u | (rn << 16) | (12u << 12) | rt2;  // strex r12, rt2, [rn]
        trampoline[2] = 0xE3500000u | (12u << 16);                     // cmp r12, #0

        uintptr_t patch_addr = so_alloc_arena(&so_mod, SWP_PATCH_B_RANGE, addr + 8, sizeof(trampoline));
        if (!patch_addr) {
            fatal_error("Failed to allocate SWP-patch trampoline for 0x%08X\n", (unsigned int) addr);
        }

        trampoline[3] = arm_b_cond(patch_addr + 3 * 4, patch_addr + 0 * 4, ARM_COND_NE); // bne retry
        trampoline[4] = arm_b_cond(patch_addr + 4 * 4, addr + 4, ARM_COND_AL);           // b resume

        kuKernelCpuUnrestrictedMemcpy((void *) patch_addr, trampoline, sizeof(trampoline));
        kuKernelFlushCaches((void *) patch_addr, sizeof(trampoline));

        uint32_t branch_out = arm_b_cond(addr, patch_addr, ARM_COND_AL);
        kuKernelCpuUnrestrictedMemcpy((void *) addr, &branch_out, sizeof(branch_out));
        kuKernelFlushCaches((void *) addr, sizeof(branch_out));

        l_info("Patched SWP at 0x%08X (r%u,r%u,[r%u]) -> LDREX/STREX trampoline at 0x%08X",
               (unsigned int) addr, rt, rt2, rn, (unsigned int) patch_addr);
    } else {
        // One of the operands is r12. Pick r0 or r1 as scratch and preserve it on stack (8-byte aligned).
        uint32_t scratch = (rn != 0 && rt != 0 && rt2 != 0) ? 0 : 1;
        uint32_t trampoline[7];
        trampoline[0] = 0xE52D0008u | (scratch << 12);                               // str scratch, [sp, #-8]!
        trampoline[1] = 0xE1900F9Fu | (rn << 16) | (rt << 12);                       // ldrex rt, [rn]
        trampoline[2] = 0xE1800F90u | (rn << 16) | (scratch << 12) | rt2;            // strex scratch, rt2, [rn]
        trampoline[3] = 0xE3500000u | (scratch << 16);                               // cmp scratch, #0

        uintptr_t patch_addr = so_alloc_arena(&so_mod, SWP_PATCH_B_RANGE, addr + 8, sizeof(trampoline));
        if (!patch_addr) {
            fatal_error("Failed to allocate SWP-patch trampoline for 0x%08X\n", (unsigned int) addr);
        }

        trampoline[4] = arm_b_cond(patch_addr + 4 * 4, patch_addr + 1 * 4, ARM_COND_NE); // bne retry (to ldrex)
        trampoline[5] = 0xE49D0008u | (scratch << 12);                               // ldr scratch, [sp], #8
        trampoline[6] = arm_b_cond(patch_addr + 6 * 4, addr + 4, ARM_COND_AL);           // b resume

        kuKernelCpuUnrestrictedMemcpy((void *) patch_addr, trampoline, sizeof(trampoline));
        kuKernelFlushCaches((void *) patch_addr, sizeof(trampoline));

        uint32_t branch_out = arm_b_cond(addr, patch_addr, ARM_COND_AL);
        kuKernelCpuUnrestrictedMemcpy((void *) addr, &branch_out, sizeof(branch_out));
        kuKernelFlushCaches((void *) addr, sizeof(branch_out));

        l_info("Patched SWP-r12 at 0x%08X (r%u,r%u,[r%u]) -> LDREX/STREX trampoline at 0x%08X (scratch r%u)",
               (unsigned int) addr, rt, rt2, rn, (unsigned int) patch_addr, scratch);
    }
}

/*
 * Confirmed (via crash dumps and disassembly across the whole binary) that every
 * real SWP-based spinlock idiom in this .so is immediately followed by either
 * `cmp Rt, #0` or `cmp Rt, Rm` (where the compiler cached 0 in a register).
 * Scans the whole loaded .text for the pattern (SWP/SWPB bit pattern and that
 * collateral cmp right after -- checking for cmp defends against false-positives
 * on embedded data or Thumb-2 instructions) and patches every match with an
 * atomic LDREX/STREX trampoline.
 */
static void patch_all_swp_spinlocks(void) {
    uintptr_t start = so_mod.text_base;
    uintptr_t end = so_mod.text_base + so_mod.text_size;
    int patched = 0;

    for (uintptr_t addr = start; addr + 8 <= end; addr += 4) {
        uint32_t instr = *(volatile uint32_t *) addr;
        if ((instr & 0x0FB00FF0u) != 0x01000090u) {
            continue;
        }

        uint32_t rt = (instr >> 12) & 0xF;
        uint32_t next = *(volatile uint32_t *) (addr + 4);
        int is_cmp_imm0 = ((next & 0x0FFF0FFFu) == (0x03500000u | (rt << 16)));
        int is_cmp_reg  = ((next & 0x0FFF0FF0u) == (0x01500000u | (rt << 16)));
        if (!is_cmp_imm0 && !is_cmp_reg) {
            l_warn("Skipping SWP-shaped word at 0x%08X (0x%08X): not followed by cmp r%u,#0 or cmp r%u,reg "
                   "(found 0x%08X), likely embedded data, not patching",
                   (unsigned int) addr, instr, rt, rt, next);
            continue;
        }

        patch_swp_with_ldrex_strex(addr);
        patched++;
    }

    l_info("SWP spinlock scan: patched %d instance(s) across the whole .so", patched);
}

static char **m_gAppPath_ptr = NULL;

static void initPath_hook(void) {
    if (m_gAppPath_ptr) {
        if (!*m_gAppPath_ptr) {
            *m_gAppPath_ptr = (char *)malloc(512);
        }
        snprintf(*m_gAppPath_ptr, 512, "%s", DATA_PATH);
        chdir(*m_gAppPath_ptr);
        l_info("initPath hook: m_gAppPath set to %s", *m_gAppPath_ptr);
    }
}

static void license_init_stub(void *env, void *clazz) {
    (void)env;
    (void)clazz;
    l_info("ALicenseCheck_InitLicense bypassed");
}

static void license_validate_stub(bool param_1) {
    (void)param_1;
    l_info("ALicenseCheck_ValidateLicense bypassed");
}

static int license_validate_server_stub(bool param_1) {
    (void)param_1;
    l_info("ALicenseCheck::ValidateServer bypassed -> returning true");
    return 1;
}

static void license_load_config_stub(void) {
    l_info("ALicenseCheck::LoadConfig bypassed");
}

static int license_load_rms_stub(void) {
    l_info("ALicenseCheck::LoadRMS bypassed");
    return 1;
}

static void license_save_rms_stub(bool param_1) {
    (void)param_1;
    l_info("ALicenseCheck::SaveRMS bypassed");
}

static void patch_game_framerender(void) {
    uintptr_t addr_hook  = so_mod.text_base + 0x000bf8c4;
    uintptr_t addr_bf8f8 = so_mod.text_base + 0x000bf8f8;
    uintptr_t addr_bf8e8 = so_mod.text_base + 0x000bf8e8;

    uint32_t trampoline[13];
    uintptr_t patch_addr = so_alloc_arena(&so_mod, SWP_PATCH_B_RANGE, addr_hook + 8, sizeof(trampoline));
    if (!patch_addr) {
        fatal_error("Failed to allocate FrameRender trampoline\n");
    }

    // [0]: ldr r3, [r4, r5] (load GOT entry)
    trampoline[0] = 0xe7943005;
    // [1]: ldr r3, [r3] (load s_impl)
    trampoline[1] = 0xe5933000;
    // [2]: cmp r3, #0 (check s_impl)
    trampoline[2] = 0xe3530000;
    // [3]: beq return_early
    trampoline[3] = arm_b_cond(patch_addr + 3 * 4, patch_addr + 11 * 4, ARM_COND_EQ);
    // [4]: ldr r2, [r3, #4] (load s_impl->m_pDriver)
    trampoline[4] = 0xe5932004;
    // [5]: cmp r2, #0 (check driver)
    trampoline[5] = 0xe3520000;
    // [6]: beq return_early
    trampoline[6] = arm_b_cond(patch_addr + 6 * 4, patch_addr + 11 * 4, ARM_COND_EQ);
    // [7]: ldr r3, [r2, #16] (load driver->field_16)
    trampoline[7] = 0xe5923010;
    // [8]: cmp r3, #0 (check field_16)
    trampoline[8] = 0xe3530000;
    // [9]: beq addr_bf8f8
    trampoline[9] = arm_b_cond(patch_addr + 9 * 4, addr_bf8f8, ARM_COND_EQ);
    // [10]: b addr_bf8e8
    trampoline[10] = arm_b_cond(patch_addr + 10 * 4, addr_bf8e8, ARM_COND_AL);
    // return_early:
    // [11]: add sp, sp, #16
    trampoline[11] = 0xe28dd010;
    // [12]: pop {r4, r5, r6, r7, r8, pc}
    trampoline[12] = 0xe8bd81f0;

    kuKernelCpuUnrestrictedMemcpy((void *) patch_addr, trampoline, sizeof(trampoline));
    kuKernelFlushCaches((void *) patch_addr, sizeof(trampoline));

    uint32_t branch_out = arm_b_cond(addr_hook, patch_addr, ARM_COND_AL);
    kuKernelCpuUnrestrictedMemcpy((void *) addr_hook, &branch_out, sizeof(branch_out));
    kuKernelFlushCaches((void *) addr_hook, sizeof(branch_out));

    l_info("Patched Game::FrameRender null-deref guard -> trampoline at 0x%08X", (unsigned int) patch_addr);
}

void so_patch(void) {
    m_gAppPath_ptr = (char **)so_symbol(&so_mod, "m_gAppPath");

    uintptr_t initPath_sym = so_symbol(&so_mod, "_Z8initPathv");
    if (initPath_sym) {
        hook_addr(initPath_sym, (uintptr_t)&initPath_hook);
        l_info("Hooked _Z8initPathv successfully");
    } else {
        l_warn("Could not find _Z8initPathv to hook");
    }

    uintptr_t license_init_sym = so_symbol(&so_mod, "ALicenseCheck_InitLicense");
    if (license_init_sym) {
        hook_addr(license_init_sym, (uintptr_t)&license_init_stub);
        l_info("Hooked ALicenseCheck_InitLicense successfully");
    }

    uintptr_t license_init_cpp_sym = so_symbol(&so_mod, "_ZN13ALicenseCheck4InitEP7_JNIEnvP7_jclass");
    if (license_init_cpp_sym) {
        hook_addr(license_init_cpp_sym, (uintptr_t)&license_init_stub);
        l_info("Hooked _ZN13ALicenseCheck4InitEP7_JNIEnvP7_jclass successfully");
    }

    uintptr_t license_validate_sym = so_symbol(&so_mod, "ALicenseCheck_ValidateLicense");
    if (license_validate_sym) {
        hook_addr(license_validate_sym, (uintptr_t)&license_validate_stub);
        l_info("Hooked ALicenseCheck_ValidateLicense successfully");
    }

    uintptr_t license_server_sym = so_symbol(&so_mod, "_ZN13ALicenseCheck14ValidateServerEb");
    if (license_server_sym) {
        hook_addr(license_server_sym, (uintptr_t)&license_validate_server_stub);
        l_info("Hooked _ZN13ALicenseCheck14ValidateServerEb successfully");
    }

    uintptr_t license_load_cfg_sym = so_symbol(&so_mod, "_ZN13ALicenseCheck10LoadConfigEv");
    if (license_load_cfg_sym) {
        hook_addr(license_load_cfg_sym, (uintptr_t)&license_load_config_stub);
        l_info("Hooked _ZN13ALicenseCheck10LoadConfigEv successfully");
    }

    uintptr_t license_load_rms_sym = so_symbol(&so_mod, "_ZN13ALicenseCheck7LoadRMSEv");
    if (license_load_rms_sym) {
        hook_addr(license_load_rms_sym, (uintptr_t)&license_load_rms_stub);
        l_info("Hooked _ZN13ALicenseCheck7LoadRMSEv successfully");
    }

    uintptr_t license_save_rms_sym = so_symbol(&so_mod, "_ZN13ALicenseCheck7SaveRMSEb");
    if (license_save_rms_sym) {
        hook_addr(license_save_rms_sym, (uintptr_t)&license_save_rms_stub);
        l_info("Hooked _ZN13ALicenseCheck7SaveRMSEb successfully");
    }

    // Confirmed across 4 separate crashes (shared_count copy ctor, sp_counted_base::release,
    // spinlock_pool<1>::scoped_lock ctor, shared_count destructor) that this .so has the same
    // broken SWP-based spinlock idiom inlined at every touch of any boost::shared_ptr<T>'s
    // refcount -- scan once for all remaining instances instead of patching them one confirmed
    // crash at a time. See patch_all_swp_spinlocks() for what makes this scan safe.
    patch_all_swp_spinlocks();

    // Defend against premature Game::FrameRender dereferencing NULL s_impl / driver
    patch_game_framerender();
}
