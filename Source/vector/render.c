/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* render.c -- scalable vector rasteriser for drawing.datatype               */
/*                                                                           */
/* The heart of the class.  Walks a display list of DrawObj structures       */
/* and rasterises every primitive into a RastPort using graphics.library.    */
/*                                                                           */
/* Design notes                                                              */
/* ============                                                              */
/*                                                                           */
/* drawing.datatype is "scalable, only rasterising at final draw time".      */
/* That goal has two implications for this module:                           */
/*                                                                           */
/*   1. We never quantise coordinates ahead of time.  The display list is    */
/*      stored in 16.16 Fixed canvas units (typically inches at 72 DPI);     */
/*      transformation to integer pixels happens HERE, with the current      */
/*      scale and rotation applied as part of a 2x3 affine transform.        */
/*                                                                           */
/*   2. Every draw call uses the maximum precision the underlying            */
/*      graphics.library will accept.  We use Move/Draw/PolyDraw for         */
/*      strokes, AreaMove/AreaDraw/AreaEnd for filled polygons,              */
/*      WritePixelLine8 / WritePixelArray8 for embedded bitmaps,             */
/*      Text() for simple text strings.                                      */
/*                                                                           */
/*   3. Beziers are de Casteljau subdivided to a per-pixel error budget;     */
/*      a higher DDTA_RenderQuality requests more subdivisions, a lower      */
/*      one fewer.                                                           */
/*                                                                           */
/*   4. Anti-aliasing is opportunistic.  graphics.library has no AA, but     */
/*      we emulate AA strokes with a 1-pixel halo at lower opacity when      */
/*      DDTA_AntiAlias is set and the rastport is in a true-colour mode.     */
/*      In palette modes we simply draw at integer precision.                */
/*                                                                           */
/* The implementation is entirely fixed-point.  Subclass loaders convert     */
/* IEEE / float source data once during load and we never touch a float      */
/* during draw.                                                              */
/*****************************************************************************/

#include "classbase.h"


/*****************************************************************************/
/* Debug tracing (console).  Routed through dos.library PutStr/Printf so it  */
/* lands on the calling process's stdout - e.g. the test harness console.    */
/* Compiled in unconditionally for now; remove the calls once the render     */
/* pipeline is verified end to end.                                          */
/*****************************************************************************/

#ifdef VECTOR_DEBUG

static void vdbg (struct ClassBase *cb, STRPTR text)
{
    if (DOSBase && text)
    {
        PutStr ("[vector] ");
        PutStr (text);
        PutStr ("\n");
    }
}

static void vdbgL (struct ClassBase *cb, STRPTR label, LONG value)
{
    if (DOSBase && label)
        Printf ("[vector] %s%ld\n", (LONG) label, value);
}

static void vdbgHex (struct ClassBase *cb, STRPTR label, LONG value)
{
    if (DOSBase && label)
        Printf ("[vector] %s0x%08lx\n", (LONG) label, value);
}

#else   /* !VECTOR_DEBUG: helpers compile away to nothing */

#define vdbg(cb,text)         ((void) 0)
#define vdbgL(cb,label,value) ((void) 0)
#define vdbgHex(cb,label,value) ((void) 0)

#endif  /* VECTOR_DEBUG */


/*****************************************************************************/
/* Forward                                                                   */
/*****************************************************************************/

static void drawSingleObject (struct ClassBase *cb, struct localData *lod,
                              struct VecVPort *vp, struct DrawObj *obj,
                              UWORD rotation, UWORD quality, ULONG flags);
static void renderObjectListContext (struct ClassBase *cb,
                              struct localData *lod,
                              struct VecVPort *vp,
                              struct List *objects,
                              UWORD rotation, UWORD quality,
                              ULONG flags,
                              ULONG *abortPtr, ULONG *progressPtr);
static BOOL  isTransparentARGB(ULONG argb);
static LONG  scaleForRenderDPI(LONG fixedScale, ULONG renderDPI);
static void  strokePolyPath   (struct VecVPort *vp,
                               struct DrawObj *obj,
                               UWORD quality);
static void  fillPolyPath     (struct VecVPort *vp,
                               struct DrawObj *obj);
static void  subdivBezier     (struct VecVPort *vp,
                               LONG p0x, LONG p0y,
                               LONG p1x, LONG p1y,
                               LONG p2x, LONG p2y,
                               LONG p3x, LONG p3y,
                               WORD depth, BOOL stroke);
static void  drawRectObject   (struct VecVPort *vp, struct DrawObj *obj);
static void  drawEllipseObject(struct VecVPort *vp, struct DrawObj *obj);
static BOOL  objectOnHiddenLayer (struct ClassBase *cb, struct localData *lod,
                                  ULONG flags, UWORD layerID);
static struct DrawFont *findDrawFont (struct localData *lod, UWORD fontID);
static void  drawTextObject   (struct ClassBase *cb, struct localData *lod,
                               struct VecVPort *vp, struct DrawObj *obj);
static void  drawTextSpan     (struct ClassBase *cb, struct RastPort *rp,
                               STRPTR txt, ULONG len,
                               LONG *penX, LONG *penY, LONG baseX,
                               LONG lineH);
static void  drawTextAlongPath (struct ClassBase *cb, struct VecVPort *vp,
                               struct DrawObj *obj, STRPTR txt, ULONG len,
                               LONG heightPx, STRPTR fontName,
                               UWORD fontFlags, UWORD styleFlags);
static BOOL  renderBuildOtagPath (STRPTR fontName, UBYTE *out, ULONG outSize);
static void  renderGradientObject (struct ClassBase *cb, struct VecVPort *vp,
                               struct DrawObj *obj);
static BOOL  renderShapeGradientObject (struct ClassBase *cb,
                               struct VecVPort *vp,
                               struct DrawObj *obj,
                               ULONG *argbRamp, UBYTE *penRamp,
                               BOOL trueColor, LONG n, LONG interpMode,
                               LONG bx0, LONG by0, LONG w, LONG h,
                               UBYTE *clipMask, ULONG clipBpr,
                               LONG gcx, LONG gcy);
static void  renderObjectFill (struct ClassBase *cb, struct localData *lod,
                               struct VecVPort *vp, struct DrawObj *obj,
                               UWORD rotation, UWORD quality, ULONG flags);
static UBYTE *buildObjectMask (struct ClassBase *cb, struct VecVPort *vp,
                               struct DrawObj *obj,
                               LONG bx0, LONG by0, LONG w, LONG h,
                               ULONG *bprOut);
static void  emitFillPathArea (struct ClassBase *cb, struct VecVPort *vp,
                               struct DrawObj *obj, struct RastPort *rp,
                               LONG offX, LONG offY);
static void  emitScaledFillPathArea (struct ClassBase *cb,
                               struct VecVPort *vp,
                               struct DrawObj *obj, struct RastPort *rp,
                               LONG offX, LONG offY,
                               LONG gcx, LONG gcy, LONG scale);
static BOOL  objDeviceBounds  (struct VecVPort *vp, struct DrawObj *obj,
                               LONG *x0, LONG *y0, LONG *x1, LONG *y1);
static ULONG renderISqrt      (ULONG value);
static LONG  fixSinDeg        (LONG deg);
static LONG  fixCosDeg        (LONG deg);
static LONG  fixAcosUnit      (LONG c);
static LONG  conicBandFor     (LONG *bc, LONG *bs, LONG n, LONG band,
                               LONG dx, LONG dy);
static LONG  gradInterpParam  (LONG mode, LONG p);
static ULONG sampleGradStops  (ULONG *stops, UWORD numStops,
                               ULONG startA, ULONG endA, LONG u);
static LONG  divFloor         (LONG a, LONG b);
static BOOL  rp_BitMapDepth   (struct ClassBase *cb, struct RastPort *rp,
                               LONG *depthOut);


/*****************************************************************************/
/* drwMulFixed                                                               */
/*                                                                           */
/* 16.16 * 16.16 -> 16.16 with rounding.                                     */
/* Done with two 32-bit halves to avoid the 64-bit type which SAS/C does     */
/* support but which costs precious cycles.                                  */
/*****************************************************************************/

LONG drwMulFixed (LONG a, LONG b)
{
    LONG hi, lo;
    BOOL neg = FALSE;

    if (a < 0) { a = -a; neg = !neg; }
    if (b < 0) { b = -b; neg = !neg; }

    /* (aH . aL) * (bH . bL) where each half is 16 bits. */
    hi = ((a >> 16) * (b >> 16)) << 16;
    lo = ((a & 0xFFFFL) * (b & 0xFFFFL)) >> 16;
    hi += ((a >> 16) * (b & 0xFFFFL));
    hi += ((b >> 16) * (a & 0xFFFFL));

    hi += lo;
    return neg ? -hi : hi;
}


/*****************************************************************************/
/* scaleForRenderDPI                                                         */
/*                                                                           */
/* vector.image stores coordinates in display-independent pixels: one dip is */
/* one PostScript point (1/72 inch).  RenderDPI converts that document-space */
/* unit to physical device pixels at final draw time.                        */
/*****************************************************************************/

static LONG scaleForRenderDPI (LONG fixedScale, ULONG renderDPI)
{
    ULONG dpi;
    LONG dpiScale;

    dpi = renderDPI ? renderDPI : 72UL;
    dpiScale = (LONG) ((dpi << 16) / 72UL);
    return drwMulFixed (fixedScale ? fixedScale : DRW_FIXED_ONE, dpiScale);
}


/*****************************************************************************/
/* renderObjectList                                                           */
/*                                                                            */
/* Top-level driver: walks a list of DrawObj nodes and rasterises each in   */
/* turn.  The current layer filter (lod_ActiveLayer) is respected here -  */
/* objects on non-active, non-visible layers are skipped.                */
/*****************************************************************************/

void renderObjectList (struct ClassBase *cb, struct localData *lod,
                       struct RastPort *rp,
                       struct ColorMap *cmap,
                       struct List *objects,
                       LONG destX, LONG destY,
                       LONG destW, LONG destH,
                       LONG fixedScaleX, LONG fixedScaleY,
                       LONG canvasOriginX, LONG canvasOriginY,
                       UWORD rotation, UWORD quality,
                       ULONG flags,
                       ULONG *abortPtr, ULONG *progressPtr)
{
    struct VecVPort vp;

    if (!rp || !objects) return;

    vecvpInit (&vp, cb, rp,
               destX, destY, destW, destH,
               canvasOriginX, canvasOriginY,
               fixedScaleX, fixedScaleY,
               rotation);
    vecvpSetPalette (&vp, cmap, lod->lvi_RGBTable, lod->lvi_NumPaletteColors);
    renderObjectListContext (cb, lod, &vp, objects,
                             rotation, quality, flags,
                             abortPtr, progressPtr);
    vecvpFree (&vp);
}


static void renderObjectListContext (struct ClassBase *cb,
                       struct localData *lod,
                       struct VecVPort *vp,
                       struct List *objects,
                       UWORD rotation, UWORD quality,
                       ULONG flags,
                       ULONG *abortPtr, ULONG *progressPtr)
{
    struct DrawObj *obj;
    struct DrawObj *next;
    ULONG  total = 0;
    ULONG  done  = 0;
    LONG   mx;
    LONG   my;

    if (!vp || !objects) return;

    /* Pre-count for progress reporting. */
    if (progressPtr)
    {
        for (obj = (struct DrawObj *) objects->lh_Head;
             (next = (struct DrawObj *) obj->do_Node.mln_Succ) != NULL;
             obj = next)
            total++;
    }

    for (obj = (struct DrawObj *) objects->lh_Head;
         (next = (struct DrawObj *) obj->do_Node.mln_Succ) != NULL;
         obj = next)
    {
        if (abortPtr && *abortPtr) break;

        if (done < 4)
        {
            vdbgL  (cb, "obj type=", (LONG) obj->do_Type);
            vdbgHex (cb, "  fillARGB=", (LONG) obj->do_Attrs.da_FillColor);
            vdbgHex (cb, "  edgeARGB=", (LONG) obj->do_Attrs.da_EdgeColor);
            vdbgL  (cb, "  fillType=", (LONG) obj->do_Attrs.da_FillType);
            vecvpMap (vp, obj->do_XMin, obj->do_YMin, &mx, &my);
            vdbgL (cb, "  XMin->px=", mx);
            vdbgL (cb, "  YMin->py=", my);
            vecvpMap (vp, obj->do_XMax, obj->do_YMax, &mx, &my);
            vdbgL (cb, "  XMax->px=", mx);
            vdbgL (cb, "  YMax->py=", my);

            /* Per-render (not process-static) text diagnostic so each file *
             * reports whether its STXT/TPTH strings actually attached.      */
            if (obj->do_Type == DROT_TEXT)
            {
                vdbgL (cb, "  STXT NumChars=",
                       (LONG) obj->do_Data.Text.do_NumChars);
                vdbgL (cb, "  STXT hasString=",
                       obj->do_Data.Text.do_String ? 1L : 0L);
                vdbgL (cb, "  STXT CharH=", obj->do_Data.Text.do_CharH);
            }
            else if (obj->do_Type == DROT_TEXTPATH)
            {
                vdbgL (cb, "  TPTH NumChars=",
                       (LONG) obj->do_Data.TextPath.do_NumChars);
                vdbgL (cb, "  TPTH hasString=",
                       obj->do_Data.TextPath.do_String ? 1L : 0L);
            }
        }

        /* Text is handled directly here rather than indirectly through
         * drawSingleObject(), keeping the text path explicit and
         * independent of the shape dispatch.  It must still honour layer
         * visibility, otherwise ProVector's hidden "TextStorage"/draft
         * layers (which carry black duplicate copies of the title) paint
         * over the displayed text. */
        if (obj->do_Type == DROT_TEXT || obj->do_Type == DROT_TEXTPATH)
        {
            if (!(flags & DRWRF_NO_TEXT) &&
                !objectOnHiddenLayer (cb, lod, flags,
                                      obj->do_Attrs.da_LayerID))
                drawTextObject (cb, lod, vp, obj);
        }
        else
            drawSingleObject (cb, lod, vp, obj, rotation, quality, flags);

        done++;
        if (progressPtr && total)
            *progressPtr = (done * 100) / total;
    }

    vdbgL (cb, "objects drawn=", (LONG) done);
}


/*****************************************************************************/
/* drawSingleObject                                                           */
/*****************************************************************************/

