/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* vrport.c -- internal virtual RastPort backend for vector.image            */
/*****************************************************************************/

#include "classbase.h"

/* Every routine in this module reaches the shared library bases through     */
/* the VecVPort's stored ClassBase (vp_CB).  Redirect the library-base       */
/* macros accordingly so the SAS/C pragmas resolve a6 from vp->vp_CB rather  */
/* than a (non-existent here) local 'cb'.  Every function that issues an     */
/* exec/graphics call below has 'vp' in scope.                               */

#undef  SysBase
#undef  GfxBase
#define SysBase   (vp->vp_CB->cb_SysBase)
#define GfxBase   (vp->vp_CB->cb_GfxBase)

#define VECCODE_INSIDE  0
#define VECCODE_LEFT    1
#define VECCODE_RIGHT   2
#define VECCODE_TOP     4
#define VECCODE_BOTTOM  8

static UWORD vecvpOutCode (struct VecVPort *vp, LONG x, LONG y);
static LONG  vecvpClipLinePixels (struct VecVPort *vp,
                                  LONG *x0, LONG *y0,
                                  LONG *x1, LONG *y1);
static void  vecvpPutPixel (struct VecVPort *vp, LONG x, LONG y);
static void  vecvpFillPixelSpan (struct VecVPort *vp, LONG x0, LONG x1,
                                 LONG y);
static ULONG vecvpISqrt (ULONG value);
static void  vecvpSwapLongs (LONG *a, LONG *b);
static BOOL  vecvpSetupTmpRas (struct VecVPort *vp);
static BOOL  vecvpSetupAreaInfo (struct VecVPort *vp, ULONG vertices);
static LONG  vecvpNearestRGBTable (struct VecVPort *vp,
                                   UBYTE r, UBYTE g, UBYTE b);


/*****************************************************************************/
/* vecvpInit                                                                 */
/*****************************************************************************/

void vecvpInit (struct VecVPort *vp, struct ClassBase *cb,
                struct RastPort *rp,
                LONG destX, LONG destY, LONG destW, LONG destH,
                LONG originX, LONG originY,
                LONG scaleX, LONG scaleY, UWORD rotation)
{
    ULONG depth;

    if (!vp) return;

    memset (vp, 0, sizeof (*vp));
    vp->vp_CB = cb;
    vp->vp_RPort = rp;
    vp->vp_DestX = destX;
    vp->vp_DestY = destY;
    vp->vp_DestW = destW;
    vp->vp_DestH = destH;
    vp->vp_OriginX = originX;
    vp->vp_OriginY = originY;
    vp->vp_ScaleX = scaleX;
    vp->vp_ScaleY = scaleY;
    vp->vp_Rotation = rotation;

    if (destW < 1) vp->vp_DestW = 1;
    if (destH < 1) vp->vp_DestH = 1;

    vp->vp_Clip.MinX = (WORD) destX;
    vp->vp_Clip.MinY = (WORD) destY;
    vp->vp_Clip.MaxX = (WORD) (destX + vp->vp_DestW - 1);
    vp->vp_Clip.MaxY = (WORD) (destY + vp->vp_DestH - 1);

    /* Probe the destination depth so the colour layer knows whether to    *
     * emit direct true-colour pens or quantise to a palette index.        */
    depth = 8;
    if (rp && rp->BitMap)
        depth = GetBitMapAttr (rp->BitMap, BMA_DEPTH);
    if (depth < 1) depth = 1;
    if (depth > 255) depth = 255;
    vp->vp_Depth = (UWORD) depth;
    vp->vp_TrueColor = (depth > 8) ? TRUE : FALSE;

    /* Allocate the per-pass ARGB->pen cache on the heap.  Keeping it off the *
     * stack lets the cache be large enough to hold every gradient ramp pen   *
     * for a whole repaint (so palette pens stay owned until vecvpFree) while *
     * leaving this struct small.  On failure caching is simply disabled.     */
    vp->vp_CacheARGB  = (ULONG *) AllocVec (
                            (ULONG) VECVP_PEN_CACHE * sizeof (ULONG),
                            MEMF_ANY);
    vp->vp_CachePen   = (UBYTE *) AllocVec ((ULONG) VECVP_PEN_CACHE, MEMF_ANY);
    vp->vp_CacheOwned = (UBYTE *) AllocVec ((ULONG) VECVP_PEN_CACHE, MEMF_ANY);
    if (vp->vp_CacheARGB && vp->vp_CachePen && vp->vp_CacheOwned)
        vp->vp_CacheMax = VECVP_PEN_CACHE;
    else
        vp->vp_CacheMax = 0;
}


