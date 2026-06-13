/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* layout.c -- GM_LAYOUT / DTM_ASYNCLAYOUT / DTM_PROCLAYOUT / GM_RENDER /    */
/*             DTM_FRAMEBOX for drawing.datatype                             */
/*                                                                           */
/* Pipeline                                                                  */
/* ========                                                                  */
/*                                                                           */
/* OM_NEW (dispatch.c) --> populates DrawingInfo via DDTM_LOADHEADERS and    */
/*                          DDTM_LOADCONTENT on the subclass                 */
/*                                                                           */
/* GM_LAYOUT -------------> notify "busy", DoSuperMethodA (base class),      */
/*                          DoAsyncLayout() puts work on the layout          */
/*                          process.                                         */
/*                                                                           */
/* DTM_ASYNCLAYOUT -------> drawingLayout(): allocate a fresh BitMap         */
/*                          for the canvas at the current scale,             */
/*                          ask the embedded vector.image to rasterise       */
/*                          the display list, then optionally remap to       */
/*                          the host screen palette.                         */
/*                                                                           */
/* GM_RENDER -------------> drawingRender(): blits the cached bitmap into    */
/*                          the gadget's domain.                             */
/*                                                                           */
/* DTM_FRAMEBOX ---------> reports our preferred DisplayInfo so the          */
/*                          datatypes.library framework can compute          */
/*                          scrolling units up front.                        */
/*****************************************************************************/

#include "classbase.h"


/*****************************************************************************/
/* computePixelSize                                                          */
/*                                                                           */
/* Translate the current canvas extents + scale + DPI into output pixels.    */
/*                                                                           */
/*   pixels = canvas_units * (renderDPI / nativeDPI) * (scale / 100)         */
/*                                                                           */
/* All inputs except renderDPI / scale are 16.16 Fixed, so we perform the    */
/* arithmetic in 64-bit-effective form (a 32x16 multiply followed by a       */
/* 16-bit shift) to keep precision.                                          */
/*                                                                           */
/* When DDTA_FitToBox is set we ignore the user's scale and let the          */
/* caller-supplied gadget domain dictate the output pixel size; the          */
/* dispatcher refreshes DDTA_FixedScale to reflect the computed fit.         */
/*****************************************************************************/

void computePixelSize (struct localData *lod, LONG *outW, LONG *outH)
{
    LONG w, h;
    LONG canvasW;
    LONG canvasH;
    LONG dpi, scale;
    LONG num;
    LONG den;
    LONG t;

    if (lod->ldd_CanvasRight  <= lod->ldd_CanvasLeft ||
        lod->ldd_CanvasBottom <= lod->ldd_CanvasTop)
    {
        *outW = 1; *outH = 1; return;
    }

    /* Canvas size in 16.16 Fixed canvas units. */
    canvasW = lod->ldd_CanvasRight  - lod->ldd_CanvasLeft;
    canvasH = lod->ldd_CanvasBottom - lod->ldd_CanvasTop;

    dpi   = (LONG) (lod->ldd_RenderDPI ? lod->ldd_RenderDPI : 72);
    scale = (LONG) (lod->ldd_Scale     ? lod->ldd_Scale     : 100);

    /* pixels = canvas_inches * renderDPI * scale/100, where
     *   canvas_inches = canvas_units / units-per-inch.
     * The units-per-inch (scaled x100 so the percentage stays integral)
     * goes into the denominator.  DR2D reports inches, SVG pixels,
     * EPS/PostScript points; pixel and user canvases fall back to
     * DDTA_NativeDPI so a source already in device pixels maps 1:1 at a
     * matching DPI.  Previously this always used NativeDPI, which made an
     * inch canvas collapse to a few pixels (the dpi/nativeDPI factor
     * cancelled), starving MultiView of a sane nominal size.            */
    num = dpi * scale;
    switch (lod->ldd_CanvasUnits)
    {
        case DRWU_INCHES:      den = 100L;  break;
        case DRWU_MILLIMETRES: den = 2540L; break;
        case DRWU_POINTS:      den = 7200L; break;
        case DRWU_PIXELS:
        case DRWU_USER:
        default:
            den = (LONG) (lod->ldd_NativeDPI ? lod->ldd_NativeDPI : 72) * 100L;
            break;
    }

    /* canvasW is in 16.16 Fixed.  We want integer pixels.  Multiply by
     * num then divide by den, dropping the .16 fraction at the end.  */
    if (num > 0x40000L && canvasW > 0x40000L)
    {
        /* Avoid a 31-bit overflow on the 32x32 multiply. */
        canvasW >>= 1;
        canvasH >>= 1;
        den    >>= 1;
        if (den == 0) den = 1;
    }

    w = (LONG) (((double) canvasW * (double) num) / (double) den / 65536.0);
    h = (LONG) (((double) canvasH * (double) num) / (double) den / 65536.0);

    if (w < 1) w = 1;
    if (h < 1) h = 1;

    if (lod->ldd_Rotation == 90 || lod->ldd_Rotation == 270)
    {
        t = w; w = h; h = t;
    }

    *outW = w;
    *outH = h;
}


