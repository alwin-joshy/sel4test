/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <autoconf.h>
#include <sel4/sel4.h>
#include <vka/capops.h>

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

#define NPAGE 128
#define NPAGE_LARGE 256

static int test_range_unmap_small(env_t env) {
    seL4_Word map_addr = 0x10000000;
    seL4_CPtr frames[NPAGE];
    int error;

    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
    seL4_CPtr pt2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);

    for (int i = 0; i < NPAGE; i++) {
        frames[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
        test_assert(frames[i] != 0);
        // ZF_LOGE("%lu", frames[i]);
        if (i > 0) {
            // test_assert(frames[i] == frames[i-1] + 1);
        }
    }

    // seL4_CPtr start;
    // vka_cspace_alloc_contigious(&env->vka, NPAGE, &start);
    // ZF_LOGE("%d", start);
    // for (int i = 0; i < NPAGE; i++) {
    //     cspacepath_t from, to; 
    //     vka_cspace_make_path(&env->vka, frames[i], &from);
    //     vka_cspace_make_path(&env->vka, start + i, &to);
    //     vka_cnode_move(&to, &from);
    //     // vka_cnode_delete(&from);
    //     // vka_cspace_free(&env->vka, frames[i]);
    // }


    /* Under an Arm Hyp configuration where the CPU only supports 40bit physical addressing, we
     * only have 3 level page tables and no PGD.
     */
    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);
    test_assert(pt2 != 0);

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
    error = seL4_ARM_PageTable_Map(pt2, vspace, map_addr + 512 * PAGE_SIZE_4K, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    for (int i = 0; i < NPAGE; i++) {
        error = seL4_ARM_Page_Map(frames[i], vspace, map_addr + i * PAGE_SIZE_4K, seL4_AllRights, seL4_ARM_Default_VMAttributes);
        test_error_eq(error, 0);
    }

    for (int i = 0; i < NPAGE/32; i += 1) {
        seL4_CapSet set;
        set.c0 = frames[32*i];
        set.c1 = frames[32*i + 1];
        set.c2 = frames[32*i + 2];
        set.c3 = frames[32*i + 3];
        set.c4 = frames[32*i + 4];
        set.c5 = frames[32*i + 5];
        set.c6 = frames[32*i + 6];
        set.c7 = frames[32*i + 7];
        set.c8 = frames[32*i + 8];
        set.c9 = frames[32*i + 9];
        set.c10 = frames[32*i + 10];
        set.c11 = frames[32*i + 11];
        set.c12 = frames[32*i + 12];
        set.c13 = frames[32*i + 13];
        set.c14 = frames[32*i + 14];
        set.c15 = frames[32*i + 15];
        set.c16 = frames[32*i + 16];
        set.c17 = frames[32*i + 17];
        set.c18 = frames[32*i + 18];
        set.c19 = frames[32*i + 19];
        set.c20 = frames[32*i + 20];
        set.c21 = frames[32*i + 21];
        set.c22 = frames[32*i + 22];
        set.c23 = frames[32*i + 23];
        set.c24 = frames[32*i + 24];
        set.c25 = frames[32*i + 25];
        set.c26 = frames[32*i + 26];
        set.c27 = frames[32*i + 27];
        set.c28 = frames[32*i + 28];
        set.c29 = frames[32*i + 29];
        set.c30 = frames[32*i + 30];
        set.c31 = frames[32*i + 31];

        // for (int j = 0; j < 32; i++) {
        //     set_caps[i] = frames[32*i + j];
        // }
        set.num = 32;
        printf("hello world\n");


        seL4_ARM_VSpace_Remap_Range_t remap_ret = seL4_ARM_VSpace_Remap_Range(vspace, &set, seL4_NoRights);
        test_error_eq(remap_ret.error, 0);
        test_assert(remap_ret.remapped = 0xffff); 
    }

    // seL4_Word curr_addr = map_addr;
    // while (curr_addr < map_addr + PAGE_SIZE_4K * NPAGE) {
    //     seL4_ARM_VSpace_Range_Remap_t remap_ret = seL4_ARM_VSpace_Range_Remap(vspace, curr_addr, 32, seL4_NoRights);
    //     test_error_eq(remap_ret.error, 0);
    //     test_assert(remap_ret.num == 32);
    //     remap_ret = seL4_ARM_VSpace_Range_Remap(vspace, curr_addr, 32, seL4_AllRights);
    //     test_error_eq(remap_ret.error, 0);
    //     test_assert(remap_ret.num == 32);
    //     curr_addr = remap_ret.end_vaddr;
    // }


    // test_error_eq(error, 0);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0011, "Test range based unmap function with small pages", test_range_unmap_small, true)

// static int test_range_unmap_large(env_t env) {
//     seL4_Word map_addr = 0x10000000;
//     seL4_CPtr frames[NPAGE_LARGE];
//     int error;

//     seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
//     seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
//     seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);

//     for (int i = 0; i < NPAGE_LARGE; i++) {
//         frames[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_LargePageObject, 0);
//         test_assert(frames[i] != 0);
//     }

//     test_assert((seL4_PGDBits == 0) || pgd != 0);
//     test_assert(pud != 0);
//     test_assert(pd != 0);

//     seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
//     seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
// #if seL4_PGDBits > 0
//     /* map pud into page global directory */
//     error = seL4_ARM_PageUpperDirectory_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);
// #endif

//     /* map pd into page upper directory */
//     error = seL4_ARM_PageDirectory_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     for (int i = 0; i < NPAGE_LARGE; i++) {
//         error = seL4_ARM_Page_Map(frames[i], vspace, map_addr + i * (1 << seL4_LargePageBits), seL4_NoRights, seL4_ARM_Default_VMAttributes);
//         test_error_eq(error, 0);
//     }

//     seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
//     test_assert(pt != 0);

//     /* Because a large page is already mapped at this level, we will not be able to map a page table*/
//     error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_DeleteFirst);


//     /* Because a large page is already mapped at this level, we will not be able to map a page table*/
//     error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_DeleteFirst);

//     seL4_Word curr_addr = map_addr;
//     seL4_Word end = map_addr + NPAGE_LARGE * (1 << seL4_LargePageBits);

//     while (curr_addr < end) {
//         seL4_ARM_VSpace_Remap_Range_t unmap_ret = seL4_ARM_VSpace_Remap_Range(vspace, frames[0], 32, seL4_AllRights);
//         test_error_eq(unmap_ret.error, 0);
//         test_assert(unmap_ret.num_remapped == 32);
//     }

//     /* Since we unmapped we should be able to do a page table map now */
//     error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, 0);

//     return sel4test_get_result();
// }
// DEFINE_TEST(VSPACE0012, "Test range based unmap function with large pages", test_range_unmap_large, true)

// static int test_range_unmap_small_large(env_t env) {
//     seL4_Word map_addr = 0x10000000;
//     seL4_CPtr small_frames[256];
//     int error;

//     seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
//     seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
//     seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
//     seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);

//     for (int i = 0; i < 256; i++) {
//         small_frames[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
//         test_assert(small_frames[i] != 0);
//     }

//     test_assert((seL4_PGDBits == 0) || pgd != 0);
//     test_assert(pud != 0);
//     test_assert(pd != 0);
//     test_assert(pt != 0);

//     seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
//     seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);

// #if seL4_PGDBits > 0
//     /* map pud into page global directory */
//     error = seL4_ARM_PageUpperDirectory_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);
// #endif

//     /* map pd into page upper directory */
//     error = seL4_ARM_PageDirectory_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     /* map page table into page directory */
//     error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     for (int i = 0; i < 256; i++) {
//         error = seL4_ARM_Page_Map(small_frames[i], vspace, map_addr + i * PAGE_SIZE_4K, seL4_NoRights, seL4_ARM_Default_VMAttributes);
//         test_error_eq(error, 0);
//     }

//     seL4_CPtr large =  vka_alloc_object_leaky(&env->vka, seL4_ARM_LargePageObject, 0);
//     test_assert(large != 0);
//     error = seL4_ARM_Page_Map(large, vspace, map_addr + (1 << seL4_LargePageBits), seL4_NoRights, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, 0);

//     seL4_CPtr pt2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
//     test_assert(pt2 != 0);

//     /* Because a large page is already mapped at this level, we will not be able to map a page table*/
//     error = seL4_ARM_PageTable_Map(pt2, vspace, map_addr + (1 << seL4_LargePageBits), seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_DeleteFirst);

//     seL4_Word curr_addr = map_addr;
//     seL4_ARM_VSpace_Range_Remap_t unmap_ret;

//     while (curr_addr < map_addr + 256 * PAGE_SIZE_4K) {
//         unmap_ret = seL4_ARM_VSpace_Range_Remap(vspace, curr_addr, 32, seL4_AllRights);
//         test_error_eq(unmap_ret.error, 0);
//         test_assert(unmap_ret.num == 32);
//         curr_addr = unmap_ret.end_vaddr;
//     }

//     unmap_ret = seL4_ARM_VSpace_Range_Remap(vspace, map_addr + 1 * (1 << seL4_LargePageBits), 1, seL4_AllRights);
//     test_error_eq(unmap_ret.error, 0);
//     test_assert(unmap_ret.num == 1);

//     /* Since we unmapped, it we should be able to do a page table map now */
//     error = seL4_ARM_PageTable_Map(pt2, vspace, map_addr + (1 << seL4_LargePageBits), seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, 0);

//     return sel4test_get_result();
// }
// DEFINE_TEST(VSPACE0013, "Test range based unmap function with large and small pages", test_range_unmap_small_large, true)

// static int test_reuse_cap(env_t env) {
//     seL4_Word map_addr = 0x10000000;
//     seL4_Word map_addr_2 = 0x10005000;
//     seL4_Word map_addr_3 = 0x1000A000;
//     int error;

//     seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
//     seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
//     seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
//     seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
//     seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
//     /* Under an Arm Hyp configuration where the CPU only supports 40bit physical addressing, we
//      * only have 3 level page tables and no PGD.
//      */
//     test_assert((seL4_PGDBits == 0) || pgd != 0);
//     test_assert(pud != 0);
//     test_assert(pd != 0);
//     test_assert(pt != 0);
//     test_assert(frame != 0);


//     seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
//     seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
// #if seL4_PGDBits > 0
//     /* map pud into page global directory */
//     error = seL4_ARM_PageUpperDirectory_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);
// #endif

//     /* map pd into page upper directory */
//     error = seL4_ARM_PageDirectory_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     /* map page table into page directory */
//     error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     /* map frame into the page table */
//     error = seL4_ARM_Page_Map(frame, vspace, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     /* Try to remap the page at a different address - should fail */
//     error = seL4_ARM_Page_Map(frame, vspace, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_InvalidArgument);

//     /* Unmap it with the single page unmap version*/
//     error = seL4_ARM_Page_Unmap(frame);
//     test_error_eq(error, seL4_NoError);

//     /* Try to remap it again at the different address (should work this time) */
//     error = seL4_ARM_Page_Map(frame, vspace, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     /* Unmap it with range unmap instead of page unmap */
//     seL4_ARM_VSpace_Range_Remap_t unmap_ret = seL4_ARM_VSpace_Range_Remap(vspace, map_addr_2, 1, seL4_AllRights);
//     test_error_eq(unmap_ret.error, 0);
//     test_assert(unmap_ret.num == 1);

//     /* Try to remap the page at a different address */
//     error = seL4_ARM_VSpace_Page_Map(vspace, frame, map_addr_3, seL4_AllRights, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     return sel4test_get_result();
// }

// DEFINE_TEST(VSPACE0014, "Test re-using frame cap for different vaddr after range unmap", test_reuse_cap, true)

// static int test_two_frames_same_vaddr(env_t env) {
//     seL4_Word map_addr = 0x10000000;
//     seL4_Word map_addr_2 = 0x10005000;
//     int error;

//     seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
//     seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
//     seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
//     seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
//     seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
//     seL4_CPtr frame2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
//     /* Under an Arm Hyp configuration where the CPU only supports 40bit physical addressing, we
//      * only have 3 level page tables and no PGD.
//      */
//     test_assert((seL4_PGDBits == 0) || pgd != 0);
//     test_assert(pud != 0);
//     test_assert(pd != 0);
//     test_assert(pt != 0);
//     test_assert(frame != 0);
//     test_assert(frame2 != 0);


//     seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
//     seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
// #if seL4_PGDBits > 0
//     /* map pud into page global directory */
//     error = seL4_ARM_PageUpperDirectory_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);
// #endif

//     /* map pd into page upper directory */
//     error = seL4_ARM_PageDirectory_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     /* map page table into page directory */
//     error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     /* map frame into the page table */
//     error = seL4_ARM_Page_Map(frame, vspace, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     /* map frame2 into the page table to replace frame*/
//     error = seL4_ARM_Page_Map(frame2, vspace, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     /* Try to map frame 2 to a different address - should fail*/
//     error = seL4_ARM_VSpace_Page_Map(vspace, frame2, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_InvalidArgument);

//     /* try map frame at a different vaddr*/
//     error = seL4_ARM_VSpace_Page_Map(vspace, frame, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     return sel4test_get_result();
// }

// DEFINE_TEST(VSPACE0015, "Test re-using frame cap for different vaddr after range unmap", test_two_frames_same_vaddr, true)

// static int test_remap_del_pt(env_t env) {
//     seL4_Word map_addr = 0x10000000;
//     seL4_Word map_addr_2 = map_addr + 1024 * PAGE_SIZE_4K;
//     int error;

//     seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
//     seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
//     seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
//     seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
//     seL4_CPtr pt2 = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);
//     seL4_CPtr frame = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);

//     test_assert((seL4_PGDBits == 0) || pgd != 0);
//     test_assert(pud != 0);
//     test_assert(pd != 0);
//     test_assert(pt != 0);
//     test_assert(pt2 != 0);
//     test_assert(frame != 0);

//     seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
//     seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
// #if seL4_PGDBits > 0
//     /* map pud into page global directory */
//     error = seL4_ARM_PageUpperDirectory_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);
// #endif

//     /* map pd into page upper directory */
//     error = seL4_ARM_PageDirectory_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     /* map page table into page directory */
//     error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     /* map page table into page directory */
//     error = seL4_ARM_PageTable_Map(pt2, vspace, map_addr_2, seL4_ARM_Default_VMAttributes);
//     ZF_LOGE("%d", error);
//     test_error_eq(error, seL4_NoError);

//     /* map frame into the page table */
//     error = seL4_ARM_Page_Map(frame, vspace, map_addr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, seL4_NoError);

//     error = seL4_ARM_PageTable_Unmap(pt);
//     test_error_eq(error, 0);

//     error = seL4_ARM_VSpace_Page_Map(vspace, frame, map_addr_2, seL4_AllRights, seL4_ARM_Default_VMAttributes);
//     test_error_eq(error, 0);

//     return sel4test_get_result();
// }

// DEFINE_TEST(VSPACE0016, "Test re-mapping a stale frame cap after unmapping page table", test_remap_del_pt, true)
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
