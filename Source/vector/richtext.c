/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* richtext.c -- ProVector embedded text command parser                       */
/*****************************************************************************/

#include "classbase.h"
#include "richtext.h"


struct PVTextState
{
    UWORD   pts_FontID;
    UWORD   pts_StyleFlags;
    ULONG   pts_FillColor;
    LONG    pts_PointSize;
    LONG    pts_DX;
    LONG    pts_DY;
    LONG    pts_Rotation;
    LONG    pts_Slant;
    LONG    pts_Aspect;
    STRPTR  pts_FontName;
};


static BOOL pvIsAlpha (UBYTE c)
{
    return (BOOL) ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}


static BOOL pvIsDigit (UBYTE c)
{
    return (BOOL) (c >= '0' && c <= '9');
}


static LONG pvFixedFromArg (STRPTR arg, UWORD len)
{
    LONG sign;
    LONG value;
    LONG frac;
    LONG scale;
    UWORD i;

    sign = 1;
    value = 0;
    frac = 0;
    scale = 1;
    i = 0;

    if (!arg || len == 0) return 0;
    if (arg[i] == '-')
    {
        sign = -1;
        i++;
    }
    else if (arg[i] == '+')
    {
        i++;
    }

    while (i < len && pvIsDigit ((UBYTE) arg[i]))
    {
        value = (value * 10) + (LONG) (arg[i] - '0');
        i++;
    }

    if (i < len && arg[i] == '.')
    {
        i++;
        while (i < len && pvIsDigit ((UBYTE) arg[i]) && scale < 10000L)
        {
            frac = (frac * 10) + (LONG) (arg[i] - '0');
            scale *= 10;
            i++;
        }
    }

    value = (value << 16) + ((frac << 16) / scale);
    return sign < 0 ? -value : value;
}


static LONG pvLongFromArg (STRPTR arg, UWORD len)
{
    LONG sign;
    LONG value;
    UWORD i;

    sign = 1;
    value = 0;
    i = 0;

    if (!arg || len == 0) return 0;
    if (arg[i] == '-')
    {
        sign = -1;
        i++;
    }
    else if (arg[i] == '+')
    {
        i++;
    }

    while (i < len && pvIsDigit ((UBYTE) arg[i]))
    {
        value = (value * 10) + (LONG) (arg[i] - '0');
        i++;
    }

    return sign < 0 ? -value : value;
}


