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

#elif defined(CONFIG_ARCH_AARCH64)
static int
test_unmap_after_delete(env_t env)
{
    seL4_Word map_addr = 0x10000000;
    cspacepath_t path;
    int error;

#if seL4_PGDBits > 0
    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
#else
    seL4_CPtr pgd = 0;
#endif
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
#endif /* CONFIG_ARCH_AARCHxx */

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

/* Range mapping tests*/
#ifdef CONFIG_ARCH_AARCH64

static int test_range_map_small_pages(env_t env) {
    seL4_Word map_addr = 0x40000000;
    int error;

#if seL4_PGDBits > 0
    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
#else
    seL4_CPtr pgd = 0;
#endif
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);

    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);

    seL4_CPtr frame_cptrs[32] = {0};
    for (int i = 0; i < 32; i++) {
        frame_cptrs[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
        test_assert(frame_cptrs[i] != 0);
    }

    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
#if seL4_PGDBits > 0
    /* Map pud into the pgd */
    error = seL4_ARM_PageTable_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* Map pd into page upper directory */
    error = seL4_ARM_PageTable_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_ARM_VSpace_Map_Range(vspace, map_addr, seL4_AllRights,
                                      seL4_ARM_Default_VMAttributes, frame_cptrs, 32);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0011, "Range map: small pages", test_range_map_small_pages, true)

static int test_range_map_large_pages(env_t env) {
    seL4_Word map_addr = 0x40000000;
    int error;

#if seL4_PGDBits > 0
    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
#else
    seL4_CPtr pgd = 0;
#endif
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);

    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);
    test_assert(pd != 0);

    seL4_CPtr frame_cptrs[32] = {0};
    for (int i = 0; i < 32; i++) {
        frame_cptrs[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_LargePageObject, 0);
        test_assert(frame_cptrs[i] != 0);
    }

    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
#if seL4_PGDBits > 0
    /* Map pud into the pgd */
    error = seL4_ARM_PageTable_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* Map pd into page upper directory */
    error = seL4_ARM_PageTable_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_ARM_VSpace_Map_Range(vspace, map_addr, seL4_AllRights,
                                      seL4_ARM_Default_VMAttributes, frame_cptrs, 32);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0012, "Range map: large pages", test_range_map_large_pages, true)

static int test_range_map_huge_pages(env_t env) {
    seL4_Word map_addr = 0x40000000;
    int error;

#if seL4_PGDBits > 0
    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
#else
    seL4_CPtr pgd = 0;
#endif
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);

    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);

    seL4_CPtr frame_cptrs[32] = {0};
    int num_allocated = 0;
    for (int i = 0; i < 1; i++) {
        frame_cptrs[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_HugePageObject, 0);

        /* Huge pages are pretty big and we might not be able to allocate 32 of them, so we just
         * do as many as we can */
        if (frame_cptrs[i] == 0) {
            break;
        }
        num_allocated++;
    }

    /* If we couldn't allocate any huge pages, we just skip this test and say it was a pass */
    if (num_allocated == 0) {
        return sel4test_get_result();
    }

    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
#if seL4_PGDBits > 0
    /* Map pud into the pgd */
    error = seL4_ARM_PageTable_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    error = seL4_ARM_VSpace_Map_Range(vspace, map_addr, seL4_AllRights,
                                      seL4_ARM_Default_VMAttributes, frame_cptrs, num_allocated);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0013, "Range map: huge pages", test_range_map_huge_pages, true)

static int test_range_map_mixed_pages(env_t env) {
    seL4_Word map_addr = 0x40000000;
    int error;

#if seL4_PGDBits > 0
    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
#else
    seL4_CPtr pgd = 0;
#endif
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);

    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);

    // TODO: What to do if a huge page can't be allocated?
    seL4_CPtr frame_cptrs[3] = {0};
    frame_cptrs[0] = vka_alloc_object_leaky(&env->vka, seL4_ARM_HugePageObject, 0);
    test_assert(frame_cptrs[0] != 0);
    frame_cptrs[1] = vka_alloc_object_leaky(&env->vka, seL4_ARM_LargePageObject, 0);
    test_assert(frame_cptrs[1] != 0);
    frame_cptrs[2] = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
    test_assert(frame_cptrs[2] != 0);

    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