/*****************************************************************************/
/* pointsPerUnitFixed                                                        */
/*                                                                           */
/* vector.image renders its coordinates as PostScript points (1 unit = 1/72  */
/* inch) scaled by FixedScale.  Our canvas, however, is stored in            */
/* ldd_CanvasUnits (DR2D = inches, SVG = pixels, ...).  This returns the     */
/* points-per-canvas-unit factor (16.16 Fixed) that the renderer must apply  */
/* on top of the user scale so a drawing measured in its own units fills the */
/* pixel bitmap computePixelSize() sized for it.  For points this is 1.0, so */
/* point-based subclasses are unchanged.                                     */
/*****************************************************************************/

static LONG pointsPerUnitFixed (struct localData *lod)
{
    ULONG ndpi = lod->ldd_NativeDPI ? lod->ldd_NativeDPI : 72;

    switch (lod->ldd_CanvasUnits)
    {
        case DRWU_INCHES:      return (LONG) (72UL << 16);
        case DRWU_MILLIMETRES: return (LONG) (((72UL << 16) * 10UL) / 254UL);
        case DRWU_POINTS:      return (LONG) DRW_FIXED_ONE;
        case DRWU_PIXELS:
        case DRWU_USER:
        default:               return (LONG) ((72UL << 16) / ndpi);
    }
}


/*****************************************************************************/
/* freeCachedBitMap                                                          */
/*****************************************************************************/

void freeCachedBitMap (struct ClassBase *cb, struct localData *lod)
{
    if (lod->ldd_NumAlloc)
    {
        LONG i;
        for (i = (LONG) (lod->ldd_NumAlloc - 1); i >= 0; i--)
            ReleasePen (lod->ldd_ColorMap, lod->ldd_Allocated[i]);
        lod->ldd_NumAlloc = 0;
    }

    if (lod->ldd_BMap && !(lod->ldd_Flags & LDDF_SAMEBM))
    {
        WaitBlit ();
        FreeBitMap (lod->ldd_BMap);
        lod->ldd_BMap = NULL;
    }
    lod->ldd_BMap = NULL;

    (void) cb;
}


/*****************************************************************************/
/* invalidateView                                                            */
/*****************************************************************************/

void invalidateView (struct ClassBase *cb, Object *o)
{
    struct localData *lod = (struct localData *) INST_DATA (OCLASS (o), o);
    lod->ldd_Flags |= LDDF_VIEW_DIRTY;
    (void) cb;
}


/*****************************************************************************/
/* fitDomainScale                                                            */
/*                                                                           */
/* When DDTA_FitToBox is set, compute the scale that makes the drawing       */
/* fit the supplied domain.  Honours DDTA_PreserveAspect.                    */
/*****************************************************************************/

static void fitDomainScale (struct localData *lod, LONG domW, LONG domH)
{
    LONG canvasW, canvasH;
    LONG pixW, pixH;
    LONG sx, sy;

    if (!lod->ldd_FitToBox) return;
    if (domW <= 0 || domH <= 0) return;

    canvasW = lod->ldd_CanvasRight  - lod->ldd_CanvasLeft;
    canvasH = lod->ldd_CanvasBottom - lod->ldd_CanvasTop;

    if (canvasW <= 0 || canvasH <= 0) return;

    /* Compute native (1.0 scale) pixel size. */
    {
        ULONG oldScale = lod->ldd_Scale;
        LONG  oldFixed = lod->ldd_FixedScale;
        lod->ldd_Scale = 100;
        lod->ldd_FixedScale = DRW_FIXED_ONE;
        computePixelSize (lod, &pixW, &pixH);
        lod->ldd_Scale = oldScale;
        lod->ldd_FixedScale = oldFixed;
    }

    if (pixW <= 0 || pixH <= 0) return;

    /* sx, sy are 16.16 Fixed scale factors. */
    sx = (LONG) (((double) domW * 65536.0) / (double) pixW);
    sy = (LONG) (((double) domH * 65536.0) / (double) pixH);

    if (lod->ldd_PreserveAspect)
    {
        if (sx < sy) sy = sx;
        else         sx = sy;
    }

    lod->ldd_FixedScale = sx;   /* X dominant; rotation handled later     */
    lod->ldd_Scale = (ULONG) ((sx * 100UL) / 65536UL);
    if (lod->ldd_Scale < 1) lod->ldd_Scale = 1;
}


