/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

static ABTU_ret_err int atoi_impl(const char *str, ABT_bool *p_is_signed,
                                  uint64_t *p_val, ABT_bool *p_overflow);

ABTU_ret_err int ABTU_atoi(const char *str, int *p_val, ABT_bool *p_overflow)
{
    uint64_t val;
    ABT_bool overflow, is_signed;
    int abt_errno = atoi_impl(str, &is_signed, &val, &overflow);
    ABTI_CHECK_ERROR(abt_errno);
    if (is_signed) {
        if (val > (uint64_t)(-(int64_t)INT_MIN)) {
            /* Underflow. */
            overflow = ABT_TRUE;
            *p_val = INT_MIN;
        } else {
            *p_val = (int)(-(int64_t)val);
        }
    } else {
        if (val > (uint64_t)INT_MAX) {
            /* Overflow. */
            overflow = ABT_TRUE;
            *p_val = INT_MAX;
        } else {
            *p_val = (int)val;
        }
    }
    if (p_overflow)
        *p_overflow = overflow;
    return abt_errno;
}

ABTU_ret_err int ABTU_atoui32(const char *str, uint32_t *p_val,
                              ABT_bool *p_overflow)
{
    uint64_t val;
    ABT_bool overflow, is_signed;
    int abt_errno = atoi_impl(str, &is_signed, &val, &overflow);
    ABTI_CHECK_ERROR(abt_errno);
    if (is_signed) {
        /* Underflow. */
        if (val != 0)
            overflow = ABT_TRUE;
        *p_val = 0;
    } else {
        if (val > (uint64_t)UINT32_MAX) {
            /* Overflow. */
            overflow = ABT_TRUE;
            *p_val = UINT32_MAX;
        } else {
            *p_val = (uint32_t)val;
        }
    }
    if (p_overflow)
        *p_overflow = overflow;
    return abt_errno;
}

ABTU_ret_err int ABTU_atoui64(const char *str, uint64_t *p_val,
                              ABT_bool *p_overflow)
{
    uint64_t val;
    ABT_bool overflow, is_signed;
    int abt_errno = atoi_impl(str, &is_signed, &val, &overflow);
    ABTI_CHECK_ERROR(abt_errno);
    if (is_signed) {
        /* Underflow. */
        if (val != 0)
            overflow = ABT_TRUE;
        *p_val = 0;
    } else {
        *p_val = val;
    }
    if (p_overflow)
        *p_overflow = overflow;
    return abt_errno;
}

ABTU_ret_err int ABTU_atosz(const char *str, size_t *p_val,
                            ABT_bool *p_overflow)
{
    ABTI_STATIC_ASSERT(sizeof(size_t) == 4 || sizeof(size_t) == 8);
    if (sizeof(size_t) == 4) {
        uint32_t val;
        ABT_bool overflow;
        int abt_errno = ABTU_atoui32(str, &val, &overflow);
        ABTI_CHECK_ERROR(abt_errno);
        *p_val = (size_t)val;
        if (p_overflow)
            *p_overflow = overflow;
        return abt_errno;
    } else {
        uint64_t val;
        ABT_bool overflow;
        int abt_errno = ABTU_atoui64(str, &val, &overflow);
        ABTI_CHECK_ERROR(abt_errno);
        *p_val = (size_t)val;
        if (p_overflow)
            *p_overflow = overflow;
        return abt_errno;
    }
}

/*****************************************************************************/
/* Internal static functions                                                 */
/*****************************************************************************/

