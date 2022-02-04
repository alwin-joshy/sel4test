/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <autoconf.h>
#include <sel4/sel4.h>

#ifdef CONFIG_ARCH_X86
#include <platsupport/arch/tsc.h>
#endif

#define N_ASID_POOLS ((int)BIT(seL4_NumASIDPoolsBits))
#define ASID_POOL_SIZE ((int)BIT(seL4_ASIDPoolIndexBits))

#include "../helpers.h"

static int remote_function(void)
{
    return 42;
}

static int test_interas_diffcspace(env_t env)
{
    helper_thread_t t;

    create_helper_process(env, &t);

    start_helper(env, &t, (helper_fn_t) remote_function, 0, 0, 0, 0);
    seL4_Word ret = wait_for_helper(&t);
    test_assert(ret == 42);
    cleanup_helper(env, &t);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0000, "Test threads in different cspace/vspace", test_interas_diffcspace, true)

#if defined(CONFIG_ARCH_AARCH32)
static int
test_unmap_after_delete(env_t env)
{
    seL4_Word map_addr = 0x10000000;
    cspacepath_t path;
    int error;

    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(frame != 0);

    seL4_ARM_ASIDPool_Assign(env->asid_pool, pd);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, pd, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_ARM_Page_Map(frame, pd, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* delete the page directory */
    vka_cspace_make_path(&env->vka, pd, &path);
    seL4_CNode_Delete(path.root, path.capPtr, path.capDepth);

    /* unmap the frame */
    seL4_ARM_Page_Unmap(frame);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0001, "Test unmapping a page after deleting the PD", test_unmap_after_delete, true)

#define NPAGE 512
#define NPAGE_LARGE 32

static int test_range_unmap_small(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_CPtr frames[NPAGE];
    int error;

    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr pt2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);

    for (int i = 0; i < NPAGE; i++) {
        frames[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
        test_assert(frames[i] != 0);
    }

    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(pt2 != 0);

    seL4_ARM_ASIDPool_Assign(env->asid_pool, pd);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, pd, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt2, pd, map_addr + NPAGE/2 * PAGE_SIZE_4K, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    for (int i = 0; i < NPAGE; i++) {
        error = seL4_ARM_Page_Map(frames[i], pd, map_addr + i * PAGE_SIZE_4K, seL4_AllRights, seL4_ARM_Default_VMAttributes);
        test_error_eq(error, 0);
    }

    seL4_Word curr_addr = map_addr;
    seL4_Word end_addr = curr_addr + NPAGE * PAGE_SIZE_4K;

    while (curr_addr < end_addr) {
        seL4_ARM_PageDirectory_Range_Protect_t remap_ret = seL4_ARM_PageDirectory_Range_Protect(pd, curr_addr, end_addr, seL4_NoRights);
        test_error_eq(remap_ret.error, 0);
        test_assert(remap_ret.num == 32);
        remap_ret = seL4_ARM_PageDirectory_Range_Protect(pd, curr_addr, end_addr, seL4_AllRights);
        test_error_eq(remap_ret.error, 0);
        test_assert(remap_ret.num == 32);
        curr_addr = remap_ret.next_vaddr;
    }
    test_error_eq(error, 0);
    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0011, "Test range based unmap function with small pages", test_range_unmap_small, true)

static int test_range_unmap_large(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_CPtr frames[NPAGE_LARGE];
    int error;

    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr pt2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);

    for (int i = 0; i < NPAGE_LARGE; i++) {
        frames[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_LargePageObject, 0);
        test_assert(frames[i] != 0);
    }


    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(pt2 != 0);

    seL4_ARM_ASIDPool_Assign(env->asid_pool, pd);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, pd, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt2, pd, map_addr + NPAGE/2 * PAGE_SIZE_4K, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    for (int i = 0; i < NPAGE_LARGE; i++) {
        error = seL4_ARM_Page_Map(frames[i], pd, map_addr + i * (1 << seL4_LargePageBits), seL4_NoRights, seL4_ARM_Default_VMAttributes);
        test_error_eq(error, 0);
    }

    seL4_CPtr small = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
    test_assert(small != 0);

    /* Because a large page is already mapped at this level, we will not be able to map a small page*/
    error = seL4_ARM_Page_Map(small, pd, map_addr + 3 * PAGE_SIZE_4K, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_DeleteFirst);

    seL4_Word curr_addr = map_addr;
    seL4_Word end = map_addr + NPAGE_LARGE * (1 << seL4_LargePageBits);

    while (curr_addr < end) {
        seL4_ARM_PageDirectory_Range_Protect_t unmap_ret = seL4_ARM_PageDirectory_Range_Protect(pd, curr_addr, end, seL4_AllRights);
        test_error_eq(unmap_ret.error, 0);
        test_assert(unmap_ret.num == 32);
        curr_addr = unmap_ret.next_vaddr;
    }

    /* Since we unmapped we should be able to do a page table map now */
    error = seL4_ARM_Page_Map(small, pd, map_addr + 3 * PAGE_SIZE_4K, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, 0);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0012, "Test range based unmap function with large pages", test_range_unmap_large, true)

static int test_range_unmap_small_large(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_CPtr small_frames[128];
    seL4_CPtr large_frames[8];
    int error;

    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);

    for (int i = 0; i < 128; i++) {
        small_frames[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
        test_assert(small_frames[i] != 0);
    }

   for (int i = 0; i < 8; i++) {
        large_frames[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_LargePageObject, 0);
        test_assert(large_frames[i] != 0);
    }

    test_assert(pd != 0);
    test_assert(pt != 0);

    seL4_ARM_ASIDPool_Assign(env->asid_pool, pd);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, pd, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    for (int i = 0; i < 128; i++) {
        error = seL4_ARM_Page_Map(small_frames[i], pd, map_addr + i * PAGE_SIZE_4K, seL4_NoRights, seL4_ARM_Default_VMAttributes);
        test_error_eq(error, 0);
    }

    for (int i = 0; i < 8; i++) {
        error = seL4_ARM_Page_Map(large_frames[i], pd, map_addr + 128 * PAGE_SIZE_4K + i * (1 << seL4_LargePageBits),
                                  seL4_NoRights, seL4_ARM_Default_VMAttributes);
        test_error_eq(error, 0);
    }


    seL4_CPtr small = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
    test_assert(small != 0);

    /* Because a large page is already mapped at this level, we will not be able to map a small page*/
    error = seL4_ARM_Page_Map(small, pd, map_addr + 128 * PAGE_SIZE_4K, seL4_NoRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_DeleteFirst);

    seL4_Word curr_addr = map_addr;
    seL4_Word end_addr = curr_addr + 256 * PAGE_SIZE_4K;
    seL4_ARM_PageDirectory_Range_Protect_t unmap_ret;

    while (curr_addr < end_addr) {
        unmap_ret = seL4_ARM_PageDirectory_Range_Protect(pd, curr_addr, end_addr, seL4_AllRights);
        test_error_eq(unmap_ret.error, 0);
        test_assert(unmap_ret.num == 32);
        curr_addr = unmap_ret.next_vaddr;
    }

    /* Since we unmapped, it we should be able to do a page table map now */
    error = seL4_ARM_Page_Map(small, pd, map_addr + 128 * PAGE_SIZE_4K, seL4_NoRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, 0);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0013, "Test range based unmap function with large and small pages", test_range_unmap_small_large, true)