/*****************************************************************************/
/* vecvpFree                                                                  */
/*****************************************************************************/

void vecvpFree (struct VecVPort *vp)
{
    UWORD i;

    if (!vp) return;

    if (vp->vp_AreaActive)
        vecvpEndArea (vp);

    /* Hand back every shared pen we obtained from the destination ColorMap. */
    if (vp->vp_ColorMap && vp->vp_CacheOwned && vp->vp_CachePen)
    {
        for (i = 0; i < vp->vp_CacheCount; i++)
        {
            if (vp->vp_CacheOwned[i])
                ReleasePen (vp->vp_ColorMap, (ULONG) vp->vp_CachePen[i]);
        }
    }
    vp->vp_CacheCount = 0;

    if (vp->vp_CacheARGB)  FreeVec (vp->vp_CacheARGB);
    if (vp->vp_CachePen)   FreeVec (vp->vp_CachePen);
    if (vp->vp_CacheOwned) FreeVec (vp->vp_CacheOwned);
    vp->vp_CacheARGB  = NULL;
    vp->vp_CachePen   = NULL;
    vp->vp_CacheOwned = NULL;
    vp->vp_CacheMax   = 0;

    if (vp->vp_TmpRaster)
        FreeRaster ((PLANEPTR) vp->vp_TmpRaster,
                    (ULONG) (vp->vp_Clip.MaxX - vp->vp_Clip.MinX + 1),
                    (ULONG) (vp->vp_Clip.MaxY - vp->vp_Clip.MinY + 1));
    if (vp->vp_AreaTable)
        FreeVec (vp->vp_AreaTable);

    vp->vp_TmpRaster = NULL;
    vp->vp_AreaTable = NULL;
    vp->vp_TmpRasterSize = 0;
    vp->vp_AreaSlots = 0;
}


/*****************************************************************************/
/* Colour layer                                                              */
/*                                                                           */
/* vector.image keeps colours as 32-bit 0xAARRGGBB inside DrawAttr.  At      */
/* render time those have to become something graphics.library can draw:     */
/*                                                                           */
/*   * On true-colour destinations (depth > 8: P96 / CyberGfx / HAM8 RGB)    */
/*     we set a direct RGB pen with SetRPAttrs(RPTAG_FgColor).               */
/*                                                                           */
/*   * On palette destinations we ask the destination ColorMap for the       */
/*     closest pen via ObtainBestPen() -- the canonical Amiga nearest-colour */
/*     search -- and cache the result so each distinct colour is resolved    */
/*     (and later released) exactly once per render pass.                    */
/*                                                                           */
/*   * When there is no destination ColorMap at all (e.g. a self-allocated   */
/*     8-bit bitmap in VIMM_RENDERPIXELS) we fall back to a nearest match in */
/*     the drawing's own RGBTable, yielding sensible palette indices.        */
/*****************************************************************************/

void vecvpSetPalette (struct VecVPort *vp, struct ColorMap *cmap,
                      UBYTE *rgbTable, ULONG numColors)
{
    if (!vp) return;
    vp->vp_ColorMap = cmap;
    vp->vp_RGBTable = rgbTable;
    vp->vp_NumColors = numColors;
}


BOOL vecvpIsTrueColor (struct VecVPort *vp)
{
    return (BOOL) (vp && vp->vp_TrueColor);
}


static LONG vecvpNearestRGBTable (struct VecVPort *vp,
                                  UBYTE r, UBYTE g, UBYTE b)
{
    LONG best;
    LONG bestDist;
    LONG dr;
    LONG dg;
    LONG db;
    LONG dist;
    ULONG c;

    best = -1;
    bestDist = 0x7FFFFFFFL;

    if (!vp || !vp->vp_RGBTable || vp->vp_NumColors == 0)
        return -1;

    for (c = 0; c < vp->vp_NumColors; c++)
    {
        dr = (LONG) vp->vp_RGBTable[3 * c + 0] - (LONG) r;
        dg = (LONG) vp->vp_RGBTable[3 * c + 1] - (LONG) g;
        db = (LONG) vp->vp_RGBTable[3 * c + 2] - (LONG) b;
        dist = (dr * dr) + (dg * dg) + (db * db);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = (LONG) c;
        }
    }

    return best;
}


