/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

static inline ABTU_hashtable_element *
get_element(const ABTU_hashtable *p_hashtable, size_t entry_index)
{
    char *p_buffer = (char *)p_hashtable;
    const size_t data_size = p_hashtable->data_size;
    const size_t hashtable_size = sizeof(ABTU_hashtable);
    const size_t hashtable_element_size =
        ABTU_roundup_size(sizeof(ABTU_hashtable_element) + data_size,
                          ABT_CONFIG_STATIC_CACHELINE_SIZE);
    return (ABTU_hashtable_element *)(p_buffer + hashtable_size +
                                      hashtable_element_size * entry_index);
}

ABTU_ret_err int ABTU_hashtable_create(size_t num_entries, size_t data_size,
                                       ABTU_hashtable **pp_hashtable)
{
    const size_t hashtable_size = sizeof(ABTU_hashtable);
    const size_t hashtable_element_size =
        ABTU_roundup_size(sizeof(ABTU_hashtable_element) + data_size,
                          ABT_CONFIG_STATIC_CACHELINE_SIZE);
    const size_t size = hashtable_size + hashtable_element_size * num_entries;

    char *p_buffer;
    int ret = ABTU_calloc(1, size, (void **)&p_buffer);
    ABTI_CHECK_ERROR(ret);

    ABTU_hashtable *p_hashtable = (ABTU_hashtable *)p_buffer;
    p_buffer += hashtable_size;
    p_hashtable->num_entries = num_entries;
    p_hashtable->data_size = data_size;
    *pp_hashtable = p_hashtable;
    return ABT_SUCCESS;
}

void ABTU_hashtable_free(ABTU_hashtable *p_hashtable)
{
    size_t i;
    for (i = 0; i < p_hashtable->num_entries; i++) {
        ABTU_hashtable_element *p_element = get_element(p_hashtable, i)->p_next;
        while (p_element) {
            ABTU_hashtable_element *p_next = p_element->p_next;
            ABTU_free(p_element);
            p_element = p_next;
        }
    }
    ABTU_free(p_hashtable);
}

void ABTU_hashtable_get(const ABTU_hashtable *p_hashtable, int key, void *data,
                        int *found)
{
    const size_t num_entries = p_hashtable->num_entries;
    const size_t data_size = p_hashtable->data_size;
    const ssize_t entry_index_tmp = ((ssize_t)key) % ((ssize_t)num_entries);
    const size_t entry_index = entry_index_tmp < 0
                                   ? (ssize_t)(entry_index_tmp + num_entries)
                                   : (ssize_t)entry_index_tmp;
    ABTU_hashtable_element *p_element = get_element(p_hashtable, entry_index);
    if (!p_element->data) {
        /* No data */
        if (found)
            *found = 0;
        return;
    } else {
        /* Iterate the list. */
        while (p_element) {
            if (p_element->key == key) {
                if (data) {
                    memcpy(data, p_element->data, data_size);
                }
                if (found)
                    *found = 1;
                return;
            } else if (!p_element->p_next) {
                if (found)
                    *found = 0;
                return;
            }
            p_element = p_element->p_next;
        }
    }
}

ABTU_ret_err int ABTU_hashtable_set(ABTU_hashtable *p_hashtable, int key,
                                    const void *data, int *overwritten)
{
    ABTI_ASSERT(data);
    const size_t num_entries = p_hashtable->num_entries;
    const size_t data_size = p_hashtable->data_size;
    const ssize_t entry_index_tmp = ((ssize_t)key) % ((ssize_t)num_entries);
    const size_t entry_index = entry_index_tmp < 0
                                   ? (ssize_t)(entry_index_tmp + num_entries)
                                   : (ssize_t)entry_index_tmp;
    ABTU_hashtable_element *p_element = get_element(p_hashtable, entry_index);
    if (!p_element->data) {
        /* No data */
        p_element->key = key;
        p_element->data = ((char *)p_element) + sizeof(ABTU_hashtable_element);
        memcpy(p_element->data, data, data_size);
        if (overwritten)
            *overwritten = 0;
        return ABT_SUCCESS;
    } else {
        /* Iterate the list. */
        while (p_element) {
            if (p_element->key == key) {
                if (data) {
                    memcpy(p_element->data, data, data_size);
                }
                if (overwritten)
                    *overwritten = 1;
                return ABT_SUCCESS;
            } else if (!p_element->p_next) {
                const size_t hashtable_element_size =
                    ABTU_roundup_size(sizeof(ABTU_hashtable_element) +
                                          data_size,
                                      ABT_CONFIG_STATIC_CACHELINE_SIZE);
                ABTU_hashtable_element *p_new_element;
                int ret = ABTU_calloc(1, hashtable_element_size,
                                      (void **)&p_new_element);
                ABTI_CHECK_ERROR(ret);
                p_new_element->key = key;
                p_new_element->data =
                    ((char *)p_new_element) + sizeof(ABTU_hashtable_element);
                memcpy(p_new_element->data, data, data_size);
                p_element->p_next = p_new_element;
                if (overwritten)
                    *overwritten = 0;
                return ABT_SUCCESS;
            }
            p_element = p_element->p_next;
        }
    }
    return ABT_SUCCESS;
}

void ABTU_hashtable_delete(ABTU_hashtable *p_hashtable, int key, int *deleted)
{
    const size_t num_entries = p_hashtable->num_entries;
    const size_t data_size = p_hashtable->data_size;
    const ssize_t entry_index_tmp = ((ssize_t)key) % ((ssize_t)num_entries);
    const size_t entry_index = entry_index_tmp < 0
                                   ? (ssize_t)(entry_index_tmp + num_entries)
                                   : (ssize_t)entry_index_tmp;
    ABTU_hashtable_element *p_element = get_element(p_hashtable, entry_index);
    if (!p_element->data) {
        /* No data */
        if (deleted)
            *deleted = 0;
        return;
    } else if (p_element->key == key) {
        ABTU_hashtable_element *p_next = p_element->p_next;
        if (p_next) {
            const size_t hashtable_element_size =
                ABTU_roundup_size(sizeof(ABTU_hashtable_element) + data_size,
                                  ABT_CONFIG_STATIC_CACHELINE_SIZE);
            memcpy(p_element, p_next, hashtable_element_size);
            /* Recalculate p_element->data. */
            p_element->data =
                ((char *)p_element) + sizeof(ABTU_hashtable_element);
            ABTU_free(p_next);
        } else {
            p_element->data = NULL;
        }
        if (deleted)
            *deleted = 1;
        return;
    } else {
        /* Iterate the list. */
        ABTU_hashtable_element **pp_element = &p_element->p_next;
        p_element = *pp_element;
        while (p_element) {
            if (p_element->key == key) {
                *pp_element = p_element->p_next;
                ABTU_free(p_element);
                if (deleted)
                    *deleted = 1;
                return;
            } else if (!p_element->p_next) {
                if (deleted)
                    *deleted = 0;
                return;
            }
            pp_element = &p_element->p_next;
            p_element = *pp_element;
        }
    }
}