static void drawSingleObject (struct ClassBase *cb, struct localData *lod,
                              struct VecVPort *vp, struct DrawObj *obj,
                              UWORD rotation, UWORD quality, ULONG flags)
{
    struct RastPort *rp;
    LONG x0;
    LONG y0;
    LONG x1;
    LONG y1;
    LONG t;

    if (!obj || !vp) return;
    rp = vp->vp_RPort;
    (void) rotation;

    if (objectOnHiddenLayer (cb, lod, flags, obj->do_Attrs.da_LayerID))
        return;

    switch (obj->do_Type)
    {
        case DROT_POLYGON:
            if (!(flags & DRWRF_NO_FILL))
            {
                if (obj->do_Attrs.da_FillType == DRFT_GRADIENT)
                    renderGradientObject (cb, vp, obj);
                else if (obj->do_Attrs.da_FillType == DRFT_OBJECTS)
                    renderObjectFill (cb, lod, vp, obj,
                                      rotation, quality, flags);
                else if (obj->do_Attrs.da_FillType == DRFT_COLOR &&
                         !isTransparentARGB (obj->do_Attrs.da_FillColor))
                {
                    vecvpSetFgARGB (vp, obj->do_Attrs.da_FillColor);
                    fillPolyPath (vp, obj);
                }
            }
            if (!(flags & DRWRF_WIREFRAME) &&
                !isTransparentARGB (obj->do_Attrs.da_EdgeColor))
            {
                vecvpSetFgARGB (vp, obj->do_Attrs.da_EdgeColor);
                strokePolyPath (vp, obj, quality);
            }
            break;

        case DROT_POLYLINE:
            if (!isTransparentARGB (obj->do_Attrs.da_EdgeColor))
            {
                vecvpSetFgARGB (vp, obj->do_Attrs.da_EdgeColor);
                strokePolyPath (vp, obj, quality);
            }
            break;

        case DROT_BEZIER:
            if (!isTransparentARGB (obj->do_Attrs.da_EdgeColor))
            {
                vecvpSetFgARGB (vp, obj->do_Attrs.da_EdgeColor);
                strokePolyPath (vp, obj, quality);
            }
            break;

        case DROT_RECT:
            if (!(flags & DRWRF_NO_FILL) &&
                obj->do_Attrs.da_FillType == DRFT_GRADIENT)
                renderGradientObject (cb, vp, obj);
            else if (!(flags & DRWRF_NO_FILL) &&
                     obj->do_Attrs.da_FillType == DRFT_OBJECTS)
                renderObjectFill (cb, lod, vp, obj, rotation, quality, flags);
            drawRectObject (vp, obj);
            break;

        case DROT_ELLIPSE:
            if (!(flags & DRWRF_NO_FILL) &&
                obj->do_Attrs.da_FillType == DRFT_GRADIENT)
                renderGradientObject (cb, vp, obj);
            else if (!(flags & DRWRF_NO_FILL) &&
                     obj->do_Attrs.da_FillType == DRFT_OBJECTS)
                renderObjectFill (cb, lod, vp, obj, rotation, quality, flags);
            drawEllipseObject (vp, obj);
            break;

        case DROT_TEXT:
            if (!(flags & DRWRF_NO_TEXT))
                drawTextObject (cb, lod, vp, obj);
            break;

        case DROT_TEXTPATH:
            /* Text-on-path is treated as a polyline draw for the path
             * (showing the path the text would follow) plus the text
             * placed at the start.  Full bezier-spaced text needs a
             * subclass override for high-fidelity output.              */
            if (!(flags & DRWRF_NO_TEXT))
                drawTextObject (cb, lod, vp, obj);
            break;

        case DROT_BITMAP:
            /* Embedded bitmaps require diskloading; the root class       *
             * skips them.  Subclasses that ship inlined bitmaps          *
             * override DDTM_RENDERVECTORS to handle these themselves.   */
            break;

        case DROT_GROUP:
            renderObjectListContext (cb, lod, vp,
                                     &obj->do_Data.Group.do_Children,
                                     rotation, quality, flags, NULL, NULL);
            break;

        case DROT_FILL:
            /* Fill prototypes do not draw on their own; they only       *
             * supply geometry to other objects with da_FillType ==      *
             * DRFT_OBJECTS.  Skip.                                      */
            break;

        case DROT_EXTERNAL:
            if (obj->do_Data.External.do_Child)
                drawSingleObject (cb, lod, vp,
                                  obj->do_Data.External.do_Child,
                                  rotation, quality, flags);
            break;

        default:
            break;
    }

    if (flags & DRWRF_BOUNDING_BOX)
    {
        vecvpMap (vp, obj->do_XMin, obj->do_YMin, &x0, &y0);
        vecvpMap (vp, obj->do_XMax, obj->do_YMax, &x1, &y1);
        if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
        if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
        SetAPen (rp, 1);
        SetDrMd (rp, COMPLEMENT);
        Move (rp, x0, y0);
        Draw (rp, x1, y0);
        Draw (rp, x1, y1);
        Draw (rp, x0, y1);
        Draw (rp, x0, y0);
        SetDrMd (rp, JAM1);
    }
}


/*****************************************************************************/
/* strokePolyPath                                                             */
/*                                                                            */
/* Stroke a polygon / polyline / bezier path.  Walks the points array and  */
/* emits Move/Draw/PolyDraw calls.  Beziers are flattened into line      */
/* segments via de Casteljau subdivision.                                */
/*****************************************************************************/

static void strokePolyPath (struct VecVPort *vp,
                            struct DrawObj *obj,
                            UWORD quality)
{
    ULONG  i, n;
    LONG  curX = 0, curY = 0;       /* canvas-space cursor           */
    LONG  startX = 0, startY = 0;   /* sub-path start, canvas        */
    BOOL  haveCursor = FALSE;
    BOOL  subPathOpen = FALSE;
    struct DrawPoint *pts;
    struct DrawPoint *p;
    LONG p0x;
    LONG p0y;
    LONG p1x;
    LONG p1y;
    LONG p2x;
    LONG p2y;
    LONG p3x;
    LONG p3y;
    WORD depth;

    n = obj->do_Data.Poly.do_NumPoints;
    pts = obj->do_Data.Poly.do_Points;
    if (!pts || n == 0) return;

    /* Use thin lines.  graphics.library has no native line-width, so   *
     * we stroke at 1 pixel.  Subclasses needing thick strokes override *
     * DDTM_RENDERVECTORS.                                              */

    for (i = 0; i < n; i++)
    {
        p = pts + i;

        if ((p->dp_Flags & (DRPF_END | DRPF_CLOSE)) &&
            !(p->dp_Flags & DRPF_MOVETO))
        {
            /* The END/CLOSE flag rides the LAST real vertex of the sub-path *
             * (see readPolyChunk), so stroke the segment to it first, then  *
             * close back to the start if requested.                         */
            if (haveCursor)
            {
                vecvpDrawLine (vp, curX, curY, p->dp_X, p->dp_Y);
                curX = p->dp_X;
                curY = p->dp_Y;
                if (p->dp_Flags & DRPF_CLOSE)
                {
                    vecvpDrawLine (vp, curX, curY, startX, startY);
                    curX = startX;
                    curY = startY;
                }
            }
            subPathOpen = FALSE;
            haveCursor  = FALSE;
            continue;
        }

        if (p->dp_Flags & DRPF_MOVETO)
        {
            curX = p->dp_X; curY = p->dp_Y;
            startX = curX; startY = curY;
            haveCursor  = TRUE;
            subPathOpen = TRUE;
            continue;
        }

        if (p->dp_Flags & DRPF_BEZIER)
        {
            /* The DR2D / SVG cubic-bezier encoding has THREE control     *
             * points (the start is the current cursor).  Look ahead for  *
             * the two control points and the endpoint.  The endpoint may *
             * itself be the sub-path's terminal vertex (DRPF_END/CLOSE). */
            if (i + 2 < n &&
                (pts[i + 1].dp_Flags & DRPF_BEZIER) &&
                !(pts[i + 2].dp_Flags & (DRPF_BEZIER | DRPF_MOVETO)))
            {
                p0x = curX;
                p0y = curY;
                p1x = p->dp_X;
                p1y = p->dp_Y;
                p2x = pts[i + 1].dp_X;
                p2y = pts[i + 1].dp_Y;
                p3x = pts[i + 2].dp_X;
                p3y = pts[i + 2].dp_Y;
                depth = (quality >= DRWQ_HIGH) ? 6 :
                        (quality >= DRWQ_NORMAL) ? 5 : 4;

                if (!haveCursor)
                {
                    haveCursor = TRUE;
                }

                subdivBezier (vp, p0x, p0y, p1x, p1y, p2x, p2y, p3x, p3y,
                              depth, TRUE);

                curX = p3x; curY = p3y;
                i += 2;     /* skip the two bezier control points */

                if (pts[i].dp_Flags & DRPF_CLOSE)
                {
                    vecvpDrawLine (vp, curX, curY, startX, startY);
                    curX = startX;
                    curY = startY;
                }
                if (pts[i].dp_Flags & (DRPF_END | DRPF_CLOSE))
                {
                    subPathOpen = FALSE;
                    haveCursor  = FALSE;
                }
                continue;
            }
            else
            {
                /* Malformed -- skip the bezier. */
                continue;
            }
        }

        /* Ordinary line segment. */
        if (!haveCursor)
        {
            haveCursor = TRUE;
            curX = p->dp_X;
            curY = p->dp_Y;
            startX = curX;
            startY = curY;
            subPathOpen = TRUE;
        }
        else
        {
            vecvpDrawLine (vp, curX, curY, p->dp_X, p->dp_Y);
            curX = p->dp_X;
            curY = p->dp_Y;
        }
    }

    /* Polygons close implicitly. */
    if (subPathOpen && obj->do_Type == DROT_POLYGON)
    {
        vecvpDrawLine (vp, curX, curY, startX, startY);
    }
}


/*****************************************************************************/
/* subdivBezier                                                              */
/*                                                                           */
/* De Casteljau cubic Bezier subdivision.  We recurse until `depth' hits     */
/* zero, then emit a straight line segment.  The maximum recursion depth     */
/* is 6 (so 64 line segments per bezier) for HIGH quality, 5 for NORMAL      */
/* and 4 for DRAFT.                                                          */
/*****************************************************************************/

/* Subdivision depth used when flattening beziers for AREA FILLS.  Fills     *
 * cannot vary with DDTA_RenderQuality the way strokes do (fillPolyPath /    *
 * emitFillPathArea have no quality argument), so a fixed 5 (== 32 segments  *
 * per curve) is used: smooth enough for canopy/logo curves without          *
 * exhausting the Area vertex table on dense paths.                          */
#define BEZ_FILL_DEPTH 5

static void subdivBezier (struct VecVPort *vp,
                          LONG p0x, LONG p0y,
                          LONG p1x, LONG p1y,
                          LONG p2x, LONG p2y,
                          LONG p3x, LONG p3y,
                          WORD depth, BOOL stroke)
{
    LONG q0x, q0y, q1x, q1y, q2x, q2y;
    LONG r0x, r0y, r1x, r1y;
    LONG s0x, s0y;

    if (depth <= 0)
    {
        if (stroke)
            vecvpDrawLine (vp, p0x, p0y, p3x, p3y);
        else
            vecvpAreaDraw (vp, p3x, p3y);
        return;
    }

    /* Midpoint subdivision (each midpoint is the average of two points). */
    q0x = (p0x + p1x) >> 1; q0y = (p0y + p1y) >> 1;
    q1x = (p1x + p2x) >> 1; q1y = (p1y + p2y) >> 1;
    q2x = (p2x + p3x) >> 1; q2y = (p2y + p3y) >> 1;
    r0x = (q0x + q1x) >> 1; r0y = (q0y + q1y) >> 1;
    r1x = (q1x + q2x) >> 1; r1y = (q1y + q2y) >> 1;
    s0x = (r0x + r1x) >> 1; s0y = (r0y + r1y) >> 1;

    subdivBezier (vp, p0x, p0y, q0x, q0y, r0x, r0y, s0x, s0y,
                  depth - 1, stroke);
    subdivBezier (vp, s0x, s0y, r1x, r1y, q2x, q2y, p3x, p3y,
                  depth - 1, stroke);
}


/*****************************************************************************/
/* fillAreaVertexCount                                                       */
/*                                                                           */
/* Worst-case number of Area vertices a poly path needs once its beziers are */
/* flattened to BEZ_FILL_DEPTH. The Area vertex table MUST be sized for this */
/* (not the raw point count) or AreaDraw() would write past the table and    */
/* corrupt memory.  Each cubic segment expands to (1 << BEZ_FILL_DEPTH)      */
/* line segments; every other entry contributes one vertex.                  */
/*****************************************************************************/

static ULONG fillAreaVertexCount (struct DrawObj *obj)
{
    ULONG i, n, verts;
    struct DrawPoint *pts;

    pts = obj->do_Data.Poly.do_Points;
    n   = obj->do_Data.Poly.do_NumPoints;
    if (!pts || n == 0) return 8;

    verts = 0;
    for (i = 0; i < n; i++)
    {
        if ((pts[i].dp_Flags & DRPF_BEZIER) && i + 2 < n &&
            (pts[i + 1].dp_Flags & DRPF_BEZIER) &&
            !(pts[i + 2].dp_Flags & (DRPF_BEZIER | DRPF_MOVETO)))
        {
            verts += (1UL << BEZ_FILL_DEPTH);
            i += 2;
        }
        else
            verts += 1;
    }
    return verts + 8;
}


/*****************************************************************************/
/* fillPolyPath                                                              */
/*                                                                           */
/* Fill a closed polygon using the private vrport AreaInfo/TmpRas context.   */
/*****************************************************************************/

static void fillPolyPath (struct VecVPort *vp, struct DrawObj *obj)
{
    ULONG i, n;
    BOOL  subPathOpen = FALSE;
    BOOL  areaStarted = FALSE;
    UWORD closeFlags;
    struct DrawPoint *pts;
    struct DrawPoint *p;
    LONG  lastX = 0;
    LONG  lastY = 0;
    LONG  bp0x, bp0y;
    ULONG areaVerts;

    pts = obj->do_Data.Poly.do_Points;
    n   = obj->do_Data.Poly.do_NumPoints;
    if (!pts || n == 0) return;

    /* Size the Area table for the flattened (post-bezier) vertex count. */
    areaVerts = fillAreaVertexCount (obj);

    /* The loader (readPolyChunk) tags the LAST real vertex of each sub-path *
     * with DRPF_END (and DRPF_CLOSE for closed shapes); the flag does NOT   *
     * mark a separate sentinel point.  So the coordinate of an END/CLOSE    *
     * vertex must be emitted into the area BEFORE the sub-path is closed,   *
     * otherwise a 4-corner rectangle fills as a 3-corner triangle.          */
    for (i = 0; i < n; i++)
    {
        p = pts + i;
        closeFlags = (UWORD) (p->dp_Flags & (DRPF_END | DRPF_CLOSE));

        if (p->dp_Flags & DRPF_MOVETO)
        {
            if (subPathOpen)
            {
                vecvpEndArea (vp);
                areaStarted = FALSE;
            }
            if (!vecvpBeginArea (vp, areaVerts)) return;
            areaStarted = TRUE;
            vecvpAreaMove (vp, p->dp_X, p->dp_Y);
            subPathOpen = TRUE;
            lastX = p->dp_X; lastY = p->dp_Y;
        }
        else if ((p->dp_Flags & DRPF_BEZIER) &&
                 i + 2 < n &&
                 (pts[i + 1].dp_Flags & DRPF_BEZIER) &&
                 !(pts[i + 2].dp_Flags & (DRPF_BEZIER | DRPF_MOVETO)))
        {
            /* Cubic bezier: p0 = current cursor, (p->dp), (pts[i+1]) the two *
             * control points, pts[i+2] the endpoint.  Flatten it with the    *
             * same de Casteljau subdivision used for strokes so filled       *
             * curved shapes (umbrella canopy, logos) are smooth rather than  *
             * faceted through the raw control polygon.                       */
            if (!subPathOpen)
            {
                if (!areaStarted)
                {
                    if (!vecvpBeginArea (vp, areaVerts)) return;
                    areaStarted = TRUE;
                }
                vecvpAreaMove (vp, p->dp_X, p->dp_Y);
                subPathOpen = TRUE;
                lastX = p->dp_X; lastY = p->dp_Y;
            }
            bp0x = lastX; bp0y = lastY;
            subdivBezier (vp, bp0x, bp0y, p->dp_X, p->dp_Y,
                          pts[i + 1].dp_X, pts[i + 1].dp_Y,
                          pts[i + 2].dp_X, pts[i + 2].dp_Y,
                          BEZ_FILL_DEPTH, FALSE);
            lastX = pts[i + 2].dp_X; lastY = pts[i + 2].dp_Y;
            i += 2;
            closeFlags = (UWORD) (pts[i].dp_Flags & (DRPF_END | DRPF_CLOSE));
        }
        else
        {
            if (!subPathOpen)
            {
                if (!areaStarted)
                {
                    if (!vecvpBeginArea (vp, areaVerts)) return;
                    areaStarted = TRUE;
                }
                vecvpAreaMove (vp, p->dp_X, p->dp_Y);
                subPathOpen = TRUE;
            }
            else
                vecvpAreaDraw (vp, p->dp_X, p->dp_Y);
            lastX = p->dp_X; lastY = p->dp_Y;
        }

        if (closeFlags)
        {
            if (subPathOpen)
            {
                vecvpEndArea (vp);
                areaStarted = FALSE;
            }
            subPathOpen = FALSE;
        }
    }
    if (subPathOpen || areaStarted)
        vecvpEndArea (vp);
}