UBYTE vecvpPenForARGB (struct VecVPort *vp, ULONG argb)
{
    UWORD i;
    LONG  pen;
    LONG  best;
    UBYTE r;
    UBYTE g;
    UBYTE b;
    ULONG r32;
    ULONG g32;
    ULONG b32;

    if (!vp) return 1;

    /* Fully transparent maps to the background pen. */
    if ((argb & 0xFF000000UL) == 0)
        return 0;

    r = (UBYTE) ((argb >> 16) & 0xFFUL);
    g = (UBYTE) ((argb >> 8) & 0xFFUL);
    b = (UBYTE) (argb & 0xFFUL);

    if (vp->vp_CacheMax)
    {
        for (i = 0; i < vp->vp_CacheCount; i++)
        {
            if (vp->vp_CacheARGB[i] == argb)
                return vp->vp_CachePen[i];
        }
    }

    pen = -1;

    /* Only allocate a shared pen when there is a free cache slot to record   *
     * its ownership.  Obtaining a pen we cannot track would leak it and (on  *
     * a shared screen) leave palette entries reallocated after the pass --   *
     * exactly the corruption seen when repainting on a window resize.  When  *
     * the cache is full or unavailable we fall back to the drawing's own     *
     * palette, which never touches the destination ColorMap.                 */
    if (vp->vp_ColorMap && vp->vp_CacheMax &&
        vp->vp_CacheCount < vp->vp_CacheMax)
    {
        /* ObtainBestPen expects 32-bit fixed channels; replicate 8 bits. */
        r32 = ((ULONG) r << 24) | ((ULONG) r << 16) |
              ((ULONG) r << 8) | (ULONG) r;
        g32 = ((ULONG) g << 24) | ((ULONG) g << 16) |
              ((ULONG) g << 8) | (ULONG) g;
        b32 = ((ULONG) b << 24) | ((ULONG) b << 16) |
              ((ULONG) b << 8) | (ULONG) b;

        pen = (LONG) ObtainBestPen (vp->vp_ColorMap, r32, g32, b32,
                                    OBP_Precision, PRECISION_IMAGE,
                                    OBP_FailIfBad, FALSE,
                                    TAG_DONE);
    }

    if (pen >= 0)
    {
        i = vp->vp_CacheCount++;
        vp->vp_CacheARGB[i] = argb;
        vp->vp_CachePen[i] = (UBYTE) pen;
        vp->vp_CacheOwned[i] = 1;
        return (UBYTE) pen;
    }

    /* No destination ColorMap, no free cache slot, or the obtain failed:    *
     * quantise against the drawing's own palette if we have one, else fall  *
     * back to pen 1.                                                        */
    best = vecvpNearestRGBTable (vp, r, g, b);
    if (best < 0)
        best = 1;

    if (vp->vp_CacheMax && vp->vp_CacheCount < vp->vp_CacheMax)
    {
        i = vp->vp_CacheCount++;
        vp->vp_CacheARGB[i] = argb;
        vp->vp_CachePen[i] = (UBYTE) best;
        vp->vp_CacheOwned[i] = 0;
    }
    return (UBYTE) best;
}