/*****************************************************************************/
/* drawingLayout -- DTM_ASYNCLAYOUT / DTM_PROCLAYOUT                         */
/*                                                                           */
/* Renders the current drawing into a freshly-allocated BitMap and stores    */
/* it in lod->ldd_BMap, replacing any previous bitmap.  The vector           */
/* rasterisation is performed by vector.image/VIMM_RENDER.                   */
/*****************************************************************************/

ULONG drawingLayout (struct ClassBase *cb, Class *cl, Object *o,
                     struct gpLayout *gpl)
{
    struct DTSpecialInfo *si = (struct DTSpecialInfo *) G (o)->SpecialInfo;
    struct localData     *lod = INST_DATA (cl, o);
    struct Screen        *scr = NULL;
    struct IBox          *domain;
    struct BitMap        *fbm = NULL;
    LONG                  pixW, pixH;
    LONG                  depth;
    ULONG                 retval = 1;
    BOOL                  send   = FALSE;
    struct RastPort       rp;
    struct BitMap        *newbm;

    ObtainSemaphore (&si->si_Lock);

    /* Bail if the object has no renderer: OM_DISPOSE NULLs ldd_VectorImage
     * under si_Lock, so a layout request the datatypes daemon delivers
     * during/after teardown must not allocate a bitmap or dispatch a render
     * into freed state.  (We deliberately do NOT key off LDDF_CANCELLED here,
     * since DDTM_CANCEL sets it too and a cancelled view must still relayout
     * on the next pass.)                                                   */
    if (!lod->ldd_VectorImage)
    {
        ReleaseSemaphore (&si->si_Lock);
        return 0;
    }

    si->si_Flags |= DTSIF_LAYOUT;
    lod->ldd_Flags |= LDDF_RENDERING;

    GetAttr (DTA_Domain, o, (ULONG *) &domain);

    if (lod->ldd_Screen)
        scr = lod->ldd_Screen;
    else if (gpl && gpl->gpl_GInfo)
        scr = gpl->gpl_GInfo->gi_Screen;

    /* Capture the destination ColorMap so vector.image can resolve true     *
     * ARGB colours to the nearest screen pen via ObtainBestPen().  Without  *
     * this the renderer can only fall back to the drawing's own palette.    */
    if (scr)
        lod->ldd_ColorMap = scr->ViewPort.ColorMap;

    /* Optional fit-to-box adjustment. */
    if (lod->ldd_FitToBox && domain)
        fitDomainScale (lod, (LONG) domain->Width, (LONG) domain->Height);