static int test_reuse_cap(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_Word map_addr_2 = 0x10010000;
    seL4_Word map_addr_3 = 0x10020000;
    int error;


    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);

    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(frame != 0);

    seL4_ARM_ASIDPool_Assign(env->asid_pool, pd);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, pd, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_ARM_Page_Map(frame, pd, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Try to remap the page at a different address - should fail */
    error = seL4_ARM_Page_Map(frame, pd, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    /* Unmap it with the single page unmap version*/
    error = seL4_ARM_Page_Unmap(frame);
    test_error_eq(error, seL4_NoError);

    /* Try to remap it again at the different address (should work this time) */
    error = seL4_ARM_Page_Map(frame, pd, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Unmap it with range unmap instead of page unmap */
    seL4_ARM_PageDirectory_Range_Protect_t unmap_ret = seL4_ARM_PageDirectory_Range_Protect(pd, map_addr_2, map_addr_2 + 16 * PAGE_SIZE_4K, seL4_AllRights);
    test_error_eq(unmap_ret.error, 0);
//    test_assert(unmap_ret.num == 16);

    /* Try to remap the page at a different address with old map - should fail */
    error = seL4_ARM_Page_Map(frame, pd, map_addr_3, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    /* Try to remap the page at a different address */
    error = seL4_ARM_PageDirectory_Page_Map(pd, frame, map_addr_3, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}

DEFINE_TEST(VSPACE0014, "Test re-using frame cap for different vaddr after range unmap", test_reuse_cap, true)

static int test_two_frames_same_vaddr(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_Word map_addr_2 = 0x10005000;
    int error;


    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
    seL4_CPtr frame2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);

    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(frame != 0);
    test_assert(frame2 != 0);

    seL4_ARM_ASIDPool_Assign(env->asid_pool, pd);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, pd, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_ARM_Page_Map(frame, pd, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame2 into the page table to replace frame*/
    error = seL4_ARM_Page_Map(frame2, pd, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Try to map frame 2 to a different address with new map - should fail*/
    error = seL4_ARM_PageDirectory_Page_Map(pd, frame2, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

   /* Try to map frame to a different address with old map - should fail*/
    error = seL4_ARM_Page_Map(frame, pd, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    /* try map frame at a different vaddr with new map*/
    error = seL4_ARM_PageDirectory_Page_Map(pd, frame, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}

DEFINE_TEST(VSPACE0015, "Test re-using a stale cap as a result of overwriting mapping", test_two_frames_same_vaddr, true)

static int test_remap_del_pt(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_Word map_addr_2 = map_addr + 512 * PAGE_SIZE_4K;
    int error;

    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr pt2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);

    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(pt2 != 0);
    test_assert(frame != 0);

    seL4_ARM_ASIDPool_Assign(env->asid_pool, pd);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, pd, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt2, pd, map_addr_2, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_ARM_Page_Map(frame, pd, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_ARM_PageTable_Unmap(pt);
    test_error_eq(error, 0);

    error = seL4_ARM_PageDirectory_Page_Map(pd, frame, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, 0);

    return sel4test_get_result();
}

DEFINE_TEST(VSPACE0016, "Test re-mapping a stale frame cap after unmapping page table", test_remap_del_pt, true)

static int test_remap_diff_vspace(env_t env) {
    seL4_Word map_addr = 0x10000000;
    int error;

    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pd2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr pt2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);

    test_assert(pd != 0);
    test_assert(pd2 != 0);
    test_assert(pt != 0);
    test_assert(pt2 != 0);
    test_assert(frame != 0);

    seL4_ARM_ASIDPool_Assign(env->asid_pool, pd);
    seL4_ARM_ASIDPool_Assign(env->asid_pool, pd2);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, pd, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_ARM_Page_Map(frame, pd, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt2, pd2, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_ARM_Page_Map(frame, pd2, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidCapability);

    /* map frame into the page table */
    error = seL4_ARM_PageDirectory_Page_Map(pd2, frame, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    ZF_LOGE("%lu", error);
    test_error_eq(error, seL4_InvalidArgument);

    seL4_ARM_PageDirectory_Range_Protect_t unmap_ret = seL4_ARM_PageDirectory_Range_Protect(pd, map_addr, map_addr + PAGE_SIZE_4K, seL4_AllRights);
    test_error_eq(unmap_ret.error, 0);

    /* map frame into the page table */
    error = seL4_ARM_Page_Map(frame, pd2, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidCapability);

    /* map frame into the page table */
    error = seL4_ARM_PageDirectory_Page_Map(pd2, frame, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);


    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0017, "Test re-mapping a stale frame cap from a different vspace", test_remap_diff_vspace, true);

#elif defined(CONFIG_ARCH_AARCH64)
static int
test_unmap_after_delete(env_t env)
{
    seL4_Word map_addr = 0x10000000;
    cspacepath_t path;
    int error;

    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
    /* Under an Arm Hyp configuration where the CPU only supports 40bit physical addressing, we
     * only have 3 level page tables and no PGD.
     */
    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(frame != 0);


    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
#if seL4_PGDBits > 0
    /* map pud into page global directory */
    error = seL4_ARM_PageUpperDirectory_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* map pd into page upper directory */
    error = seL4_ARM_PageDirectory_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_ARM_Page_Map(frame, vspace, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* delete the page directory */
    vka_cspace_make_path(&env->vka, vspace, &path);
    seL4_CNode_Delete(path.root, path.capPtr, path.capDepth);

    /* unmap the frame */
    seL4_ARM_Page_Unmap(frame);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0001, "Test unmapping a page after deleting the PD", test_unmap_after_delete, true)

#define NPAGE 1024
#define NPAGE_LARGE 256

static int test_range_unmap_small(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_CPtr frames[NPAGE];
    int error;

    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);

    for (int i = 0; i < NPAGE; i++) {
        frames[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
        test_assert(frames[i] != 0);
    }

    /* Under an Arm Hyp configuration where the CPU only supports 40bit physical addressing, we
     * only have 3 level page tables and no PGD.
     */
    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);

    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
#if seL4_PGDBits > 0
    /* map pud into page global directory */
    error = seL4_ARM_PageUpperDirectory_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* map pd into page upper directory */
    error = seL4_ARM_PageDirectory_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

     seL4_CPtr pt2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    /* map page table into page directory */

    error = seL4_ARM_PageTable_Map(pt2, vspace, map_addr + NPAGE/2 * PAGE_SIZE_4K, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    for (int i = 0; i < NPAGE; i++) {
        error = seL4_ARM_Page_Map(frames[i], vspace, map_addr + i * PAGE_SIZE_4K, seL4_AllRights, seL4_ARM_Default_VMAttributes);
        test_error_eq(error, 0);
    }

    seL4_Word curr_addr = map_addr;
    seL4_Word end_addr = curr_addr + NPAGE * PAGE_SIZE_4K; 

    while (curr_addr < end_addr) {
        seL4_ARM_VSpace_Range_Protect_t remap_ret = seL4_ARM_VSpace_Range_Protect(vspace, curr_addr, end_addr, seL4_NoRights);
        test_error_eq(remap_ret.error, 0);
        test_assert(remap_ret.num == 32);
        remap_ret = seL4_ARM_VSpace_Range_Protect(vspace, curr_addr, end_addr, seL4_AllRights);
        test_error_eq(remap_ret.error, 0);
        test_assert(remap_ret.num == 32);
        curr_addr = remap_ret.next_vaddr;
    }


    test_error_eq(error, 0);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0011, "Test range based unmap function with small pages", test_range_unmap_small, true)

static int test_range_unmap_large(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_CPtr frames[NPAGE_LARGE];
    int error;

    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);

    for (int i = 0; i < NPAGE_LARGE; i++) {
        frames[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_LargePageObject, 0);
        test_assert(frames[i] != 0);
    }

    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);
    test_assert(pd != 0);

    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
#if seL4_PGDBits > 0
    /* map pud into page global directory */
    error = seL4_ARM_PageUpperDirectory_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* map pd into page upper directory */
    error = seL4_ARM_PageDirectory_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    for (int i = 0; i < NPAGE_LARGE; i++) {
        error = seL4_ARM_Page_Map(frames[i], vspace, map_addr + i * (1 << seL4_LargePageBits), seL4_NoRights, seL4_ARM_Default_VMAttributes);
        test_error_eq(error, 0);
    }

    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    test_assert(pt != 0);

    /* Because a large page is already mapped at this level, we will not be able to map a page table*/
    error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_DeleteFirst);


    /* Because a large page is already mapped at this level, we will not be able to map a page table*/
    error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_DeleteFirst);

    seL4_Word curr_addr = map_addr;
    seL4_Word end = map_addr + NPAGE_LARGE * (1 << seL4_LargePageBits);

    while (curr_addr < end) {
        seL4_ARM_VSpace_Range_Protect_t unmap_ret = seL4_ARM_VSpace_Range_Protect(vspace, curr_addr, end, seL4_AllRights);
        test_error_eq(unmap_ret.error, 0);
        test_assert(unmap_ret.num == 32);
        curr_addr = unmap_ret.next_vaddr;
    }

    /* Since we unmapped we should be able to do a page table map now */
    error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, 0);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0012, "Test range based unmap function with large pages", test_range_unmap_large, true)

static int test_range_unmap_small_large(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_CPtr small_frames[256];
    int error;

    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);

    for (int i = 0; i < 256; i++) {
        small_frames[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
        test_assert(small_frames[i] != 0);
    }

    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);

    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);

#if seL4_PGDBits > 0
    /* map pud into page global directory */
    error = seL4_ARM_PageUpperDirectory_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* map pd into page upper directory */
    error = seL4_ARM_PageDirectory_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    for (int i = 0; i < 256; i++) {
        error = seL4_ARM_Page_Map(small_frames[i], vspace, map_addr + i * PAGE_SIZE_4K, seL4_NoRights, seL4_ARM_Default_VMAttributes);
        test_error_eq(error, 0);
    }

    seL4_CPtr large =  vka_alloc_object_leaky(&env->vka, seL4_ARM_LargePageObject, 0);
    test_assert(large != 0);
    error = seL4_ARM_Page_Map(large, vspace, map_addr + (1 << seL4_LargePageBits), seL4_NoRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, 0);

    seL4_CPtr pt2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    test_assert(pt2 != 0);

    /* Because a large page is already mapped at this level, we will not be able to map a page table*/
    error = seL4_ARM_PageTable_Map(pt2, vspace, map_addr + (1 << seL4_LargePageBits), seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_DeleteFirst);

    seL4_Word curr_addr = map_addr;
    seL4_Word end_addr = curr_addr + 256 * PAGE_SIZE_4K;
    seL4_ARM_VSpace_Range_Protect_t unmap_ret;

    while (curr_addr < end_addr) {
        unmap_ret = seL4_ARM_VSpace_Range_Protect(vspace, curr_addr, end_addr, seL4_AllRights);
        test_error_eq(unmap_ret.error, 0);
        test_assert(unmap_ret.num == 32);
        curr_addr = unmap_ret.next_vaddr;
    }

    unmap_ret = seL4_ARM_VSpace_Range_Protect(vspace, map_addr + 1 * (1 << seL4_LargePageBits), map_addr + 2 * (1 << seL4_LargePageBits), seL4_AllRights);
    test_error_eq(unmap_ret.error, 0);
    test_assert(unmap_ret.num == 1);

    /* Since we unmapped, it we should be able to do a page table map now */
    error = seL4_ARM_PageTable_Map(pt2, vspace, map_addr + (1 << seL4_LargePageBits), seL4_ARM_Default_VMAttributes);
    test_error_eq(error, 0);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0013, "Test range based unmap function with large and small pages", test_range_unmap_small_large, true)

static int test_reuse_cap(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_Word map_addr_2 = 0x10005000;
    seL4_Word map_addr_3 = 0x1000A000;
    int error;

    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
    /* Under an Arm Hyp configuration where the CPU only supports 40bit physical addressing, we
     * only have 3 level page tables and no PGD.
     */
    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(frame != 0);


    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
#if seL4_PGDBits > 0
    /* map pud into page global directory */
    error = seL4_ARM_PageUpperDirectory_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* map pd into page upper directory */
    error = seL4_ARM_PageDirectory_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_ARM_Page_Map(frame, vspace, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Try to remap the page at a different address - should fail */
    error = seL4_ARM_Page_Map(frame, vspace, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    /* Unmap it with the single page unmap version*/
    error = seL4_ARM_Page_Unmap(frame);
    test_error_eq(error, seL4_NoError);

    /* Try to remap it again at the different address (should work this time) */
    error = seL4_ARM_Page_Map(frame, vspace, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Unmap it with range unmap instead of page unmap */
    seL4_ARM_VSpace_Range_Protect_t unmap_ret = seL4_ARM_VSpace_Range_Protect(vspace, map_addr_2, map_addr_2 + PAGE_SIZE_4K, seL4_AllRights);
    test_error_eq(unmap_ret.error, 0);
    test_assert(unmap_ret.num == 1);

    /* Try to remap the page at a different address with old map - should fail */
    error = seL4_ARM_Page_Map(frame, vspace, map_addr_3, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    /* Try to remap the page at a different address */
    error = seL4_ARM_VSpace_Page_Map(vspace, frame, map_addr_3, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}

DEFINE_TEST(VSPACE0014, "Test re-using frame cap for different vaddr after range unmap", test_reuse_cap, true)

static int test_two_frames_same_vaddr(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_Word map_addr_2 = 0x10005000;
    int error;

    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
    seL4_CPtr frame2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
    /* Under an Arm Hyp configuration where the CPU only supports 40bit physical addressing, we
     * only have 3 level page tables and no PGD.
     */
    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(frame != 0);
    test_assert(frame2 != 0);


    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
#if seL4_PGDBits > 0
    /* map pud into page global directory */
    error = seL4_ARM_PageUpperDirectory_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* map pd into page upper directory */
    error = seL4_ARM_PageDirectory_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_ARM_Page_Map(frame, vspace, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame2 into the page table to replace frame*/
    error = seL4_ARM_Page_Map(frame2, vspace, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Try to map frame 2 to a different address with new map - should fail*/
    error = seL4_ARM_VSpace_Page_Map(vspace, frame2, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

   /* Try to map frame to a different address with old map - should fail*/
    error = seL4_ARM_Page_Map(frame, vspace, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    /* try map frame at a different vaddr*/
    error = seL4_ARM_VSpace_Page_Map(vspace, frame, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}

DEFINE_TEST(VSPACE0015, "Test re-using a stale cap as a result of overwriting mapping", test_two_frames_same_vaddr, true)

static int test_remap_del_pt(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_Word map_addr_2 = map_addr + 1024 * PAGE_SIZE_4K;
    int error;

    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr pt2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);

    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(pt2 != 0);
    test_assert(frame != 0);

    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
#if seL4_PGDBits > 0
    /* map pud into page global directory */
    error = seL4_ARM_PageUpperDirectory_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* map pd into page upper directory */
    error = seL4_ARM_PageDirectory_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt2, vspace, map_addr_2, seL4_ARM_Default_VMAttributes);
    ZF_LOGE("%d", error);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_ARM_Page_Map(frame, vspace, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_ARM_PageTable_Unmap(pt);
    test_error_eq(error, 0);

    error = seL4_ARM_VSpace_Page_Map(vspace, frame, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, 0);

    return sel4test_get_result();
}

DEFINE_TEST(VSPACE0016, "Test re-mapping a stale frame cap after unmapping page table", test_remap_del_pt, true)

static int test_remap_diff_vspace(env_t env) {
    seL4_Word map_addr = 0x10000000;
    int error;

    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr pgd2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
    seL4_CPtr pud2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);

    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert((seL4_PGDBits == 0) || pgd2 != 0);
    test_assert(pud != 0);
    test_assert(pud2 != 0);
    test_assert(pd != 0);
    test_assert(pd2 != 0);
    test_assert(pt != 0);
    test_assert(pt2 != 0);
    test_assert(frame != 0);

    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_CPtr vspace2 = (seL4_PGDBits == 0) ? pud2 : pgd2;

    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace2);

#if seL4_PGDBits > 0
    /* map pud into page global directory */
    error = seL4_ARM_PageUpperDirectory_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* map pd into page upper directory */
    error = seL4_ARM_PageDirectory_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_ARM_Page_Map(frame, vspace, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

#if seL4_PGDBits > 0
    /* map pud into page global directory */
    error = seL4_ARM_PageUpperDirectory_Map(pud2, vspace2, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* map pd into page upper directory */
    error = seL4_ARM_PageDirectory_Map(pd2, vspace2, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt2, vspace2, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_ARM_Page_Map(frame, vspace2, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidCapability);

    /* map frame into the page table */
    error = seL4_ARM_Vspace_Page_Map(vspace2, frame, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    seL4_ARM_Vspace_Range_Protect_t unmap_ret = seL4_ARM_Vspace_Range_Protect(vspace, map_addr, map_addr + PAGE_SIZE_4K, seL4_AllRights);
    test_error_eq(unmap_ret.error, 0);

    /* map frame into the page table */
    error = seL4_ARM_Page_Map(frame, vspace2, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidCapability);

    /* map frame into the page table */
    error = seL4_ARM_Vspace_Page_Map(vspace2, frame, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

     return sel4test_get_result();
}
DEFINE_TEST(VSPACE0017, "Test re-mapping a stale frame cap from a different vspace", test_remap_del_pt, true);
#elif defined(CONFIG_ARCH_RISCV)

#define NPAGE 1024
#define NPAGE_LARGE 256

static int test_range_unmap_small(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_CPtr frames[NPAGE];
    int error;

    seL4_CPtr root_pt = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl2 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl3_1 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl3_2 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);

    for (int i = 0; i < NPAGE; i++) {
        frames[i] = vka_alloc_object_leaky(&env->vka, seL4_RISCV_4K_Page, 0);
        test_assert(frames[i] != 0);
    }

    test_assert(root_pt != 0);
    test_assert(lvl2 != 0);
    test_assert(lvl3_1 != 0);
    test_assert(lvl3_2 != 0);

    seL4_RISCV_ASIDPool_Assign(env->asid_pool, root_pt);

    error = seL4_RISCV_PageTable_Map(lvl2, root_pt, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_RISCV_PageTable_Map(lvl3_1, root_pt, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_RISCV_PageTable_Map(lvl3_2, root_pt, map_addr + NPAGE/2 * PAGE_SIZE_4K, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    for (int i = 0; i < NPAGE; i++) {
        error = seL4_RISCV_Page_Map(frames[i], root_pt, map_addr + i * PAGE_SIZE_4K, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
        test_error_eq(error, seL4_NoError);
    }

    seL4_Word curr_addr = map_addr;
    seL4_Word end_addr = curr_addr + NPAGE * PAGE_SIZE_4K;

    while (curr_addr < end_addr) {
        seL4_RISCV_PageTable_RangeProtect_t remap_ret = seL4_RISCV_PageTable_RangeProtect(root_pt, curr_addr, end_addr, seL4_NoRights);
        test_error_eq(remap_ret.error, 0);
        test_assert(remap_ret.num == 32);
        remap_ret = seL4_RISCV_PageTable_RangeProtect(root_pt, curr_addr, end_addr, seL4_AllRights);
        test_error_eq(remap_ret.error, 0);
        test_assert(remap_ret.num == 32);
        curr_addr = remap_ret.next_vaddr;
    }

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0011, "Test range based unmap function with small pages", test_range_unmap_small, true)

static int test_range_unmap_large(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_CPtr frames[NPAGE_LARGE];
    int error;

    seL4_CPtr root_pt = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl2 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl3 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);

    for (int i = 0; i < NPAGE_LARGE; i++) {
        frames[i] = vka_alloc_object_leaky(&env->vka, seL4_RISCV_Mega_Page, 0);
        test_assert(frames[i] != 0);
    }

    test_assert(root_pt != 0);
    test_assert(lvl2 != 0);
    test_assert(lvl3 != 0);

    seL4_RISCV_ASIDPool_Assign(env->asid_pool, root_pt);

    error = seL4_RISCV_PageTable_Map(lvl2, root_pt, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    for (int i = 0; i < NPAGE_LARGE; i++) {
        error = seL4_RISCV_Page_Map(frames[i], root_pt, map_addr + i * (1 << seL4_LargePageBits), seL4_NoRights, seL4_ARCH_Default_VMAttributes);
        test_error_eq(error, seL4_NoError);
    }


    /* Because a large page is already mapped at this level, we will not be able to map a level 3 page table */
    error = seL4_RISCV_PageTable_Map(lvl3, root_pt, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_DeleteFirst);

    seL4_Word curr_addr = map_addr;
    seL4_Word end_addr = map_addr + NPAGE_LARGE * (1 << seL4_LargePageBits);

    while (curr_addr < end_addr) {
        seL4_RISCV_PageTable_RangeProtect_t unmap_ret = seL4_RISCV_PageTable_RangeProtect(root_pt, curr_addr, end_addr, seL4_AllRights);
        test_error_eq(unmap_ret.error, seL4_NoError);
        test_assert(unmap_ret.num == 32);
        curr_addr = unmap_ret.next_vaddr;
    }

    /* Since we unmapped we should be able to do a page table map now */
    error = seL4_RISCV_PageTable_Map(lvl3, root_pt, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, 0);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0012, "Test range based unmap function with large pages", test_range_unmap_large, true)


static int test_range_unmap_small_large(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_CPtr small_frames[256];
    int error;

    seL4_CPtr root_pt = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl2 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl3 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);

    for (int i = 0; i < 256; i++) {
        small_frames[i] = vka_alloc_object_leaky(&env->vka, seL4_RISCV_4K_Page, 0);
        test_assert(small_frames[i] != 0);
    }

    test_assert(root_pt != 0);
    test_assert(lvl2 != 0);
    test_assert(lvl3 != 0);


    seL4_RISCV_ASIDPool_Assign(env->asid_pool, root_pt);

    error = seL4_RISCV_PageTable_Map(lvl2, root_pt, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_RISCV_PageTable_Map(lvl3, root_pt, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    for (int i = 0; i < 256; i++) {
        seL4_RISCV_Page_Map(small_frames[i], root_pt, map_addr + i * PAGE_SIZE_4K, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
        test_error_eq(error, 0);
    }

    seL4_CPtr large =  vka_alloc_object_leaky(&env->vka, seL4_RISCV_Mega_Page, 0);
    test_assert(large != 0);

    error = seL4_RISCV_Page_Map(large, root_pt, map_addr + (1 << seL4_LargePageBits), seL4_NoRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    seL4_CPtr lvl3_2 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    test_assert(lvl3_2 != 0);

    /* Because a large page is already mapped at this level, we will not be able to map a page table*/
    error = seL4_RISCV_PageTable_Map(lvl3_2, root_pt, map_addr + (1 << seL4_LargePageBits), seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_DeleteFirst);

    seL4_Word curr_addr = map_addr;
    seL4_Word end_addr = curr_addr + 256 * PAGE_SIZE_4K;
    seL4_RISCV_PageTable_RangeProtect_t unmap_ret;

    while (curr_addr < end_addr) {
        unmap_ret = seL4_RISCV_PageTable_RangeProtect(root_pt, curr_addr, end_addr, seL4_AllRights);
        test_error_eq(unmap_ret.error, seL4_NoError);
        test_assert(unmap_ret.num == 32);
        curr_addr = unmap_ret.next_vaddr;
    }

    unmap_ret = seL4_RISCV_PageTable_RangeProtect(root_pt, map_addr + (1 << seL4_LargePageBits),
                                                    map_addr + 2 * (1 << seL4_LargePageBits), seL4_AllRights);
    test_error_eq(unmap_ret.error, seL4_NoError);
    test_assert(unmap_ret.num == 1);

    /* Since we unmapped, it we should be able to do a page table map now */
    error = seL4_RISCV_PageTable_Map(lvl3_2, root_pt, map_addr + (1 << seL4_LargePageBits), seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0013, "Test range based unmap function with large and small pages", test_range_unmap_small_large, true)

static int test_reuse_cap(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_Word map_addr_2 = 0x10005000;
    seL4_Word map_addr_3 = 0x1000A000;
    int error;

    seL4_CPtr root_pt = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl2 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl3 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_RISCV_4K_Page, 0);

    test_assert(root_pt != 0);
    test_assert(lvl2 != 0);
    test_assert(lvl3 != 0);
    test_assert(frame != 0);

    seL4_RISCV_ASIDPool_Assign(env->asid_pool, root_pt);

    error = seL4_RISCV_PageTable_Map(lvl2, root_pt, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_RISCV_PageTable_Map(lvl3, root_pt, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_RISCV_Page_Map(frame, root_pt, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Try to remap the page at a different address - should fail */
    error = seL4_RISCV_Page_Map(frame, root_pt, map_addr_2, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    /* Unmap it with the single page unmap version*/
    error = seL4_RISCV_Page_Unmap(frame);
    test_error_eq(error, seL4_NoError);

    /* Try to remap it again at the different address (should work this time) */
    error = seL4_RISCV_Page_Map(frame, root_pt, map_addr_2, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Unmap it with range unmap instead of page unmap */
    seL4_RISCV_PageTable_RangeProtect_t unmap_ret = seL4_RISCV_PageTable_RangeProtect(root_pt, map_addr_2, map_addr_2 + PAGE_SIZE_4K, seL4_AllRights);
    test_error_eq(unmap_ret.error, 0);
    test_assert(unmap_ret.num == 1);

    /* Try to remap the page at a different address with old map - should fail */
    error = seL4_RISCV_Page_Map(frame, root_pt, map_addr_3, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    /* Try to remap the page at a different address with new map */
    error = seL4_RISCV_PageTable_PageMap(root_pt, frame, map_addr_3, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0014, "Test re-using frame cap for different vaddr after range unmap", test_reuse_cap, true)

static int test_two_frames_same_vaddr(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_Word map_addr_2 = 0x10005000;
    int error;

    seL4_CPtr root_pt = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl2 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl3 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_RISCV_4K_Page, 0);
    seL4_CPtr frame2 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_4K_Page, 0);

    test_assert(root_pt != 0);
    test_assert(lvl2 != 0);
    test_assert(lvl3 != 0);
    test_assert(frame != 0);
    test_assert(frame2 != 0);

    seL4_RISCV_ASIDPool_Assign(env->asid_pool, root_pt);

    error = seL4_RISCV_PageTable_Map(lvl2, root_pt, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_RISCV_PageTable_Map(lvl3, root_pt, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_RISCV_PageTable_PageMap(root_pt, frame, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame2 into the page table to replace frame*/
    error = seL4_RISCV_PageTable_PageMap(root_pt, frame2, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Try to map frame 2 to a different address with new map - should fail*/
    error = seL4_RISCV_PageTable_PageMap(root_pt, frame2, map_addr_2, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    ZF_LOGE("%d", error);
    test_error_eq(error, seL4_InvalidArgument);

    /* Try to map frame to a different address with old map - should fail*/
    error = seL4_RISCV_Page_Map(frame, root_pt, map_addr_2, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    /* try map frame at a different vaddr with new map*/
    error = seL4_RISCV_PageTable_PageMap(root_pt, frame, map_addr_2, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}

DEFINE_TEST(VSPACE0015, "Test re-using a stale cap as a result of overwriting mapping", test_two_frames_same_vaddr, true)

static int test_remap_del_pt(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_Word map_addr_2 = map_addr + 512 * PAGE_SIZE_4K;
    int error;

    seL4_CPtr root_pt = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl2 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl3_1 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl3_2 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_RISCV_4K_Page, 0);

    test_assert(root_pt != 0);
    test_assert(lvl2 != 0);
    test_assert(lvl3_1 != 0);
    test_assert(lvl3_2 != 0);
    test_assert(frame != 0);

    seL4_RISCV_ASIDPool_Assign(env->asid_pool, root_pt);

    error = seL4_RISCV_PageTable_Map(lvl2, root_pt, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_RISCV_PageTable_Map(lvl3_1, root_pt, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_RISCV_PageTable_Map(lvl3_2, root_pt, map_addr_2, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_RISCV_PageTable_PageMap(root_pt, frame, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_RISCV_PageTable_Unmap(lvl3_1);
    test_error_eq(error, seL4_NoError);

    error = seL4_RISCV_Page_Map(frame, root_pt, map_addr_2, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    error = seL4_RISCV_PageTable_PageMap(root_pt, frame, map_addr_2, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}

DEFINE_TEST(VSPACE0016, "Test re-mapping a stale frame cap after unmapping page table", test_remap_del_pt, true)

static int test_remap_diff_vspace(env_t env) {
    seL4_Word map_addr = 0x10000000;
    int error;

    seL4_CPtr root_pt_1 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl2_1 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl3_1 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);

    seL4_CPtr root_pt_2 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl2_2 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);
    seL4_CPtr lvl3_2 = vka_alloc_object_leaky(&env->vka, seL4_RISCV_PageTableObject, 0);

    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_RISCV_4K_Page, 0);

    test_assert(root_pt_1 != 0);
    test_assert(lvl2_1 != 0);
    test_assert(lvl3_1 != 0);
    test_assert(root_pt_2 != 0);
    test_assert(lvl2_2 != 0);
    test_assert(lvl3_2 != 0);
    test_assert(frame != 0);


    seL4_RISCV_ASIDPool_Assign(env->asid_pool, root_pt_1);
    seL4_RISCV_ASIDPool_Assign(env->asid_pool, root_pt_2);

    /* Set up first vspace*/
    error = seL4_RISCV_PageTable_Map(lvl2_1, root_pt_1, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_RISCV_PageTable_Map(lvl3_1, root_pt_1, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into first vspace */
    error = seL4_RISCV_Page_Map(frame, root_pt_1, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Set up second vspace*/
    error = seL4_RISCV_PageTable_Map(lvl2_2, root_pt_2, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_RISCV_PageTable_Map(lvl3_2, root_pt_2, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Try to map frame into the page table */
    error = seL4_RISCV_Page_Map(frame, root_pt_2, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidCapability);

    /* map frame into the page table */
    error = seL4_RISCV_PageTable_PageMap(root_pt_2, frame, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    seL4_RISCV_PageTable_RangeProtect_t unmap_ret = seL4_RISCV_PageTable_RangeProtect(root_pt_1, map_addr, map_addr + PAGE_SIZE_4K, seL4_AllRights);
    test_error_eq(unmap_ret.error, 0);

    /* map frame into the page table */
    error = seL4_RISCV_Page_Map(frame, root_pt_2, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidCapability);

    /* map frame into the page table */
    error = seL4_RISCV_PageTable_PageMap(root_pt_2, frame, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

     return sel4test_get_result();
}
DEFINE_TEST(VSPACE0017, "Test re-mapping a stale frame cap from a different vspace", test_remap_del_pt, true);

#elif defined(CONFIG_ARCH_X86_64)

#define NPAGE 1024
#define NPAGE_LARGE 256

static int test_range_unmap_small(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_CPtr frames[NPAGE];
    int error;

    seL4_CPtr pml4 = vka_alloc_object_leaky(&env->vka, seL4_X64_PML4Object, 0);
    seL4_CPtr pdpt = vka_alloc_object_leaky(&env->vka, seL4_X86_PDPTObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_X86_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_X86_PageTableObject, 0);
    seL4_CPtr pt2 = vka_alloc_object_leaky(&env->vka, seL4_X86_PageTableObject, 0);

    for (int i = 0; i < NPAGE; i++) {
        frames[i] = vka_alloc_object_leaky(&env->vka, seL4_X86_4K, 0);
        test_assert(frames[i] != 0);
    }

    test_assert(pml4 != 0);
    test_assert(pdpt != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(pt2 != 0);

    seL4_X86_ASIDPool_Assign(env->asid_pool, pml4);

    error = seL4_X86_PDPT_Map(pdpt, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageDirectory_Map(pd, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageTable_Map(pt, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageTable_Map(pt2, pml4, map_addr + NPAGE/2 * PAGE_SIZE_4K, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    for (int i = 0; i < NPAGE; i++) {
        error = seL4_X86_Page_Map(frames[i], pml4, map_addr + i * PAGE_SIZE_4K, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
        test_error_eq(error, seL4_NoError);
    }

    seL4_Word curr_addr = map_addr;
    seL4_Word end_addr = curr_addr + NPAGE * PAGE_SIZE_4K;

    while (curr_addr < end_addr) {
        seL4_X64_PML4_RangeProtect_t remap_ret = seL4_X64_PML4_RangeProtect(pml4, curr_addr, end_addr, seL4_NoRights);
        test_error_eq(remap_ret.error, 0);
        test_assert(remap_ret.num == 32);
        remap_ret = seL4_X64_PML4_RangeProtect(pml4, curr_addr, end_addr, seL4_AllRights);
        test_error_eq(remap_ret.error, 0);
        test_assert(remap_ret.num == 32);
        curr_addr = remap_ret.next_vaddr;
    }

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0011, "Test range based unmap function with small pages", test_range_unmap_small, true)

static int test_range_unmap_large(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_CPtr frames[NPAGE_LARGE];
    int error;

    seL4_CPtr pml4 = vka_alloc_object_leaky(&env->vka, seL4_X64_PML4Object, 0);
    seL4_CPtr pdpt = vka_alloc_object_leaky(&env->vka, seL4_X86_PDPTObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_X86_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_X86_PageTableObject, 0);

    for (int i = 0; i < NPAGE_LARGE; i++) {
        frames[i] = vka_alloc_object_leaky(&env->vka, seL4_X86_LargePageObject, 0);
        test_assert(frames[i] != 0);
    }

    test_assert(pml4 != 0);
    test_assert(pdpt != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);

    seL4_X86_ASIDPool_Assign(env->asid_pool, pml4);

    error = seL4_X86_PDPT_Map(pdpt, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageDirectory_Map(pd, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    for (int i = 0; i < NPAGE_LARGE; i++) {
        error = seL4_X86_Page_Map(frames[i], pml4, map_addr + i * (1 << seL4_LargePageBits), seL4_NoRights, seL4_ARCH_Default_VMAttributes);
        test_error_eq(error, 0);
    }

    /* Because a large page is already mapped at this level, we will not be able to map a level 3 page table */
    error = seL4_X86_PageTable_Map(pt, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_DeleteFirst);

    seL4_Word curr_addr = map_addr;
    seL4_Word end_addr = map_addr + NPAGE_LARGE * (1 << seL4_LargePageBits);

    while (curr_addr < end_addr) {
        seL4_X64_PML4_RangeProtect_t unmap_ret = seL4_X64_PML4_RangeProtect(pml4, curr_addr, end_addr, seL4_AllRights);
        test_error_eq(unmap_ret.error, seL4_NoError);
        test_assert(unmap_ret.num == 32);
        curr_addr = unmap_ret.next_vaddr;
    }

    /* Since we unmapped we should be able to do a page table map now */
    error = seL4_X86_PageTable_Map(pt, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, 0);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0012, "Test range based unmap function with large pages", test_range_unmap_large, true)

static int test_range_unmap_small_large(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_CPtr small_frames[256];
    int error;

    seL4_CPtr pml4 = vka_alloc_object_leaky(&env->vka, seL4_X64_PML4Object, 0);
    seL4_CPtr pdpt = vka_alloc_object_leaky(&env->vka, seL4_X86_PDPTObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_X86_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_X86_PageTableObject, 0);

    for (int i = 0; i < 256; i++) {
        small_frames[i] = vka_alloc_object_leaky(&env->vka, seL4_X86_4K, 0);
        test_assert(small_frames[i] != 0);
    }

    test_assert(pml4 != 0);
    test_assert(pdpt != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);

    seL4_X86_ASIDPool_Assign(env->asid_pool, pml4);

    error = seL4_X86_PDPT_Map(pdpt, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageDirectory_Map(pd, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageTable_Map(pt, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    for (int i = 0; i < 256; i++) {
        seL4_X86_Page_Map(small_frames[i], pml4, map_addr + i * PAGE_SIZE_4K, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
        test_error_eq(error, 0);
    }

    seL4_CPtr large =  vka_alloc_object_leaky(&env->vka, seL4_X86_LargePageObject, 0);
    test_assert(large != 0);

    error = seL4_X86_Page_Map(large, pml4, map_addr + (1 << seL4_LargePageBits), seL4_NoRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    seL4_CPtr pt2 = vka_alloc_object_leaky(&env->vka, seL4_X86_PageTableObject, 0);
    test_assert(pt2 != 0);

    /* Because a large page is already mapped at this level, we will not be able to map a page table*/
    error = seL4_X86_PageTable_Map(pt2, pml4, map_addr + (1 << seL4_LargePageBits), seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_DeleteFirst);

    seL4_Word curr_addr = map_addr;
    seL4_Word end_addr = curr_addr + 256 * PAGE_SIZE_4K;
    seL4_X64_PML4_RangeProtect_t unmap_ret;

    while (curr_addr < end_addr) {
        unmap_ret = seL4_X64_PML4_RangeProtect(pml4, curr_addr, end_addr, seL4_AllRights);
        test_error_eq(unmap_ret.error, seL4_NoError);
        test_assert(unmap_ret.num == 32);
        curr_addr = unmap_ret.next_vaddr;
    }

    unmap_ret = seL4_X64_PML4_RangeProtect(pml4, map_addr + (1 << seL4_LargePageBits),
                                                    map_addr + 2 * (1 << seL4_LargePageBits), seL4_AllRights);
    test_error_eq(unmap_ret.error, seL4_NoError);
    test_assert(unmap_ret.num == 1);

    /* Since we unmapped, it we should be able to do a page table map now */
    error = seL4_X86_PageTable_Map(pt2, pml4, map_addr + (1 << seL4_LargePageBits), seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0013, "Test range based unmap function with large and small pages", test_range_unmap_small_large, true)

static int test_reuse_cap(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_Word map_addr_2 = 0x10010000;
    seL4_Word map_addr_3 = 0x10020000;
    int error;

    seL4_CPtr pml4 = vka_alloc_object_leaky(&env->vka, seL4_X64_PML4Object, 0);
    seL4_CPtr pdpt = vka_alloc_object_leaky(&env->vka, seL4_X86_PDPTObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_X86_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_X86_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_X86_4K, 0);

    test_assert(pml4 != 0);
    test_assert(pdpt != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(frame != 0);

    seL4_X86_ASIDPool_Assign(env->asid_pool, pml4);

    error = seL4_X86_PDPT_Map(pdpt, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageDirectory_Map(pd, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageTable_Map(pt, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_X86_Page_Map(frame, pml4, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Try to remap the page at a different address - should fail */
    error = seL4_X86_Page_Map(frame, pml4, map_addr_2, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    /* Unmap it with the single page unmap version*/
    error = seL4_X86_Page_Unmap(frame);
    test_error_eq(error, seL4_NoError);

    /* Try to remap it again at the different address (should work this time) */
    error = seL4_X86_Page_Map(frame, pml4, map_addr_2, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Unmap it with range unmap instead of page unmap */
    seL4_X64_PML4_RangeProtect_t unmap_ret = seL4_X64_PML4_RangeProtect(pml4, map_addr_2, map_addr_2 + PAGE_SIZE_4K, seL4_AllRights);
    test_error_eq(unmap_ret.error, seL4_NoError);

    /* Try to remap the page at a different address with old map - should fail */
    error = seL4_X86_Page_Map(frame, pml4, map_addr_3, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    /* Try to remap the page at a different address */
    error = seL4_X64_PML4_PageMap(pml4, frame, map_addr_3, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}

DEFINE_TEST(VSPACE0014, "Test re-using frame cap for different vaddr after range unmap", test_reuse_cap, true)

static int test_two_frames_same_vaddr(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_Word map_addr_2 = 0x10005000;
    int error;

    seL4_CPtr pml4 = vka_alloc_object_leaky(&env->vka, seL4_X64_PML4Object, 0);
    seL4_CPtr pdpt = vka_alloc_object_leaky(&env->vka, seL4_X86_PDPTObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_X86_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_X86_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_X86_4K, 0);
    seL4_CPtr frame2 = vka_alloc_object_leaky(&env->vka, seL4_X86_4K, 0);

    test_assert(pml4 != 0);
    test_assert(pdpt != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(frame != 0);
    test_assert(frame2 != 0);

    seL4_X86_ASIDPool_Assign(env->asid_pool, pml4);

    error = seL4_X86_PDPT_Map(pdpt, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageDirectory_Map(pd, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageTable_Map(pt, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_X86_Page_Map(frame, pml4, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame2 into the page table to replace frame*/
    error = seL4_X86_Page_Map(frame2, pml4, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Try to map frame 2 to a different address with new map - should fail*/
    error = seL4_X64_PML4_PageMap(pml4, frame2, map_addr_2, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

   /* Try to map frame to a different address with old map - should fail*/
    error = seL4_X86_Page_Map(frame, pml4, map_addr_2, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    /* try map frame at a different vaddr with new map*/
    error = seL4_X64_PML4_PageMap(pml4, frame, map_addr_2, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}

DEFINE_TEST(VSPACE0015, "Test re-using a stale cap as a result of overwriting mapping", test_two_frames_same_vaddr, true)

static int test_remap_del_pt(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_Word map_addr_2 = map_addr + 512 * PAGE_SIZE_4K;
    int error;

    seL4_CPtr pml4 = vka_alloc_object_leaky(&env->vka, seL4_X64_PML4Object, 0);
    seL4_CPtr pdpt = vka_alloc_object_leaky(&env->vka, seL4_X86_PDPTObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_X86_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_X86_PageTableObject, 0);
    seL4_CPtr pt2 = vka_alloc_object_leaky(&env->vka, seL4_X86_PageTableObject, 0);
    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_X86_4K, 0);

    test_assert(pml4 != 0);
    test_assert(pdpt != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(pt2 != 0);
    test_assert(frame != 0);

    seL4_X86_ASIDPool_Assign(env->asid_pool, pml4);

    /* map page table into page directory */
    error = seL4_X86_PDPT_Map(pdpt, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageDirectory_Map(pd, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageTable_Map(pt, pml4, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageTable_Map(pt2, pml4, map_addr_2, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* map frame into the page table */
    error = seL4_X86_Page_Map(frame, pml4, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageTable_Unmap(pt);
    test_error_eq(error, 0);

    error = seL4_X64_PML4_PageMap(pml4, frame, map_addr_2, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, 0);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0016, "Test re-mapping a stale frame cap after unmapping page table", test_remap_del_pt, true)

static int test_remap_diff_vspace(env_t env) {
    seL4_Word map_addr = 0x10000000;
    int error;

    seL4_CPtr pml4_1 = vka_alloc_object_leaky(&env->vka, seL4_X64_PML4Object, 0);
    seL4_CPtr pdpt_1 = vka_alloc_object_leaky(&env->vka, seL4_X86_PDPTObject, 0);
    seL4_CPtr pd_1 = vka_alloc_object_leaky(&env->vka, seL4_X86_PageDirectoryObject, 0);
    seL4_CPtr pt_1 = vka_alloc_object_leaky(&env->vka, seL4_X86_PageTableObject, 0);

    seL4_CPtr pml4_2 = vka_alloc_object_leaky(&env->vka, seL4_X64_PML4Object, 0);
    seL4_CPtr pdpt_2 = vka_alloc_object_leaky(&env->vka, seL4_X86_PDPTObject, 0);
    seL4_CPtr pd_2 = vka_alloc_object_leaky(&env->vka, seL4_X86_PageDirectoryObject, 0);
    seL4_CPtr pt_2 = vka_alloc_object_leaky(&env->vka, seL4_X86_PageTableObject, 0);

    seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_X86_4K, 0);

    test_assert(pml4_1 != 0);
    test_assert(pdpt_1 != 0);
    test_assert(pd_1 != 0);
    test_assert(pt_1 != 0);
    test_assert(pml4_2 != 0);
    test_assert(pdpt_2 != 0);
    test_assert(pd_2 != 0);
    test_assert(pt_2 != 0);
    test_assert(frame != 0);


    seL4_X86_ASIDPool_Assign(env->asid_pool, pml4_1);
    seL4_X86_ASIDPool_Assign(env->asid_pool, pml4_1);

    /* map page table into page directory */
    error = seL4_X86_PDPT_Map(pdpt_1, pml4_1, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageDirectory_Map(pd_1, pml4_1, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageTable_Map(pt_1, pml4_1, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_Page_Map(frame, pml4_1, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

     /* Set up second vspace*/
    error = seL4_X86_PDPT_Map(pdpt_2, pml4_2, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageDirectory_Map(pd_2, pml4_2, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_X86_PageTable_Map(pt_2, pml4_2, map_addr, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Try to map frame into the page table */
    error = seL4_X86_Page_Map(frame, pml4_2, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidCapability);

    /* map frame into the page table */
    error = seL4_X64_PML4_PageMap(pml4_2, frame, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidArgument);

    seL4_X64_PML4_RangeProtect_t unmap_ret = seL4_X64_PML4_RangeProtect(pml4_1, map_addr, map_addr + PAGE_SIZE_4K, seL4_AllRights);
    test_error_eq(unmap_ret.error, 0);

    /* map frame into the page table */
    error = seL4_X86_Page_Map(frame, pml4_2, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_InvalidCapability);

    /* map frame into the page table */
    error = seL4_X64_PML4_PageMap(pml4_2, frame, map_addr, seL4_AllRights, seL4_ARCH_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

     return sel4test_get_result();
}
DEFINE_TEST(VSPACE0017, "Test re-mapping a stale frame cap from a different vspace", test_remap_del_pt, true);
#endif

static int
test_asid_pool_make(env_t env)
{
    vka_t *vka = &env->vka;
    cspacepath_t path;
    seL4_CPtr pool = vka_alloc_untyped_leaky(vka, seL4_PageBits);
    test_assert(pool);
    int ret = vka_cspace_alloc_path(vka, &path);
    ret = seL4_ARCH_ASIDControl_MakePool(env->asid_ctrl, pool, env->cspace_root, path.capPtr, path.capDepth);
    test_eq(ret, seL4_NoError);
    ret = seL4_ARCH_ASIDPool_Assign(path.capPtr, env->page_directory);
    test_eq(ret, seL4_InvalidCapability);
    vka_object_t vspaceroot;
    ret = vka_alloc_vspace_root(vka, &vspaceroot);
    test_assert(!ret);
    ret = seL4_ARCH_ASIDPool_Assign(path.capPtr, vspaceroot.cptr);
    test_eq(ret, seL4_NoError);
    return sel4test_get_result();

}
DEFINE_TEST(VSPACE0002, "Test create ASID pool", test_asid_pool_make, true)

static int
test_alloc_multi_asid_pools(env_t env)
{
    vka_t *vka = &env->vka;
    seL4_CPtr pool;
    cspacepath_t path;
    int i, ret;

    for (i = 0; i < N_ASID_POOLS - 1; i++) {    /* Obviously there is already one ASID allocated */
        pool = vka_alloc_untyped_leaky(vka, seL4_PageBits);
        test_assert(pool);
        ret = vka_cspace_alloc_path(vka, &path);
        ret = seL4_ARCH_ASIDControl_MakePool(env->asid_ctrl, pool, env->cspace_root, path.capPtr, path.capDepth);
        test_eq(ret, seL4_NoError);
    }
    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0003, "Test create multiple ASID pools", test_alloc_multi_asid_pools, true)

static int
test_run_out_asid_pools(env_t env)
{
    vka_t *vka = &env->vka;
    seL4_CPtr pool;
    cspacepath_t path;
    int i, ret;

    for (i = 0; i < N_ASID_POOLS - 1; i++) {
        pool = vka_alloc_untyped_leaky(vka, seL4_PageBits);
        test_assert(pool);
        ret = vka_cspace_alloc_path(vka, &path);
        ret = seL4_ARCH_ASIDControl_MakePool(env->asid_ctrl, pool, env->cspace_root, path.capPtr, path.capDepth);
        test_eq(ret, seL4_NoError);
    }
    /* We do one more pool allocation that is supposed to fail (at this point there shouldn't be any more available) */
    ret = seL4_ARCH_ASIDControl_MakePool(env->asid_ctrl, pool, env->cspace_root, path.capPtr, path.capDepth);
    test_eq(ret, seL4_DeleteFirst);
    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0004, "Test running out of ASID pools", test_run_out_asid_pools, true)

static int
test_overassign_asid_pool(env_t env)
{
    vka_t *vka = &env->vka;
    seL4_CPtr pool;
    cspacepath_t path;
    vka_object_t vspaceroot;
    int i, ret;

    pool = vka_alloc_untyped_leaky(vka, seL4_PageBits);
    test_assert(pool);
    ret = vka_cspace_alloc_path(vka, &path);
    ret = seL4_ARCH_ASIDControl_MakePool(env->asid_ctrl, pool, env->cspace_root, path.capPtr, path.capDepth);
    test_eq(ret, seL4_NoError);
    for (i = 0; i < ASID_POOL_SIZE; i++) {
        ret = vka_alloc_vspace_root(vka, &vspaceroot);
        test_assert(!ret);
        ret = seL4_ARCH_ASIDPool_Assign(path.capPtr, vspaceroot.cptr);
        test_eq(ret, seL4_NoError);
        if (ret != seL4_NoError) {
            break;
        }
    }
    test_eq(i, ASID_POOL_SIZE);
    ret = vka_alloc_vspace_root(vka, &vspaceroot);
    test_assert(!ret);
    ret = seL4_ARCH_ASIDPool_Assign(path.capPtr, vspaceroot.cptr);
    test_eq(ret, seL4_DeleteFirst);
    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0005, "Test overassigning ASID pool", test_overassign_asid_pool, true)

static char
incr_mem(seL4_Word tag)
{
    unsigned int *test = (void *)0x10000000;

    *test = tag;
    return *test;
}

static int test_create_asid_pools_and_touch(env_t env)
{
    vka_t *vka = &env->vka;
    seL4_CPtr pool;
    cspacepath_t poolCap;
    helper_thread_t t;
    int i, ret;

    for (i = 0; i < N_ASID_POOLS - 1; i++) {
        pool = vka_alloc_untyped_leaky(vka, seL4_PageBits);
        test_assert(pool);
        ret = vka_cspace_alloc_path(vka, &poolCap);
        ret = seL4_ARCH_ASIDControl_MakePool(env->asid_ctrl, pool, env->cspace_root, poolCap.capPtr, poolCap.capDepth);
        test_eq(ret, seL4_NoError);

        create_helper_process_custom_asid(env, &t, poolCap.capPtr);
        start_helper(env, &t, (helper_fn_t) incr_mem, i, 0, 0, 0);
        ret = wait_for_helper(&t);
        test_eq(ret, i);
        cleanup_helper(env, &t);
    }
    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0006, "Test touching all available ASID pools", test_create_asid_pools_and_touch, true)

#ifdef CONFIG_ARCH_IA32
static int
test_dirty_accessed_bits(env_t env)
{
    seL4_CPtr frame;
    int err;
    seL4_X86_PageDirectory_GetStatusBits_t status;

    void *vaddr;
    reservation_t reservation;

    reservation = vspace_reserve_range(&env->vspace,
                                       PAGE_SIZE_4K, seL4_AllRights, 1, &vaddr);
    test_assert(reservation.res);

    /* Create a frame */
    frame = vka_alloc_frame_leaky(&env->vka, PAGE_BITS_4K);
    test_assert(frame != seL4_CapNull);

    /* Map it in */
    err = vspace_map_pages_at_vaddr(&env->vspace, &frame, NULL, vaddr, 1, seL4_PageBits, reservation);
    test_assert(!err);

    /* Check the status flags */
    status = seL4_X86_PageDirectory_GetStatusBits(vspace_get_root(&env->vspace), (seL4_Word)vaddr);
    test_assert(!status.error);
    test_assert(!status.accessed);
    test_assert(!status.dirty);
    /* try and prevent prefetcher */
    rdtsc_cpuid();

    /* perform a read and check status flags */
    asm volatile("" :: "r"(*(uint32_t *)vaddr) : "memory");
    status = seL4_X86_PageDirectory_GetStatusBits(vspace_get_root(&env->vspace), (seL4_Word)vaddr);
    test_assert(!status.error);
    test_assert(status.accessed);
    test_assert(!status.dirty);
    /* try and prevent prefetcher */
    rdtsc_cpuid();

    /* perform a write and check status flags */
    *(uint32_t *)vaddr = 42;
    asm volatile("" ::: "memory");
    status = seL4_X86_PageDirectory_GetStatusBits(vspace_get_root(&env->vspace), (seL4_Word)vaddr);
    test_assert(!status.error);
    test_assert(status.accessed);
    test_assert(status.dirty);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0010, "Test dirty and accessed bits on mappings", test_dirty_accessed_bits, true)
#endif