UBYTE vecvpObtainPen (struct VecVPort *vp, ULONG argb, BOOL *ownedOut)
{
    LONG  pen;
    LONG  best;
    UBYTE r;
    UBYTE g;
    UBYTE b;
    ULONG r32;
    ULONG g32;
    ULONG b32;

    if (ownedOut) *ownedOut = FALSE;
    if (!vp) return 1;

    r = (UBYTE) ((argb >> 16) & 0xFFUL);
    g = (UBYTE) ((argb >> 8) & 0xFFUL);
    b = (UBYTE) (argb & 0xFFUL);

    if (vp->vp_ColorMap)
    {
        r32 = ((ULONG) r << 24) | ((ULONG) r << 16) |
              ((ULONG) r << 8) | (ULONG) r;
        g32 = ((ULONG) g << 24) | ((ULONG) g << 16) |
              ((ULONG) g << 8) | (ULONG) g;
        b32 = ((ULONG) b << 24) | ((ULONG) b << 16) |
              ((ULONG) b << 8) | (ULONG) b;

        pen = (LONG) ObtainBestPen (vp->vp_ColorMap, r32, g32, b32,
                                    OBP_Precision, PRECISION_IMAGE,
                                    OBP_FailIfBad, FALSE,
                                    TAG_DONE);
        if (pen >= 0)
        {
            if (ownedOut) *ownedOut = TRUE;
            return (UBYTE) pen;
        }
    }

    best = vecvpNearestRGBTable (vp, r, g, b);
    if (best < 0) best = 1;
    return (UBYTE) best;
}


void vecvpReleasePen (struct VecVPort *vp, UBYTE pen, BOOL owned)
{
    if (!vp || !owned || !vp->vp_ColorMap) return;
    ReleasePen (vp->vp_ColorMap, (ULONG) pen);
}


void vecvpSetFgARGB (struct VecVPort *vp, ULONG argb)
{
    UBYTE pen;

    if (!vp || !vp->vp_RPort) return;

    if (vp->vp_TrueColor)
    {
#ifdef RPTAG_FgColor
        /* Direct 24-bit pen.  Move/Draw/RectFill/Area all honour the        *
         * FgColor in pen-mode FALSE on true-colour boards.                  */
        SetRPAttrs (vp->vp_RPort,
                    RPTAG_PenMode, FALSE,
                    RPTAG_FgColor, argb,
                    TAG_DONE);
        SetDrMd (vp->vp_RPort, JAM1);
        return;
#endif
    }

    pen = vecvpPenForARGB (vp, argb);
    SetAPen (vp->vp_RPort, pen);
    SetDrMd (vp->vp_RPort, JAM1);
}


/*****************************************************************************/
/* vecvpMap                                                                   */
/*****************************************************************************/

void vecvpMap (struct VecVPort *vp, LONG x, LONG y, LONG *outX, LONG *outY)
{
    LONG dx;
    LONG dy;
    LONG px;
    LONG py;
    LONG halfW;
    LONG halfH;
    LONG t;

    if (!vp || !outX || !outY) return;

    dx = drwMulFixed (x - vp->vp_OriginX, vp->vp_ScaleX);
    dy = drwMulFixed (y - vp->vp_OriginY, vp->vp_ScaleY);
    px = (dx + DRW_FIXED_HALF) >> 16;
    py = (dy + DRW_FIXED_HALF) >> 16;

    if (vp->vp_Rotation)
    {
        halfW = vp->vp_DestW >> 1;
        halfH = vp->vp_DestH >> 1;
        px -= halfW;
        py -= halfH;

        switch (vp->vp_Rotation)
        {
            case 90:
                t = px;
                px = -py;
                py = t;
                t = halfW;
                halfW = halfH;
                halfH = t;
                break;

            case 180:
                px = -px;
                py = -py;
                break;

            case 270:
                t = px;
                px = py;
                py = -t;
                t = halfW;
                halfW = halfH;
                halfH = t;
                break;
        }

        px += halfW;
        py += halfH;
    }

    *outX = vp->vp_DestX + px;
    *outY = vp->vp_DestY + py;
}


/*****************************************************************************/
/* vecvpDrawLine                                                              */
/*****************************************************************************/

LONG vecvpDrawLine (struct VecVPort *vp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG px0;
    LONG py0;
    LONG px1;
    LONG py1;

    if (!vp || !vp->vp_RPort) return FALSE;

    vecvpMap (vp, x0, y0, &px0, &py0);
    vecvpMap (vp, x1, y1, &px1, &py1);

    if (!vecvpClipLinePixels (vp, &px0, &py0, &px1, &py1))
        return FALSE;

    Move (vp->vp_RPort, px0, py0);
    Draw (vp->vp_RPort, px1, py1);
    return TRUE;
}


/*****************************************************************************/
/* vecvpDrawBox / vecvpFillBox                                                */
/*****************************************************************************/