static UBYTE pvHexNibble (UBYTE c)
{
    if (c >= '0' && c <= '9') return (UBYTE) (c - '0');
    if (c >= 'a' && c <= 'f') return (UBYTE) (c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (UBYTE) (c - 'A' + 10);
    return 0;
}


static ULONG pvARGBFromHex (STRPTR arg, UWORD len)
{
    ULONG rgb;
    UWORD i;

    rgb = 0;
    if (!arg) return DDTA_TRANSPARENT;
    if (len > 0 && arg[0] == '#')
    {
        arg++;
        len--;
    }
    if (len < 6) return DDTA_TRANSPARENT;

    for (i = 0; i < 6; i++)
        rgb = (rgb << 4) | (ULONG) pvHexNibble ((UBYTE) arg[i]);

    return 0xFF000000UL | rgb;
}


static ULONG pvARGBFromIndex (struct localData *lod, LONG idx)
{
    ULONG n;
    UBYTE *rgb;

    if (!lod || !lod->lvi_RGBTable || idx < 0) return DDTA_TRANSPARENT;
    n = (ULONG) idx;
    if (n >= lod->lvi_NumPaletteColors) return DDTA_TRANSPARENT;

    rgb = lod->lvi_RGBTable + (n * 3);
    return 0xFF000000UL | ((ULONG) rgb[0] << 16) |
           ((ULONG) rgb[1] << 8) | (ULONG) rgb[2];
}


static STRPTR pvCopyArg (struct ClassBase *cb, struct localData *lod,
                         STRPTR arg, UWORD len)
{
    STRPTR out;
    UWORD start;
    UWORD end;
    UWORD n;

    if (!arg) return NULL;

    start = 0;
    end = len;
    if (end > start && arg[start] == '"') start++;
    if (end > start && arg[end - 1] == '"') end--;
    n = (UWORD) (end - start);

    out = (STRPTR) poolAlloc (cb, lod, (ULONG) n + 1);
    if (!out) return NULL;
    if (n > 0) CopyMem ((APTR) (arg + start), (APTR) out, (ULONG) n);
    out[n] = '\0';
    return out;
}


static BOOL pvEmitRun (struct ClassBase *cb, struct localData *lod,
                       struct DrawTextRun *runs, UWORD *runCount,
                       struct PVTextState *state,
                       ULONG start, ULONG outPos)
{
    struct DrawTextRun *run;

    (void) cb;
    (void) lod;

    if (outPos <= start) return TRUE;

    run = runs + *runCount;
    run->tr_Offset = start;
    run->tr_Length = outPos - start;
    run->tr_FontID = state->pts_FontID;
    run->tr_StyleFlags = state->pts_StyleFlags;
    run->tr_FillColor = state->pts_FillColor;
    run->tr_PointSize = state->pts_PointSize;
    run->tr_DX = state->pts_DX;
    run->tr_DY = state->pts_DY;
    run->tr_Rotation = state->pts_Rotation;
    run->tr_Slant = state->pts_Slant;
    run->tr_Aspect = state->pts_Aspect;
    run->tr_FontName = state->pts_FontName;

    state->pts_DX = 0;
    state->pts_DY = 0;
    (*runCount)++;
    return TRUE;
}


static void pvApplyCommand (struct localData *lod, struct PVTextState *state,
                            UBYTE c0, UBYTE c1, STRPTR arg, UWORD argLen)
{
    LONG idx;
    LONG upp;

    idx = pvLongFromArg (arg, argLen);

    /* Point size and inter-character motion are typographic points; convert *
     * them to the drawing's canvas units so styled text scales with the     *
     * surrounding geometry instead of being 72x too large on inch drawings. */
    upp = lod ? lod->lvi_UnitsPerPoint : (DRW_FIXED_ONE / 72);
    if (upp <= 0) upp = DRW_FIXED_ONE / 72;

    if (c0 == 'p' && c1 == 's')
        state->pts_PointSize = drwMulFixed (pvFixedFromArg (arg, argLen), upp);
    else if (c0 == 'h' && c1 == 'm')
        state->pts_DX += drwMulFixed (pvFixedFromArg (arg, argLen), upp);
    else if (c0 == 'v' && c1 == 'm')
        state->pts_DY += drwMulFixed (pvFixedFromArg (arg, argLen), upp);
    else if (c0 == 'r' && c1 == 'o')
        state->pts_Rotation = pvFixedFromArg (arg, argLen);
    else if (c0 == 's' && c1 == 'l')
        state->pts_Slant = pvFixedFromArg (arg, argLen);
    else if (c0 == 'a' && c1 == 'r')
        state->pts_Aspect = pvFixedFromArg (arg, argLen);
    else if (c0 == 'f' && c1 == 'v')
        state->pts_FillColor = pvARGBFromIndex (lod, idx);
    else if (c0 == 'f' && c1 == 'c')
        state->pts_FillColor = pvARGBFromHex (arg, argLen);
    else if (c0 == 'f' && c1 == 's')
        (void) idx;  /* Fill style.  The current renderer supports colour
                      * fills for text, so the style index is consumed. */
    else if (c0 == 'h' && c1 == 's')
        (void) idx;  /* Parsed and consumed; graphics.library has no run
                      * level tracking adjustment. */
    else if (c0 == 'w' && c1 == 's')
        (void) idx;
    else if (c0 == 'e' && (c1 == 's' || c1 == 'v' || c1 == 'w' || c1 == 'c'))
        (void) idx;
}


BOOL parseProVectorText (struct ClassBase *cb, struct localData *lod,
                         struct DrawObj *obj)
{
    STRPTR src;
    STRPTR clean;
    UWORD len;
    UWORD i;
    UWORD cmdStart;
    UWORD argStart;
    UWORD argLen;
    UWORD cmdLen;
    UWORD runCount;
    ULONG outPos;
    ULONG runStart;
    ULONG charNo;
    struct DrawTextRun *runs;
    struct PVTextState state;
    UBYTE c0;
    UBYTE c1;

    if (!lod || !obj) return FALSE;

    if (obj->do_Type == DROT_TEXT)
    {
        src = obj->do_Data.Text.do_String;
        len = obj->do_Data.Text.do_NumChars;
        state.pts_FontID = obj->do_Data.Text.do_WhichFont;
    }
    else if (obj->do_Type == DROT_TEXTPATH)
    {
        src = obj->do_Data.TextPath.do_String;
        len = obj->do_Data.TextPath.do_NumChars;
        state.pts_FontID = obj->do_Data.TextPath.do_WhichFont;
    }
    else
    {
        return TRUE;
    }

    if (!src || len == 0) return TRUE;

    clean = (STRPTR) poolAlloc (cb, lod, (ULONG) len + 1);
    runs = (struct DrawTextRun *) poolAllocClear (cb, lod,
            ((ULONG) len + 1) * sizeof (struct DrawTextRun));
    if (!clean || !runs) return FALSE;

    state.pts_StyleFlags = 0;
    state.pts_FillColor = DDTA_TRANSPARENT;
    state.pts_PointSize = 0;
    state.pts_DX = 0;
    state.pts_DY = 0;
    state.pts_Rotation = 0;
    state.pts_Slant = 0;
    state.pts_Aspect = DRW_FIXED_ONE;
    state.pts_FontName = NULL;

    i = 0;
    outPos = 0;
    runStart = 0;
    runCount = 0;

    while (i < len)
    {
        if (src[i] == '\0')
            break;

        if (src[i] != '\\')
        {
            clean[outPos++] = src[i++];
            continue;
        }

        if (i + 1 < len && src[i + 1] == '\\')
        {
            clean[outPos++] = '\\';
            i += 2;
            continue;
        }

        cmdStart = (UWORD) (i + 1);
        cmdLen = 0;
        while ((cmdStart + cmdLen) < len &&
               pvIsAlpha ((UBYTE) src[cmdStart + cmdLen]))
            cmdLen++;

        if (cmdLen == 0)
        {
            clean[outPos++] = src[i++];
            continue;
        }

        c0 = (UBYTE) src[cmdStart];
        c1 = (cmdLen > 1) ? (UBYTE) src[cmdStart + 1] : 0;
        i = (UWORD) (cmdStart + cmdLen);
        argStart = i;
        argLen = 0;

        if (i < len && src[i] == '(')
        {
            i++;
            argStart = i;
            while (i < len && src[i] != ')')
                i++;
            argLen = (UWORD) (i - argStart);
            if (i < len && src[i] == ')') i++;
        }

        if (!pvEmitRun (cb, lod, runs, &runCount, &state, runStart, outPos))
            return FALSE;
        runStart = outPos;

        if (c0 == 'n' && c1 == 'l')
        {
            clean[outPos++] = '\n';
            continue;
        }
        if (c0 == 'c' && c1 == 'n')
        {
            charNo = (ULONG) pvLongFromArg (src + argStart, argLen);
            if (charNo >= 24 && charNo <= 255)
                clean[outPos++] = (UBYTE) charNo;
            continue;
        }
        if (c0 == 'f' && c1 == 'n')
        {
            state.pts_FontName = pvCopyArg (cb, lod, src + argStart, argLen);
            continue;
        }

        pvApplyCommand (lod, &state, c0, c1, src + argStart, argLen);
    }

    if (!pvEmitRun (cb, lod, runs, &runCount, &state, runStart, outPos))
        return FALSE;

    clean[outPos] = '\0';

    if (obj->do_Type == DROT_TEXT)
    {
        obj->do_Data.Text.do_String = clean;
        obj->do_Data.Text.do_NumChars = (UWORD) outPos;
        obj->do_Data.Text.do_Runs = runs;
        obj->do_Data.Text.do_NumRuns = runCount;
    }
    else
    {
        obj->do_Data.TextPath.do_String = clean;
        obj->do_Data.TextPath.do_NumChars = (UWORD) outPos;
        obj->do_Data.TextPath.do_Runs = runs;
        obj->do_Data.TextPath.do_NumRuns = runCount;
    }

    return TRUE;
}
