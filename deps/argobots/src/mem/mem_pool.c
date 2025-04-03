/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"
#include <stddef.h>

static inline ABTI_mem_pool_page *
mem_pool_lifo_elem_to_page(ABTI_sync_lifo_element *lifo_elem)
{
    return (ABTI_mem_pool_page *)(((char *)lifo_elem) -
                                  offsetof(ABTI_mem_pool_page, lifo_elem));
}

static inline ABTI_mem_pool_header *
mem_pool_lifo_elem_to_header(ABTI_sync_lifo_element *lifo_elem)
{
    return (
        ABTI_mem_pool_header *)(((char *)lifo_elem) -
                                (offsetof(ABTI_mem_pool_header, bucket_info) +
                                 offsetof(ABTI_mem_pool_header_bucket_info,
                                          lifo_elem)));
}

static ABTU_ret_err int protect_memory(void *addr, size_t size,
                                       size_t page_size, ABT_bool protect,
                                       ABT_bool adjust_size)
{
    /* Align addr. */
    void *mprotect_addr = ABTU_roundup_ptr(addr, page_size);
    if (adjust_size) {
        size -= ((uintptr_t)mprotect_addr) - ((uintptr_t)addr);
    }
    return ABTU_mprotect(mprotect_addr, size, protect);
}

static void
mem_pool_return_partial_bucket(ABTI_mem_pool_global_pool *p_global_pool,
                               ABTI_mem_pool_header *bucket)
{
    int i;
    const int num_headers_per_bucket = p_global_pool->num_headers_per_bucket;
    /* Return headers in the last bucket to partial_bucket. */
    ABTD_spinlock_acquire(&p_global_pool->partial_bucket_lock);
    if (!p_global_pool->partial_bucket) {
        p_global_pool->partial_bucket = bucket;
    } else {
        int num_headers_in_partial_bucket =
            p_global_pool->partial_bucket->bucket_info.num_headers;
        int num_headers_in_bucket = bucket->bucket_info.num_headers;
        if (num_headers_in_partial_bucket + num_headers_in_bucket <
            num_headers_per_bucket) {
            /* Connect partial_bucket + bucket. Still not enough to make
             * a complete bucket. */
            ABTI_mem_pool_header *partial_bucket_tail =
                p_global_pool->partial_bucket;
            for (i = 1; i < num_headers_in_partial_bucket; i++) {
                partial_bucket_tail = partial_bucket_tail->p_next;
            }
            partial_bucket_tail->p_next = bucket;
            p_global_pool->partial_bucket->bucket_info.num_headers =
                num_headers_in_partial_bucket + num_headers_in_bucket;
        } else {
            /* partial_bucket + bucket can make a complete bucket. */
            ABTI_mem_pool_header *partial_bucket_header =
                p_global_pool->partial_bucket;
            for (i = 1; i < num_headers_per_bucket - num_headers_in_bucket;
                 i++) {
                partial_bucket_header = partial_bucket_header->p_next;
            }
            ABTI_mem_pool_header *new_partial_bucket = NULL;
            if (num_headers_in_partial_bucket + num_headers_in_bucket !=
                num_headers_per_bucket) {
                new_partial_bucket = partial_bucket_header->p_next;
                new_partial_bucket->bucket_info.num_headers =
                    num_headers_per_bucket -
                    (num_headers_in_partial_bucket + num_headers_in_bucket);
            }
            partial_bucket_header->p_next = bucket;
            ABTI_mem_pool_return_bucket(p_global_pool,
                                        p_global_pool->partial_bucket);
            p_global_pool->partial_bucket = new_partial_bucket;
        }
    }
    ABTD_spinlock_release(&p_global_pool->partial_bucket_lock);
}