void vecvpDrawBox (struct VecVPort *vp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    vecvpDrawLine (vp, x0, y0, x1, y0);
    vecvpDrawLine (vp, x1, y0, x1, y1);
    vecvpDrawLine (vp, x1, y1, x0, y1);
    vecvpDrawLine (vp, x0, y1, x0, y0);
}


void vecvpFillBox (struct VecVPort *vp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG px0;
    LONG py0;
    LONG px1;
    LONG py1;

    if (!vp || !vp->vp_RPort) return;

    vecvpMap (vp, x0, y0, &px0, &py0);
    vecvpMap (vp, x1, y1, &px1, &py1);

    if (px0 > px1) vecvpSwapLongs (&px0, &px1);
    if (py0 > py1) vecvpSwapLongs (&py0, &py1);

    if (px1 < vp->vp_Clip.MinX || px0 > vp->vp_Clip.MaxX) return;
    if (py1 < vp->vp_Clip.MinY || py0 > vp->vp_Clip.MaxY) return;

    if (px0 < vp->vp_Clip.MinX) px0 = vp->vp_Clip.MinX;
    if (px1 > vp->vp_Clip.MaxX) px1 = vp->vp_Clip.MaxX;
    if (py0 < vp->vp_Clip.MinY) py0 = vp->vp_Clip.MinY;
    if (py1 > vp->vp_Clip.MaxY) py1 = vp->vp_Clip.MaxY;

    RectFill (vp->vp_RPort, px0, py0, px1, py1);
}


/*****************************************************************************/
/* vecvpDrawEllipse / vecvpFillEllipse                                        */
/*****************************************************************************/

void vecvpDrawEllipse (struct VecVPort *vp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG px0;
    LONG py0;
    LONG px1;
    LONG py1;
    LONG cx;
    LONG cy;
    LONG rx;
    LONG ry;
    LONG scanY;
    LONG spanX;
    ULONG rem;
    ULONG root;
    ULONG ry2;
    LONG y;

    if (!vp || !vp->vp_RPort) return;

    vecvpMap (vp, x0, y0, &px0, &py0);
    vecvpMap (vp, x1, y1, &px1, &py1);
    if (px0 > px1) vecvpSwapLongs (&px0, &px1);
    if (py0 > py1) vecvpSwapLongs (&py0, &py1);

    cx = (px0 + px1) >> 1;
    cy = (py0 + py1) >> 1;
    rx = (px1 - px0) >> 1;
    ry = (py1 - py0) >> 1;
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;

    if (rx > 32767 || ry > 32767)
        return;

    ry2 = (ULONG) (ry * ry);
    for (scanY = -ry; scanY <= ry; scanY++)
    {
        y = scanY;
        rem = ry2 - (ULONG) (y * y);
        root = vecvpISqrt (rem);
        spanX = (ry > 0) ? (LONG) (((ULONG) rx * root) / (ULONG) ry) : 0;
        vecvpPutPixel (vp, cx - spanX, cy + scanY);
        vecvpPutPixel (vp, cx + spanX, cy + scanY);
    }
}


void vecvpFillEllipse (struct VecVPort *vp, LONG x0, LONG y0, LONG x1, LONG y1)
{
    LONG px0;
    LONG py0;
    LONG px1;
    LONG py1;
    LONG cx;
    LONG cy;
    LONG rx;
    LONG ry;
    LONG scanY;
    LONG spanX;
    ULONG rem;
    ULONG root;
    ULONG ry2;
    LONG y;

    if (!vp || !vp->vp_RPort) return;

    vecvpMap (vp, x0, y0, &px0, &py0);
    vecvpMap (vp, x1, y1, &px1, &py1);
    if (px0 > px1) vecvpSwapLongs (&px0, &px1);
    if (py0 > py1) vecvpSwapLongs (&py0, &py1);

    cx = (px0 + px1) >> 1;
    cy = (py0 + py1) >> 1;
    rx = (px1 - px0) >> 1;
    ry = (py1 - py0) >> 1;
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;

    if (rx > 32767 || ry > 32767)
        return;

    ry2 = (ULONG) (ry * ry);
    for (scanY = -ry; scanY <= ry; scanY++)
    {
        y = scanY;
        rem = ry2 - (ULONG) (y * y);
        root = vecvpISqrt (rem);
        spanX = (ry > 0) ? (LONG) (((ULONG) rx * root) / (ULONG) ry) : 0;
        vecvpFillPixelSpan (vp, cx - spanX, cx + spanX, cy + scanY);
    }
}


