/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* ieee.c -- IEEE-single -> 16.16 Fixed                                       */
/*                                                                            */
/* IFF DR2D stores every floating-point quantity as a 32-bit big-endian      */
/* IEEE single (the type "IEEE = long" in dr2d.h).  The drawing.datatype      */
/* superclass works exclusively in 16.16 Fixed so we convert at load time.    */
/*                                                                            */
/* This is a pure-integer routine; no mathieeesingbas.library or FPU calls.   */
/*****************************************************************************/

#include "classbase.h"


LONG dr2dIEEEToFixed (ULONG ieee)
{
    ULONG sign;
    ULONG expFld;
    ULONG mant;
    LONG  exp;
    LONG  result;
    LONG  sh;

    if (ieee == 0 || ieee == 0x80000000UL)
        return 0;

    sign  = (ieee >> 31) & 1UL;
    expFld = (ieee >> 23) & 0xFFUL;
    mant  = (ieee & 0x7FFFFFUL) | 0x800000UL;

    if (expFld == 0)
        return 0;
    if (expFld == 0xFF)
        return sign ? (LONG) 0x80000000L : (LONG) 0x7FFFFFFFL;

    exp = (LONG) expFld - 127;

    sh = exp - 7;
    if (sh >= 0)
    {
        if (sh > 31)
            result = sign ? (LONG) 0x80000000L : (LONG) 0x7FFFFFFFL;
        else
            result = (LONG) (mant << sh);
    }
    else
    {
        sh = -sh;
        if (sh > 31)
            result = 0;
        else
            result = (LONG) (mant >> sh);
    }

    if (sign) result = -result;
    return result;
}


/*****************************************************************************/
/* dr2dCoordToFixed -- IEEE-single -> 16.16 Fixed, NATIVE UNITS              */
/*                                                                           */
/* DR2D spatial quantities (coordinates, page extents, character cell sizes, */
/* edge thickness, bounding boxes) are kept in the file's OWN units with no  */
/* inches->points rescaling. Real DR2D files in the wild do NOT consistently */
/* use inches: e.g. tiger.dr2d declares a 1000x800 user-unit canvas, where   */
/* a x72 factor would overflow 16.16 Fixed and blow the drawing far off the  */
/* page.  The robust, unit-agnostic approach is to load coordinates as-is    */
/* and let DDTM_FITTOBOX scale the DRHD canvas to the output rectangle; the  */
/* file's unit cancels out of that ratio, so both inch- and user-unit files  */
/* fit the display correctly.                                                */
/*                                                                           */
/* Retained as a thin wrapper (rather than removed) so existing spatial      */
/* call sites read clearly as "coordinate" conversions versus the angular    */
/* dr2dIEEEToFixed used for rotations.                                       */
/*****************************************************************************/

LONG dr2dCoordToFixed (ULONG ieee)
{
    return dr2dIEEEToFixed (ieee);
}
