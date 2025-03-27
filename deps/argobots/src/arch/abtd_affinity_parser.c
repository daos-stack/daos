/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/* If the value is too big, the input should be wrong. */
#define MAX_NUM_ELEMS (1024 * 1024)

typedef struct alloc_header {
    struct alloc_header *p_prev;
    struct alloc_header *p_next;
} alloc_header;
#define ALLOC_HEADER_SIZE                                                      \
    ((sizeof(alloc_header) + ABTU_MAX_ALIGNMENT - 1) / ABTU_MAX_ALIGNMENT *    \
     ABTU_MAX_ALIGNMENT)

typedef struct {
    alloc_header *p_head;
    alloc_header *p_tail;
} alloc_list;

/* Since it is extremely cumbersome to handle memory leaks on memory allocation
 * failure, the following implementation uses a linked list. */
ABTU_ret_err static int list_calloc(alloc_list *p_alloc_list, size_t size,
                                    void **p_ptr)
{
    alloc_header *p_header;
    int ret = ABTU_calloc(1, size + ALLOC_HEADER_SIZE, (void **)&p_header);
    ABTI_CHECK_ERROR(ret);
    /* Allocation succeeded. */
    *p_ptr = (void *)(((char *)p_header) + ALLOC_HEADER_SIZE);
    /* Append this header to alloc_list */
    p_header->p_next = NULL;
    p_header->p_prev = p_alloc_list->p_tail;
    if (p_alloc_list->p_tail) {
        p_alloc_list->p_tail->p_next = p_header;
        p_alloc_list->p_tail = p_header;
    } else {
        p_alloc_list->p_head = p_header;
        p_alloc_list->p_tail = p_header;
    }
    return ABT_SUCCESS;
}

ABTU_ret_err static int list_realloc(alloc_list *p_alloc_list, size_t old_size,
                                     size_t new_size, void **p_ptr)
{
    /* Read header information before realloc() */
    if (old_size == 0) {
        return list_calloc(p_alloc_list, new_size, p_ptr);
    } else {
        alloc_header *p_old_header =
            (alloc_header *)(((char *)*p_ptr) - ALLOC_HEADER_SIZE);
        alloc_header *p_old_prev_header = p_old_header->p_prev;
        alloc_header *p_old_next_header = p_old_header->p_next;
        alloc_header *p_new_header = p_old_header;
        int ret =
            ABTU_realloc(old_size + ALLOC_HEADER_SIZE,
                         new_size + ALLOC_HEADER_SIZE, (void **)&p_new_header);
        ABTI_CHECK_ERROR(ret);
        /* Allocation succeeded. */
        *p_ptr = (void *)(((char *)p_new_header) + ALLOC_HEADER_SIZE);
        /* Replace p_old_header with p_new_header */
        if (p_alloc_list->p_head == p_old_header)
            p_alloc_list->p_head = p_new_header;
        if (p_alloc_list->p_tail == p_old_header)
            p_alloc_list->p_tail = p_new_header;
        p_new_header->p_prev = p_old_prev_header;
        if (p_old_prev_header)
            p_old_prev_header->p_next = p_new_header;
        p_new_header->p_next = p_old_next_header;
        if (p_old_next_header)
            p_old_next_header->p_prev = p_new_header;
        return ABT_SUCCESS;
    }
}

static void list_free_all(void *p_head)
{
    alloc_header *p_cur = (alloc_header *)p_head;
    while (p_cur) {
        alloc_header *p_next = p_cur->p_next;
        ABTU_free(p_cur);
        p_cur = p_next;
    }
}

ABTU_ret_err static int id_list_create(alloc_list *p_alloc_list,
                                       ABTD_affinity_id_list **pp_id_list)
{
    return list_calloc(p_alloc_list, sizeof(ABTD_affinity_id_list),
                       (void **)pp_id_list);
}

