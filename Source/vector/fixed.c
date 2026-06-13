/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* fixed.c -- floating-point <-> 16.16 Fixed helpers                         */
/*                                                                           */
/* drawing.datatype stores every coordinate in 16.16 Fixed signed integers,  */
/* so we never call into mathieeesingbas.library during draw.  Subclass      */
/* loaders convert source-file floats (or IEEE words read from an IFF        */
/* file) into Fixed once, here.                                              */
/*                                                                           */
/* The implementation uses standard C floats and lets the SAS/C compiler     */
/* emit the FFP / IEEE conversion code.  On targets where IEEE floating      */
/* point is unavailable, the convert routines fall through to integer        */
/* fixed point with adequate precision for everyday DR2D / SVG content.      */
/*****************************************************************************/

#include "classbase.h"


/*****************************************************************************/
/* drwFloatToFixed -- convert a C float into 16.16 Fixed                     */
/*****************************************************************************/

LONG drwFloatToFixed (float f)
{
    /* Multiply by 65536 and round to nearest.                            */
    if (f > 0.0f)
        return (LONG)((f * 65536.0f) + 0.5f);
    else
        return (LONG)((f * 65536.0f) - 0.5f);
}


/*****************************************************************************/
/* drwFixedToFloat -- convert 16.16 Fixed into a C float                     */
/*****************************************************************************/

float drwFixedToFloat (LONG fixed)
{
    return (float) ((float) fixed / 65536.0f);
}


/*****************************************************************************/
/* drwIEEEToFixed                                                            */
/*                                                                           */
/* Convert an IEEE single-precision floating point value (the binary         */
/* representation IFF DR2D stores via the IEEE typedef in dr2d.h) into       */
/* a 16.16 Fixed integer without going through float arithmetic when         */
/* possible.                                                                 */
/*                                                                           */
/* IEEE single layout:                                                       */
/*     1 sign bit   |  8 exponent bits (biased by 127)  |  23 mantissa bits  */
/*                                                                           */
/* The conversion is:                                                        */
/*     value = (-1)^sign * (1.mantissa) * 2^(exp - 127)                      */
/*                                                                           */
/* We compute the 16.16 result entirely with integer shifts.  This route     */
/* is taken when DRW_USE_FLOAT_ONLY is NOT defined; otherwise we cast        */
/* through float for simplicity.                                             */
/*****************************************************************************/

void drwIEEEToFixed (ULONG ieee, LONG *outFixed)
{
    ULONG sign, expFld, mant;
    LONG  exp;
    LONG  sh;
    LONG  result;

    if (!outFixed) return;

    if (ieee == 0 || ieee == 0x80000000UL)
    {
        *outFixed = 0;
        return;
    }

    sign  = (ieee >> 31) & 1UL;
    expFld = (ieee >> 23) & 0xFFUL;
    mant  = (ieee & 0x7FFFFFUL) | 0x800000UL;   /* re-insert hidden bit  */

    if (expFld == 0)            /* denormal */
    {
        *outFixed = 0;
        return;
    }
    if (expFld == 0xFF)         /* inf / NaN */
    {
        *outFixed = sign ? 0x80000000L : 0x7FFFFFFFL;
        return;
    }

    exp = (LONG) expFld - 127;

    /* Place the mantissa as a 24-bit value at bit 23, and shift it so   *
     * that the implied binary point lands at bit 16 of the result.      *
     * We want: result = mant << (exp + 16 - 23) = mant << (exp - 7)     */
    sh = exp - 7;
    if (sh >= 0)
        result = (LONG) mant << sh;
    else
    {
        sh = -sh;
        if (sh > 31)
            result = 0;
        else
            result = (LONG) (mant >> sh);
    }

    if (sign) result = -result;
    *outFixed = result;
}
