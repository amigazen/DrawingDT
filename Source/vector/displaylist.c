/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* displaylist.c -- the display-list data structure for drawing.datatype     */
/*                                                                            */
/* The core data structure of the class is a hierarchical list of            */
/* struct DrawObj.  Subclass loaders, application code (via DDTM_ADDOBJECT) */
/* and the OS4 version compatibility shims (DDTM_ADDPOLY) all append into     */
/* this list through the helpers below.                                    */
/*                                                                            */
/* Memory ownership                                                          */
/* ----------------                                                          */
/* Every node in the display list is allocated from lod->lvi_Pool with       */
/* poolAlloc().  OM_DISPOSE drops the entire pool in one DeletePool() so   */
/* the helpers below never have to free anything; appending is monotonic. */
/*                                                                            */
/* Grouping                                                                 */
/* --------                                                                 */
/* DDTM_BEGINGROUP pushes a frame onto lvi_GroupStack and re-targets         */
/* appendObject() at the new group's child list.  DDTM_ENDGROUP pops the  */
/* frame, leaving the now-finished group in its parent's child list (or  */
/* the root list if the stack is empty).                                  */
/*****************************************************************************/

#include "classbase.h"


/*****************************************************************************/
/* initDisplayList                                                            */
/*                                                                            */
/* Called from OM_NEW after the pool has been allocated.  Sets the         */
/* DrawingInfo back-pointers and zeroes the group stack.                  */
/*****************************************************************************/

void initDisplayList (struct ClassBase *cb, struct localData *lod)
{
    (void) cb;

    NewList (&lod->lvi_ObjectsList);
    NewList (&lod->lvi_LayerList);
    NewList (&lod->lvi_FontList);

    lod->lvi_GroupStack.gs_Top = 0;
    lod->lvi_PolyBuilder.pb_Open       = FALSE;
    lod->lvi_PolyBuilder.pb_HasBezier  = FALSE;
    lod->lvi_PolyBuilder.pb_Used       = 0;
    lod->lvi_PolyBuilder.pb_Allocated  = 0;
    lod->lvi_PolyBuilder.pb_Points     = NULL;
    lod->lvi_PolyBuilder.pb_LineColor  = 0xFF000000UL;
    lod->lvi_PolyBuilder.pb_FillColor  = 0xFFFFFFFFUL;
}


/*****************************************************************************/
/* clearDisplayList                                                           */
/*                                                                            */
/* DDTM_CLEARDRAWING handler.  Drops every list head but does NOT free      */
/* the pool memory itself - the next allocation will reuse the freed     */
/* nodes from the pool's internal free list.                            */
/*****************************************************************************/

void clearDisplayList (struct ClassBase *cb, struct localData *lod)
{
    (void) cb;

    NewList (&lod->lvi_ObjectsList);
    NewList (&lod->lvi_LayerList);
    NewList (&lod->lvi_FontList);

    lod->lvi_ObjectCount = 0;
    lod->lvi_LayerCount  = 0;
    lod->lvi_FontCount   = 0;
    lod->lvi_GroupStack.gs_Top = 0;
    lod->lvi_PolyBuilder.pb_Open      = FALSE;
    lod->lvi_PolyBuilder.pb_HasBezier = FALSE;
    lod->lvi_PolyBuilder.pb_Used      = 0;

    lod->lvi_NumPaletteColors = 0;

    lod->lvi_Flags |= LVIF_CACHE_DIRTY;
}


/*****************************************************************************/
/* appendObject                                                               */
/*                                                                            */
/* Adds an already-built DrawObj to the current list - the topmost group   */
/* on lvi_GroupStack if any, otherwise the root list.                     */
/*****************************************************************************/

ULONG appendObject (struct ClassBase *cb, struct localData *lod,
                    struct DrawObj *obj)
{
    struct DrawObj *parent;
    struct List    *target;

    (void) cb;

    if (!obj) return 0;