ABTU_ret_err static int id_list_add(alloc_list *p_alloc_list,
                                    ABTD_affinity_id_list *p_id_list, int id,
                                    uint32_t num, int stride)
{
    /* Needs to add num ids. */
    uint32_t i;
    int ret = list_realloc(p_alloc_list, sizeof(int) * p_id_list->num,
                           sizeof(int) * (p_id_list->num + num),
                           (void **)&p_id_list->ids);
    ABTI_CHECK_ERROR(ret);
    for (i = 0; i < num; i++) {
        p_id_list->ids[p_id_list->num + i] = id + stride * i;
    }
    p_id_list->num += num;
    return ABT_SUCCESS;
}

ABTU_ret_err static int list_create(alloc_list *p_alloc_list,
                                    ABTD_affinity_list **pp_affinity_list)
{
    return list_calloc(p_alloc_list, sizeof(ABTD_affinity_list),
                       (void **)pp_affinity_list);
}

ABTU_ret_err static int list_add(alloc_list *p_alloc_list,
                                 ABTD_affinity_list *p_list,
                                 ABTD_affinity_id_list *p_base, uint32_t num,
                                 int stride)
{
    /* Needs to add num id-lists. */
    uint32_t i, j;
    int ret;

    ret = list_realloc(p_alloc_list,
                       sizeof(ABTD_affinity_id_list *) * p_list->num,
                       sizeof(ABTD_affinity_id_list *) * (p_list->num + num),
                       (void **)&p_list->p_id_lists);
    ABTI_CHECK_ERROR(ret);
    for (i = 1; i < num; i++) {
        ABTD_affinity_id_list *p_id_list;
        ret = id_list_create(p_alloc_list, &p_id_list);
        ABTI_CHECK_ERROR(ret);
        p_id_list->num = p_base->num;
        ret = list_calloc(p_alloc_list, sizeof(int) * p_id_list->num,
                          (void **)&p_id_list->ids);
        ABTI_CHECK_ERROR(ret);
        for (j = 0; j < p_id_list->num; j++)
            p_id_list->ids[j] = p_base->ids[j] + stride * i;
        p_list->p_id_lists[p_list->num + i] = p_id_list;
    }
    p_list->p_id_lists[p_list->num] = p_base;
    p_list->num += num;
    return ABT_SUCCESS;
}

static inline int is_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Integer. */
static int consume_int(const char *str, uint32_t *p_index, int *p_val)
{
    uint32_t index = *p_index;
    int val = 0, val_sign = 1;
    char flag = 'n';
    while (1) {
        char c = *(str + index);
        if (flag != 'v' && c == '-') {
            /* Negative sign. */
            flag = 's';
            val_sign = -val_sign;
        } else if (flag != 'v' && c == '+') {
            /* Positive sign. */
            flag = 's';
        } else if (flag == 'n' && is_whitespace(c)) {
            /* Skip a whitespace. */
        } else if ('0' <= c && c <= '9') {
            /* Value. */
            flag = 'v';
            val = val * 10 + (int)(c - '0');
        } else {
            /* Encounters a symbol. */
            if (flag == 'v') {
                /* Succeeded. */
                *p_val = val * val_sign;
                *p_index = index;
                return 1;
            } else {
                /* Failed. The parser could not consume a value. */
                return 0;
            }
        }
        index++;
    }
}

/* Positive integer */
static int consume_pint(const char *str, uint32_t *p_index, int *p_val)
{
    uint32_t index = *p_index;
    int val;
    /* The value must be positive. */
    if (consume_int(str, &index, &val) && val > 0) {
        *p_index = index;
        *p_val = val;
        return 1;
    }
    return 0;
}

/* Symbol.  If succeeded, it returns a consumed characters. */
static int consume_symbol(const char *str, uint32_t *p_index, char symbol)
{
    uint32_t index = *p_index;
    while (1) {
        char c = *(str + index);
        if (c == symbol) {
            *p_index = index + 1;
            return 1;
        } else if (is_whitespace(c)) {
            /* Skip a whitespace. */
        } else {
            /* Failed. The parser could not consume a symbol. */
            return 0;
        }
        index++;
    }
}

