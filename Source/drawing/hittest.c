/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* hittest.c -- canvas-space hit testing and bounding-box queries            */
/*                                                                           */
/* The DDTM_HITTESTPOINT method maps a canvas-space (x, y) onto the          */
/* DrawObj that the point falls inside.  Used by editor applications that    */
/* need to identify a clicked object.                                        */
/*                                                                           */
/* DDTM_GETBOUNDINGBOX returns the bounding box of all top-level objects,    */
/* useful for "fit to window" or "zoom to extent" features.                  */
/*                                                                           */
/* DDTM_FITTOBOX computes the FixedScale required for the drawing to fit     */
/* a caller-supplied destination rectangle.                                  */
/*****************************************************************************/

#include "classbase.h"


/*****************************************************************************/
/* boxContainsPoint                                                          */
/*****************************************************************************/

static BOOL boxContainsPoint (struct DrawObj *obj, LONG x, LONG y)
{
    if (!obj) return FALSE;
    if (x < obj->do_XMin || x > obj->do_XMax) return FALSE;
    if (y < obj->do_YMin || y > obj->do_YMax) return FALSE;
    return TRUE;
}


/*****************************************************************************/
/* findObjectAtRecursive                                                     */
/*                                                                           */
/* Depth-first walk of the display list, returning the topmost object        */
/* whose bounding box contains the supplied canvas-space point.  We do       */
/* not currently perform precise hit testing against bezier outlines         */
/* etc. - bounding-box accuracy is enough for any conceivable picker UI      */
/* the class will host without becoming an editor in its own right.          */
/*****************************************************************************/

static struct DrawObj *findObjectAtRecursive (struct List *list,
                                              LONG x, LONG y)
{
    struct DrawObj *obj;
    struct DrawObj *hit;

    if (!list) return NULL;

    /* Walk back-to-front so the most-recently-added (topmost) wins.     */
    for (obj = (struct DrawObj *) list->lh_TailPred;
         obj && obj->do_Node.mln_Pred != NULL;
         obj = (struct DrawObj *) obj->do_Node.mln_Pred)
    {
        if (!boxContainsPoint (obj, x, y)) continue;

        switch (obj->do_Type)
        {
            case DROT_GROUP:
                hit = findObjectAtRecursive (&obj->do_Data.Group.do_Children,
                                             x, y);
                if (hit) return hit;
                break;
            case DROT_FILL:
                hit = findObjectAtRecursive (&obj->do_Data.Fill.do_Children,
                                             x, y);
                if (hit) return hit;
                break;
            default:
                return obj;
        }
    }
    return NULL;
}


/*****************************************************************************/
/* hitTestPointMethod -- DDTM_HITTESTPOINT                                   */
/*****************************************************************************/

ULONG hitTestPointMethod (struct ClassBase *cb, Class *cl, Object *o,
                          struct drwHitTestPoint *msg)
{
    struct localData *lod = INST_DATA (cl, o);
    struct DrawObj *hit;

    (void) cb;

    if (!msg || !msg->drwh_Result) return 0;

    hit = findObjectAtRecursive (drawingGetObjectList (lod),
                                 msg->drwh_X, msg->drwh_Y);
    *msg->drwh_Result = hit;
    return hit ? 1UL : 0UL;
}


/*****************************************************************************/
/* getBBoxMethod -- DDTM_GETBOUNDINGBOX                                      */
/*****************************************************************************/

ULONG getBBoxMethod (struct ClassBase *cb, Class *cl, Object *o,
                     struct drwGetBoundingBox *msg)
{
    struct localData *lod = INST_DATA (cl, o);
    struct DrawObj *obj;
    struct DrawObj *next;
    struct List *olist;
    LONG xmin;
    LONG ymin;
    LONG xmax;
    LONG ymax;
    BOOL any;

    (void) cb;

    if (!msg) return 0;