/*****************************************************************************/
/* Area wrappers                                                              */
/*****************************************************************************/

BOOL vecvpBeginArea (struct VecVPort *vp, ULONG vertices)
{
    if (!vp || !vp->vp_RPort) return FALSE;
    if (!vecvpSetupTmpRas (vp)) return FALSE;
    if (!vecvpSetupAreaInfo (vp, vertices)) return FALSE;

    vp->vp_OldTmpRas = vp->vp_RPort->TmpRas;
    vp->vp_OldAreaInfo = vp->vp_RPort->AreaInfo;
    vp->vp_RPort->TmpRas = &vp->vp_TmpRas;
    vp->vp_RPort->AreaInfo = &vp->vp_AreaInfo;
    InitArea (&vp->vp_AreaInfo, vp->vp_AreaTable, vertices);
    vp->vp_AreaActive = TRUE;
    return TRUE;
}


void vecvpAreaMove (struct VecVPort *vp, LONG x, LONG y)
{
    LONG px;
    LONG py;

    if (!vp || !vp->vp_AreaActive) return;
    vecvpMap (vp, x, y, &px, &py);
    AreaMove (vp->vp_RPort, px, py);
}


void vecvpAreaDraw (struct VecVPort *vp, LONG x, LONG y)
{
    LONG px;
    LONG py;

    if (!vp || !vp->vp_AreaActive) return;
    vecvpMap (vp, x, y, &px, &py);
    AreaDraw (vp->vp_RPort, px, py);
}


void vecvpEndArea (struct VecVPort *vp)
{
    if (!vp || !vp->vp_AreaActive) return;

    AreaEnd (vp->vp_RPort);
    vp->vp_RPort->TmpRas = vp->vp_OldTmpRas;
    vp->vp_RPort->AreaInfo = vp->vp_OldAreaInfo;
    vp->vp_OldTmpRas = NULL;
    vp->vp_OldAreaInfo = NULL;
    vp->vp_AreaActive = FALSE;
}


/*****************************************************************************/
/* Internal clipping helpers                                                  */
/*****************************************************************************/

static UWORD vecvpOutCode (struct VecVPort *vp, LONG x, LONG y)
{
    UWORD code;

    code = VECCODE_INSIDE;
    if (x < vp->vp_Clip.MinX) code |= VECCODE_LEFT;
    else if (x > vp->vp_Clip.MaxX) code |= VECCODE_RIGHT;
    if (y < vp->vp_Clip.MinY) code |= VECCODE_TOP;
    else if (y > vp->vp_Clip.MaxY) code |= VECCODE_BOTTOM;
    return code;
}


static LONG vecvpClipLinePixels (struct VecVPort *vp,
                                 LONG *x0, LONG *y0, LONG *x1, LONG *y1)
{
    UWORD code0;
    UWORD code1;
    UWORD codeOut;
    LONG x;
    LONG y;
    LONG dx;
    LONG dy;
    LONG accept;
    WORD guard;

    accept = FALSE;

    /* Cohen-Sutherland clips at most one endpoint coordinate to an exact   *
     * clip boundary per pass, so a correct run converges in <= 4 passes.   *
     * Integer intersection arithmetic on coordinates far outside the clip  *
     * box (or a 32-bit overflow of dx*(bound-y)) can leave a truncated     *
     * endpoint still outside, making the outcodes ping-pong between two    *
     * edges forever.  Bound the loop so such a segment is rejected rather  *
     * than hanging the whole render (and thus MultiView).                  */
    for (guard = 0; guard < 16; guard++)
    {
        code0 = vecvpOutCode (vp, *x0, *y0);
        code1 = vecvpOutCode (vp, *x1, *y1);

        if (!(code0 | code1))
        {
            accept = TRUE;
            break;
        }
        if (code0 & code1)
            break;

        codeOut = code0 ? code0 : code1;
        dx = *x1 - *x0;
        dy = *y1 - *y0;

        if (codeOut & VECCODE_BOTTOM)
        {
            if (dy == 0) return FALSE;
            x = *x0 + (dx * (vp->vp_Clip.MaxY - *y0)) / dy;
            y = vp->vp_Clip.MaxY;
        }
        else if (codeOut & VECCODE_TOP)
        {
            if (dy == 0) return FALSE;
            x = *x0 + (dx * (vp->vp_Clip.MinY - *y0)) / dy;
            y = vp->vp_Clip.MinY;
        }
        else if (codeOut & VECCODE_RIGHT)
        {
            if (dx == 0) return FALSE;
            y = *y0 + (dy * (vp->vp_Clip.MaxX - *x0)) / dx;
            x = vp->vp_Clip.MaxX;
        }
        else
        {
            if (dx == 0) return FALSE;
            y = *y0 + (dy * (vp->vp_Clip.MinX - *x0)) / dx;
            x = vp->vp_Clip.MinX;
        }

        if (codeOut == code0)
        {
            *x0 = x;
            *y0 = y;
        }
        else
        {
            *x1 = x;
            *y1 = y;
        }
    }

    return accept;
}