ABTU_ret_err static int
parse_es_id_list(alloc_list *p_alloc_list, const char *affinity_str,
                 uint32_t *p_index, ABTD_affinity_id_list **pp_affinity_id_list)
{
    int ret, val;
    ABTD_affinity_id_list *p_affinity_id_list;
    ret = id_list_create(p_alloc_list, &p_affinity_id_list);
    ABTI_CHECK_ERROR(ret);
    /* Expect either <id> or { <id-list> } */
    if (consume_int(affinity_str, p_index, &val)) {
        /* If the first token is an integer, it is <id> */
        ret = id_list_add(p_alloc_list, p_affinity_id_list, val, 1, 1);
        ABTI_CHECK_ERROR(ret);
        *pp_affinity_id_list = p_affinity_id_list;
        return ABT_SUCCESS;
    } else if (consume_symbol(affinity_str, p_index, '{')) {
        /* It should be "{" <id-list> "}".  Parse <id-list> and "}" */
        while (1) {
            int id, num = 1, stride = 1;
            /* Parse <id-interval>.  First, expect <id> */
            if (!consume_int(affinity_str, p_index, &id))
                return ABT_ERR_OTHER;
            /* Optional: ":" <num> */
            if (consume_symbol(affinity_str, p_index, ':')) {
                /* Expect <num> */
                if (!consume_pint(affinity_str, p_index, &num))
                    return ABT_ERR_OTHER;
                /* Optional: ":" <stride> */
                if (consume_symbol(affinity_str, p_index, ':')) {
                    /* Expect <stride> */
                    if (!consume_int(affinity_str, p_index, &stride))
                        return ABT_ERR_OTHER;
                }
            }
            if (num >= MAX_NUM_ELEMS)
                return ABT_ERR_OTHER;
            /* Add ids based on <id-interval> */
            ret =
                id_list_add(p_alloc_list, p_affinity_id_list, id, num, stride);
            ABTI_CHECK_ERROR(ret);
            /* After <id-interval>, we expect either "," (in <id-list>) or "}"
             * (in <es-id-list>) */
            if (consume_symbol(affinity_str, p_index, ',')) {
                /* Parse <id-interval> again. */
                continue;
            }
            /* Expect "}" */
            if (!consume_symbol(affinity_str, p_index, '}'))
                return ABT_ERR_OTHER;
            /* Succeeded. */
            *pp_affinity_id_list = p_affinity_id_list;
            return ABT_SUCCESS;
        }
    }
    return ABT_ERR_OTHER;
}

ABTU_ret_err static int parse_list(alloc_list *p_alloc_list,
                                   const char *affinity_str,
                                   ABTD_affinity_list **pp_affinity_list)
{
    if (!affinity_str)
        return ABT_ERR_OTHER;
    int ret;
    uint32_t index = 0;
    ABTD_affinity_list *p_affinity_list;
    ret = list_create(p_alloc_list, &p_affinity_list);
    ABTI_CHECK_ERROR(ret);

    ABTD_affinity_id_list *p_id_list = NULL;
    while (1) {
        int num = 1, stride = 1;
        /* Parse <interval> */
        /* Expect <es-id-list> */
        ret = parse_es_id_list(p_alloc_list, affinity_str, &index, &p_id_list);
        ABTI_CHECK_ERROR(ret);
        /* Optional: ":" <num> */
        if (consume_symbol(affinity_str, &index, ':')) {
            /* Expect <num> */
            if (!consume_pint(affinity_str, &index, &num))
                return ABT_ERR_OTHER;
            /* Optional: ":" <stride> */
            if (consume_symbol(affinity_str, &index, ':')) {
                /* Expect <stride> */
                if (!consume_int(affinity_str, &index, &stride))
                    return ABT_ERR_OTHER;
            }
        }
        if (num >= MAX_NUM_ELEMS)
            return ABT_ERR_OTHER;
        /* Add <es-id-list> based on <interval> */
        ret = list_add(p_alloc_list, p_affinity_list, p_id_list, num, stride);
        ABTI_CHECK_ERROR(ret);
        p_id_list = NULL;
        /* After <interval>, expect either "," (in <list>) or "\0" */
        if (consume_symbol(affinity_str, &index, ',')) {
            /* Parse <interval> again. */
            continue;
        }
        /* Expect "\0" */
        if (!consume_symbol(affinity_str, &index, '\0'))
            return ABT_ERR_OTHER;
        /* Succeeded. */
        *pp_affinity_list = p_affinity_list;
        return ABT_SUCCESS;
    }
    /* Unreachable. */
}