void ABTI_mem_pool_init_global_pool(
    ABTI_mem_pool_global_pool *p_global_pool, size_t num_headers_per_bucket,
    size_t header_size, size_t header_offset, size_t page_size,
    const ABTU_MEM_LARGEPAGE_TYPE *lp_type_requests,
    uint32_t num_lp_type_requests, size_t alignment_hint,
    ABTI_mem_pool_global_pool_mprotect_config *p_mprotect_config)
{
    p_global_pool->num_headers_per_bucket = num_headers_per_bucket;
    ABTI_ASSERT(header_offset + sizeof(ABTI_mem_pool_header) <= header_size);
    p_global_pool->header_size = header_size;
    p_global_pool->header_offset = header_offset;
    p_global_pool->page_size = page_size;
    if (p_mprotect_config) {
        memcpy(&p_global_pool->mprotect_config, p_mprotect_config,
               sizeof(ABTI_mem_pool_global_pool_mprotect_config));
    } else {
        p_global_pool->mprotect_config.enabled = ABT_FALSE;
    }

    /* Note that lp_type_requests is a constant-sized array */
    ABTI_ASSERT(num_lp_type_requests <=
                sizeof(p_global_pool->lp_type_requests) /
                    sizeof(ABTU_MEM_LARGEPAGE_TYPE));
    p_global_pool->num_lp_type_requests = num_lp_type_requests;
    memcpy(p_global_pool->lp_type_requests, lp_type_requests,
           sizeof(ABTU_MEM_LARGEPAGE_TYPE) * num_lp_type_requests);
    /* If mprotect_config is set, we should not use a large page. */
    if (p_global_pool->mprotect_config.enabled) {
        uint32_t i, idx = 0;
        for (i = 0; i < num_lp_type_requests; i++) {
            if (p_global_pool->lp_type_requests[i] !=
                ABTU_MEM_LARGEPAGE_MMAP_HUGEPAGE) {
                p_global_pool->lp_type_requests[idx++] =
                    p_global_pool->lp_type_requests[i];
            }
        }
        if (idx == 0) {
            /* Use a fallback allocation type. */
            p_global_pool->lp_type_requests[0] = ABTU_MEM_LARGEPAGE_MALLOC;
            p_global_pool->num_lp_type_requests = 1;
        } else {
            p_global_pool->num_lp_type_requests = idx;
        }
    }
    p_global_pool->alignment_hint = alignment_hint;

    ABTI_sync_lifo_init(&p_global_pool->mem_page_lifo);
    ABTD_atomic_relaxed_store_ptr(&p_global_pool->p_mem_page_empty, NULL);
    ABTI_sync_lifo_init(&p_global_pool->bucket_lifo);
    ABTD_spinlock_clear(&p_global_pool->partial_bucket_lock);
    p_global_pool->partial_bucket = NULL;
}

void ABTI_mem_pool_destroy_global_pool(ABTI_mem_pool_global_pool *p_global_pool)
{
    /* All local pools must be released in advance.
     * Because all headers are from memory pages, each individual header does
     * not need to be freed. */
    ABTI_mem_pool_page *p_page;
    ABTI_sync_lifo_element *p_page_lifo_elem;
    while ((p_page_lifo_elem =
                ABTI_sync_lifo_pop_unsafe(&p_global_pool->mem_page_lifo))) {
        p_page = mem_pool_lifo_elem_to_page(p_page_lifo_elem);
        if (p_global_pool->mprotect_config.enabled) {
            /* Undo mprotect() */
            int abt_errno =
                protect_memory(p_page->mem, p_page->page_size,
                               p_global_pool->mprotect_config.alignment,
                               ABT_FALSE, ABT_TRUE);
            /* This should not fail since the allocated region is not newly
             * split by this operation. */
            ABTI_ASSERT(abt_errno == ABT_SUCCESS);
        }
        ABTU_free_largepage(p_page->mem, p_page->page_size, p_page->lp_type);
    }
    p_page = (ABTI_mem_pool_page *)ABTD_atomic_relaxed_load_ptr(
        &p_global_pool->p_mem_page_empty);
    while (p_page) {
        ABTI_mem_pool_page *p_next = p_page->p_next_empty_page;
        if (p_global_pool->mprotect_config.enabled) {
            /* Undo mprotect() */
            int abt_errno =
                protect_memory(p_page->mem, p_page->page_size,
                               p_global_pool->mprotect_config.alignment,
                               ABT_FALSE, ABT_TRUE);
            /* This should not fail since the allocated region is not newly
             * split by this operation. */
            ABTI_ASSERT(abt_errno == ABT_SUCCESS);
        }
        ABTU_free_largepage(p_page->mem, p_page->page_size, p_page->lp_type);
        p_page = p_next;
    }
    ABTI_sync_lifo_destroy(&p_global_pool->bucket_lifo);
    ABTI_sync_lifo_destroy(&p_global_pool->mem_page_lifo);
}