#if seL4_PGDBits > 0
    /* Map pud into the pgd */
    error = seL4_ARM_PageTable_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* Map pd into page upper directory */
    error = seL4_ARM_PageTable_Map(pd, vspace, map_addr + PAGE_SIZE_1G, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, vspace, map_addr + PAGE_SIZE_1G + PAGE_SIZE_2M, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_ARM_VSpace_Map_Range(vspace, map_addr, seL4_AllRights,
                                      seL4_ARM_Default_VMAttributes, frame_cptrs, 3);
    test_error_eq(error, seL4_NoError);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0014, "Range map: mixed pages", test_range_map_mixed_pages, true)

static int test_range_map_too_many(env_t env) {
    seL4_Word map_addr = 0x40000000;
    int error;

    #if seL4_PGDBits > 0
    seL4_CPtr vspace = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
#else
    seL4_CPtr vspace = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
#endif

    test_assert(vspace != 0);

    seL4_CPtr frame_cptrs[64] = {0};
    for (int i = 0; i < 64; i++) {
        frame_cptrs[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
        test_assert(frame_cptrs[i] != 0);
    }

    error = seL4_ARM_VSpace_Map_Range(vspace, map_addr, seL4_AllRights,
                                      seL4_ARM_Default_VMAttributes, frame_cptrs, 64);
    test_error_eq(error, seL4_InvalidArgument);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0015, "Range map: too many caps", test_range_map_too_many, true)

static int test_range_map_double(env_t env) {
    seL4_Word map_addr = 0x40000000;
    int error;

#if seL4_PGDBits > 0
    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
#else
    seL4_CPtr pgd = 0;
#endif
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);

    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);

    seL4_CPtr frame_cptrs[32] = {0};
    for (int i = 0; i < 32; i++) {
        frame_cptrs[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
        test_assert(frame_cptrs[i] != 0);
    }

    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
#if seL4_PGDBits > 0
    /* Map pud into the pgd */
    error = seL4_ARM_PageTable_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* Map pd into page upper directory */
    error = seL4_ARM_PageTable_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_ARM_VSpace_Map_Range(vspace, map_addr, seL4_AllRights,
                                      seL4_ARM_Default_VMAttributes, frame_cptrs, 32);
    test_error_eq(error, seL4_NoError);

    error = seL4_ARM_VSpace_Map_Range(vspace, map_addr + 32 * PAGE_SIZE_4K, seL4_AllRights,
                                      seL4_ARM_Default_VMAttributes, frame_cptrs, 32);
    test_error_eq(error, seL4_InvalidArgument);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0016, "Range map: Double map", test_range_map_double, true)

static int test_range_map_missing(env_t env) {
    seL4_Word map_addr = 0x40000000;
    int error;

#if seL4_PGDBits > 0
    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
#else
    seL4_CPtr pgd = 0;
#endif
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);

    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);

    seL4_CPtr frame_cptrs[32] = {0};
    for (int i = 0; i < 32; i++) {
        frame_cptrs[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
        test_assert(frame_cptrs[i] != 0);
    }

    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
#if seL4_PGDBits > 0
    /* Map pud into the pgd */
    error = seL4_ARM_PageTable_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* Map pd into page upper directory */
    error = seL4_ARM_PageTable_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_ARM_VSpace_Map_Range(vspace, map_addr, seL4_AllRights,
                                      seL4_ARM_Default_VMAttributes, frame_cptrs, 32);
    test_error_eq(error, seL4_FailedLookup);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0017, "Range map: Missing structures", test_range_map_missing, true)

static int test_range_map_overmap(env_t env) {
    seL4_Word map_addr = 0x40000000;
    int error;

#if seL4_PGDBits > 0
    seL4_CPtr pgd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageGlobalDirectoryObject, 0);