    if (lod->lvi_GroupStack.gs_Top > 0)
    {
        parent = lod->lvi_GroupStack.gs_Items
                   [lod->lvi_GroupStack.gs_Top - 1];
        switch (parent->do_Type)
        {
            case DROT_GROUP:
                target = &parent->do_Data.Group.do_Children;
                parent->do_Data.Group.do_NumChildren++;
                break;
            case DROT_FILL:
                target = &parent->do_Data.Fill.do_Children;
                break;
            case DROT_EXTERNAL:
                parent->do_Data.External.do_Child = obj;
                lod->lvi_Flags |= LVIF_CACHE_DIRTY;
                return 1;
            default:
                target = &lod->lvi_ObjectsList;
                lod->lvi_ObjectCount++;
                break;
        }
    }
    else
    {
        target = &lod->lvi_ObjectsList;
        lod->lvi_ObjectCount++;
    }

    AddTail (target, (struct Node *) obj);
    lod->lvi_Flags |= LVIF_CACHE_DIRTY;

    return 1;
}


/*****************************************************************************/
/* beginGroupObject                                                           */
/*                                                                            */
/* DDTM_BEGINGROUP handler: spawn a fresh DROT_GROUP (or DROT_FILL /         */
/* DROT_EXTERNAL) DrawObj, append it to the current list, then push it    */
/* onto the group stack so subsequent DDTM_ADDOBJECT calls land inside.  */
/*****************************************************************************/

ULONG beginGroupObject (struct ClassBase *cb, struct localData *lod,
                        UWORD groupType, ULONG expect, ULONG fillID)
{
    struct DrawObj *grp;

    if (lod->lvi_GroupStack.gs_Top >= LVI_GROUP_STACK_DEPTH)
        return 0;

    grp = poolDrawObj (cb, lod, groupType);
    if (!grp) return 0;

    switch (groupType)
    {
        case DROT_GROUP:
            NewList (&grp->do_Data.Group.do_Children);
            grp->do_Data.Group.do_NumChildren = 0;
            lod->lvi_DrawingFlags |= DRWF_HAS_GROUPS;
            break;
        case DROT_FILL:
            NewList (&grp->do_Data.Fill.do_Children);
            grp->do_Data.Fill.do_FillID = (UWORD) fillID;
            lod->lvi_DrawingFlags |= DRWF_HAS_PATTERN;
            break;
        case DROT_EXTERNAL:
            grp->do_Data.External.do_Child = NULL;
            grp->do_Data.External.do_Name  = NULL;
            grp->do_Data.External.do_NameLen = 0;
            grp->do_Data.External.do_CallBacks = 0;
            break;
        default:
            return 0;
    }

    (void) expect;

    appendObject (cb, lod, grp);

    lod->lvi_GroupStack.gs_Items[lod->lvi_GroupStack.gs_Top++] = grp;

    return 1;
}


/*****************************************************************************/
/* endGroupObject -- DDTM_ENDGROUP                                            */
/*****************************************************************************/

ULONG endGroupObject (struct ClassBase *cb, struct localData *lod)
{
    struct DrawObj *grp;

    (void) cb;

    if (lod->lvi_GroupStack.gs_Top == 0)
        return 0;

    grp = lod->lvi_GroupStack.gs_Items
            [--lod->lvi_GroupStack.gs_Top];

    /* Recompute the group's own bounding box from its children so that
     * culling decisions later in the pipeline have something to work
     * with.                                                              */
    if (grp)
    {
        computeObjectBounds (grp);
    }

    return 1;
}


/*****************************************************************************/
/* poolDrawObj                                                                */
/*                                                                            */
/* Helper used by subclass loaders and primitives.c to allocate a fresh    */
/* DrawObj from the per-instance pool with a sensible default DrawAttr.  */
/*****************************************************************************/