    if (gpl->gpl_Initial || (lod->ldd_Flags & LDDF_VIEW_DIRTY))
    {
        send = TRUE;
        lod->ldd_Flags &= ~LDDF_VIEW_DIRTY;

        computePixelSize (lod, &pixW, &pixH);

        lod->ldd_NativeWidth  = pixW;
        lod->ldd_NativeHeight = pixH;

        if (scr)
        {
            depth = scr->RastPort.BitMap->Depth;
            fbm = &scr->BitMap;
        }
        else
        {
            switch (lod->ldd_ColorMode)
            {
                case DRWCM_MONOCHROME: depth = 1; break;
                case DRWCM_GREYSCALE:  depth = 8; break;
                case DRWCM_TRUECOLOR:  depth = 24; break;
                default:               depth = 8; break;
            }
        }

        freeCachedBitMap (cb, lod);

        newbm = AllocBitMap ((ULONG) pixW, (ULONG) pixH, (ULONG) depth,
                             BMF_CLEAR, fbm);
        if (!newbm)
        {
            retval = 0;
            goto done;
        }

        lod->ldd_BMap = newbm;
        lod->ldd_Flags &= ~LDDF_SAMEBM;

        lod->ldd_BMHD.bmh_Width        = (UWORD) pixW;
        lod->ldd_BMHD.bmh_Height       = (UWORD) pixH;
        lod->ldd_BMHD.bmh_Depth        = (UBYTE) depth;
        lod->ldd_BMHD.bmh_Masking      = mskNone;
        lod->ldd_BMHD.bmh_Compression  = cmpNone;
        lod->ldd_BMHD.bmh_PageWidth    = (WORD) pixW;
        lod->ldd_BMHD.bmh_PageHeight   = (WORD) pixH;
        lod->ldd_BMHD.bmh_XAspect      = 1;
        lod->ldd_BMHD.bmh_YAspect      = 1;

        /* Set up the rastport to draw into the new bitmap, paint the
         * background and ask the vector renderer to draw the display
         * list.                                                            */
        InitRastPort (&rp);
        rp.BitMap = newbm;

        /* InitRastPort() leaves RastPort.Font == NULL.  The vector renderer
         * may fall back to graphics.library Text()/TextLength() for text
         * runs that the outline engine cannot open, and those calls
         * dereference rp.Font.  MultiView supplies the destination screen to
         * the async layout daemon, so copy its valid RastPort font onto this
         * otherwise bare off-screen RastPort before rasterising.             */
        if (rp.Font == NULL && scr && scr->RastPort.Font)
            SetFont (&rp, scr->RastPort.Font);

        /* Background fill -- pen 0 in palette modes; explicit RGB in
         * true-colour modes is the renderer's responsibility.           */
        if (!(lod->ldd_RenderFlags & DRWRF_NO_BACKGROUND))
        {
            SetAPen (&rp, 0);
            SetDrMd (&rp, JAM1);
            RectFill (&rp, 0, 0, pixW - 1, pixH - 1);
        }

        /* If the subclass overrides DDTM_RENDERVECTORS we want it to be
         * called; otherwise vector.image supplies the generic renderer.  */
        {
            struct drwRenderVectors rv;
            rv.MethodID            = DDTM_RENDERVECTORS;
            rv.drwrv_GInfo         = gpl ? gpl->gpl_GInfo : NULL;
            rv.drwrv_RPort         = &rp;
            rv.drwrv_Objects       = drawingGetObjectList (lod);
            rv.drwrv_DestX         = 0;
            rv.drwrv_DestY         = 0;
            rv.drwrv_DestWidth     = pixW;
            rv.drwrv_DestHeight    = pixH;
            {
                /* The cached bitmap is computePixelSize() pixels wide; the
                 * canvas is in ldd_CanvasUnits.  Fold the points-per-unit
                 * factor into the user FixedScale so the display list fills
                 * the bitmap exactly regardless of the canvas unit.        */
                LONG ppu = pointsPerUnitFixed (lod);
                LONG eff = (LONG) (((double) lod->ldd_FixedScale *
                                    (double) ppu) / 65536.0);
                if (eff < 1) eff = 1;
                rv.drwrv_FixedScaleX = eff;
                rv.drwrv_FixedScaleY = eff;
            }
            rv.drwrv_CanvasX       = lod->ldd_CanvasLeft;
            rv.drwrv_CanvasY       = lod->ldd_CanvasTop;
            rv.drwrv_Rotation      = (UWORD) lod->ldd_Rotation;
            rv.drwrv_RenderQuality = (UWORD) lod->ldd_RenderQuality;
            rv.drwrv_RenderFlags   = lod->ldd_RenderFlags;
            rv.drwrv_AbortPtr      = lod->ldd_AbortPtr
                                          ? lod->ldd_AbortPtr
                                          : &lod->ldd_AbortStorage;
            rv.drwrv_ProgressPtr   = lod->ldd_ProgressPtr
                                          ? lod->ldd_ProgressPtr
                                          : &lod->ldd_ProgressStorage;
            if (rv.drwrv_ProgressPtr) *rv.drwrv_ProgressPtr = 0;

            DoMethodA (o, (Msg) &rv);
        }
    }

done:
    si->si_Flags &= ~DTSIF_LAYOUT;
    lod->ldd_Flags &= ~LDDF_RENDERING;