/*****************************************************************************/
/* Gradient + object-fill support                                            */
/*                                                                           */
/* DR2D (ProVector 3) and SVG describe two fill styles beyond a flat colour: */
/*                                                                           */
/*   DRFT_GRADIENT  -- a linear or radial colour ramp between two colours.   */
/*   DRFT_OBJECTS   -- a "tile" of objects repeated across the page and      */
/*                     clipped to the filled shape.                          */
/*                                                                           */
/* graphics.library Area* fills do not honour the software clip rectangle    */
/* vrport maintains, so both styles are rendered by first rasterising the    */
/* filled shape into a 1-bitplane MASK (sized to the shape's device bounding */
/* box so the Area fill can never write outside the bitmap), then painting   */
/* through that mask: gradients pixel-by-pixel, object-fills via a tiled     */
/* pattern bitmap and BltMaskBitMapRastPort().                               */
/*****************************************************************************/

/* sin(deg) for deg 0..90 in 16.16 fixed point.  Used only to resolve the    *
 * linear-gradient direction; one-degree resolution is ample.                */
static const LONG vecSinTab[91] = {
    0x00000L, 0x00478L, 0x008EFL, 0x00D66L, 0x011DCL, 0x01650L, 0x01AC2L, 0x01F33L,
    0x023A1L, 0x0280CL, 0x02C74L, 0x030D9L, 0x0353AL, 0x03996L, 0x03DEFL, 0x04242L,
    0x04690L, 0x04AD9L, 0x04F1CL, 0x05358L, 0x0578FL, 0x05BBEL, 0x05FE6L, 0x06407L,
    0x06820L, 0x06C31L, 0x07039L, 0x07439L, 0x0782FL, 0x07C1CL, 0x08000L, 0x083DAL,
    0x087A9L, 0x08B6DL, 0x08F27L, 0x092D6L, 0x09679L, 0x09A11L, 0x09D9CL, 0x0A11BL,
    0x0A48EL, 0x0A7F3L, 0x0AB4CL, 0x0AE97L, 0x0B1D5L, 0x0B505L, 0x0B827L, 0x0BB3AL,
    0x0BE3FL, 0x0C135L, 0x0C41BL, 0x0C6F3L, 0x0C9BBL, 0x0CC73L, 0x0CF1CL, 0x0D1B4L,
    0x0D43CL, 0x0D6B3L, 0x0D91AL, 0x0DB6FL, 0x0DDB4L, 0x0DFE7L, 0x0E209L, 0x0E419L,
    0x0E617L, 0x0E804L, 0x0E9DEL, 0x0EBA6L, 0x0ED5CL, 0x0EEFFL, 0x0F090L, 0x0F20EL,
    0x0F378L, 0x0F4D0L, 0x0F615L, 0x0F747L, 0x0F865L, 0x0F970L, 0x0FA68L, 0x0FB4CL,
    0x0FC1CL, 0x0FCD9L, 0x0FD82L, 0x0FE18L, 0x0FE99L, 0x0FF07L, 0x0FF60L, 0x0FFA6L,
    0x0FFD8L, 0x0FFF6L, 0x10000L
};


static LONG fixSinDeg (LONG deg)
{
    deg %= 360;
    if (deg < 0) deg += 360;

    if (deg <= 90)  return  vecSinTab[deg];
    if (deg <= 180) return  vecSinTab[180 - deg];
    if (deg <= 270) return -vecSinTab[deg - 180];
    return -vecSinTab[360 - deg];
}


static LONG fixCosDeg (LONG deg)
{
    return fixSinDeg (deg + 90);
}