struct DrawObj *poolDrawObj (struct ClassBase *cb, struct localData *lod,
                             UWORD type)
{
    struct DrawObj *obj;

    obj = (struct DrawObj *) poolAllocClear (cb, lod, sizeof (struct DrawObj));
    if (!obj) return NULL;

    obj->do_Type = type;
    obj->do_Attrs.da_FillType   = DRFT_NONE;
    obj->do_Attrs.da_JoinType   = DRJT_MITER;
    obj->do_Attrs.da_CapType    = DRCT_BUTT;
    obj->do_Attrs.da_FillColor  = 0xFFFFFFFFUL;
    obj->do_Attrs.da_EdgeColor  = 0xFF000000UL;
    obj->do_Attrs.da_EdgeThick  = DRW_FIXED_ONE;
    obj->do_Attrs.da_FillOpacity = DRW_FIXED_ONE;
    obj->do_Attrs.da_LayerID    = 0;

    obj->do_XMin = obj->do_YMin = 0x7FFFFFFFL;
    obj->do_XMax = obj->do_YMax = 0x80000000L;

    return obj;
}


/*****************************************************************************/
/* poolDrawPoints                                                             */
/*                                                                            */
/* Allocate a vector of DrawPoint structures from the pool.                */
/*****************************************************************************/

struct DrawPoint *poolDrawPoints (struct ClassBase *cb,
                                  struct localData *lod, ULONG nPoints)
{
    ULONG sz;

    if (nPoints == 0) return NULL;
    sz = nPoints * sizeof (struct DrawPoint);
    return (struct DrawPoint *) poolAlloc (cb, lod, sz);
}


/*****************************************************************************/
/* computeObjectBounds                                                        */
/*                                                                            */
/* Walks an object's geometry and updates do_XMin/YMin/XMax/YMax.  For    */
/* GROUP / FILL / EXTERNAL we recurse into the children.  Coordinates    */
/* with the DR2D "indicator" flag DRPF_END / DRPF_CLOSE skip the bounds */
/* update; only points that contribute to drawing affect the box.        */
/*****************************************************************************/

static void mergeBounds (struct DrawObj *into, LONG x, LONG y)
{
    if (x < into->do_XMin) into->do_XMin = x;
    if (y < into->do_YMin) into->do_YMin = y;
    if (x > into->do_XMax) into->do_XMax = x;
    if (y > into->do_YMax) into->do_YMax = y;
}


void computeObjectBounds (struct DrawObj *obj)
{
    ULONG  i;
    struct DrawObj *child;
    struct DrawObj *next;
    struct List    *children;
    struct DrawPoint *p;

    if (!obj) return;

    obj->do_XMin = obj->do_YMin = 0x7FFFFFFFL;
    obj->do_XMax = obj->do_YMax = 0x80000000L;

    switch (obj->do_Type)
    {
        case DROT_POLYGON:
        case DROT_POLYLINE:
        case DROT_BEZIER:
            for (i = 0; i < obj->do_Data.Poly.do_NumPoints; i++)
            {
                p = obj->do_Data.Poly.do_Points + i;
                if (p->dp_Flags & (DRPF_END | DRPF_CLOSE))
                    continue;
                mergeBounds (obj, p->dp_X, p->dp_Y);
            }
            break;

        case DROT_RECT:
        case DROT_ELLIPSE:
            /* bounds already set by caller */
            break;

        case DROT_TEXT:
            mergeBounds (obj, obj->do_Data.Text.do_BaseX,
                              obj->do_Data.Text.do_BaseY);
            mergeBounds (obj,
                obj->do_Data.Text.do_BaseX +
                    (LONG) obj->do_Data.Text.do_NumChars *
                    obj->do_Data.Text.do_CharW,
                obj->do_Data.Text.do_BaseY +
                    obj->do_Data.Text.do_CharH);
            break;

        case DROT_TEXTPATH:
            for (i = 0; i < obj->do_Data.TextPath.do_NumPath; i++)
            {
                p = obj->do_Data.TextPath.do_Path + i;
                if (p->dp_Flags & (DRPF_END | DRPF_CLOSE)) continue;
                mergeBounds (obj, p->dp_X, p->dp_Y);
            }
            break;

        case DROT_BITMAP:
            mergeBounds (obj, obj->do_Data.Bitmap.do_XPos,
                              obj->do_Data.Bitmap.do_YPos);
            mergeBounds (obj,
                obj->do_Data.Bitmap.do_XPos + obj->do_Data.Bitmap.do_Width,
                obj->do_Data.Bitmap.do_YPos + obj->do_Data.Bitmap.do_Height);
            break;

        case DROT_GROUP:
        case DROT_FILL:
            children = (obj->do_Type == DROT_GROUP)
                         ? &obj->do_Data.Group.do_Children
                         : &obj->do_Data.Fill.do_Children;
            for (child = (struct DrawObj *) children->lh_Head;
                 (next = (struct DrawObj *) child->do_Node.mln_Succ) != NULL;
                 child = next)
            {
                computeObjectBounds (child);
                mergeBounds (obj, child->do_XMin, child->do_YMin);
                mergeBounds (obj, child->do_XMax, child->do_YMax);
            }
            break;

        case DROT_EXTERNAL:
            if (obj->do_Data.External.do_Child)
            {
                computeObjectBounds (obj->do_Data.External.do_Child);
                mergeBounds (obj,
                    obj->do_Data.External.do_Child->do_XMin,
                    obj->do_Data.External.do_Child->do_YMin);
                mergeBounds (obj,
                    obj->do_Data.External.do_Child->do_XMax,
                    obj->do_Data.External.do_Child->do_YMax);
            }
            break;

        default:
            break;
    }

    /* Sanity: if no points were merged, collapse to zero rectangle.       */
    if (obj->do_XMin == 0x7FFFFFFFL)
    {
        obj->do_XMin = obj->do_YMin = 0;
        obj->do_XMax = obj->do_YMax = 0;
    }
}