ABTU_ret_err int
ABTI_mem_pool_init_local_pool(ABTI_mem_pool_local_pool *p_local_pool,
                              ABTI_mem_pool_global_pool *p_global_pool)
{
    p_local_pool->p_global_pool = p_global_pool;
    p_local_pool->num_headers_per_bucket =
        p_global_pool->num_headers_per_bucket;
    /* There must be always at least one header in the local pool.
     * Let's take one bucket. */
    int abt_errno =
        ABTI_mem_pool_take_bucket(p_global_pool, &p_local_pool->buckets[0]);
    ABTI_CHECK_ERROR(abt_errno);
    p_local_pool->bucket_index = 0;
    return ABT_SUCCESS;
}

void ABTI_mem_pool_destroy_local_pool(ABTI_mem_pool_local_pool *p_local_pool)
{
    /* Return the remaining buckets to the global pool. */
    int bucket_index = p_local_pool->bucket_index;
    int i;
    for (i = 0; i < bucket_index; i++) {
        ABTI_mem_pool_return_bucket(p_local_pool->p_global_pool,
                                    p_local_pool->buckets[i]);
    }
    const size_t num_headers_per_bucket = p_local_pool->num_headers_per_bucket;
    ABTI_mem_pool_header *cur_bucket = p_local_pool->buckets[bucket_index];
    if (cur_bucket->bucket_info.num_headers == num_headers_per_bucket) {
        /* The last bucket is also full. Return the last bucket as well. */
        ABTI_mem_pool_return_bucket(p_local_pool->p_global_pool,
                                    p_local_pool->buckets[bucket_index]);
    } else {
        mem_pool_return_partial_bucket(p_local_pool->p_global_pool, cur_bucket);
    }
}