static ABTU_ret_err int atoi_impl(const char *str, ABT_bool *p_is_signed,
                                  uint64_t *p_val, ABT_bool *p_overflow)
{
    uint64_t val = 0;
    ABT_bool is_signed = ABT_FALSE, read_char = ABT_FALSE,
             read_digit = ABT_FALSE;
    while (1) {
        if ((*str == '\n' || *str == '\t' || *str == ' ' || *str == '\r') &&
            read_char == ABT_FALSE) {
            /* Do nothing. */
        } else if (*str == '+' && read_digit == ABT_FALSE) {
            read_char = ABT_TRUE;
        } else if (*str == '-' && read_digit == ABT_FALSE) {
            /* Flip the digit. */
            read_char = ABT_TRUE;
            is_signed = is_signed ? ABT_FALSE : ABT_TRUE;
        } else if ('0' <= *str && *str <= '9') {
            read_char = ABT_TRUE;
            read_digit = ABT_TRUE;
            /* Will val overflow? */
            if ((val > UINT64_MAX / 10) ||
                (val * 10 > UINT64_MAX - (uint64_t)(*str - '0'))) {
                /* Overflow. */
                *p_overflow = ABT_TRUE;
                *p_val = UINT64_MAX;
                *p_is_signed = is_signed;
                return ABT_SUCCESS;
            }
            val = val * 10 + (uint64_t)(*str - '0');
            read_digit = ABT_TRUE;
        } else {
            /* Stop reading str. */
            if (read_digit == ABT_FALSE) {
                /* No integer. */
                return ABT_ERR_INV_ARG;
            }
            *p_overflow = ABT_FALSE;
            *p_val = val;
            *p_is_signed = is_signed;
            return ABT_SUCCESS;
        }
        str++;
    }
}

#if 0

void test_ABTU_atoi(const char *str, int err, int val, ABT_bool overflow)
{
    int ret_val;
    ABT_bool ret_overflow;
    int ret_err = ABTU_atoi(str, &ret_val, &ret_overflow);
    assert(err == ret_err);
    if (err == ABT_SUCCESS) {
        assert(val == ret_val);
        assert(overflow == ret_overflow);
    }
}

void test_ABTU_atoui32(const char *str, int err, uint32_t val, ABT_bool overflow)
{
    uint32_t ret_val;
    ABT_bool ret_overflow;
    int ret_err = ABTU_atoui32(str, &ret_val, &ret_overflow);
    assert(err == ret_err);
    if (err == ABT_SUCCESS) {
        assert(val == ret_val);
        assert(overflow == ret_overflow);
    }
}

void test_ABTU_atoui64(const char *str, int err, uint64_t val, ABT_bool overflow)
{
    uint64_t ret_val;
    ABT_bool ret_overflow;
    int ret_err = ABTU_atoui64(str, &ret_val, &ret_overflow);
    assert(err == ret_err);
    if (err == ABT_SUCCESS) {
        assert(val == ret_val);
        assert(overflow == ret_overflow);
    }
}

void test_ABTU_atosz(const char *str, int err, size_t val, ABT_bool overflow)
{
    size_t ret_val;
    ABT_bool ret_overflow;
    int ret_err = ABTU_atosz(str, &ret_val, &ret_overflow);
    assert(err == ret_err);
    if (err == ABT_SUCCESS) {
        assert(val == ret_val);
        assert(overflow == ret_overflow);
    }
}

