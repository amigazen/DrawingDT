/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* cache.c -- bitmap cache and IM_DRAW for vector.image                      */
/*****************************************************************************/

#include "classbase.h"


static LONG frameScaleForExtent (LONG pixels, LONG extent)
{
    LONG units;
    LONG scale;

    units = DRW_FIXED_TO_INT (extent);
    if (units < 1) units = 1;
    if (pixels < 1) pixels = 1;

    scale = (LONG) (((ULONG) pixels << 16) / (ULONG) units);
    if (scale < 1) scale = 1;
    return scale;
}


void freeVectorCache (struct ClassBase *cb, struct localData *lod)
{
    struct BitMap *bm;

    (void) cb;

    if (!lod) return;

    bm = lod->lvi_CachedBM;
    if (bm)
    {
        FreeBitMap (bm);
        lod->lvi_CachedBM = NULL;
    }
    lod->lvi_CacheW = 0;
    lod->lvi_CacheH = 0;
}


ULONG rebuildCache (struct ClassBase *cb, Class *cl, Object *o)
{
    struct localData *lod;
    struct Image *im;
    struct BitMap *bm;
    struct RastPort rp;
    struct vecRender rmsg;
    UWORD w;
    UWORD h;

    lod = INST_DATA (cl, o);
    im  = (struct Image *) o;

    w = im->Width;
    h = im->Height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    if (!(lod->lvi_Flags & LVIF_CACHE_DIRTY) &&
        lod->lvi_CachedBM &&
        lod->lvi_CacheW == w &&
        lod->lvi_CacheH == h)
        return 1;

    freeVectorCache (cb, lod);

    if (GfxBase && GfxBase->lib_Version >= 39)
        bm = AllocBitMap (w, h, 8, BMF_CLEAR, NULL);
    else
        bm = NULL;

    if (!bm) return 0;

    InitRastPort (&rp);
    rp.BitMap = bm;

    rmsg.MethodID         = VIMM_RENDER;
    rmsg.vecr_RPort       = &rp;
    rmsg.vecr_Left        = 0;
    rmsg.vecr_Top         = 0;
    rmsg.vecr_Width       = (LONG) w;
    rmsg.vecr_Height      = (LONG) h;
    rmsg.vecr_FixedScaleX = lod->lvi_FixedScale;
    rmsg.vecr_FixedScaleY = lod->lvi_FixedScale;
    rmsg.vecr_Rotation    = (UWORD) lod->lvi_Rotation;
    rmsg.vecr_RenderQuality = (UWORD) lod->lvi_RenderQuality;
    rmsg.vecr_RenderFlags = lod->lvi_RenderFlags;
    rmsg.vecr_AbortPtr    = NULL;
    rmsg.vecr_ProgressPtr = NULL;
    rmsg.vecr_ColorMap    = lod->lvi_ColorMap;

    renderVectorMethod (cb, cl, o, &rmsg);

    lod->lvi_CachedBM = bm;
    lod->lvi_CacheW   = w;
    lod->lvi_CacheH   = h;
    im->ImageData     = (UWORD *) bm;
    lod->lvi_Flags   &= ~LVIF_CACHE_DIRTY;

    return 1;
}


ULONG vectorIMDrawFrame (struct ClassBase *cb, Class *cl, Object *o,
                         struct impDraw *msg)
{
    struct localData *lod;
    struct Image *im;
    struct vecRender rmsg;
    LONG x;
    LONG y;
    LONG w;
    LONG h;
    LONG canvasW;
    LONG canvasH;

    if (!msg || !msg->imp_RPort) return 0;

    lod = INST_DATA (cl, o);
    im  = (struct Image *) o;

    w = (LONG) msg->imp_Dimensions.Width;
    h = (LONG) msg->imp_Dimensions.Height;
    if (w < 1) w = (LONG) im->Width;
    if (h < 1) h = (LONG) im->Height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    x = (LONG) im->LeftEdge + (LONG) msg->imp_Offset.X;
    y = (LONG) im->TopEdge  + (LONG) msg->imp_Offset.Y;

    canvasW = lod->lvi_CanvasRight - lod->lvi_CanvasLeft;
    canvasH = lod->lvi_CanvasBottom - lod->lvi_CanvasTop;