ABTU_ret_err int
ABTI_mem_pool_take_bucket(ABTI_mem_pool_global_pool *p_global_pool,
                          ABTI_mem_pool_header **p_bucket)
{
    /* Try to get a bucket. */
    ABTI_sync_lifo_element *p_popped_bucket_lifo_elem =
        ABTI_sync_lifo_pop(&p_global_pool->bucket_lifo);
    const int num_headers_per_bucket = p_global_pool->num_headers_per_bucket;
    if (ABTU_likely(p_popped_bucket_lifo_elem)) {
        /* Use this bucket. */
        ABTI_mem_pool_header *popped_bucket =
            mem_pool_lifo_elem_to_header(p_popped_bucket_lifo_elem);
        popped_bucket->bucket_info.num_headers = num_headers_per_bucket;
        *p_bucket = popped_bucket;
        return ABT_SUCCESS;
    } else {
        /* Allocate headers by myself */
        const size_t header_size = p_global_pool->header_size;
        int num_headers = 0, i;
        ABTI_mem_pool_header *p_head = NULL;
        while (1) {
            ABTI_mem_pool_page *p_page;
            ABTI_sync_lifo_element *p_page_lifo_elem;
            /* Before really allocating memory, check if a page has unused
             * memory. */
            if ((p_page_lifo_elem =
                     ABTI_sync_lifo_pop(&p_global_pool->mem_page_lifo))) {
                /* Use a page popped from mem_page_lifo */
                p_page = mem_pool_lifo_elem_to_page(p_page_lifo_elem);
            } else {
                /* Let's allocate memory by myself */
                const size_t page_size = p_global_pool->page_size;
                ABTU_MEM_LARGEPAGE_TYPE lp_type;
                void *p_alloc_mem;
                int abt_errno =
                    ABTU_alloc_largepage(page_size,
                                         p_global_pool->alignment_hint,
                                         p_global_pool->lp_type_requests,
                                         p_global_pool->num_lp_type_requests,
                                         &lp_type, &p_alloc_mem);
                if (ABTI_IS_ERROR_CHECK_ENABLED && abt_errno != ABT_SUCCESS) {
                    /* It fails to take a large page. Let's return. */
                    if (num_headers != 0) {
                        /* p_head has some elements, so let's return them. */
                        p_head->bucket_info.num_headers = num_headers;
                        mem_pool_return_partial_bucket(p_global_pool, p_head);
                    }
                    return abt_errno;
                }
                p_page =
                    (ABTI_mem_pool_page *)(((char *)p_alloc_mem) + page_size -
                                           sizeof(ABTI_mem_pool_page));
                p_page->mem = p_alloc_mem;
                p_page->page_size = page_size;
                p_page->lp_type = lp_type;
                p_page->p_mem_extra = p_alloc_mem;
                p_page->mem_extra_size = page_size - sizeof(ABTI_mem_pool_page);
            }
            /* Take some memory left in this page. */
            int num_provided = p_page->mem_extra_size / header_size;
            int num_required = num_headers_per_bucket - num_headers;
            if (num_required < num_provided)
                num_provided = num_required;
            ABTI_ASSERT(num_provided != 0);

            void *p_mem_extra = p_page->p_mem_extra;
            p_page->p_mem_extra =
                (void *)(((char *)p_mem_extra) + header_size * num_provided);
            p_page->mem_extra_size -= header_size * num_provided;
            /* We've already gotten necessary p_mem_extra from this page. Let's
             * return it. */
            if (p_page->mem_extra_size >= header_size) {
                /* This page still has some extra memory. Someone will use it in
                 * the future. */
                ABTI_sync_lifo_push(&p_global_pool->mem_page_lifo,
                                    &p_page->lifo_elem);
            } else {
                /* No extra memory is left in this page. Let's push it to a list
                 * of empty pages.  Since mem_page_empty_lifo is push-only and
                 * thus there's no ABA problem, use a simpler lock-free LIFO
                 * algorithm. */
                void *p_cur_mem_page;
                do {
                    p_cur_mem_page = ABTD_atomic_acquire_load_ptr(
                        &p_global_pool->p_mem_page_empty);
                    p_page->p_next_empty_page =
                        (ABTI_mem_pool_page *)p_cur_mem_page;
                } while (!ABTD_atomic_bool_cas_weak_ptr(&p_global_pool
                                                             ->p_mem_page_empty,
                                                        p_cur_mem_page,
                                                        p_page));
            }

            size_t header_offset = p_global_pool->header_offset;
            ABTI_mem_pool_header *p_local_tail =
                (ABTI_mem_pool_header *)(((char *)p_mem_extra) + header_offset);
            p_local_tail->p_next = p_head;
            ABTI_mem_pool_header *p_prev = p_local_tail;
            if (!p_global_pool->mprotect_config.enabled) {
                /* Fast path. */
                for (i = 1; i < num_provided; i++) {
                    ABTI_mem_pool_header *p_cur =
                        (ABTI_mem_pool_header *)(((char *)p_prev) +
                                                 header_size);
                    p_cur->p_next = p_prev;
                    p_prev = p_cur;
                }
            } else {
                /* Slow path.  Use mprotect(). */
                const ABT_bool check_error =
                    p_global_pool->mprotect_config.check_error;
                const size_t protect_offset =
                    p_global_pool->mprotect_config.offset;
                const size_t protect_page_size =
                    p_global_pool->mprotect_config.page_size;
                const size_t protect_alignment =
                    p_global_pool->mprotect_config.alignment;
                int abt_errno;
                abt_errno =
                    protect_memory((void *)(((char *)p_prev) - header_offset +
                                            protect_offset),
                                   protect_page_size, protect_alignment,
                                   ABT_TRUE, ABT_FALSE);
                if (check_error) {
                    ABTI_ASSERT(abt_errno == ABT_SUCCESS);
                }
                for (i = 1; i < num_provided; i++) {
                    ABTI_mem_pool_header *p_cur =
                        (ABTI_mem_pool_header *)(((char *)p_prev) +
                                                 header_size);
                    p_cur->p_next = p_prev;
                    p_prev = p_cur;
                    abt_errno =
                        protect_memory((void *)(((char *)p_prev) -
                                                header_offset + protect_offset),
                                       protect_page_size, protect_alignment,
                                       ABT_TRUE, ABT_FALSE);
                    if (check_error) {
                        ABTI_ASSERT(abt_errno == ABT_SUCCESS);
                    }
                }
            }
            p_head = p_prev;
            num_headers += num_provided;
            if (num_headers == num_headers_per_bucket) {
                p_head->bucket_info.num_headers = num_headers_per_bucket;
                *p_bucket = p_head;
                return ABT_SUCCESS;
            }
        }
    }
}

void ABTI_mem_pool_return_bucket(ABTI_mem_pool_global_pool *p_global_pool,
                                 ABTI_mem_pool_header *bucket)
{
    /* Simply return that bucket to the pool */
    ABTI_sync_lifo_push(&p_global_pool->bucket_lifo,
                        &bucket->bucket_info.lifo_elem);
}
