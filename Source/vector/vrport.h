/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* vrport.h -- internal virtual RastPort backend for vector.image            */
/*****************************************************************************/

#ifndef VECTOR_VRPORT_H
#define VECTOR_VRPORT_H

struct VecPoint32
{
    LONG vp_X;
    LONG vp_Y;
};

/* Number of distinct ARGB->pen results cached per render pass.  Palette      *
 * gradients draw with up to 32 ramp colours per object; files such as        *
 * Bubbles.dr2d combine a full-page gradient with many radial highlights, so  *
 * the cache must be large enough to keep those pens owned until the repaint  *
 * finishes.  Releasing a ramp immediately after an object lets later objects *
 * reuse/recolour pens that earlier pixels still reference on 8-bit screens.  */
#define VECVP_PEN_CACHE   256

struct VecVPort
{
    struct ClassBase *vp_CB;
    struct RastPort  *vp_RPort;
    struct Rectangle  vp_Clip;

    LONG              vp_DestX;
    LONG              vp_DestY;
    LONG              vp_DestW;
    LONG              vp_DestH;
    LONG              vp_OriginX;
    LONG              vp_OriginY;
    LONG              vp_ScaleX;
    LONG              vp_ScaleY;
    UWORD             vp_Rotation;

    struct TmpRas     vp_TmpRas;
    struct AreaInfo   vp_AreaInfo;
    APTR              vp_TmpRaster;
    APTR              vp_AreaTable;
    ULONG             vp_TmpRasterSize;
    ULONG             vp_AreaSlots;

    struct TmpRas    *vp_OldTmpRas;
    struct AreaInfo  *vp_OldAreaInfo;
    BOOL              vp_AreaActive;

    /* Colour context.  vp_ColorMap (if set) drives ObtainBestPen() nearest   *
     * matching for palette-mapped destinations.  vp_RGBTable is the          *
     * drawing's own palette, used when no destination ColorMap is available  *
     * (e.g. self-rendered 8-bit bitmaps).  vp_Depth/vp_TrueColor decide      *
     * whether to emit a direct RGB pen or a quantised palette index.         */
    struct ColorMap  *vp_ColorMap;
    UBYTE            *vp_RGBTable;
    ULONG             vp_NumColors;
    UWORD             vp_Depth;
    BOOL              vp_TrueColor;

    /* The pen cache is heap-allocated (see vecvpInit) rather than embedded    *
     * so a large VECVP_PEN_CACHE does not inflate this stack-resident struct  *
     * -- an oversized struct here previously overran the render call stack    *
     * and corrupted the exec memory list.  vp_CacheMax is the allocated slot  *
     * count (0 when the allocation failed and caching is disabled).           */
    ULONG            *vp_CacheARGB;
    UBYTE            *vp_CachePen;
    UBYTE            *vp_CacheOwned;
    UWORD             vp_CacheCount;
    UWORD             vp_CacheMax;
};

void  vecvpInit      (struct VecVPort *vp, struct ClassBase *cb,
                      struct RastPort *rp,
                      LONG destX, LONG destY, LONG destW, LONG destH,
                      LONG originX, LONG originY,
                      LONG scaleX, LONG scaleY, UWORD rotation);
void  vecvpFree      (struct VecVPort *vp);
void  vecvpSetPalette (struct VecVPort *vp, struct ColorMap *cmap,
                      UBYTE *rgbTable, ULONG numColors);
UBYTE vecvpPenForARGB (struct VecVPort *vp, ULONG argb);
void  vecvpSetFgARGB (struct VecVPort *vp, ULONG argb);
BOOL  vecvpIsTrueColor (struct VecVPort *vp);

/* Obtain a pen for argb WITHOUT using the per-pass pen cache.  Kept for      *
 * callers that need short-lived temporary pens; visible drawing should prefer*
 * vecvpPenForARGB() so palette pens stay owned through the render pass.      *
 * *ownedOut is TRUE when the returned pen came from ObtainBestPen() and the  *
 * caller must hand it back with vecvpReleasePen().                           */
UBYTE vecvpObtainPen  (struct VecVPort *vp, ULONG argb, BOOL *ownedOut);
void  vecvpReleasePen (struct VecVPort *vp, UBYTE pen, BOOL owned);
void  vecvpMap       (struct VecVPort *vp, LONG x, LONG y,
                      LONG *outX, LONG *outY);
LONG  vecvpDrawLine  (struct VecVPort *vp, LONG x0, LONG y0,
                      LONG x1, LONG y1);
void  vecvpDrawBox   (struct VecVPort *vp, LONG x0, LONG y0,
                      LONG x1, LONG y1);
void  vecvpFillBox   (struct VecVPort *vp, LONG x0, LONG y0,
                      LONG x1, LONG y1);
void  vecvpDrawEllipse (struct VecVPort *vp, LONG x0, LONG y0,
                        LONG x1, LONG y1);
void  vecvpFillEllipse (struct VecVPort *vp, LONG x0, LONG y0,
                        LONG x1, LONG y1);
BOOL  vecvpBeginArea (struct VecVPort *vp, ULONG vertices);
void  vecvpAreaMove  (struct VecVPort *vp, LONG x, LONG y);
void  vecvpAreaDraw  (struct VecVPort *vp, LONG x, LONG y);
void  vecvpEndArea   (struct VecVPort *vp);

#endif /* VECTOR_VRPORT_H */