    /* Use the canvas extents (DRHD chunk in DR2D, viewBox in SVG) when *
     * the loader provided them; otherwise fall back to the union of    *
     * all top-level objects' bounding boxes.                           */
    if (lod->ldd_CanvasRight > lod->ldd_CanvasLeft &&
        lod->ldd_CanvasBottom > lod->ldd_CanvasTop)
    {
        if (msg->drwbb_XMin) *msg->drwbb_XMin = lod->ldd_CanvasLeft;
        if (msg->drwbb_YMin) *msg->drwbb_YMin = lod->ldd_CanvasTop;
        if (msg->drwbb_XMax) *msg->drwbb_XMax = lod->ldd_CanvasRight;
        if (msg->drwbb_YMax) *msg->drwbb_YMax = lod->ldd_CanvasBottom;
    }
    else
    {
        xmin = 0x7FFFFFFFL;
        ymin = 0x7FFFFFFFL;
        xmax = 0x80000000L;
        ymax = 0x80000000L;
        any = FALSE;

        olist = drawingGetObjectList (lod);
        if (olist)
        {
            for (obj = (struct DrawObj *) olist->lh_Head;
                 (next = (struct DrawObj *) obj->do_Node.mln_Succ) != NULL;
                 obj = next)
            {
                if (obj->do_XMin < xmin) xmin = obj->do_XMin;
                if (obj->do_YMin < ymin) ymin = obj->do_YMin;
                if (obj->do_XMax > xmax) xmax = obj->do_XMax;
                if (obj->do_YMax > ymax) ymax = obj->do_YMax;
                any = TRUE;
            }
        }
        if (!any)
        {
            xmin = 0;
            ymin = 0;
            xmax = 0;
            ymax = 0;
        }

        if (msg->drwbb_XMin) *msg->drwbb_XMin = xmin;
        if (msg->drwbb_YMin) *msg->drwbb_YMin = ymin;
        if (msg->drwbb_XMax) *msg->drwbb_XMax = xmax;
        if (msg->drwbb_YMax) *msg->drwbb_YMax = ymax;
    }

    return 1;
}


/*****************************************************************************/
/* fitToBoxMethod -- DDTM_FITTOBOX                                           */
/*                                                                           */
/* Compute the 16.16 FixedScale that fits the drawing into a destination     */
/* rectangle of pixel width drwf_BoxWidth and pixel height drwf_BoxHeight.   */
/* The result is returned through the caller's pointers; the class state     */
/* is NOT modified.  Use OM_SET / DDTA_FixedScale to commit the value.       */
/*****************************************************************************/

ULONG fitToBoxMethod (struct ClassBase *cb, Class *cl, Object *o,
                      struct drwFitToBox *msg)
{
    struct localData *lod = INST_DATA (cl, o);
    LONG canvasW, canvasH;
    LONG sx, sy;
    LONG dpi;
    LONG dpiFixed;
    LONG nativeW;
    LONG nativeH;

    (void) cb;

    if (!msg) return 0;
    if (msg->drwf_BoxWidth <= 0 || msg->drwf_BoxHeight <= 0) return 0;

    canvasW = lod->ldd_CanvasRight  - lod->ldd_CanvasLeft;
    canvasH = lod->ldd_CanvasBottom - lod->ldd_CanvasTop;
    if (canvasW <= 0 || canvasH <= 0) return 0;

    dpi = (LONG) (lod->ldd_NativeDPI ? lod->ldd_NativeDPI : 72);

    /* Canvas is in display-independent pixels (one dip = one PostScript  *
     * point = 1/72 inch); native pixels at base scale = canvas*(dpi/72). */
    dpiFixed = (LONG) (((ULONG) dpi << 16) / 72UL);
    nativeW = drwMulFixed (canvasW, dpiFixed) >> 16;
    nativeH = drwMulFixed (canvasH, dpiFixed) >> 16;
    if (nativeW < 1) nativeW = 1;
    if (nativeH < 1) nativeH = 1;
    sx = (LONG) (((double) msg->drwf_BoxWidth  * 65536.0)
                                              / (double) nativeW);
    sy = (LONG) (((double) msg->drwf_BoxHeight * 65536.0)
                                              / (double) nativeH);

    if (msg->drwf_PreserveAspect)
    {
        if (sx < sy) sy = sx;
        else         sx = sy;
    }

    if (msg->drwf_FixedScaleX) *msg->drwf_FixedScaleX = sx;
    if (msg->drwf_FixedScaleY) *msg->drwf_FixedScaleY = sy;
    return 1;
}