    rmsg.MethodID           = VIMM_RENDER;
    rmsg.vecr_RPort         = msg->imp_RPort;
    rmsg.vecr_Left          = x;
    rmsg.vecr_Top           = y;
    rmsg.vecr_Width         = w;
    rmsg.vecr_Height        = h;
    rmsg.vecr_FixedScaleX   = frameScaleForExtent (w, canvasW);
    rmsg.vecr_FixedScaleY   = frameScaleForExtent (h, canvasH);
    rmsg.vecr_Rotation      = (UWORD) lod->lvi_Rotation;
    rmsg.vecr_RenderQuality = (UWORD) lod->lvi_RenderQuality;
    rmsg.vecr_RenderFlags   = lod->lvi_RenderFlags;
    rmsg.vecr_AbortPtr      = NULL;
    rmsg.vecr_ProgressPtr   = NULL;
    rmsg.vecr_ColorMap      = lod->lvi_ColorMap;

    return renderVectorMethod (cb, cl, o, &rmsg);
}


ULONG vectorIMErase (struct ClassBase *cb, Class *cl, Object *o,
                     struct impErase *msg)
{
    struct Image *im;
    LONG x;
    LONG y;
    LONG w;
    LONG h;

    (void) cb;
    (void) cl;

    if (!msg || !msg->imp_RPort) return 0;

    im = (struct Image *) o;

    w = (msg->MethodID == IM_ERASEFRAME)
            ? (LONG) msg->imp_Dimensions.Width
            : (LONG) im->Width;
    h = (msg->MethodID == IM_ERASEFRAME)
            ? (LONG) msg->imp_Dimensions.Height
            : (LONG) im->Height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    x = (LONG) im->LeftEdge + (LONG) msg->imp_Offset.X;
    y = (LONG) im->TopEdge  + (LONG) msg->imp_Offset.Y;

    EraseRect (msg->imp_RPort, x, y, x + w - 1, y + h - 1);
    return TRUE;
}


ULONG vectorIMHitTest (struct ClassBase *cb, Class *cl, Object *o,
                       struct impHitTest *msg)
{
    struct Image *im;
    LONG x;
    LONG y;
    LONG w;
    LONG h;

    (void) cb;
    (void) cl;

    if (!msg) return 0;

    im = (struct Image *) o;

    x = (LONG) im->LeftEdge;
    y = (LONG) im->TopEdge;
    w = (msg->MethodID == IM_HITFRAME)
            ? (LONG) msg->imp_Dimensions.Width
            : (LONG) im->Width;
    h = (msg->MethodID == IM_HITFRAME)
            ? (LONG) msg->imp_Dimensions.Height
            : (LONG) im->Height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    if ((LONG) msg->imp_Point.X < x) return FALSE;
    if ((LONG) msg->imp_Point.Y < y) return FALSE;
    if ((LONG) msg->imp_Point.X >= x + w) return FALSE;
    if ((LONG) msg->imp_Point.Y >= y + h) return FALSE;
    return TRUE;
}


ULONG vectorIMDraw (struct ClassBase *cb, Class *cl, Object *o,
                    struct impDraw *msg)
{
    struct localData *lod;
    struct Image *im;
    ULONG bltMode;

    if (!msg || !msg->imp_RPort) return 0;

    lod = INST_DATA (cl, o);
    im  = (struct Image *) o;

    if (!rebuildCache (cb, cl, o)) return 0;
    if (!lod->lvi_CachedBM) return 0;

    if (msg->imp_State == IDS_SELECTED)
        bltMode = 0x030;
    else
        bltMode = 0x0C0;

    BltBitMapRastPort (lod->lvi_CachedBM,
                       0, 0,
                       msg->imp_RPort,
                       im->LeftEdge + msg->imp_Offset.X,
                       im->TopEdge + msg->imp_Offset.Y,
                       im->Width, im->Height,
                       bltMode);

    return TRUE;
}


ULONG vectorIMDomainFrame (struct ClassBase *cb, Class *cl, Object *o,
                           struct impDomainFrame *msg)
{
    struct Image *im;

    (void) cb;
    (void) cl;

    if (!msg) return 0;

    im = (struct Image *) o;

    switch (msg->imp_Which)
    {
        case IDOMAIN_NOMINAL:
            msg->imp_Domain.Width = im->Width;
            msg->imp_Domain.Height = im->Height;
            return 1;
        case IDOMAIN_MINIMUM:
            msg->imp_Domain.Width = 1;
            msg->imp_Domain.Height = 1;
            return 1;
        case IDOMAIN_MAXIMUM:
            msg->imp_Domain.Width = 16383;
            msg->imp_Domain.Height = 16383;
            return 1;
        default:
            break;
    }

    return 0;
}