ABTU_ret_err int
ABTD_affinity_list_create(const char *affinity_str,
                          ABTD_affinity_list **pp_affinity_list)
{
    ABTD_affinity_list *p_affinity_list;
    alloc_list tmp_alloc_list = { NULL, NULL };
    int ret = parse_list(&tmp_alloc_list, affinity_str, &p_affinity_list);
    if (ret != ABT_SUCCESS) {
        /* Free all the allocated memory. */
        list_free_all((void *)tmp_alloc_list.p_head);
        return ret;
    } else {
        /* Save p_head in p_affinity_list to free it in
         * ABTD_affinity_list_free(). */
        p_affinity_list->p_mem_head = (void *)tmp_alloc_list.p_head;
        *pp_affinity_list = p_affinity_list;
        return ABT_SUCCESS;
    }
}

void ABTD_affinity_list_free(ABTD_affinity_list *p_affinity_list)
{
    if (p_affinity_list) {
        list_free_all(p_affinity_list->p_mem_head);
    }
}

#if 0

static int is_equal(const ABTD_affinity_list *a, const ABTD_affinity_list *b)
{
    int i, j;
    if (a->num != b->num)
        return 0;
    for (i = 0; i < a->num; i++) {
        const ABTD_affinity_id_list *a_id = a->p_id_lists[i];
        const ABTD_affinity_id_list *b_id = b->p_id_lists[i];
        if (a_id->num != b_id->num)
            return 0;
        for (j = 0; j < a_id->num; j++) {
            if (a_id->ids[j] != b_id->ids[j])
                return 0;
        }
    }
    return 1;
}

static int is_equal_str(const char *a_str, const char *b_str)
{
    int ret = 1;
    ABTD_affinity_list *a, *b;
    alloc_list tmp_alloc_list1 = { NULL, NULL };
    alloc_list tmp_alloc_list2 = { NULL, NULL };
    int ret1 = parse_list(&tmp_alloc_list1, a_str, &a);
    int ret2 = parse_list(&tmp_alloc_list2, b_str, &b);
    ret = ret1 == ABT_SUCCESS && ret2 == ABT_SUCCESS && a && b && is_equal(a, b);
    list_free_all((void *)tmp_alloc_list1.p_head);
    list_free_all((void *)tmp_alloc_list2.p_head);
    return ret;
}

static int is_err_str(const char *str)
{
    alloc_list tmp_alloc_list = { NULL, NULL };
    ABTD_affinity_list *a;
    int ret = parse_list(&tmp_alloc_list, str, &a);
    list_free_all((void *)tmp_alloc_list.p_head);
    if (ret == ABT_SUCCESS) {
        return 0;
    }
    return 1;
}