static void vecvpPutPixel (struct VecVPort *vp, LONG x, LONG y)
{
    if (x < vp->vp_Clip.MinX || x > vp->vp_Clip.MaxX) return;
    if (y < vp->vp_Clip.MinY || y > vp->vp_Clip.MaxY) return;
    WritePixel (vp->vp_RPort, x, y);
}


static void vecvpFillPixelSpan (struct VecVPort *vp, LONG x0, LONG x1, LONG y)
{
    if (y < vp->vp_Clip.MinY || y > vp->vp_Clip.MaxY) return;
    if (x0 > x1) vecvpSwapLongs (&x0, &x1);
    if (x1 < vp->vp_Clip.MinX || x0 > vp->vp_Clip.MaxX) return;
    if (x0 < vp->vp_Clip.MinX) x0 = vp->vp_Clip.MinX;
    if (x1 > vp->vp_Clip.MaxX) x1 = vp->vp_Clip.MaxX;
    RectFill (vp->vp_RPort, x0, y, x1, y);
}


static ULONG vecvpISqrt (ULONG value)
{
    ULONG root;
    ULONG bit;
    ULONG trial;

    root = 0;
    bit = 1UL << 30;

    while (bit > value)
        bit >>= 2;

    while (bit != 0)
    {
        trial = root + bit;
        if (value >= trial)
        {
            value -= trial;
            root = (root >> 1) + bit;
        }
        else
            root >>= 1;
        bit >>= 2;
    }

    return root;
}


static void vecvpSwapLongs (LONG *a, LONG *b)
{
    LONG t;

    t = *a;
    *a = *b;
    *b = t;
}


static BOOL vecvpSetupTmpRas (struct VecVPort *vp)
{
    ULONG width;
    ULONG height;
    ULONG size;

    if (vp->vp_TmpRaster)
        return TRUE;
    if (!vp->vp_RPort || !vp->vp_RPort->BitMap)
        return FALSE;

    width = (ULONG) (vp->vp_Clip.MaxX - vp->vp_Clip.MinX + 1);
    height = (ULONG) (vp->vp_Clip.MaxY - vp->vp_Clip.MinY + 1);
    if (width < 1 || height < 1)
        return FALSE;

    size = RASSIZE (width, height);
    vp->vp_TmpRaster = AllocRaster (width, height);
    if (!vp->vp_TmpRaster)
        return FALSE;

    vp->vp_TmpRasterSize = size;
    InitTmpRas (&vp->vp_TmpRas, (PLANEPTR) vp->vp_TmpRaster, size);
    return TRUE;
}


static BOOL vecvpSetupAreaInfo (struct VecVPort *vp, ULONG vertices)
{
    ULONG bytes;

    if (vertices < 3)
        vertices = 3;
    if (vp->vp_AreaTable && vp->vp_AreaSlots >= vertices)
        return TRUE;

    if (vp->vp_AreaTable)
    {
        FreeVec (vp->vp_AreaTable);
        vp->vp_AreaTable = NULL;
        vp->vp_AreaSlots = 0;
    }

    bytes = (vertices * 5) + 4;
    vp->vp_AreaTable = AllocVec (bytes, MEMF_ANY | MEMF_CLEAR);
    if (!vp->vp_AreaTable)
        return FALSE;

    vp->vp_AreaSlots = vertices;
    return TRUE;
}