#else
    seL4_CPtr pgd = 0;
#endif
    seL4_CPtr pud = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageUpperDirectoryObject, 0);
    seL4_CPtr pd = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageDirectoryObject, 0);
    seL4_CPtr pt = vka_alloc_object_leaky(&env->vka, seL4_ARM_PageTableObject, 0);

    test_assert((seL4_PGDBits == 0) || pgd != 0);
    test_assert(pud != 0);
    test_assert(pd != 0);
    test_assert(pt != 0);

    seL4_CPtr frame_cptrs[32] = {0};
    for (int i = 0; i < 32; i++) {
        frame_cptrs[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
        test_assert(frame_cptrs[i] != 0);
    }

    seL4_CPtr frame_cptrs2[32] = {0};
    for (int i = 0; i < 32; i++) {
        frame_cptrs2[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
        test_assert(frame_cptrs2[i] != 0);
    }

    seL4_CPtr vspace = (seL4_PGDBits == 0) ? pud : pgd;
    seL4_ARM_ASIDPool_Assign(env->asid_pool, vspace);
#if seL4_PGDBits > 0
    /* Map pud into the pgd */
    error = seL4_ARM_PageTable_Map(pud, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);
#endif

    /* Map pd into page upper directory */
    error = seL4_ARM_PageTable_Map(pd, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    /* Map page table into page directory */
    error = seL4_ARM_PageTable_Map(pt, vspace, map_addr, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, seL4_NoError);

    error = seL4_ARM_VSpace_Map_Range(vspace, map_addr, seL4_AllRights,
                                      seL4_ARM_Default_VMAttributes, frame_cptrs, 32);
    test_error_eq(error, seL4_NoError);

    error = seL4_ARM_VSpace_Map_Range(vspace, map_addr, seL4_AllRights,
                                  seL4_ARM_Default_VMAttributes, frame_cptrs2, 32);
    test_error_eq(error, seL4_DeleteFirst);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0018, "Range map: Overmapping", test_range_map_overmap, true)

static int test_range_map_read_write(env_t env)
{
    int error;

    /* Grab some free vspace big enough to hold a couple of large pages. */
    seL4_Word vstart;
    reservation_t reserve = vspace_reserve_range(&env->vspace, PAGE_SIZE_2M * 4,
                                                 seL4_AllRights, 1, (void **) &vstart);
    vstart = ALIGN_UP(vstart, PAGE_SIZE_2M * 2);

    seL4_CPtr frame_cptrs[32] = {0};
    for (int i = 0; i < 32; i++) {
      frame_cptrs[i] = vka_alloc_object_leaky(&env->vka, seL4_ARM_SmallPageObject, 0);
      test_assert(frame_cptrs[i]  != 0);
    }

    /* Also create a pagetable to map the pages into. */
    seL4_CPtr pt = vka_alloc_page_table_leaky(&env->vka);

    error = seL4_ARM_PageTable_Map(pt, env->page_directory,
                                   vstart, seL4_ARM_Default_VMAttributes);
    test_error_eq(error, 0);


    error = seL4_ARM_VSpace_Map_Range(env->page_directory, vstart, seL4_AllRights,
                                      seL4_ARM_Default_VMAttributes, frame_cptrs, 32);
    test_error_eq(error, 0);

    /* Fill the pages with some data */
    for (char *addr = (char *) vstart; addr < (char *) vstart + 32 * PAGE_SIZE_4K; addr++) {
        *addr = 'a';
    }

    /* Read it back and make sure it's the same */
    for (char *addr = (char *) vstart; addr < (char *) vstart + 32 * PAGE_SIZE_4K; addr++) {
        test_assert(*addr = 'a');
    }

    vspace_free_reservation(&env->vspace, reserve);

    return sel4test_get_result();
}
DEFINE_TEST(VSPACE0019, "Range map: Read/write", test_range_map_read_write, true)

#endif /* CONFIG_ARCH_AARCH64 */