static void test_parse(void)
{
    /* Legal strings */
    assert(!is_err_str("++1"));
    assert(!is_err_str("+-1"));
    assert(!is_err_str("+-+-1"));
    assert(!is_err_str("+0"));
    assert(!is_err_str("-0"));
    assert(!is_err_str("-9:1:-9"));
    assert(!is_err_str("-9:1:0"));
    assert(!is_err_str("-9:1:9"));
    assert(!is_err_str("0:1:-9"));
    assert(!is_err_str("0:1:0"));
    assert(!is_err_str("0:1:9"));
    assert(!is_err_str("9:1:-9"));
    assert(!is_err_str("9:1:0"));
    assert(!is_err_str("9:1:9"));
    assert(!is_err_str("{-9:1:-9}"));
    assert(!is_err_str("{-9:1:0}"));
    assert(!is_err_str("{-9:1:9}"));
    assert(!is_err_str("{0:1:-9}"));
    assert(!is_err_str("{0:1:0}"));
    assert(!is_err_str("{0:1:9}"));
    assert(!is_err_str("{9:1:-9}"));
    assert(!is_err_str("{9:1:0}"));
    assert(!is_err_str("{9:1:9}"));
    assert(!is_err_str("1,2,3"));
    assert(!is_err_str("1,2,{1,2}"));
    assert(!is_err_str("1,2,{1:2}"));
    assert(!is_err_str("1:2,{1:2}"));
    assert(!is_err_str("1:2:1,2"));
    assert(!is_err_str(" 1 :  +2 , { -1 : \r 2\n:2}\n"));
    /* Illegal strings */
    assert(is_err_str(""));
    assert(is_err_str("{}"));
    assert(is_err_str("+ 1"));
    assert(is_err_str("+ +1"));
    assert(is_err_str("+ -1"));
    assert(is_err_str("1:"));
    assert(is_err_str("1:2:"));
    assert(is_err_str("1:2,"));
    assert(is_err_str("1:-2"));
    assert(is_err_str("1:0"));
    assert(is_err_str("1:-2:4"));
    assert(is_err_str("1:0:4"));
    assert(is_err_str("1:1:1:"));
    assert(is_err_str("1:1:1:1"));
    assert(is_err_str("1:1:1:1,1"));
    assert(is_err_str("{1:2:3},"));
    assert(is_err_str("{1:2:3}:"));
    assert(is_err_str("{1:2:3}:2:"));
    assert(is_err_str("{:2:3}"));
    assert(is_err_str("{{2:3}}"));
    assert(is_err_str("{2:3}}"));
    assert(is_err_str("2:3}"));
    assert(is_err_str("{1:2:3"));
    assert(is_err_str("{1,2,}"));
    assert(is_err_str("{1:-2}"));
    assert(is_err_str("{1:0}"));
    assert(is_err_str("{1:-2:4}"));
    assert(is_err_str("{1:0:4}"));
    /* Comparison */
    assert(is_equal_str("{1},{2},{3},{4}", "1,2,3,4"));
    assert(is_equal_str("{1:4:1}", "{1,2,3,4}"));
    assert(is_equal_str("{1:4}", "{1,2,3,4}"));
    assert(is_equal_str("1:2,3:2", "1,2,3,4"));
    assert(is_equal_str("{1:2},3:2", "{1,2},3,4"));
    assert(is_equal_str("{1:1:4},{2:1:-4},{3:1:0},{4:1}", "1,2,3,4"));
    assert(is_equal_str("{3:4:-1}", "{3,2,1,0}"));
    assert(is_equal_str("3:4:-1,-1", "3,2,1,0,-1"));
    assert(is_equal_str("{1:2:3}:1", "{1,4}"));
    assert(is_equal_str("{1:2:3}:3", "{1,4},{2,5},{3,6}"));
    assert(is_equal_str("{1:2:3}:3:2", "{1,4},{3,6},{5,8}"));
    assert(is_equal_str("{1:2:3}:3:-2", "{1,4},{-1,2},{-3,0}"));
    assert(is_equal_str("{1:2:3}:3:-2,1", "{1,4},{-1,2},{-3,0},1"));
    assert(is_equal_str("{-2:3:-2}:2:-4", "{-2,-4,-6},{-6,-8,-10}"));
}

#endif