/*****************************************************************************/
/* recomputeDrawingBounds                                                     */
/*                                                                            */
/* Walks the top-level display list and updates lvi_CanvasLeft / Top /     */
/* Right / Bottom if no canvas extents were set explicitly (i.e. by a     */
/* subclass loader from a DR2D DRHD chunk or an SVG viewBox).            */
/*****************************************************************************/

void recomputeDrawingBounds (struct localData *lod)
{
    struct DrawObj *obj;
    struct DrawObj *next;
    LONG  xmin, ymin, xmax, ymax;
    BOOL  any = FALSE;

    if (!lod) return;

    xmin = ymin = 0x7FFFFFFFL;
    xmax = ymax = 0x80000000L;

    for (obj = (struct DrawObj *) lod->lvi_ObjectsList.lh_Head;
         (next = (struct DrawObj *) obj->do_Node.mln_Succ) != NULL;
         obj = next)
    {
        computeObjectBounds (obj);
        if (obj->do_XMax >= obj->do_XMin && obj->do_YMax >= obj->do_YMin)
        {
            if (obj->do_XMin < xmin) xmin = obj->do_XMin;
            if (obj->do_YMin < ymin) ymin = obj->do_YMin;
            if (obj->do_XMax > xmax) xmax = obj->do_XMax;
            if (obj->do_YMax > ymax) ymax = obj->do_YMax;
            any = TRUE;
        }
    }

    if (!any)
        return;

    /* Only update if the caller / subclass loader has not set explicit
     * canvas extents (canvas defaults to 0..8 inches in OM_NEW).        */
    if (lod->lvi_CanvasRight == DRW_INT_TO_FIXED (8) &&
        lod->lvi_CanvasBottom == DRW_INT_TO_FIXED (8) &&
        lod->lvi_CanvasLeft == 0 &&
        lod->lvi_CanvasTop  == 0)
    {
        lod->lvi_CanvasLeft   = xmin;
        lod->lvi_CanvasTop    = ymin;
        lod->lvi_CanvasRight  = xmax;
        lod->lvi_CanvasBottom = ymax;
    }

    syncCanvasExtents (lod);
}


/*****************************************************************************/
/* syncCanvasExtents                                                          */
/*****************************************************************************/

void syncCanvasExtents (struct localData *lod)
{
    lod->lvi_Flags |= LVIF_CACHE_DIRTY;
}