    if (lod->ldd_BMap && domain)
    {
        LONG vis;
        vis = (LONG) domain->Height;
        if (vis < 0) vis = 0;

        si->si_VisVert  = vis;
        si->si_TotVert  = (LONG) lod->ldd_BMHD.bmh_Height;
        si->si_VisHoriz = (LONG) domain->Width;
        si->si_TotHoriz = (LONG) lod->ldd_BMHD.bmh_Width;
    }

    if (send)
        lod->ldd_Flags |= LDDF_NEED_REFRESH;

    ReleaseSemaphore (&si->si_Lock);
    return retval;
}


/*****************************************************************************/
/* drawingRender -- GM_RENDER                                                */
/*                                                                           */
/* Blits the cached drawing bitmap into the gadget's domain.                 */
/*****************************************************************************/

static UWORD ghost_pattern[] = { 0x4444, 0x1111, 0x4444, 0x1111 };

ULONG drawingRender (struct ClassBase *cb, Class *cl, Object *o,
                     struct gpRender *msg)
{
    struct DTSpecialInfo *si  = (struct DTSpecialInfo *) G (o)->SpecialInfo;
    struct localData     *lod = INST_DATA (cl, o);
    struct IBox          *domain;
    struct IBox          *sel;
    struct RastPort      *rp;
    LONG  width, height;
    LONG  tx, ty, bx, by;

    (void) cb;

    if (si->si_Flags & DTSIF_LAYOUT)
        return 0;
    if (!AttemptSemaphoreShared (&si->si_Lock))
        return 0;

    GetAttr (DTA_Domain,       o, (ULONG *) &domain);
    GetAttr (DTA_SelectDomain, o, (ULONG *) &sel);

    rp = msg->gpr_RPort;
    if (!rp || !domain)
    {
        ReleaseSemaphore (&si->si_Lock);
        return 0;
    }

    if (lod->ldd_BMap)
    {
        width  = MIN ((LONG) lod->ldd_BMHD.bmh_Width,  (LONG) domain->Width);
        height = MIN ((LONG) lod->ldd_BMHD.bmh_Height, (LONG) domain->Height);

        BltBitMapRastPort (lod->ldd_BMap,
                           (LONG) si->si_TopHoriz, (LONG) si->si_TopVert,
                           rp, domain->Left, domain->Top,
                           width, height, 0xC0);

        if (sel)
        {
            tx = (LONG) sel->Left  - si->si_TopHoriz;
            ty = (LONG) sel->Top   - si->si_TopVert;
            bx = tx + (LONG) sel->Width  - 1;
            by = ty + (LONG) sel->Height - 1;

            if (tx < width && ty < height && bx > 0 && by > 0)
            {
                if (tx < 0) tx = 0;
                if (ty < 0) ty = 0;
                if (bx > width - 1)  bx = width - 1;
                if (by > height - 1) by = height - 1;

                SetAfPt (rp, ghost_pattern, 2);
                SetDrMd (rp, JAM1);
                SetAPen (rp, 2);
                WaitBlit ();
                RectFill (rp, domain->Left + tx, domain->Top + ty,
                              domain->Left + bx, domain->Top + by);
                SetAfPt (rp, NULL, 0);
            }
        }
    }
    else
    {
        /* No bitmap yet -- clear the content area.                       */
        SetAPen  (rp, 0);
        SetDrMd  (rp, JAM1);
        SetAfPt  (rp, NULL, 0);
        RectFill (rp, domain->Left, domain->Top,
                      domain->Left + domain->Width - 1,
                      domain->Top + domain->Height - 1);
    }

    si->si_OTopVert  = si->si_TopVert;
    si->si_OTopHoriz = si->si_TopHoriz;

    ReleaseSemaphore (&si->si_Lock);
    return 1;
}


/*****************************************************************************/
/* drawingFrameBox -- DTM_FRAMEBOX                                           */
/*****************************************************************************/

ULONG drawingFrameBox (struct ClassBase *cb, Class *cl, Object *o,
                       struct dtFrameBox *dtf)
{
    struct FrameInfo *fri;

    (void) cb;
    (void) cl;

    if (!(dtf->dtf_FrameFlags & FRAMEF_SPECIFY))
    {
        GetAttr (DTA_FrameInfo, o, (ULONG *) &fri);
        CopyMem (fri, dtf->dtf_FrameInfo,
                 MIN (dtf->dtf_SizeFrameInfo, sizeof (struct FrameInfo)));
    }

    return 1;
}