int main()
{
    typedef struct {
        const char *str;
        int err;
        int val;
    } base_case_t;

    /* Basic cases (no overflow). */
    base_case_t cases[] = {
        { "0", ABT_SUCCESS, 0 },
        { "63", ABT_SUCCESS, 63 },
        { "+14", ABT_SUCCESS, 14 },
        { "+0", ABT_SUCCESS, 0 },
        { "+-+-+---++0", ABT_SUCCESS, 0 },
        { "+-+-+---+-+8800", ABT_SUCCESS, 8800 },
        { "----1---", ABT_SUCCESS, 1 },
        { "abc", ABT_ERR_INV_ARG, 0 },
        { "13abc", ABT_SUCCESS, 13 },
        { "000123456", ABT_SUCCESS, 123456 },
        { "00000000", ABT_SUCCESS, 0 },
        { "123x456", ABT_SUCCESS, 123 },
        { "123+456", ABT_SUCCESS, 123 },
        { "123 456", ABT_SUCCESS, 123 },
        { "--12-3-45-6", ABT_SUCCESS, 12 },
        { "", ABT_ERR_INV_ARG, 0 },
        { "+", ABT_ERR_INV_ARG, 0 },
        { "-", ABT_ERR_INV_ARG, 0 },
        { "+ 2", ABT_ERR_INV_ARG, 0 },
        { "    \n\t\r+-+-", ABT_ERR_INV_ARG, 0 },
        { "    \n\t\r+-+-123", ABT_SUCCESS, 123 },
    };

    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        test_ABTU_atoi(cases[i].str, cases[i].err, cases[i].val, ABT_FALSE);
        test_ABTU_atoui32(cases[i].str, cases[i].err, cases[i].val, ABT_FALSE);
        test_ABTU_atoui64(cases[i].str, cases[i].err, cases[i].val, ABT_FALSE);
        test_ABTU_atosz(cases[i].str, cases[i].err, cases[i].val, ABT_FALSE);
    }

    /* Check negative values. */
    test_ABTU_atoi("-1", ABT_SUCCESS, -1, ABT_FALSE);
    test_ABTU_atoi("-9990", ABT_SUCCESS, -9990, ABT_FALSE);
    test_ABTU_atoi(" --+-1234a-", ABT_SUCCESS, -1234, ABT_FALSE);

    /* Check overflow/underflow */
    test_ABTU_atoi("2147483646", ABT_SUCCESS, 2147483646, ABT_FALSE);
    test_ABTU_atoi("2147483647", ABT_SUCCESS, 2147483647, ABT_FALSE);
    test_ABTU_atoi("2147483648", ABT_SUCCESS, 2147483647, ABT_TRUE);
    test_ABTU_atoi("11112147483648", ABT_SUCCESS, 2147483647, ABT_TRUE);
    test_ABTU_atoi("-2147483647", ABT_SUCCESS, -2147483647, ABT_FALSE);
    test_ABTU_atoi("-2147483648", ABT_SUCCESS, -2147483648, ABT_FALSE);
    test_ABTU_atoi("-2147483649", ABT_SUCCESS, -2147483648, ABT_TRUE);
    test_ABTU_atoi("-11112147483648", ABT_SUCCESS, -2147483648, ABT_TRUE);

    test_ABTU_atoui32("4294967294", ABT_SUCCESS, 4294967294, ABT_FALSE);
    test_ABTU_atoui32("4294967295", ABT_SUCCESS, 4294967295, ABT_FALSE);
    test_ABTU_atoui32("4294967296", ABT_SUCCESS, 4294967295, ABT_TRUE);
    test_ABTU_atoui32("11114294967295", ABT_SUCCESS, 4294967295, ABT_TRUE);
    test_ABTU_atoui32("-1", ABT_SUCCESS, 0, ABT_TRUE);
    test_ABTU_atoui32("-2147483649", ABT_SUCCESS, 0, ABT_TRUE);

    test_ABTU_atoui64("18446744073709551614", ABT_SUCCESS,
                      18446744073709551614u, ABT_FALSE);
    test_ABTU_atoui64("18446744073709551615", ABT_SUCCESS,
                      18446744073709551615u, ABT_FALSE);
    test_ABTU_atoui64("18446744073709551616", ABT_SUCCESS,
                      18446744073709551615u, ABT_TRUE);
    test_ABTU_atoui64("111118446744073709551615", ABT_SUCCESS,
                      18446744073709551615u, ABT_TRUE);
    test_ABTU_atoui64("-1", ABT_SUCCESS, 0, ABT_TRUE);
    test_ABTU_atoui64("-18446744073709551616", ABT_SUCCESS, 0, ABT_TRUE);

    if (sizeof(size_t) == 4) {
        test_ABTU_atosz("4294967294", ABT_SUCCESS, 4294967294, ABT_FALSE);
        test_ABTU_atosz("4294967295", ABT_SUCCESS, 4294967295, ABT_FALSE);
        test_ABTU_atosz("4294967296", ABT_SUCCESS, 4294967295, ABT_TRUE);
        test_ABTU_atosz("11114294967295", ABT_SUCCESS, 4294967295, ABT_TRUE);
        test_ABTU_atosz("-1", ABT_SUCCESS, 0, ABT_TRUE);
        test_ABTU_atosz("-2147483649", ABT_SUCCESS, 0, ABT_TRUE);
    } else {
        assert(sizeof(size_t) == 8);
        test_ABTU_atosz("18446744073709551614", ABT_SUCCESS,
                        18446744073709551614u, ABT_FALSE);
        test_ABTU_atosz("18446744073709551615", ABT_SUCCESS,
                        18446744073709551615u, ABT_FALSE);
        test_ABTU_atosz("18446744073709551616", ABT_SUCCESS,
                        18446744073709551615u, ABT_TRUE);
        test_ABTU_atosz("111118446744073709551615", ABT_SUCCESS,
                        18446744073709551615u, ABT_TRUE);
        test_ABTU_atosz("-1", ABT_SUCCESS, 0, ABT_TRUE);
        test_ABTU_atosz("-18446744073709551616", ABT_SUCCESS, 0, ABT_TRUE);
    }
}

#endif