static ULONG renderISqrt (ULONG value)
{
    ULONG root;
    ULONG bit;
    ULONG trial;

    root = 0;
    bit = 1UL << 30;
    while (bit > value) bit >>= 2;
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


/*****************************************************************************/
/* objDeviceBounds                                                           */
/*                                                                           */
/* Compute an object's bounding box in DEVICE pixels by mapping its geometry */
/* through the current transform.  Polygons map every contributing point so  */
/* the box exactly contains the Area-fill vertices; rect/ellipse map the two */
/* stored canvas corners.                                                    */
/*****************************************************************************/

static BOOL objDeviceBounds (struct VecVPort *vp, struct DrawObj *obj,
                             LONG *x0, LONG *y0, LONG *x1, LONG *y1)
{
    LONG  mnx;
    LONG  mny;
    LONG  mxx;
    LONG  mxy;
    LONG  px;
    LONG  py;
    ULONG i;
    ULONG n;
    struct DrawPoint *pts;
    struct DrawPoint *p;
    BOOL  any;

    mnx = mny = 0x7FFFFFFFL;
    mxx = mxy = (LONG) 0x80000000L;
    any = FALSE;

    if (obj->do_Type == DROT_POLYGON ||
        obj->do_Type == DROT_POLYLINE ||
        obj->do_Type == DROT_BEZIER)
    {
        pts = obj->do_Data.Poly.do_Points;
        n   = obj->do_Data.Poly.do_NumPoints;
        if (!pts) return FALSE;
        for (i = 0; i < n; i++)
        {
            /* Every entry in Poly.do_Points is a real vertex (the loader   *
             * consumes DR2D flag records, it never stores sentinels), so   *
             * the END/CLOSE-tagged terminal vertices must be included or   *
             * the fill mask bounding box clips the shape.                  */
            p = pts + i;
            vecvpMap (vp, p->dp_X, p->dp_Y, &px, &py);
            if (px < mnx) mnx = px;
            if (py < mny) mny = py;
            if (px > mxx) mxx = px;
            if (py > mxy) mxy = py;
            any = TRUE;
        }
    }
    else
    {
        vecvpMap (vp, obj->do_XMin, obj->do_YMin, &px, &py);
        mnx = mxx = px; mny = mxy = py;
        vecvpMap (vp, obj->do_XMax, obj->do_YMax, &px, &py);
        if (px < mnx) mnx = px;
        if (py < mny) mny = py;
        if (px > mxx) mxx = px;
        if (py > mxy) mxy = py;
        any = TRUE;
    }

    if (!any) return FALSE;
    *x0 = mnx; *y0 = mny; *x1 = mxx; *y1 = mxy;
    return TRUE;
}


/*****************************************************************************/
/* emitFillPathArea                                                          */
/*                                                                           */
/* Walk a polygon path and emit AreaMove/AreaDraw into a caller-prepared     */
/* RastPort, offset by (offX,offY) into device space.  All sub-paths are     */
/* added to a single area so AreaEnd() fills them with the even-odd rule,    */
/* giving correct holes (the "O" glyph case from the DR2D spec).  The caller */
/* does InitArea() before and AreaEnd() after.                               */
/*****************************************************************************/

/* De Casteljau subdivision in DEVICE space, emitting AreaDraw() into the     *
 * mask RastPort.  emitFillPathArea works in already-mapped pixel coordinates *
 * (unlike fillPolyPath, which works in canvas units via vecvp*), so it needs *
 * its own flattener rather than reusing subdivBezier.  p0 is assumed to be   *
 * the current area cursor and is not re-emitted.                             */
static void emitBezierArea (struct ClassBase *cb, struct RastPort *rp,
                            LONG p0x, LONG p0y, LONG p1x, LONG p1y,
                            LONG p2x, LONG p2y, LONG p3x, LONG p3y,
                            WORD depth)
{
    LONG q0x, q0y, q1x, q1y, q2x, q2y;
    LONG r0x, r0y, r1x, r1y;
    LONG s0x, s0y;

    if (depth <= 0)
    {
        AreaDraw (rp, p3x, p3y);
        return;
    }

    q0x = (p0x + p1x) >> 1; q0y = (p0y + p1y) >> 1;
    q1x = (p1x + p2x) >> 1; q1y = (p1y + p2y) >> 1;
    q2x = (p2x + p3x) >> 1; q2y = (p2y + p3y) >> 1;
    r0x = (q0x + q1x) >> 1; r0y = (q0y + q1y) >> 1;
    r1x = (q1x + q2x) >> 1; r1y = (q1y + q2y) >> 1;
    s0x = (r0x + r1x) >> 1; s0y = (r0y + r1y) >> 1;

    emitBezierArea (cb, rp, p0x, p0y, q0x, q0y, r0x, r0y, s0x, s0y, depth - 1);
    emitBezierArea (cb, rp, s0x, s0y, r1x, r1y, q2x, q2y, p3x, p3y, depth - 1);
}


static void emitFillPathArea (struct ClassBase *cb, struct VecVPort *vp,
                              struct DrawObj *obj, struct RastPort *rp,
                              LONG offX, LONG offY)
{
    ULONG i;
    ULONG n;
    struct DrawPoint *pts;
    struct DrawPoint *p;
    LONG  px;
    LONG  py;
    BOOL  subStarted;
    BOOL  closeHere;
    LONG  lastX = 0;
    LONG  lastY = 0;
    LONG  c1x, c1y, c2x, c2y, ex, ey;

    pts = obj->do_Data.Poly.do_Points;
    n   = obj->do_Data.Poly.do_NumPoints;
    if (!pts || n == 0) return;

    subStarted = FALSE;
    for (i = 0; i < n; i++)
    {
        p = pts + i;
        closeHere = (p->dp_Flags & (DRPF_END | DRPF_CLOSE)) ? TRUE : FALSE;

        if (p->dp_Flags & DRPF_MOVETO)
        {
            vecvpMap (vp, p->dp_X, p->dp_Y, &px, &py);
            AreaMove (rp, px - offX, py - offY);
            subStarted = TRUE;
            lastX = px - offX; lastY = py - offY;
        }
        else if ((p->dp_Flags & DRPF_BEZIER) &&
                 i + 2 < n &&
                 (pts[i + 1].dp_Flags & DRPF_BEZIER) &&
                 !(pts[i + 2].dp_Flags & (DRPF_BEZIER | DRPF_MOVETO)))
        {
            /* Flatten the cubic into the mask area so gradient/object fills *
             * clipped to curved shapes get the same smooth edge as strokes. */
            vecvpMap (vp, pts[i].dp_X, pts[i].dp_Y, &px, &py);
            c1x = px - offX; c1y = py - offY;
            if (!subStarted)
            {
                AreaMove (rp, c1x, c1y);
                subStarted = TRUE;
                lastX = c1x; lastY = c1y;
            }
            vecvpMap (vp, pts[i + 1].dp_X, pts[i + 1].dp_Y, &px, &py);
            c2x = px - offX; c2y = py - offY;
            vecvpMap (vp, pts[i + 2].dp_X, pts[i + 2].dp_Y, &px, &py);
            ex = px - offX; ey = py - offY;
            emitBezierArea (cb, rp, lastX, lastY, c1x, c1y, c2x, c2y, ex, ey,
                            BEZ_FILL_DEPTH);
            lastX = ex; lastY = ey;
            i += 2;
            closeHere = (pts[i].dp_Flags & (DRPF_END | DRPF_CLOSE))
                        ? TRUE : FALSE;
        }
        else
        {
            /* Ordinary vertex (including the terminal END/CLOSE vertex,    *
             * whose real coordinate must contribute to the area).          */
            vecvpMap (vp, p->dp_X, p->dp_Y, &px, &py);
            if (!subStarted)
            {
                AreaMove (rp, px - offX, py - offY);
                subStarted = TRUE;
            }
            else
                AreaDraw (rp, px - offX, py - offY);
            lastX = px - offX; lastY = py - offY;
        }

        if (closeHere)
            subStarted = FALSE;
    }

}


/*****************************************************************************/
/* scaledDevicePoint                                                         */
/*****************************************************************************/

static void scaledDevicePoint (LONG x, LONG y, LONG gcx, LONG gcy,
                               LONG scale, LONG *sx, LONG *sy)
{
    LONG dx;
    LONG dy;

    dx = x - gcx;
    dy = y - gcy;
    *sx = gcx + ((dx * scale) >> 16);
    *sy = gcy + ((dy * scale) >> 16);
}


/*****************************************************************************/
/* emitScaledFillPathArea                                                     */
/*                                                                            */
/* Emit the same filled path as emitFillPathArea(), but after mapping each   */
/* vertex to device space, scale it around the gradient centre.  This mirrors */
/* ProVector's SHAPE gradient bands, which repeatedly draw the original path */
/* shrunk toward CentX/CentY.                                                 */
/*****************************************************************************/

static void emitScaledFillPathArea (struct ClassBase *cb, struct VecVPort *vp,
                                    struct DrawObj *obj, struct RastPort *rp,
                                    LONG offX, LONG offY,
                                    LONG gcx, LONG gcy, LONG scale)
{
    ULONG i;
    ULONG n;
    struct DrawPoint *pts;
    struct DrawPoint *p;
    LONG  px;
    LONG  py;
    LONG  sx;
    LONG  sy;
    BOOL  subStarted;
    BOOL  closeHere;
    LONG  lastX;
    LONG  lastY;
    LONG  c1x, c1y, c2x, c2y, ex, ey;

    pts = obj->do_Data.Poly.do_Points;
    n   = obj->do_Data.Poly.do_NumPoints;
    if (!pts || n == 0) return;

    subStarted = FALSE;
    lastX = 0;
    lastY = 0;
    for (i = 0; i < n; i++)
    {
        p = pts + i;
        closeHere = (p->dp_Flags & (DRPF_END | DRPF_CLOSE)) ? TRUE : FALSE;

        if (p->dp_Flags & DRPF_MOVETO)
        {
            vecvpMap (vp, p->dp_X, p->dp_Y, &px, &py);
            scaledDevicePoint (px, py, gcx, gcy, scale, &sx, &sy);
            AreaMove (rp, sx - offX, sy - offY);
            subStarted = TRUE;
            lastX = sx - offX; lastY = sy - offY;
        }
        else if ((p->dp_Flags & DRPF_BEZIER) &&
                 i + 2 < n &&
                 (pts[i + 1].dp_Flags & DRPF_BEZIER) &&
                 !(pts[i + 2].dp_Flags & (DRPF_BEZIER | DRPF_MOVETO)))
        {
            vecvpMap (vp, pts[i].dp_X, pts[i].dp_Y, &px, &py);
            scaledDevicePoint (px, py, gcx, gcy, scale, &sx, &sy);
            c1x = sx - offX; c1y = sy - offY;
            if (!subStarted)
            {
                AreaMove (rp, c1x, c1y);
                subStarted = TRUE;
                lastX = c1x; lastY = c1y;
            }

            vecvpMap (vp, pts[i + 1].dp_X, pts[i + 1].dp_Y, &px, &py);
            scaledDevicePoint (px, py, gcx, gcy, scale, &sx, &sy);
            c2x = sx - offX; c2y = sy - offY;

            vecvpMap (vp, pts[i + 2].dp_X, pts[i + 2].dp_Y, &px, &py);
            scaledDevicePoint (px, py, gcx, gcy, scale, &sx, &sy);
            ex = sx - offX; ey = sy - offY;

            emitBezierArea (cb, rp, lastX, lastY, c1x, c1y, c2x, c2y, ex, ey,
                            BEZ_FILL_DEPTH);
            lastX = ex; lastY = ey;
            i += 2;
            closeHere = (pts[i].dp_Flags & (DRPF_END | DRPF_CLOSE))
                        ? TRUE : FALSE;
        }
        else
        {
            vecvpMap (vp, p->dp_X, p->dp_Y, &px, &py);
            scaledDevicePoint (px, py, gcx, gcy, scale, &sx, &sy);
            if (!subStarted)
            {
                AreaMove (rp, sx - offX, sy - offY);
                subStarted = TRUE;
            }
            else
                AreaDraw (rp, sx - offX, sy - offY);
            lastX = sx - offX; lastY = sy - offY;
        }

        if (closeHere)
            subStarted = FALSE;
    }
}


/*****************************************************************************/
/* buildObjectMask                                                            */
/*                                                                            */
/* Rasterise the object's filled shape into a freshly allocated 1-bitplane   */
/* raster of size w x h (device pixels) whose top-left maps to (bx0,by0).    */
/* Returns the plane (free with FreeRaster(plane,w,h)) and its bytes-per-row */
/* in *bprOut, or NULL on failure / refusal.                                 */
/*****************************************************************************/

static UBYTE *buildObjectMask (struct ClassBase *cb, struct VecVPort *vp,
                               struct DrawObj *obj,
                               LONG bx0, LONG by0, LONG w, LONG h,
                               ULONG *bprOut)
{
    struct BitMap   maskBM;
    struct RastPort maskRP;
    struct TmpRas   tr;
    struct AreaInfo ai;
    UBYTE  *plane;
    UBYTE  *tmpRaster;
    APTR    areaTable;
    ULONG   bpr;
    ULONG   rassize;
    ULONG   verts;
    LONG    cx;
    LONG    cy;
    LONG    rx;
    LONG    ry;
    LONG    px;
    LONG    py;
    LONG    qx;
    LONG    qy;
    LONG    t;

    if (w < 1 || h < 1) return NULL;
    if (w > 4096 || h > 4096) return NULL;

    bpr = ((ULONG) (w + 15) >> 4) << 1;
    rassize = bpr * (ULONG) h;

    plane = (UBYTE *) AllocRaster ((ULONG) w, (ULONG) h);
    if (!plane) return NULL;

    tmpRaster = (UBYTE *) AllocRaster ((ULONG) w, (ULONG) h);
    if (!tmpRaster)
    {
        FreeRaster ((PLANEPTR) plane, (ULONG) w, (ULONG) h);
        return NULL;
    }

    verts = 8;
    if (obj->do_Type == DROT_POLYGON || obj->do_Type == DROT_POLYLINE ||
        obj->do_Type == DROT_BEZIER)
        verts = fillAreaVertexCount (obj);   /* counts flattened beziers */
    if (verts < 8) verts = 8;

    areaTable = AllocVec (verts * 5 + 8, MEMF_ANY | MEMF_CLEAR);
    if (!areaTable)
    {
        FreeRaster ((PLANEPTR) tmpRaster, (ULONG) w, (ULONG) h);
        FreeRaster ((PLANEPTR) plane, (ULONG) w, (ULONG) h);
        return NULL;
    }

    InitBitMap (&maskBM, 1, (LONG) w, (LONG) h);
    maskBM.Planes[0] = (PLANEPTR) plane;

    InitRastPort (&maskRP);
    maskRP.BitMap   = &maskBM;
    InitTmpRas (&tr, (PLANEPTR) tmpRaster, rassize);
    maskRP.TmpRas   = &tr;
    maskRP.AreaInfo = &ai;
    InitArea (&ai, areaTable, verts);

    SetRast (&maskRP, 0);
    SetAPen (&maskRP, 1);

    if (obj->do_Type == DROT_ELLIPSE)
    {
        vecvpMap (vp, obj->do_XMin, obj->do_YMin, &px, &py);
        vecvpMap (vp, obj->do_XMax, obj->do_YMax, &qx, &qy);
        if (px > qx) { t = px; px = qx; qx = t; }
        if (py > qy) { t = py; py = qy; qy = t; }
        cx = ((px + qx) >> 1) - bx0;
        cy = ((py + qy) >> 1) - by0;
        rx = (qx - px) >> 1;
        ry = (qy - py) >> 1;
        if (rx < 1) rx = 1;
        if (ry < 1) ry = 1;
        AreaEllipse (&maskRP, cx, cy, rx, ry);
        AreaEnd (&maskRP);
    }
    else if (obj->do_Type == DROT_RECT)
    {
        vecvpMap (vp, obj->do_XMin, obj->do_YMin, &px, &py);
        vecvpMap (vp, obj->do_XMax, obj->do_YMax, &qx, &qy);
        if (px > qx) { t = px; px = qx; qx = t; }
        if (py > qy) { t = py; py = qy; qy = t; }
        AreaMove (&maskRP, px - bx0, py - by0);
        AreaDraw (&maskRP, qx - bx0, py - by0);
        AreaDraw (&maskRP, qx - bx0, qy - by0);
        AreaDraw (&maskRP, px - bx0, qy - by0);
        AreaEnd (&maskRP);
    }
    else
    {
        emitFillPathArea (cb, vp, obj, &maskRP, bx0, by0);
        AreaEnd (&maskRP);
    }

    FreeVec (areaTable);
    FreeRaster ((PLANEPTR) tmpRaster, (ULONG) w, (ULONG) h);

    if (bprOut) *bprOut = bpr;
    return plane;
}


/*****************************************************************************/
/* gradSpan -- paint one horizontal run of a gradient at colour `band`.       */
/*****************************************************************************/

static void gradSpan (struct VecVPort *vp, BOOL trueColor,
                      ULONG argb, UBYTE pen, LONG xa, LONG xb, LONG y)
{
    struct ClassBase *cb = vp->vp_CB;
    struct RastPort  *rp = vp->vp_RPort;

    if (xb < xa) return;
    if (trueColor)
        vecvpSetFgARGB (vp, argb);
    else
    {
        SetAPen (rp, pen);
        SetDrMd (rp, JAM1);
    }
    RectFill (rp, xa, y, xb, y);
}


/*****************************************************************************/
/* renderMaskBand                                                             */
/*****************************************************************************/

static void renderMaskBand (struct VecVPort *vp, BOOL trueColor,
                            ULONG argb, UBYTE pen,
                            UBYTE *mask, UBYTE *clipMask, ULONG bpr,
                            LONG bx0, LONG by0, LONG w, LONG h)
{
    UBYTE *maskRow;
    UBYTE *clipRow;
    LONG x;
    LONG x0;
    LONG x1;
    LONG y;
    LONG py;
    LONG segStart;
    LONG lx;
    BOOL covered;

    for (y = 0; y < h; y++)
    {
        py = by0 + y;
        if (py < vp->vp_Clip.MinY || py > vp->vp_Clip.MaxY)
            continue;

        x0 = 0;
        x1 = w - 1;
        if (bx0 + x0 < vp->vp_Clip.MinX)
            x0 = vp->vp_Clip.MinX - bx0;
        if (bx0 + x1 > vp->vp_Clip.MaxX)
            x1 = vp->vp_Clip.MaxX - bx0;
        if (x0 > x1)
            continue;

        maskRow = mask + (ULONG) y * bpr;
        clipRow = clipMask ? (clipMask + (ULONG) y * bpr) : NULL;
        x = x0;
        while (x <= x1)
        {
            lx = x;
            covered = (maskRow[lx >> 3] & (0x80 >> (lx & 7))) ? TRUE : FALSE;
            if (covered && clipMask)
                covered = (clipRow[lx >> 3] & (0x80 >> (lx & 7)))
                          ? TRUE : FALSE;
            if (!covered)
            {
                x++;
                continue;
            }

            segStart = x;
            while (x <= x1)
            {
                lx = x;
                covered = (maskRow[lx >> 3] & (0x80 >> (lx & 7)))
                          ? TRUE : FALSE;
                if (covered && clipMask)
                    covered = (clipRow[lx >> 3] & (0x80 >> (lx & 7)))
                              ? TRUE : FALSE;
                if (!covered)
                    break;
                x++;
            }
            gradSpan (vp, trueColor, argb, pen,
                      bx0 + segStart, bx0 + x - 1, py);
        }
    }
}


/*****************************************************************************/
/* renderShapeGradientObject                                                 */
/*                                                                           */
/* ProVector's SHAPE gradient draws the filled object repeatedly, scaling    */
/* every vertex toward GradX/GradY and clipping each pass to the original    */
/* object.  Rendering it as a radial distance field is cheaper but wrong for */
/* letterforms and concave shapes.  This routine mirrors the original method */
/* using temporary 1-bit masks: outer bands draw first, then smaller scaled  */
/* copies overwrite toward the centre.                                       */
/*****************************************************************************/

static BOOL renderShapeGradientObject (struct ClassBase *cb,
                                       struct VecVPort *vp,
                                       struct DrawObj *obj,
                                       ULONG *argbRamp, UBYTE *penRamp,
                                       BOOL trueColor, LONG n,
                                       LONG interpMode,
                                       LONG bx0, LONG by0, LONG w, LONG h,
                                       UBYTE *clipMask, ULONG clipBpr,
                                       LONG gcx, LONG gcy)
{
    struct BitMap   maskBM;
    struct RastPort maskRP;
    struct TmpRas   tr;
    struct AreaInfo ai;
    UBYTE  *plane;
    UBYTE  *tmpRaster;
    APTR    areaTable;
    ULONG   bpr;
    ULONG   rassize;
    ULONG   verts;
    LONG    k;
    LONG    gp;
    LONG    t;
    LONG    scale;
    LONG    px;
    LONG    py;
    LONG    qx;
    LONG    qy;
    LONG    sx0;
    LONG    sy0;
    LONG    sx1;
    LONG    sy1;
    LONG    ex0;
    LONG    ey0;
    LONG    ex1;
    LONG    ey1;
    LONG    cx;
    LONG    cy;
    LONG    rx;
    LONG    ry;
    LONG    swap;

    if (!clipMask || !vp || !obj) return FALSE;
    if (w < 1 || h < 1) return TRUE;

    bpr = ((ULONG) (w + 15) >> 4) << 1;
    if (bpr != clipBpr) return FALSE;
    rassize = bpr * (ULONG) h;

    plane = (UBYTE *) AllocRaster ((ULONG) w, (ULONG) h);
    if (!plane) return FALSE;

    tmpRaster = (UBYTE *) AllocRaster ((ULONG) w, (ULONG) h);
    if (!tmpRaster)
    {
        FreeRaster ((PLANEPTR) plane, (ULONG) w, (ULONG) h);
        return FALSE;
    }

    verts = 8;
    if (obj->do_Type == DROT_POLYGON || obj->do_Type == DROT_POLYLINE ||
        obj->do_Type == DROT_BEZIER)
        verts = fillAreaVertexCount (obj);
    if (verts < 8) verts = 8;

    areaTable = AllocVec (verts * 5 + 8, MEMF_ANY | MEMF_CLEAR);
    if (!areaTable)
    {
        FreeRaster ((PLANEPTR) tmpRaster, (ULONG) w, (ULONG) h);
        FreeRaster ((PLANEPTR) plane, (ULONG) w, (ULONG) h);
        return FALSE;
    }

    InitBitMap (&maskBM, 1, (LONG) w, (LONG) h);
    maskBM.Planes[0] = (PLANEPTR) plane;

    InitRastPort (&maskRP);
    maskRP.BitMap = &maskBM;
    InitTmpRas (&tr, (PLANEPTR) tmpRaster, rassize);
    maskRP.TmpRas = &tr;
    maskRP.AreaInfo = &ai;

    for (k = 0; k < n; k++)
    {
        if (k == 0)
        {
            renderMaskBand (vp, trueColor, argbRamp[k], penRamp[k],
                            clipMask, clipMask, bpr, bx0, by0, w, h);
            continue;
        }

        gp = (n > 1) ? (LONG) (((ULONG) k << 16) / (ULONG) (n - 1)) : 0;
        t = gradInterpParam (interpMode, gp);
        scale = DRW_FIXED_ONE - t;
        if (scale < 0) scale = 0;

        SetRast (&maskRP, 0);
        SetAPen (&maskRP, 1);
        InitArea (&ai, areaTable, verts);

        if (obj->do_Type == DROT_ELLIPSE)
        {
            vecvpMap (vp, obj->do_XMin, obj->do_YMin, &px, &py);
            vecvpMap (vp, obj->do_XMax, obj->do_YMax, &qx, &qy);
            scaledDevicePoint (px, py, gcx, gcy, scale, &sx0, &sy0);
            scaledDevicePoint (qx, qy, gcx, gcy, scale, &sx1, &sy1);
            if (sx0 > sx1) { swap = sx0; sx0 = sx1; sx1 = swap; }
            if (sy0 > sy1) { swap = sy0; sy0 = sy1; sy1 = swap; }
            cx = ((sx0 + sx1) >> 1) - bx0;
            cy = ((sy0 + sy1) >> 1) - by0;
            rx = (sx1 - sx0) >> 1;
            ry = (sy1 - sy0) >> 1;
            if (rx < 1) rx = 1;
            if (ry < 1) ry = 1;
            AreaEllipse (&maskRP, cx, cy, rx, ry);
            AreaEnd (&maskRP);
        }
        else if (obj->do_Type == DROT_RECT)
        {
            vecvpMap (vp, obj->do_XMin, obj->do_YMin, &px, &py);
            vecvpMap (vp, obj->do_XMax, obj->do_YMax, &qx, &qy);
            scaledDevicePoint (px, py, gcx, gcy, scale, &sx0, &sy0);
            scaledDevicePoint (qx, py, gcx, gcy, scale, &sx1, &sy1);
            scaledDevicePoint (qx, qy, gcx, gcy, scale, &ex1, &ey1);
            scaledDevicePoint (px, qy, gcx, gcy, scale, &ex0, &ey0);
            AreaMove (&maskRP, sx0 - bx0, sy0 - by0);
            AreaDraw (&maskRP, sx1 - bx0, sy1 - by0);
            AreaDraw (&maskRP, ex1 - bx0, ey1 - by0);
            AreaDraw (&maskRP, ex0 - bx0, ey0 - by0);
            AreaEnd (&maskRP);
        }
        else
        {
            emitScaledFillPathArea (cb, vp, obj, &maskRP, bx0, by0,
                                    gcx, gcy, scale);
            AreaEnd (&maskRP);
        }

        renderMaskBand (vp, trueColor, argbRamp[k], penRamp[k],
                        plane, clipMask, bpr, bx0, by0, w, h);
    }

    FreeVec (areaTable);
    FreeRaster ((PLANEPTR) tmpRaster, (ULONG) w, (ULONG) h);
    FreeRaster ((PLANEPTR) plane, (ULONG) w, (ULONG) h);
    return TRUE;
}


/*****************************************************************************/
/* fixAcosUnit -- arccos returned as a 16.16 fraction in [0,1] (== acos/PI).  */
/*                                                                            */
/* c is 16.16 in [-1,1].  Implemented by a binary search over the existing    */
/* one-degree cosine table; cos is monotonic-decreasing on [0,180] so the     */
/* search needs no extra table.  One-degree resolution matches the gradient   */
/* band granularity, so finer interpolation would not change the output.      */
/*****************************************************************************/

static LONG fixAcosUnit (LONG c)
{
    LONG lo;
    LONG hi;
    LONG mid;

    if (c >=  DRW_FIXED_ONE) return 0;
    if (c <= -DRW_FIXED_ONE) return DRW_FIXED_ONE;

    lo = 0;
    hi = 180;
    while (hi - lo > 1)
    {
        mid = (lo + hi) >> 1;
        /* cos decreasing: cos(mid) > c means the true angle is larger.       */
        if (fixCosDeg (mid) > c) lo = mid;
        else                     hi = mid;
    }
    return (LONG) (((ULONG) lo << 16) / 180UL);
}


/******************************************************************************/
/* conicBandFor -- locate the conic-gradient band of a pixel without atan2.   */
/*                                                                            */
/* The circle is pre-divided into n angular sectors; bc[b]/bs[b] hold the     */
/* 16.16 cosine/sine of sector boundary b's absolute angle.  A pixel offset   */
/* (dx,dy) lies in band b exactly when it is counter-clockwise of ray b and   */
/* clockwise of ray b+1, tested by the sign of the 2-D cross product          */
/* (bc*dy - bs*dx) == |D||P| sin(angle-of-P minus angle-of-ray).  Because the */
/* sweep angle is monotonic along any scanline, the caller passes the band    */
/* found for the previous pixel and this routine only nudges it up or down a  */
/* sector at a time -- amortised O(1) per pixel, and no transcendental call.  */
/* The guard bounds the walk so a pixel exactly on the centre (dx==dy==0, a   */
/* genuine singularity) cannot spin.                                          */
/******************************************************************************/

static LONG conicBandFor (LONG *bc, LONG *bs, LONG n, LONG band,
                          LONG dx, LONG dy)
{
    LONG b1;
    LONG cB;
    LONG cB1;
    LONG guard;

    if (band < 0 || band >= n) band = 0;

    for (guard = 0; guard < n; guard++)
    {
        b1 = band + 1;
        if (b1 >= n) b1 = 0;

        cB  = bc[band] * dy - bs[band] * dx;
        cB1 = bc[b1]   * dy - bs[b1]   * dx;

        if (cB >= 0 && cB1 < 0)
            break;                       /* inside sector [band, band+1) */

        if (cB < 0)
        {
            band--;
            if (band < 0) band = n - 1;  /* pixel is clockwise of ray b   */
        }
        else
        {
            band++;
            if (band >= n) band = 0;      /* pixel is past ray b+1 (CCW)   */
        }
    }
    return band;
}


/*****************************************************************************/
/* gradInterpParam -- apply the ProVector GradInterp easing mode.            */
/*                                                                           */
/* ProVector draws colour C(u) at the spatial boundary interp(u), where C(u) */
/* samples the colour stops uniformly.  This routine therefore returns the   */
/* forward easing curve, not its inverse:                                    */
/*   ACCEL   u^2                                                             */
/*   DECEL   1-(1-u)^2                                                       */
/*   SINE    (1-cos PIu)/2                                                   */
/*   ARCSINE acos(1-2u)/PI                                                   */
/* All in 16.16 integer maths using the existing trig tables.                */
/*****************************************************************************/

static LONG gradInterpParam (LONG mode, LONG p)
{
    LONG q;
    LONG deg;

    if (p < 0) p = 0;
    if (p > DRW_FIXED_ONE) p = DRW_FIXED_ONE;

    switch (mode)
    {
        case DRGI_ACCEL:
            return drwMulFixed (p, p);

        case DRGI_DECEL:
            q = DRW_FIXED_ONE - p;
            return DRW_FIXED_ONE - drwMulFixed (q, q);

        case DRGI_SINE:
            deg = (LONG) (((ULONG) p * 180UL) >> 16);
            return (DRW_FIXED_ONE - fixCosDeg (deg)) >> 1;

        case DRGI_ARCSINE:
            return fixAcosUnit (DRW_FIXED_ONE - (p << 1));

        case DRGI_LINEAR:
        default:
            return p;
    }
}


/******************************************************************************/
/* sampleGradStops -- piecewise-linear ARGB at ramp parameter u (16.16, 0..1).*/
/*                                                                            */
/* When a multi-stop array is present the stops are treated as evenly spaced  */
/* (matching the ProVector GRAD model) and the two bracketing stops are       */
/* blended per channel.  With no array it blends the legacy start/end pair so */
/* older two-colour producers are unaffected.                                 */
/******************************************************************************/

static ULONG sampleGradStops (ULONG *stops, UWORD numStops,
                              ULONG startA, ULONG endA, LONG u)
{
    LONG  segs;
    LONG  seg;
    LONG  pos;
    LONG  frac;
    ULONG c0;
    ULONG c1;
    LONG  a0, r0, g0, b0;
    LONG  a1, r1, g1, b1;
    LONG  a, r, g, b;

    if (u < 0) u = 0;
    if (u > DRW_FIXED_ONE) u = DRW_FIXED_ONE;

    if (!stops || numStops < 2)
    {
        c0   = startA;
        c1   = endA;
        frac = u;
    }
    else
    {
        segs = (LONG) numStops - 1;
        pos  = (LONG) ((ULONG) u * (ULONG) segs);   /* 16.16 * segs */
        seg  = pos >> 16;
        if (seg >= segs)
        {
            seg  = segs - 1;
            frac = DRW_FIXED_ONE;
        }
        else
        {
            frac = pos - (seg << 16);
        }
        c0 = stops[seg];
        c1 = stops[seg + 1];
    }

    a0 = (LONG) ((c0 >> 24) & 0xFF); r0 = (LONG) ((c0 >> 16) & 0xFF);
    g0 = (LONG) ((c0 >>  8) & 0xFF); b0 = (LONG) ( c0        & 0xFF);
    a1 = (LONG) ((c1 >> 24) & 0xFF); r1 = (LONG) ((c1 >> 16) & 0xFF);
    g1 = (LONG) ((c1 >>  8) & 0xFF); b1 = (LONG) ( c1        & 0xFF);

    a = a0 + (((a1 - a0) * frac) >> 16);
    r = r0 + (((r1 - r0) * frac) >> 16);
    g = g0 + (((g1 - g0) * frac) >> 16);
    b = b0 + (((b1 - b0) * frac) >> 16);

    return ((ULONG) a << 24) | ((ULONG) r << 16) |
           ((ULONG) g <<  8) |  (ULONG) b;
}


/*****************************************************************************/
/* renderGradientObject                                                      */
/*                                                                           */
/* Paint a DRFT_GRADIENT fill: build a clip mask of the shape, then sweep    */
/* scanlines computing a per-pixel colour band (linear projection, radial /  */
/* shape distance, or conic sweep angle) and emit one RectFill per constant- */
/* colour run.  The band ramp is sampled from the object's multi-stop array  */
/* (ProVector GRAD), warped by its GradInterp easing mode.                   */
/*****************************************************************************/

static void renderGradientObject (struct ClassBase *cb, struct VecVPort *vp,
                                  struct DrawObj *obj)
{
    struct RastPort *rp;
    UBYTE  *mask;
    UBYTE  *maskRow;
    ULONG   bpr;
    LONG    bx0, by0, bx1, by1;
    LONG    w, h;
    LONG    cx0, cy0, cx1, cy1;
    LONG    x, y;
    LONG    n;
    ULONG   startA, endA;
    ULONG  *stops;
    UWORD   numStops;
    LONG    interpMode;
    LONG    gradMode;
    LONG    gp;
    ULONG   argbRamp[64];
    UBYTE   penRamp[64];
    BOOL    trueColor;
    LONG    k;
    LONG    deg, dirx, diry, angDeg;
    LONG    smin, smax, srange, s;
    LONG    cxp, cyp, corner;
    LONG    gcx, gcy, gr;
    LONG    dxp, ydir;
    ULONG   d2, maxd2;
    LONG    band, prevBand, segStart;
    LONG    lx;
    LONG    dirx8, diry8;       /* direction cosines pre-shifted (>>8)            */
    LONG    dACC;               /* LINEAR: per-pixel change of acc                */
    LONG    acc;                /* LINEAR: incremental band accumulator           */
    LONG    linThr[65];         /* LINEAR: interpolated band boundaries           */
    LONG    dyc, rb;            /* RADIAL: y-distance to centre / boundary radius */
    ULONG   dyc2;               /* RADIAL: dyc squared, constant per scanline     */
    ULONG   rad2[65];           /* RADIAL: squared band-boundary radii (n+1 used) */
    LONG    degB;               /* CONIC: integer-degree angle of a sector edge   */
    LONG    bcos[64];           /* CONIC: cos of each sector-boundary ray (16.16) */
    LONG    bsin[64];           /* CONIC: sin of each sector-boundary ray (16.16) */
    LONG    edge;               /* 16.16 unwarped band boundary parameter         */
    LONG    edgeParam;          /* 16.16 GradInterp-warped boundary parameter     */

    if (!vp || !obj) return;
    rp = vp->vp_RPort;
    if (!rp) return;

    if (!objDeviceBounds (vp, obj, &bx0, &by0, &bx1, &by1)) return;
    if (bx1 < bx0 || by1 < by0) return;
    w = bx1 - bx0 + 1;
    h = by1 - by0 + 1;

    trueColor = vecvpIsTrueColor (vp);
    n = trueColor ? 64 : 32;

    startA     = obj->do_Attrs.da_FillColor;
    endA       = obj->do_Attrs.da_FillColor2;
    stops      = obj->do_Attrs.da_GradStops;
    numStops   = obj->do_Attrs.da_NumGradStops;
    interpMode = (LONG) obj->do_Attrs.da_GradInterp;
    gradMode   = (LONG) obj->do_Attrs.da_GradType;

    /* Build the colour ramp once.  ProVector's GradInterp does not warp the  *
     * colour stops themselves; it warps where each colour band is drawn.     *
     * Therefore the ramp samples stops uniformly here, and the geometry      *
     * boundaries below (linThr/rad2/conic rays/SHAPE scale) receive the      *
     * interpolation easing.                                                  */
    for (k = 0; k < n; k++)
    {
        gp = (n > 1) ? (LONG) (((ULONG) k << 16) / (ULONG) (n - 1)) : 0;
        argbRamp[k] = sampleGradStops (stops, numStops, startA, endA, gp);
        penRamp[k] = 1;
        if (!trueColor)
            penRamp[k] = vecvpPenForARGB (vp, argbRamp[k]);
    }

    mask = buildObjectMask (cb, vp, obj, bx0, by0, w, h, &bpr);
    if (!mask)
    {
        /* Allocation refused: fall back to a flat start-colour fill so the  *
         * object is still visible.                                          */
        vecvpSetFgARGB (vp, startA);
        if (obj->do_Type == DROT_RECT)
            vecvpFillBox (vp, obj->do_XMin, obj->do_YMin,
                          obj->do_XMax, obj->do_YMax);
        else if (obj->do_Type == DROT_ELLIPSE)
            vecvpFillEllipse (vp, obj->do_XMin, obj->do_YMin,
                              obj->do_XMax, obj->do_YMax);
        else
            fillPolyPath (vp, obj);

        return;
    }

    deg    = (obj->do_Attrs.da_GradAngle + 0x8000) >> 16;
    dirx   = fixCosDeg (deg);
    diry   = fixSinDeg (deg);
    angDeg = deg % 360;
    if (angDeg < 0) angDeg += 360;

    smin = 0x7FFFFFFFL;
    smax = (LONG) 0x80000000L;
    for (corner = 0; corner < 4; corner++)
    {
        cxp = (corner & 1) ? bx1 : bx0;
        cyp = (corner & 2) ? by1 : by0;
        s = (cxp * (dirx >> 8)) + (cyp * (diry >> 8));
        if (s < smin) smin = s;
        if (s > smax) smax = s;
    }
    srange = smax - smin;
    if (srange <= 0) srange = 1;

    gcx = bx0 + ((obj->do_Attrs.da_GradCX * w) >> 16);
    gcy = by0 + ((obj->do_Attrs.da_GradCY * h) >> 16);
    if (gradMode == DRGT_SHAPE)
    {
        if (renderShapeGradientObject (cb, vp, obj, argbRamp, penRamp,
                                       trueColor, n, interpMode,
                                       bx0, by0, w, h, mask, bpr,
                                       gcx, gcy))
        {
            FreeRaster ((PLANEPTR) mask, (ULONG) w, (ULONG) h);
            return;
        }
    }

    maxd2 = 0;
    for (corner = 0; corner < 4; corner++)
    {
        cxp = ((corner & 1) ? bx1 : bx0) - gcx;
        cyp = ((corner & 2) ? by1 : by0) - gcy;
        d2 = (ULONG) (cxp * cxp + cyp * cyp);
        if (d2 > maxd2) maxd2 = d2;
    }
    gr = (LONG) renderISqrt (maxd2);
    if (gr < 1) gr = 1;

    cx0 = bx0; cy0 = by0; cx1 = bx1; cy1 = by1;
    if (cx0 < vp->vp_Clip.MinX) cx0 = vp->vp_Clip.MinX;
    if (cy0 < vp->vp_Clip.MinY) cy0 = vp->vp_Clip.MinY;
    if (cx1 > vp->vp_Clip.MaxX) cx1 = vp->vp_Clip.MaxX;
    if (cy1 > vp->vp_Clip.MaxY) cy1 = vp->vp_Clip.MaxY;

    /* Pre-shift the direction cosines once and derive the LINEAR per-pixel    *
     * step.  The original loop performed a divide (LINEAR) or a square root   *
     * plus divide (RADIAL/SHAPE) for *every* covered pixel, which dominates   *
     * the cost of a full-canvas background fill on 68k.  Both are replaced    *
     * below by integer accumulators that advance with one add (and an         *
     * occasional band-boundary compare) per pixel.                            */
    dirx8 = dirx >> 8;
    diry8 = diry >> 8;
    dACC  = dirx8;              /* acc moves by this much for each x step      */

    if (gradMode == DRGT_LINEAR)
    {
        /* Band boundaries are warped by GradInterp in ProVector.  linThr[]  *
         * is in the same projection units as acc == s - smin, so the inner  *
         * loop only compares and nudges the current band.  The multiply is  *
         * split by 8+8 bits to keep 68k signed LONG arithmetic in range.    */
        for (k = 0; k <= n; k++)
        {
            edge = (k == n) ? DRW_FIXED_ONE
                            : (LONG) (((ULONG) k << 16) / (ULONG) n);
            edgeParam = gradInterpParam (interpMode, edge);
            linThr[k] = (srange * (edgeParam >> 8)) >> 8;
        }
    }
    else if (gradMode == DRGT_RADIAL || gradMode == DRGT_SHAPE)
    {
        /* Band b spans isqrt(d2) between two GradInterp-warped radii.        *
         * Precompute the squared lower bound of each band so the per-pixel   *
         * test compares the incrementally maintained squared distance        *
         * directly -- no sqrt and no divide in the inner loop.               */
        for (k = 0; k <= n; k++)
        {
            edge = (k == n) ? DRW_FIXED_ONE
                            : (LONG) (((ULONG) k << 16) / (ULONG) n);
            edgeParam = gradInterpParam (interpMode, edge);
            rb = ((LONG) gr * (edgeParam >> 8)) >> 8;
            rad2[k] = (ULONG) (rb * rb);
        }
    }
    else if (gradMode == DRGT_CONIC)
    {
        /* Sector boundary b sits at absolute angle angDeg plus the           *
         * GradInterp-warped fraction of a full sweep.  Cache its direction   *
         * cosines once so the per-pixel band walk only needs cross-product   *
         * sign tests (see conicBandFor) instead of an atan2.                 */
        for (k = 0; k < n; k++)
        {
            edge = (LONG) (((ULONG) k << 16) / (ULONG) n);
            edgeParam = gradInterpParam (interpMode, edge);
            degB = angDeg + (LONG) ((((edgeParam >> 8) * 360L) + 128L) >> 8);
            degB %= 360;
            bcos[k] = fixCosDeg (degB);
            bsin[k] = fixSinDeg (degB);
        }
    }

    /* Inner rasterisation loop.  Each covered run seeds its band once and    *
     * then carries it forward incrementally: LINEAR advances an accumulator  *
     * (acc += dACC) and only crosses a band boundary with a compare/add;     *
     * RADIAL/SHAPE advance the squared distance (d2 += 2*dx+1) and compare   *
     * against the precomputed rad2[] boundaries; CONIC steps between the     *
     * precomputed sector rays with cross-product sign tests (conicBandFor).  *
     * No path divides, takes a square root, or calls atan2 per pixel.        *
     * prevBand starts at -1 so the first covered pixel of each run always    *
     * emits without a duplicated computation.                                */
    for (y = cy0; y <= cy1; y++)
    {
        maskRow = mask + (ULONG) (y - by0) * bpr;
        ydir    = y * diry8;
        dyc     = y - gcy;
        dyc2    = (ULONG) (dyc * dyc);
        x = cx0;
        while (x <= cx1)
        {
            lx = x - bx0;
            if (!(maskRow[lx >> 3] & (0x80 >> (lx & 7))))
            {
                x++;
                continue;
            }

            /* Start of a covered run: seed the band at the run's first pixel  *
             * for the active gradient mode, then walk the run splitting on    *
             * band change.  The band is carried across pixels and advanced    *
             * incrementally at the foot of the loop -- no mode recomputes it  *
             * from scratch per pixel.                                         */
            segStart = x;
            prevBand = -1;

            switch (gradMode)
            {
                case DRGT_LINEAR:
                    acc  = x * dirx8 + ydir - smin;
                    band = 0;
                    while (band < n - 1 && linThr[band + 1] <= acc)
                        band++;
                    break;

                case DRGT_RADIAL:
                case DRGT_SHAPE:
                    dxp  = x - gcx;
                    d2   = (ULONG) (dxp * dxp) + dyc2;
                    band = 0;
                    while (band < n - 1 && rad2[band + 1] <= d2)
                        band++;
                    break;

                default:        /* DRGT_CONIC                                  */
                    dxp  = x - gcx;
                    band = conicBandFor (bcos, bsin, n, 0, dxp, dyc);
                    break;
            }

            while (x <= cx1)
            {
                lx = x - bx0;
                if (!(maskRow[lx >> 3] & (0x80 >> (lx & 7))))
                    break;

                if (band != prevBand)
                {
                    if (prevBand >= 0)
                        gradSpan (vp, trueColor, argbRamp[prevBand],
                                  penRamp[prevBand], segStart, x - 1, y);
                    segStart = x;
                    prevBand = band;
                }
                x++;

                /* Advance the cheap incremental state to the next pixel.     */
                switch (gradMode)
                {
                    case DRGT_LINEAR:
                        acc += dACC;
                        while (band < n - 1 && linThr[band + 1] <= acc)
                            band++;
                        while (band > 0 && acc < linThr[band])
                            band--;
                        break;

                    case DRGT_RADIAL:
                    case DRGT_SHAPE:
                        d2 += (ULONG) (2 * dxp + 1);
                        dxp++;
                        while (band < n - 1 && rad2[band + 1] <= d2)
                            band++;
                        while (band > 0 && d2 < rad2[band])
                            band--;
                        break;

                    default:        /* DRGT_CONIC                              */
                        dxp++;
                        band = conicBandFor (bcos, bsin, n, band, dxp, dyc);
                        break;
                }
            }
            if (prevBand >= 0)
                gradSpan (vp, trueColor, argbRamp[prevBand],
                          penRamp[prevBand], segStart, x - 1, y);
        }
    }

    FreeRaster ((PLANEPTR) mask, (ULONG) w, (ULONG) h);
}


/*****************************************************************************/
/* divFloor -- integer division rounding toward negative infinity.           */
/*****************************************************************************/

static LONG divFloor (LONG a, LONG b)
{
    LONG q;

    if (b == 0) return 0;
    q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}


/*****************************************************************************/
/* renderObjectFill                                                          */
/*                                                                           */
/* DRFT_OBJECTS (DR2D FT_OBJECTS / ProVector tiled fill).  The ATTR's        */
/* FillID selects a DROT_FILL prototype in the top-level display list.  Its  */
/* child objects form a tile (their combined bounding box is the cell) that  */
/* is repeated across the page and clipped to the filled object's shape.     */
/*                                                                           */
/* Implementation: render the tiles into a temporary, depth-matched bitmap   */
/* padded by one cell on each side (so Area fills of the pattern can never   */
/* write outside it), build a clip mask of the filled object, then composite */
/* only the masked pixels with BltMaskBitMapRastPort().  Pattern gaps are    */
/* painted in the cleared background rather than left transparent.           */
/*****************************************************************************/

static struct DrawObj *findFillPrototype (struct localData *lod, UWORD fillID)
{
    struct DrawObj *o;
    struct DrawObj *next;

    for (o = (struct DrawObj *) lod->lvi_ObjectsList.lh_Head;
         (next = (struct DrawObj *) o->do_Node.mln_Succ) != NULL;
         o = next)
    {
        if (o->do_Type == DROT_FILL &&
            o->do_Data.Fill.do_FillID == fillID)
            return o;
    }
    return NULL;
}


static void renderObjectFill (struct ClassBase *cb, struct localData *lod,
                              struct VecVPort *vp, struct DrawObj *obj,
                              UWORD rotation, UWORD quality, ULONG flags)
{
    struct DrawObj  *fill;
    struct BitMap   *tmpBM;
    struct RastPort  tmpRP;
    struct VecVPort  tvp;
    UBYTE  *mask;
    ULONG   bpr;
    LONG    bx0, by0, bx1, by1;
    LONG    w, h;
    LONG    pad;
    LONG    padX0, padY0, padW, padH;
    LONG    cx0, cy0, cx1, cy1;
    LONG    tileW, tileH;
    LONG    i0, i1, j0, j1, i, j;
    LONG    depth;
    ULONG   tileCount;

    if (!vp || !obj || !lod) return;

    fill = findFillPrototype (lod, obj->do_Attrs.da_FillID);
    if (!fill) return;

    if (!objDeviceBounds (vp, obj, &bx0, &by0, &bx1, &by1)) return;
    if (bx1 < bx0 || by1 < by0) return;
    w = bx1 - bx0 + 1;
    h = by1 - by0 + 1;
    if (w > 2048 || h > 2048) return;

    /* Tile cell in canvas units. */
    tileW = fill->do_XMax - fill->do_XMin;
    tileH = fill->do_YMax - fill->do_YMin;
    if (tileW < 0) tileW = -tileW;
    if (tileH < 0) tileH = -tileH;

    /* Pad the temp bitmap by one cell (device px) on each side so a tile    *
     * straddling the edge cannot drive an Area fill out of bounds.          */
    pad = 0;
    if (tileW > 0)
    {
        i0 = drwMulFixed (tileW, vp->vp_ScaleX) >> 16;
        if (i0 > pad) pad = i0;
    }
    if (tileH > 0)
    {
        i0 = drwMulFixed (tileH, vp->vp_ScaleY) >> 16;
        if (i0 > pad) pad = i0;
    }
    if (pad < 1)   pad = 1;
    if (pad > 512) pad = 512;

    padX0 = bx0 - pad;
    padY0 = by0 - pad;
    padW  = w + 2 * pad;
    padH  = h + 2 * pad;
    if (padW > 4096 || padH > 4096) return;
    if (((ULONG) padW * (ULONG) padH) > 2000000UL) return;

    depth = 8;
    if (rp_BitMapDepth (cb, vp->vp_RPort, &depth) == FALSE)
        depth = 8;

    tmpBM = AllocBitMap ((ULONG) padW, (ULONG) padH, (ULONG) depth,
                         BMF_CLEAR, vp->vp_RPort->BitMap);
    if (!tmpBM) return;

    mask = buildObjectMask (cb, vp, obj, padX0, padY0, padW, padH, &bpr);
    if (!mask)
    {
        FreeBitMap (tmpBM);
        return;
    }

    InitRastPort (&tmpRP);
    tmpRP.BitMap = tmpBM;

    /* Temp render context: same scale/palette as the live one, but drawing *
     * into the padded temp bitmap with the origin shifted per tile.        */
    vecvpInit (&tvp, cb, &tmpRP,
               vp->vp_DestX - padX0, vp->vp_DestY - padY0, padW, padH,
               vp->vp_OriginX, vp->vp_OriginY,
               vp->vp_ScaleX, vp->vp_ScaleY, 0);
    tvp.vp_Clip.MinX = 0;
    tvp.vp_Clip.MinY = 0;
    tvp.vp_Clip.MaxX = (WORD) (padW - 1);
    tvp.vp_Clip.MaxY = (WORD) (padH - 1);
    vecvpSetPalette (&tvp, vp->vp_ColorMap, vp->vp_RGBTable, vp->vp_NumColors);

    /* Tile index range covering the filled object's canvas bounds. */
    if (tileW > 0 && tileH > 0)
    {
        i0 = divFloor (obj->do_XMin - fill->do_XMin, tileW);
        i1 = divFloor (obj->do_XMax - fill->do_XMin, tileW);
        j0 = divFloor (obj->do_YMin - fill->do_YMin, tileH);
        j1 = divFloor (obj->do_YMax - fill->do_YMin, tileH);
    }
    else
    {
        i0 = i1 = 0;
        j0 = j1 = 0;
    }

    tileCount = (ULONG) (i1 - i0 + 1) * (ULONG) (j1 - j0 + 1);
    if (tileCount > 1024UL)
    {
        /* Pathological tile count: draw a single cell rather than spin. */
        i1 = i0;
        j1 = j0;
    }

    for (j = j0; j <= j1; j++)
    {
        for (i = i0; i <= i1; i++)
        {
            tvp.vp_OriginX = vp->vp_OriginX - i * tileW;
            tvp.vp_OriginY = vp->vp_OriginY - j * tileH;
            renderObjectListContext (cb, lod, &tvp,
                                     &fill->do_Data.Fill.do_Children,
                                     rotation, quality, flags, NULL, NULL);
        }
    }

    vecvpFree (&tvp);

    /* Composite the pattern onto the destination through the shape mask,    *
     * clipped to the live render rectangle.                                 */
    cx0 = bx0; cy0 = by0; cx1 = bx1; cy1 = by1;
    if (cx0 < vp->vp_Clip.MinX) cx0 = vp->vp_Clip.MinX;
    if (cy0 < vp->vp_Clip.MinY) cy0 = vp->vp_Clip.MinY;
    if (cx1 > vp->vp_Clip.MaxX) cx1 = vp->vp_Clip.MaxX;
    if (cy1 > vp->vp_Clip.MaxY) cy1 = vp->vp_Clip.MaxY;

    if (cx1 >= cx0 && cy1 >= cy0)
    {
        /* 0xE0 == (ABC|ABNC|ANBC): copy source where the mask bit is set,   *
         * leave the destination untouched where it is clear.                */
        BltMaskBitMapRastPort (tmpBM,
                               cx0 - padX0, cy0 - padY0,
                               vp->vp_RPort, cx0, cy0,
                               cx1 - cx0 + 1, cy1 - cy0 + 1,
                               0xE0L,
                               (PLANEPTR) mask);
    }

    FreeRaster ((PLANEPTR) mask, (ULONG) padW, (ULONG) padH);
    FreeBitMap (tmpBM);
}


/*****************************************************************************/
/* rp_BitMapDepth -- helper: read a RastPort's bitmap depth.                 */
/*****************************************************************************/

static BOOL rp_BitMapDepth (struct ClassBase *cb, struct RastPort *rp,
                            LONG *depthOut)
{
    ULONG d;

    if (!rp || !rp->BitMap) return FALSE;
    d = GetBitMapAttr (rp->BitMap, BMA_DEPTH);
    if (d < 1) return FALSE;
    if (depthOut) *depthOut = (LONG) d;
    return TRUE;
}


/*****************************************************************************/
/* drawRectObject / drawEllipseObject                                        */
/*****************************************************************************/

static void drawRectObject (struct VecVPort *vp, struct DrawObj *obj)
{
    if (obj->do_Attrs.da_FillType == DRFT_COLOR &&
        !isTransparentARGB (obj->do_Attrs.da_FillColor))
    {
        vecvpSetFgARGB (vp, obj->do_Attrs.da_FillColor);
        vecvpFillBox (vp, obj->do_XMin, obj->do_YMin,
                      obj->do_XMax, obj->do_YMax);
    }
    if (!isTransparentARGB (obj->do_Attrs.da_EdgeColor))
    {
        vecvpSetFgARGB (vp, obj->do_Attrs.da_EdgeColor);
        vecvpDrawBox (vp, obj->do_XMin, obj->do_YMin,
                      obj->do_XMax, obj->do_YMax);
    }
}


static void drawEllipseObject (struct VecVPort *vp, struct DrawObj *obj)
{
    if (obj->do_Attrs.da_FillType == DRFT_COLOR &&
        !isTransparentARGB (obj->do_Attrs.da_FillColor))
    {
        vecvpSetFgARGB (vp, obj->do_Attrs.da_FillColor);
        vecvpFillEllipse (vp, obj->do_XMin, obj->do_YMin,
                          obj->do_XMax, obj->do_YMax);
    }
    if (!isTransparentARGB (obj->do_Attrs.da_EdgeColor))
    {
        vecvpSetFgARGB (vp, obj->do_Attrs.da_EdgeColor);
        vecvpDrawEllipse (vp, obj->do_XMin, obj->do_YMin,
                          obj->do_XMax, obj->do_YMax);
    }
}


/*****************************************************************************/
/* objectOnHiddenLayer                                                       */
/*                                                                           */
/* Decides whether an object should be skipped because of its WhichLayer.    */
/*                                                                           */
/* ProVector uses non-displayed layers (LF_DISPLAYED clear) to hold          */
/* construction / backup / "storage" geometry that is NOT part of the        */
/* finished drawing -- e.g. RainReport.dr2d keeps a solid-black copy of the  */
/* umbrella canopy and a duplicate of the text-on-path baseline on the       */
/* hidden "TextStorage" layer.  Rendering those hidden layers paints a black */
/* slab over the visible gradient panels.  Inspection of every ProVector     */
/* sample shows the real artwork always lives on LF_DISPLAYED layers, so a   */
/* faithful viewer must honour the bit: render an object only when its layer */
/* is displayed.                                                             */
/*                                                                           */
/* Defensive defaults: with no LAYR table at all (lvi_LayerCount == 0) we    */
/* render everything, and an object whose WhichLayer matches no known layer  */
/* is also rendered (treating an unknown layer as visible is safer than      */
/* silently dropping content).                                               */
/*****************************************************************************/

static BOOL objectOnHiddenLayer (struct ClassBase *cb, struct localData *lod,
                                 ULONG flags, UWORD layerID)
{
    struct DrawLayer *layer;
    struct DrawLayer *lnext;
    static UWORD layerDbgCount = 0;

    (void) flags;

    if (!lod || lod->lvi_LayerCount == 0)
        return FALSE;

    for (layer = (struct DrawLayer *) lod->lvi_LayerList.lh_Head;
         (lnext = (struct DrawLayer *) layer->dl_Node.mln_Succ) != NULL;
         layer = lnext)
    {
        if (layer->dl_ID == layerID)
        {
            if (layerDbgCount < 8)
            {
                layerDbgCount++;
                vdbgL (cb, "  obj layerID=", (LONG) layerID);
                vdbgL (cb, "  layer flags=", (LONG) layer->dl_Flags);
            }

            /* Hide the object when its layer is not flagged displayed. */
            if (layer->dl_Flags & DRLF_DISPLAYED)
                return FALSE;
            return TRUE;
        }
    }

    /* Unknown layer: render rather than risk dropping real content. */
    return FALSE;
}


/*****************************************************************************/
/* findDrawFont                                                              */
/*                                                                           */
/* Locate the FONS table entry that STXT/TPTH WhichFont indexes.  The list   */
/* is populated by dr2d.datatype via DDTM_ADDFONT during the parse.          */
/*****************************************************************************/

static struct DrawFont *findDrawFont (struct localData *lod, UWORD fontID)
{
    struct DrawFont *font;
    struct DrawFont *next;

    if (!lod) return NULL;

    for (font = (struct DrawFont *) lod->lvi_FontList.lh_Head;
         (next = (struct DrawFont *) font->df_Node.mln_Succ) != NULL;
         font = next)
    {
        if (font->df_FontID == (UBYTE) fontID)
            return font;
    }
    return NULL;
}


/*****************************************************************************/
/* openSizedFont                                                             */
/*                                                                           */
/* Open a font scaled to the requested rendered height.  vector.image is     */
/* resolution-independent, so STXT text must be drawn at the size produced   */
/* by the current vector transform, not at a fixed Amiga bitmap font size.   */
/* We pass a TTextAttr with OT_PointHeight/OT_DeviceDPI so diskfont can hand */
/* the request to bullet.library as an outline-font point-height request.    */
/* Plain ta_YSize remains populated as a fallback for bitmap-only fonts.     */
/*                                                                           */
/* ProVector/Type1 names are mapped onto the stock Compugraphic outline      */
/* families first.  A NULL return means "keep the rastport's current font"   */
/* (diskfont.library absent or the requested face is not installed).         */
/*****************************************************************************/

static UBYTE renderLower (UBYTE c)
{
    if (c >= 'A' && c <= 'Z') return (UBYTE) (c + ('a' - 'A'));
    return c;
}


static BOOL renderNameStarts (STRPTR name, STRPTR prefix)
{
    UWORD i;

    if (!name || !prefix) return FALSE;
    i = 0;
    while (prefix[i])
    {
        if (renderLower ((UBYTE) name[i]) !=
            renderLower ((UBYTE) prefix[i]))
            return FALSE;
        i++;
    }
    return TRUE;
}


static BOOL renderNameContains (STRPTR name, STRPTR needle)
{
    UWORD i;
    UWORD j;

    if (!name || !needle) return FALSE;
    for (i = 0; name[i]; i++)
    {
        j = 0;
        while (needle[j] &&
               renderLower ((UBYTE) name[i + j]) ==
               renderLower ((UBYTE) needle[j]))
            j++;
        if (needle[j] == '\0') return TRUE;
    }
    return FALSE;
}


static STRPTR renderMapFontName (STRPTR name, UWORD fontFlags,
                                 UWORD *styleFlags)
{
    UWORD style;
    STRPTR mapped;

    style = styleFlags ? *styleFlags : 0;
    mapped = NULL;

    if (name && name[0])
    {
        if (renderNameContains (name, "italic") ||
            renderNameContains (name, "oblique") ||
            renderNameContains (name, "script"))
            style |= DRFS_ITALIC;
        if (renderNameContains (name, "bold") ||
            renderNameContains (name, "demi"))
            style |= DRFS_BOLD;

        if (renderNameStarts (name, "cg"))
            mapped = name;
        else if (renderNameStarts (name, "pv_roman") ||
                 renderNameContains (name, "serif") ||
                 renderNameStarts (name, "palatino") ||
                 renderNameStarts (name, "times"))
            mapped = (STRPTR) "CGTimes.font";
        else if (renderNameStarts (name, "pv_sans") ||
                 renderNameContains (name, "simple_stroke") ||
                 renderNameStarts (name, "avantgarde") ||
                 renderNameStarts (name, "helvetica"))
            mapped = (STRPTR) "CGTriumvirate.font";
        else
            mapped = name;
    }

    if (!mapped)
    {
        if (fontFlags & 0x0001)
            mapped = (STRPTR) "CGTimes.font";
        else
            mapped = (STRPTR) "CGTriumvirate.font";
    }

    if (styleFlags) *styleFlags = style;
    return mapped;
}


static struct TextFont *openSizedFont (struct ClassBase *cb, LONG heightPx,
                                       UWORD fontFlags, UWORD styleFlags,
                                       STRPTR preferName)
{
    struct TTextAttr tta;
    struct TagItem tags[4];
    struct TextFont *tf;
    LONG pointHeight;
    STRPTR names[4];
    STRPTR mappedName;
    UBYTE amigaStyle;
    UWORD i;

    if (!DiskfontBase) return NULL;

    /* Clamp to a sane range: sub-pixel text is unreadable noise and huge  *
     * sizes waste time/memory for no visible benefit on screen.           */
    if (heightPx < 5)   heightPx = 5;
    if (heightPx > 256) heightPx = 256;

    /* We already transformed the drawing's display-independent text height *
     * to device pixels.  Tell diskfont/bullet that this should be produced *
     * at 72 DPI, where one PostScript point maps to one device pixel.      *
     * This avoids treating DR2D's CharH as an Amiga bitmap pixel size,     *
     * while still allowing the vector transform to decide the final size.  */
    pointHeight = heightPx << 16;

    tags[0].ti_Tag  = OT_DeviceDPI;
    tags[0].ti_Data = (72UL << 16) | 72UL;
    tags[1].ti_Tag  = OT_PointHeight;
    tags[1].ti_Data = (ULONG) pointHeight;
    tags[2].ti_Tag  = OT_SetFactor;
    tags[2].ti_Data = (ULONG) DRW_FIXED_ONE;
    tags[3].ti_Tag  = TAG_DONE;
    tags[3].ti_Data = 0;

    /* fontFlags pack the FONS hints: bit0 = serif, bit1 = fixed-width.
     * DR2D/ProVector names are mapped to the Compugraphic outline families
     * that ship with stock Amiga systems, with topaz only as a last resort. */
    if (fontFlags & 0x0001)
    {
        names[0] = (STRPTR) "CGTimes.font";
        names[1] = (STRPTR) "CGTriumvirate.font";
        names[2] = (STRPTR) "courier.font";
        names[3] = (STRPTR) "topaz.font";
    }
    else if (fontFlags & 0x0002)
    {
        names[0] = (STRPTR) "courier.font";
        names[1] = (STRPTR) "CGTriumvirate.font";
        names[2] = (STRPTR) "CGTimes.font";
        names[3] = (STRPTR) "topaz.font";
    }
    else
    {
        names[0] = (STRPTR) "CGTriumvirate.font";
        names[1] = (STRPTR) "CGTimes.font";
        names[2] = (STRPTR) "courier.font";
        names[3] = (STRPTR) "topaz.font";
    }

    tf = NULL;
    mappedName = renderMapFontName (preferName, fontFlags, &styleFlags);
    amigaStyle = FSF_TAGGED;
    if (styleFlags & DRFS_BOLD)   amigaStyle |= FSF_BOLD;
    if (styleFlags & DRFS_ITALIC) amigaStyle |= FSF_ITALIC;

    if (mappedName && mappedName[0] != '\0')
    {
        tta.tta_Name  = mappedName;
        tta.tta_YSize = (UWORD) heightPx;
        tta.tta_Style = amigaStyle;
        tta.tta_Flags = FPF_DISKFONT;
        tta.tta_Tags  = tags;
        tf = OpenDiskFont ((struct TextAttr *) &tta);
    }

    for (i = 0; i < 4 && !tf; i++)
    {
        tta.tta_Name  = names[i];
        tta.tta_YSize = (UWORD) heightPx;
        /* FSF_TAGGED tells diskfont this is a TTextAttr, not a plain
         * TextAttr.  The OT_* tags are then forwarded to the outline
         * engine (bullet.library for standard Amiga outline fonts). */
        tta.tta_Style = amigaStyle;
        /* Do not set FPF_DESIGNED: that would force a hand-designed bitmap
         * size and prevent diskfont/bullet from scaling outlines. */
        tta.tta_Flags = FPF_DISKFONT;
        tta.tta_Tags  = tags;

        tf = OpenDiskFont ((struct TextAttr *) &tta);
    }

    return tf;
}


/*****************************************************************************/
/* drawTextObject                                                            */
/*                                                                           */
/* Renders an STXT/text-path string at its requested size and colour.        */
/* The font is opened scaled to CharH x the active vertical scale, set on    */
/* the rastport for the duration of the draw, then restored.  DR2D places    */
/* text by its baseline (BaseX/BaseY), which is exactly where Move()/Text()  */
/* anchor the glyph row, so no baseline fix-up is needed.                    */
/*****************************************************************************/

static void drawTextSpan (struct ClassBase *cb, struct RastPort *rp,
                          STRPTR txt, ULONG len,
                          LONG *penX, LONG *penY, LONG baseX, LONG lineH)
{
    ULONG start;
    ULONG i;
    LONG segLen;

    if (!rp || !txt || !penX || !penY) return;

    start = 0;
    for (i = 0; i <= len; i++)
    {
        if (i == len || txt[i] == '\n')
        {
            if (i > start)
            {
                segLen = (LONG) (i - start);
                Move (rp, *penX, *penY);
                Text (rp, txt + start, segLen);
                *penX += TextLength (rp, txt + start, segLen);
            }
            if (i < len && txt[i] == '\n')
            {
                *penX = baseX;
                *penY += lineH;
            }
            start = i + 1;
        }
    }
}


/*****************************************************************************/
/* isqrtLong                                                                 */
/*                                                                           */
/* Integer square root (floor) for non-negative LONG values, used to measure */
/* path-segment lengths in device pixels.  No floating point so it is safe   */
/* in the resident render path.                                              */
/*****************************************************************************/

static LONG isqrtLong (LONG v)
{
    ULONG x;
    ULONG bit;
    ULONG n;

    if (v <= 0) return 0;

    n   = (ULONG) v;
    x   = 0;
    bit = 1UL << 30;

    while (bit > n)
        bit >>= 2;

    while (bit != 0)
    {
        if (n >= x + bit)
        {
            n -= x + bit;
            x = (x >> 1) + bit;
        }
        else
        {
            x >>= 1;
        }
        bit >>= 2;
    }

    return (LONG) x;
}


/*****************************************************************************/
/* renderBuildOtagPath                                                        */
/*                                                                            */
/* Turn an Amiga font name ("CGTimes.font") into the full path of its outline */
/* tag file ("FONTS:CGTimes.otag"), which is what bullet.library needs to     */
/* select a typeface.  A trailing ".font" is replaced by ".otag"; a bare      */
/* family name simply gains the suffix.  A name already carrying a ':' device */
/* is taken verbatim (only its suffix is normalised).  Returns FALSE when the */
/* result would not fit, so the caller can skip this candidate.               */
/*****************************************************************************/

static BOOL renderBuildOtagPath (STRPTR fontName, UBYTE *out, ULONG outSize)
{
    STRPTR       tail;
    CONST UBYTE *dev;
    ULONG        len;
    ULONG        base;
    ULONG        i;
    ULONG        k;
    BOOL         hasDevice;

    if (!fontName || !fontName[0] || !out || outSize < 8)
        return FALSE;

    len       = 0;
    hasDevice = FALSE;
    while (fontName[len])
    {
        if (fontName[len] == ':') hasDevice = TRUE;
        len++;
    }

    /* Strip a trailing ".font" / ".otag" so we can re-attach ".otag".       */
    base = len;
    tail = (STRPTR) fontName + (base >= 5 ? base - 5 : 0);
    if (base >= 5 &&
        (renderNameStarts (tail, ".font") || renderNameStarts (tail, ".otag")))
        base -= 5;

    i   = 0;
    dev = (CONST UBYTE *) "FONTS:";
    if (!hasDevice)
        while (dev[i]) { out[i] = dev[i]; i++; }

    if (i + base + 5 + 1 > outSize)
        return FALSE;

    for (k = 0; k < base; k++)
        out[i++] = (UBYTE) fontName[k];
    out[i++] = '.';
    out[i++] = 'o';
    out[i++] = 't';
    out[i++] = 'a';
    out[i++] = 'g';
    out[i]   = '\0';
    return TRUE;
}


/******************************************************************************/
/* drawTextAlongPath                                                          */
/*                                                                            */
/* Lays a TPTH string out along its control path instead of as one straight   */
/* horizontal run from path[0].  ProVector titles ("text on a path") store    */
/* a polyline that the string should flow along; drawing it straight makes a  */
/* long title shoot off the page.  Each glyph is positioned at the point on   */
/* the polyline reached by accumulating glyph advance widths (arc length).    */
/*                                                                            */
/* Two renderers are used.  When bullet.library can open the run's outline    */
/* typeface, each glyph is rasterised rotated to the local path tangent (the  */
/* IntelliFont engine rotates a glyph's baseline via OT_RotateSin/Cos), which */
/* is how ProVector draws curved titles.  When the engine or the .otag file   */
/* is unavailable the code falls back to upright graphics.library Text() at   */
/* the baseline points -- legible and contained, just not rotated.            */
/******************************************************************************/

#define TPATH_MAX_POINTS 256

static void drawTextAlongPath (struct ClassBase *cb, struct VecVPort *vp,
                               struct DrawObj *obj, STRPTR txt, ULONG len,
                               LONG heightPx, STRPTR fontName,
                               UWORD fontFlags, UWORD styleFlags)
{
    struct RastPort  *rp;
    struct DrawPoint *path;
    ULONG  numPts;
    ULONG  i;
    ULONG  nUsable;
    ULONG  ci;
    ULONG  seg;
    LONG   ptX[TPATH_MAX_POINTS];   /* baseline points, device pixels */
    LONG   ptY[TPATH_MAX_POINTS];
    LONG   px;
    LONG   py;
    LONG   segDX;
    LONG   segDY;
    LONG   segLen;
    LONG   walked;     /* distance already consumed on the current segment */
    LONG   advance;
    LONG   gx;
    LONG   gy;
    LONG   sinFx;
    LONG   cosFx;
    struct GlyphRenderCtx glyf;
    BOOL   useEngine;
    STRPTR cand[5];
    UWORD  candCount;
    UWORD  ck;
    UWORD  styleLocal;
    UBYTE  otagPath[128];
    WORD   oldDrMd;

    if (!vp || !obj) return;
    rp = vp->vp_RPort;
    if (!rp) return;

    path   = obj->do_Data.TextPath.do_Path;
    numPts = obj->do_Data.TextPath.do_NumPath;
    if (!path || numPts < 2)
        return;

    /* Build a compact list of real baseline points in device pixels.        *
     * DR2D_IND markers were turned into zero-coord points carrying          *
     * MOVETO/BEZIER/END flags by the loader; those are control records, not *
     * coordinates, so skip them and stop at the first END marker.           */
    nUsable = 0;
    for (i = 0; i < numPts && nUsable < TPATH_MAX_POINTS; i++)
    {
        if (path[i].dp_Flags & DRPF_END)
            break;
        /* DR2D_IND control records (MOVETO/BEZIER markers) were stored by   *
         * the loader as zero-coordinate points.  ProVector text paths lead  *
         * with such a MOVETO marker, so skip every zero-coord marker, even  *
         * at index 0 -- otherwise the run starts at the canvas origin and   *
         * the first segment spans the whole drawing.                        */
        if ((path[i].dp_Flags & (DRPF_MOVETO | DRPF_BEZIER)) &&
            path[i].dp_X == 0 && path[i].dp_Y == 0)
            continue;

        vecvpMap (vp, path[i].dp_X, path[i].dp_Y, &px, &py);

        /* ProVector MOVETO markers restate the previous endpoint as the      *
         * next subpath start, so consecutive points often coincide.  Drop    *
         * the duplicates to avoid degenerate zero-length segments that would *
         * stall a glyph in place.                                            */
        if (nUsable > 0 && px == ptX[nUsable - 1] && py == ptY[nUsable - 1])
            continue;

        ptX[nUsable] = px;
        ptY[nUsable] = py;
        nUsable++;
    }

    if (nUsable < 2)
    {
        /* Degenerate path: fall back to a straight run from the start.      */
        if (nUsable == 1)
        {
            Move (rp, ptX[0], ptY[0]);
            for (ci = 0; ci < len; ci++)
                if (txt[ci] != '\n')
                    Text (rp, txt + ci, 1);
        }
        return;
    }

    /* Try to bind an outline typeface so glyphs can be rotated to the path  *
     * tangent.  Candidate order mirrors openSizedFont(): the run's own       *
     * (mapped) face first, then the stock Compugraphic fallbacks.  Bold and  *
     * italic synthesis is left to the upright fallback; the rotated path     *
     * uses the base family outline, which is what matters for curved titles. */
    useEngine = FALSE;
    glyf.grc_Engine   = NULL;
    glyf.grc_OTag     = NULL;
    glyf.grc_ChipBuf  = NULL;
    glyf.grc_ChipSize = 0;
    glyf.grc_EmWidth  = heightPx;

    styleLocal = styleFlags;
    candCount  = 0;
    cand[candCount++] = renderMapFontName (fontName, fontFlags, &styleLocal);
    if (fontFlags & 0x0002)
    {
        cand[candCount++] = (STRPTR) "courier.font";
        cand[candCount++] = (STRPTR) "CGTriumvirate.font";
        cand[candCount++] = (STRPTR) "CGTimes.font";
    }
    else if (fontFlags & 0x0001)
    {
        cand[candCount++] = (STRPTR) "CGTimes.font";
        cand[candCount++] = (STRPTR) "CGTriumvirate.font";
        cand[candCount++] = (STRPTR) "courier.font";
    }
    else
    {
        cand[candCount++] = (STRPTR) "CGTriumvirate.font";
        cand[candCount++] = (STRPTR) "CGTimes.font";
        cand[candCount++] = (STRPTR) "courier.font";
    }

    if (BulletBase && heightPx > 0)
    {
        for (ck = 0; ck < candCount && !useEngine; ck++)
        {
            if (!cand[ck] || !cand[ck][0])
                continue;
            if (!renderBuildOtagPath (cand[ck], otagPath, sizeof (otagPath)))
                continue;
            if (gleOpenFont (cb, &glyf, (STRPTR) otagPath,
                             heightPx, 72, 72))
                useEngine = TRUE;
        }
    }

    if (useEngine)
        vdbg (cb, "TPTH: rotated outline glyphs");

    seg    = 0;
    segDX  = ptX[1] - ptX[0];
    segDY  = ptY[1] - ptY[0];
    segLen = isqrtLong (segDX * segDX + segDY * segDY);
    if (segLen < 1) segLen = 1;
    walked = 0;

    oldDrMd = (WORD) rp->DrawMode;
    if (useEngine)
        SetDrMd (rp, JAM1);

    for (ci = 0; ci < len; ci++)
    {
        if (txt[ci] == '\n')
            continue;

        gx = ptX[seg] + (segDX * walked) / segLen;
        gy = ptY[seg] + (segDY * walked) / segLen;

        if (useEngine)
        {
            /* Tangent direction in font space (baseline +x, +y up): the      *
             * device path runs +y downward, so negate dy for the sine.       */
            sinFx = (-segDY << 16) / segLen;
            cosFx = ( segDX << 16) / segLen;

            advance = 0;
            if (!gleDrawGlyph (cb, &glyf, rp, (ULONG) (UBYTE) txt[ci],
                               gx, gy, sinFx, cosFx, &advance))
                advance = 0;
            if (advance < 1)
                advance = glyf.grc_EmWidth / 2;
        }
        else
        {
            Move (rp, gx, gy);
            Text (rp, txt + ci, 1);
            advance = (LONG) TextLength (rp, txt + ci, 1);
        }

        if (advance < 1) advance = 1;
        walked += advance;

        /* Step forward over any path segments this advance crossed.         */
        while (walked >= segLen && seg + 2 < nUsable)
        {
            walked -= segLen;
            seg++;
            segDX  = ptX[seg + 1] - ptX[seg];
            segDY  = ptY[seg + 1] - ptY[seg];
            segLen = isqrtLong (segDX * segDX + segDY * segDY);
            if (segLen < 1) segLen = 1;
        }
    }

    if (useEngine)
    {
        SetDrMd (rp, (ULONG) oldDrMd);
        gleCloseFont (cb, &glyf);
    }
}


static void drawTextObject (struct ClassBase *cb, struct localData *lod,
                            struct VecVPort *vp, struct DrawObj *obj)
{
    LONG   px;
    LONG   py;
    LONG   baseX;
    LONG   penX;
    LONG   penY;
    STRPTR txt;
    UWORD  len;
    LONG   charH;
    UWORD  fontFlags;
    UWORD  whichFont;
    LONG   heightPx;
    LONG   dxPx;
    LONG   dyPx;
    LONG   lineH;
    ULONG  colour;
    ULONG  runLen;
    UWORD  runCount;
    UWORD  runIndex;
    UWORD  styleFlags;
    struct DrawPoint *p;
    struct DrawFont  *df;
    struct TextFont  *tf;
    struct TextFont  *oldFont;
    struct TextFont  *active;
    struct RastPort  *rp;
    struct DrawTextRun *runs;
    struct DrawTextRun  fallbackRun;

    if (!vp || !obj) return;
    rp = vp->vp_RPort;
    if (!rp) return;

    fontFlags = 0;
    whichFont = 0;
    df = NULL;

    switch (obj->do_Type)
    {
        case DROT_TEXT:
            txt        = obj->do_Data.Text.do_String;
            len        = obj->do_Data.Text.do_NumChars;
            charH      = obj->do_Data.Text.do_CharH;
            whichFont  = obj->do_Data.Text.do_WhichFont;
            runs       = obj->do_Data.Text.do_Runs;
            runCount   = obj->do_Data.Text.do_NumRuns;
            vecvpMap (vp, obj->do_Data.Text.do_BaseX,
                      obj->do_Data.Text.do_BaseY, &px, &py);
            break;

        case DROT_TEXTPATH:
            txt        = obj->do_Data.TextPath.do_String;
            len        = obj->do_Data.TextPath.do_NumChars;
            charH      = obj->do_Data.TextPath.do_CharH;
            whichFont  = obj->do_Data.TextPath.do_WhichFont;
            runs       = obj->do_Data.TextPath.do_Runs;
            runCount   = obj->do_Data.TextPath.do_NumRuns;
            if (obj->do_Data.TextPath.do_Path &&
                obj->do_Data.TextPath.do_NumPath > 0)
            {
                p = &obj->do_Data.TextPath.do_Path[0];
                vecvpMap (vp, p->dp_X, p->dp_Y, &px, &py);
            }
            else
                return;
            break;

        default:
            return;
    }

    if (!txt || len == 0) return;

    /* Per-text-object diagnostics (logged for every text object, unlike the *
     * 4-object cap in the shape loop) so files whose text appears late in   *
     * the display list still report their size/placement.                   */
    vdbgL (cb, "text type=", (LONG) obj->do_Type);
    vdbgL (cb, "  text len=", (LONG) len);
    vdbgL (cb, "  text charH=", charH);
    vdbgL (cb, "  text px=", px);
    vdbgL (cb, "  text py=", py);

    /* Area fills borrow the RastPort's TmpRas/AreaInfo; make sure a prior *
     * polygon pass has released them before we call Move()/Text().        */
    if (vp->vp_AreaActive)
        vecvpEndArea (vp);

    if (!runs || runCount == 0)
    {
        fallbackRun.tr_Offset = 0;
        fallbackRun.tr_Length = len;
        fallbackRun.tr_FontID = whichFont;
        fallbackRun.tr_StyleFlags = 0;
        fallbackRun.tr_FillColor = DDTA_TRANSPARENT;
        fallbackRun.tr_PointSize = 0;
        fallbackRun.tr_DX = 0;
        fallbackRun.tr_DY = 0;
        fallbackRun.tr_Rotation = 0;
        fallbackRun.tr_Slant = 0;
        fallbackRun.tr_Aspect = DRW_FIXED_ONE;
        fallbackRun.tr_FontName = NULL;
        runs = &fallbackRun;
        runCount = 1;
    }

    baseX = px;
    penX = px;
    penY = py;

    for (runIndex = 0; runIndex < runCount; runIndex++)
    {
        if (runs[runIndex].tr_Offset >= (ULONG) len)
            continue;

        runLen = runs[runIndex].tr_Length;
        if (runs[runIndex].tr_Offset + runLen > (ULONG) len)
            runLen = (ULONG) len - runs[runIndex].tr_Offset;
        if (runLen == 0)
            continue;

        dxPx = drwMulFixed (runs[runIndex].tr_DX, vp->vp_ScaleX) >> 16;
        dyPx = drwMulFixed (runs[runIndex].tr_DY, vp->vp_ScaleY) >> 16;
        penX += dxPx;
        penY -= dyPx;

        df = findDrawFont (lod, runs[runIndex].tr_FontID ?
                                runs[runIndex].tr_FontID : whichFont);
        fontFlags = 0;
        styleFlags = runs[runIndex].tr_StyleFlags;
        if (df)
        {
            if (df->df_Serif) fontFlags |= 0x0001;
            if (!df->df_Proportional) fontFlags |= 0x0002;
            styleFlags |= (UWORD) df->df_StyleFlags;
        }

        colour = runs[runIndex].tr_FillColor;
        if (isTransparentARGB (colour))
            colour = obj->do_Attrs.da_FillColor;
        if (isTransparentARGB (colour))
            colour = obj->do_Attrs.da_EdgeColor;
        if (isTransparentARGB (colour))
            colour = 0xFF000000UL;
        vecvpSetFgARGB (vp, colour);

        if (runs[runIndex].tr_PointSize)
            heightPx = drwMulFixed (runs[runIndex].tr_PointSize,
                                    vp->vp_ScaleY) >> 16;
        else
            heightPx = drwMulFixed (charH, vp->vp_ScaleY) >> 16;
        if (heightPx < 0) heightPx = -heightPx;

        oldFont = NULL;
        tf = openSizedFont (cb, heightPx, fontFlags, styleFlags,
                            runs[runIndex].tr_FontName ?
                            runs[runIndex].tr_FontName :
                            (df ? df->df_Name : NULL));
        if (tf)
        {
            oldFont = rp->Font;
            SetFont (rp, tf);
        }

        active = rp->Font;
        lineH = active ? (LONG) active->tf_YSize : heightPx;
        if (lineH <= 0) lineH = (heightPx > 0) ? heightPx : 1;

        if (obj->do_Type == DROT_TEXTPATH &&
            obj->do_Data.TextPath.do_Path &&
            obj->do_Data.TextPath.do_NumPath >= 2)
            drawTextAlongPath (cb, vp, obj,
                               txt + runs[runIndex].tr_Offset, runLen,
                               heightPx,
                               runs[runIndex].tr_FontName ?
                               runs[runIndex].tr_FontName :
                               (df ? df->df_Name : NULL),
                               fontFlags, styleFlags);
        else
            drawTextSpan (cb, rp, txt + runs[runIndex].tr_Offset, runLen,
                          &penX, &penY, baseX, lineH);

        if (tf)
        {
            if (oldFont) SetFont (rp, oldFont);
            CloseFont (tf);
        }
    }
}


/*****************************************************************************/
/* isTransparentARGB                                                         */
/*                                                                           */
/* Centralised "should we skip drawing this colour?" test.  Returns TRUE     */
/* for any colour with zero alpha OR for the DDTA_TRANSPARENT sentinel       */
/* used by subclass loaders to mean "no fill / no edge".  Subclasses can     */
/* therefore freely emit either 0x00000000 or 0x01000000 to signal "skip"    */
/* without the renderer second-guessing them.                                */
/*                                                                           */
/* The actual 32-bit ARGB -> pen mapping lives in vrport.c (vecvpSetFgARGB / */
/* vecvpPenForARGB): it uses ObtainBestPen() against the destination         */
/* ColorMap for palette modes, a direct RGB pen for true-colour modes, and   */
/* the drawing's own RGBTable as a last resort.                              */
/*****************************************************************************/

static BOOL isTransparentARGB (ULONG argb)
{
    if ((argb & 0xFF000000UL) == 0) return TRUE;
    if (argb == DDTA_TRANSPARENT)   return TRUE;
    return FALSE;
}


/*****************************************************************************/
/* renderVectorMethod -- VIMM_RENDER                                          */
/*****************************************************************************/

ULONG renderVectorMethod (struct ClassBase *cb, Class *cl, Object *o,
                          struct vecRender *msg)
{
    struct localData *lod;
    LONG  sx;
    LONG  sy;
    LONG  destW;
    LONG  destH;

    (void) cl;

    if (!msg || !msg->vecr_RPort) return 0;

    lod = INST_DATA (cl, o);

    sx = scaleForRenderDPI (
             msg->vecr_FixedScaleX ? msg->vecr_FixedScaleX
                                    : lod->lvi_FixedScale,
             lod->lvi_RenderDPI);
    sy = scaleForRenderDPI (
             msg->vecr_FixedScaleY ? msg->vecr_FixedScaleY
                                    : lod->lvi_FixedScale,
             lod->lvi_RenderDPI);
    destW = msg->vecr_Width;
    destH = msg->vecr_Height;
    if (destW < 1) destW = 1;
    if (destH < 1) destH = 1;

    vdbg  (cb, "renderVectorMethod entered");
    vdbgHex (cb, "  scaleX=", sx);
    vdbgHex (cb, "  scaleY=", sy);
    vdbgL (cb, "  destX=", msg->vecr_Left);
    vdbgL (cb, "  destY=", msg->vecr_Top);
    vdbgL (cb, "  destW=", destW);
    vdbgL (cb, "  destH=", destH);
    vdbgHex (cb, "  canvasLeft=", lod->lvi_CanvasLeft);
    vdbgHex (cb, "  canvasTop=", lod->lvi_CanvasTop);
    vdbgHex (cb, "  canvasRight=", lod->lvi_CanvasRight);
    vdbgHex (cb, "  canvasBottom=", lod->lvi_CanvasBottom);
    vdbgL (cb, "  renderDPI=", (LONG) lod->lvi_RenderDPI);

    renderObjectList (cb, lod,
                      msg->vecr_RPort,
                      msg->vecr_ColorMap
                          ? msg->vecr_ColorMap
                          : lod->lvi_ColorMap,
                      &lod->lvi_ObjectsList,
                      msg->vecr_Left, msg->vecr_Top,
                      destW, destH,
                      sx, sy,
                      lod->lvi_CanvasLeft, lod->lvi_CanvasTop,
                      msg->vecr_Rotation,
                      msg->vecr_RenderQuality,
                      /* 0 is a legitimate RenderFlags value ("no optional   *
                       * bits"); merge with the class defaults instead of   *
                       * treating 0 as "unspecified" in a C ternary.       */
                      msg->vecr_RenderFlags | lod->lvi_RenderFlags,
                      msg->vecr_AbortPtr,
                      msg->vecr_ProgressPtr);
    return 1;
}


/*****************************************************************************/
/* renderVectorPixelsMethod -- VIMM_RENDERPIXELS                              */
/*****************************************************************************/

ULONG renderVectorPixelsMethod (struct ClassBase *cb, Class *cl, Object *o,
                                struct vecRenderPixels *msg)
{
    struct localData *lod;
    struct BitMap   *bm;
    struct RastPort  rp;
    LONG  depth;
    LONG  sx;
    LONG  sy;

    (void) cl;

    if (!msg || !msg->vecrp_PixelData) return 0;
    if (msg->vecrp_Width <= 0 || msg->vecrp_Height <= 0) return 0;

    lod = INST_DATA (cl, o);

    switch (msg->vecrp_PixelFormat)
    {
        case 0x00000008UL:
        case 0x00000009UL: depth = 8; break;
        default:           depth = 24; break;
    }

    bm = AllocBitMap ((ULONG) msg->vecrp_Width,
                      (ULONG) msg->vecrp_Height,
                      (ULONG) depth,
                      BMF_CLEAR, NULL);
    if (!bm) return 0;

    InitRastPort (&rp);
    rp.BitMap = bm;

    sx = scaleForRenderDPI (
             msg->vecrp_FixedScaleX ? msg->vecrp_FixedScaleX
                                     : lod->lvi_FixedScale,
             lod->lvi_RenderDPI);
    sy = scaleForRenderDPI (
             msg->vecrp_FixedScaleY ? msg->vecrp_FixedScaleY
                                     : lod->lvi_FixedScale,
             lod->lvi_RenderDPI);

    renderObjectList (cb, lod, &rp,
                      msg->vecrp_ColorMap,
                      &lod->lvi_ObjectsList,
                      0, 0,
                      msg->vecrp_Width, msg->vecrp_Height,
                      sx, sy,
                      lod->lvi_CanvasLeft, lod->lvi_CanvasTop,
                      msg->vecrp_Rotation,
                      msg->vecrp_RenderQuality,
                      msg->vecrp_RenderFlags | lod->lvi_RenderFlags,
                      msg->vecrp_AbortPtr,
                      msg->vecrp_ProgressPtr);

    /* Reading the rendered bitmap back into an arbitrary-format pixel     *
     * array needs cybergraphics.library/ReadPixelArray, which is not      *
     * available in the NDK we build against.  Disabled for now; a         *
     * graphics.library ReadPixelArray8 fallback (LUT8 only) or proper     *
     * cybergraphics support can be wired in later.  Until then the        *
     * VIMM_RENDERPIXELS method reports failure rather than returning      *
     * an unpopulated buffer.                                              */
#if 0
    ReadPixelArray (msg->vecrp_PixelData,
                    0, 0,
                    msg->vecrp_PixelArrayMod,
                    &rp,
                    0, 0,
                    msg->vecrp_Width, msg->vecrp_Height,
                    msg->vecrp_PixelFormat);
#endif

    WaitBlit ();
    FreeBitMap (bm);
    return 0;
}
