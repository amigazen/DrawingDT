/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* coord.c -- IFF ILUS COORD -> 16.16 Fixed conversion and helpers           */
/*                                                                            */
/* IFF ILUS stores every spatial quantity as a signed 32-bit COORD in units  */
/* of 1/1828800 inch (1/25400 point).  drawing.datatype works exclusively in */
/* 16.16 Fixed, so the loader converts COORDs to Fixed "points" - one Fixed  */
/* unit == one PostScript point (1/72 inch).  Because every coordinate, the  */
/* canvas extent and the edge thickness all use the same conversion, the     */
/* exact unit cancels out of DDTM_FITTOBOX and the drawing fits the output   */
/* rectangle regardless of its absolute size.                                */
/*                                                                            */
/* These are pure-integer routines; no mathieeesingbas.library or FPU calls. */
/* The path-placement transform (PA box over PD local coordinates) needs a   */
/* 32x32->64 multiply followed by a 64/32 divide, which ilusMulDiv provides  */
/* without relying on any 64-bit compiler type (the SAS/C 68000 target has   */
/* none).                                                                    */
/*****************************************************************************/

#include "classbase.h"


/*****************************************************************************/
/* ilusCoordToFixed -- COORD (1/1828800 inch) -> 16.16 Fixed points           */
/*****************************************************************************/

LONG ilusCoordToFixed (LONG coord)
{
    LONG  neg = 0;
    LONG  ip;
    ULONG rem;
    ULONG frac;
    LONG  result;

    if (coord < 0)
    {
        neg = 1;
        coord = -coord;
    }

    /* Integer part (points) fits easily: an 11-inch page is only ~792    *
     * points, so (ip << 16) cannot overflow.  The remainder is strictly  *
     * less than ILUS_COORDS_PER_POINT (25400), so (rem << 16) stays well  *
     * inside an unsigned 32-bit value (25399 << 16 < 2^31).              */
    ip   = coord / ILUS_COORDS_PER_POINT;
    rem  = (ULONG)(coord % ILUS_COORDS_PER_POINT);
    frac = (rem << 16) / (ULONG) ILUS_COORDS_PER_POINT;

    result = (ip << 16) | (LONG) frac;

    return neg ? -result : result;
}


/*****************************************************************************/
/* 32x32 -> 64 unsigned multiply (split into 16-bit partial products)         */
/*****************************************************************************/

static void mulu32 (ULONG a, ULONG b, ULONG *hi, ULONG *lo)
{
    ULONG al = a & 0xFFFFUL;
    ULONG ah = a >> 16;
    ULONG bl = b & 0xFFFFUL;
    ULONG bh = b >> 16;
    ULONG ll = al * bl;
    ULONG lh = al * bh;
    ULONG hl = ah * bl;
    ULONG hh = ah * bh;
    ULONG mid;

    mid  = (ll >> 16) + (lh & 0xFFFFUL) + (hl & 0xFFFFUL);
    *lo  = (ll & 0xFFFFUL) | (mid << 16);
    *hi  = hh + (lh >> 16) + (hl >> 16) + (mid >> 16);
}


/*****************************************************************************/
/* 64 / 32 -> 32 unsigned divide (restoring long division, 64 iterations)     */
/*                                                                            */
/* The quotient is assumed to fit in 32 bits, which it always does for our    */
/* call sites (a coordinate offset never exceeds the original path width).    */
/*****************************************************************************/

static ULONG divu64 (ULONG hi, ULONG lo, ULONG d)
{
    ULONG q = 0;
    ULONG r = 0;
    int   i;

    if (d == 0)
        return 0;

    for (i = 0; i < 64; i++)
    {
        r  = (r << 1) | ((hi >> 31) & 1UL);
        hi = (hi << 1) | ((lo >> 31) & 1UL);
        lo = lo << 1;
        q  = q << 1;
        if (r >= d)
        {
            r -= d;
            q |= 1UL;
        }
    }
    return q;
}


/*****************************************************************************/
/* ilusMulDiv -- signed (a * b) / c with a 64-bit intermediate               */
/*****************************************************************************/

LONG ilusMulDiv (LONG a, LONG b, LONG c)
{
    LONG  sign = 1;
    ULONG ua, ub, uc;
    ULONG hi, lo;
    ULONG q;

    if (c == 0)
        return 0;

    if (a < 0) { sign = -sign; a = -a; }
    if (b < 0) { sign = -sign; b = -b; }
    if (c < 0) { sign = -sign; c = -c; }

    ua = (ULONG) a;
    ub = (ULONG) b;
    uc = (ULONG) c;

    mulu32 (ua, ub, &hi, &lo);
    q = divu64 (hi, lo, uc);

    return (sign < 0) ? -(LONG) q : (LONG) q;
}
