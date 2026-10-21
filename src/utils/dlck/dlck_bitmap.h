/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DLCK_BITMAP__
#define __DLCK_BITMAP__

#include <stdint.h>
#include <sys/param.h>

/**
 * Typed variant of the setbit() macro. For 32-bit values.
 *
 * \param[in,out] bitmap        Bitmap to modify.
 * \param[in] bit               Bit to set.
 */
static inline void
dlck_bitmap_setbit32(uint32_t *bitmap, int bit)
{
	setbit((uint8_t *)bitmap, bit);
}

/**
 * Typed variant of the isset() macro. For 32-bit values.
 *
 * \param[in] bitmap    Bitmap to check.
 * \param[in] bit       Bit to check.
 *
 * \retval true if \p bit is set.
 * \retval false otherwise.
 */
static inline bool
dlck_bitmap_isset32(uint32_t bitmap, int bit)
{
	return isset((uint8_t *)&bitmap, bit);
}

/**
 * Typed variant of the isclr() macro. For 32-bit values.
 *
 * \param[in] bitmap    Bitmap to check.
 * \param[in] bit       Bit to check.
 *
 * \retval true if \p bit is NOT set.
 * \retval false otherwise.
 */
static inline bool
dlck_bitmap_isclr32(uint32_t bitmap, int bit)
{
	return isclr((uint8_t *)&bitmap, bit);
}

#endif /** __DLCK_BITMAP__ */
