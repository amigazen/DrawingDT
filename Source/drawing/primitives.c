/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* primitives.c -- display-list construction methods                         */
/*                                                                           */
/* This module implements:                                                   */
/*                                                                           */
/*   * The OS4 version draft compatibility methods DDTM_ADDPOLY,             */
/*     DDTM_ADDATTR and DDTM_SETSIZE.  Together they let an application      */
/*     stream a drawing into a freshly-created drawing.datatype object       */
/*     a point at a time.                                                    */
/*                                                                           */
/*   * The modern DDTM_ADDOBJECT family, used by subclass loaders to         */
/*     append already-constructed DrawObj nodes (the DR2D subclass uses      */
/*     these to convert IFF chunks into display-list objects).               */
/*                                                                           */
/*   * Auxiliary construction methods: DDTM_BEGINGROUP, DDTM_ENDGROUP,       */
/*     DDTM_ADDLAYER, DDTM_ADDCOLOR, DDTM_ADDFONT, DDTM_CLEARDRAWING,        */
/*     DDTM_SETBOUNDS.                                                       */
/*****************************************************************************/

#include "classbase.h"


/*****************************************************************************/
/* commitPolyBuilder                                                         */
/*                                                                           */
/* The OS4 version DDTM_ADDPOLY contract is streaming: an application calls  */
/* DDTM_ADDPOLY with a series of LINE / BEZIER / MOVETO commands and then    */
/* a final CLOSE (closed polygon) or END (open polyline).  When that         */
/* terminator arrives we copy the accumulated points into a fresh, pool-     */
/* owned DrawObj and append to the display list.                             */
/*****************************************************************************/

static void commitPolyBuilder (struct ClassBase *cb, struct localData *lod,
                               BOOL closed)
{
    struct DrawObj   *obj;
    struct DrawPoint *dst;
    struct PolyBuilder *pb;
    ULONG  i;

    pb = &lod->ldd_PolyBuilder;
    if (pb->pb_Used == 0)
    {
        pb->pb_Open = FALSE;
        return;
    }

    obj = poolDrawObj (cb, lod,
                       closed ? DROT_POLYGON : DROT_POLYLINE);
    if (!obj)
    {
        pb->pb_Used = 0;
        pb->pb_Open = FALSE;
        return;
    }

    dst = poolDrawPoints (cb, lod, pb->pb_Used);
    if (!dst)
    {
        pb->pb_Used = 0;
        pb->pb_Open = FALSE;
        return;
    }

    for (i = 0; i < pb->pb_Used; i++)
        dst[i] = pb->pb_Points[i];

    obj->do_Data.Poly.do_Points    = dst;
    obj->do_Data.Poly.do_NumPoints = pb->pb_Used;
    obj->do_Attrs.da_EdgeColor     = pb->pb_LineColor;
    obj->do_Attrs.da_FillColor     = pb->pb_FillColor;
    obj->do_Attrs.da_FillType      = closed ? DRFT_COLOR : DRFT_NONE;

    if (pb->pb_HasBezier)
        lod->ldd_DrawingFlags |= DRWF_HAS_BEZIER;

    computeObjectBounds (obj);
    appendObject (cb, lod, obj);

    pb->pb_Used      = 0;
    pb->pb_Open      = FALSE;
    pb->pb_HasBezier = FALSE;
}


/*****************************************************************************/
/* growBuilder -- enlarge the PolyBuilder point buffer if needed             */
/*****************************************************************************/

static BOOL growBuilder (struct ClassBase *cb, struct localData *lod,
                         ULONG needed)
{
    struct PolyBuilder *pb = &lod->ldd_PolyBuilder;
    ULONG newCap;
    struct DrawPoint *newPts;
    ULONG  i;

    if (needed <= pb->pb_Allocated) return TRUE;

    newCap = pb->pb_Allocated ? pb->pb_Allocated : LDD_POLY_INITIAL;
    while (newCap < needed) newCap += LDD_POLY_GROW;

    newPts = (struct DrawPoint *) poolAlloc (cb, lod,
                                             newCap * sizeof (struct DrawPoint));
    if (!newPts) return FALSE;

    for (i = 0; i < pb->pb_Used; i++)
        newPts[i] = pb->pb_Points[i];

    pb->pb_Points    = newPts;
    pb->pb_Allocated = newCap;
    return TRUE;
}


/*****************************************************************************/
/* addPolyMethod -- DDTM_ADDPOLY                                             */
/*****************************************************************************/

ULONG addPolyMethod (struct ClassBase *cb, Class *cl, Object *o,
                     struct ddtPolygon *msg)
{
    struct localData *lod = INST_DATA (cl, o);
    struct PolyBuilder *pb = &lod->ldd_PolyBuilder;
    LONG  fx, fy;
    LONG  fx2, fy2;
    LONG  fx3, fy3;
    ULONG type;

    if (!msg) return 0;

    type = msg->ddtp_Type;

    switch (type)
    {
        case DDTP_MOVETO:
            /* A MOVETO inside an already-open path implicitly starts a
             * new sub-path; mark the current builder point with
             * DRPF_MOVETO and keep going.                                */
            if (!growBuilder (cb, lod, pb->pb_Used + 1)) return 0;
            fx = drwFloatToFixed (msg->ddtp_PosX);
            fy = drwFloatToFixed (msg->ddtp_PosY);
            pb->pb_Points[pb->pb_Used].dp_X     = fx;
            pb->pb_Points[pb->pb_Used].dp_Y     = fy;
            pb->pb_Points[pb->pb_Used].dp_Flags = DRPF_MOVETO;
            pb->pb_Used++;
            pb->pb_Open = TRUE;
            return 1;

        case DDTP_LINE:
            if (!growBuilder (cb, lod, pb->pb_Used + 1)) return 0;
            fx = drwFloatToFixed (msg->ddtp_PosX);
            fy = drwFloatToFixed (msg->ddtp_PosY);
            pb->pb_Points[pb->pb_Used].dp_X     = fx;
            pb->pb_Points[pb->pb_Used].dp_Y     = fy;
            pb->pb_Points[pb->pb_Used].dp_Flags = DRPF_NORMAL;
            pb->pb_Used++;
            pb->pb_Open = TRUE;
            return 1;

        case DDTP_BEZIER:
            if (!growBuilder (cb, lod, pb->pb_Used + 3)) return 0;
            fx  = drwFloatToFixed (msg->ddtp_PosX);
            fy  = drwFloatToFixed (msg->ddtp_PosY);
            fx2 = drwFloatToFixed (msg->ddtp_PosX2);
            fy2 = drwFloatToFixed (msg->ddtp_PosY2);
            fx3 = drwFloatToFixed (msg->ddtp_PosX3);
            fy3 = drwFloatToFixed (msg->ddtp_PosY3);
            /* Three control points (the start point is implicitly the     *
             * previous builder entry).  Mark the first two as bezier      *
             * control points and the third (the endpoint) as normal.      */
            pb->pb_Points[pb->pb_Used].dp_X     = fx;
            pb->pb_Points[pb->pb_Used].dp_Y     = fy;
            pb->pb_Points[pb->pb_Used].dp_Flags = DRPF_BEZIER;
            pb->pb_Points[pb->pb_Used + 1].dp_X = fx2;
            pb->pb_Points[pb->pb_Used + 1].dp_Y = fy2;
            pb->pb_Points[pb->pb_Used + 1].dp_Flags = DRPF_BEZIER;
            pb->pb_Points[pb->pb_Used + 2].dp_X = fx3;
            pb->pb_Points[pb->pb_Used + 2].dp_Y = fy3;
            pb->pb_Points[pb->pb_Used + 2].dp_Flags = DRPF_NORMAL;
            pb->pb_Used      += 3;
            pb->pb_HasBezier = TRUE;
            pb->pb_Open      = TRUE;
            return 1;

        case DDTP_CLOSE:
            commitPolyBuilder (cb, lod, TRUE);
            return 1;

        case DDTP_END:
            commitPolyBuilder (cb, lod, FALSE);
            return 1;

        default:
            return 0;
    }
}


/*****************************************************************************/
/* addAttrMethod -- DDTM_ADDATTR                                             */
/*                                                                           */
/* The OS4 version DDTM_ADDATTR was specified with a single attribute type:  */
/* DDTA_COLOURS, which packs line and fill colour ARGB values into two       */
/* ULONG fields.  Setting the colours applies to the NEXT polygon that the   */
/* application commits; we store them in the PolyBuilder and pick them up    */
/* in commitPolyBuilder.                                                     */
/*****************************************************************************/

ULONG addAttrMethod (struct ClassBase *cb, Class *cl, Object *o,
                     struct ddtAttr *msg)
{
    struct localData *lod = INST_DATA (cl, o);
    struct PolyBuilder *pb = &lod->ldd_PolyBuilder;

    (void) cb;

    if (!msg) return 0;

    switch (msg->ddta_Type)
    {
        case DDTA_COLOURS:
            pb->pb_LineColor = msg->ddta_Data;
            pb->pb_FillColor = msg->ddta_Data2;
            return 1;

        default:
            /* Unknown attribute type - silently accept for forward
             * compatibility.                                              */
            return 1;
    }
}


/*****************************************************************************/
/* setSizeMethod -- DDTM_SETSIZE                                             */
/*****************************************************************************/

ULONG setSizeMethod (struct ClassBase *cb, Class *cl, Object *o,
                     struct ddtSize *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    (void) cb;

    if (!msg) return 0;

    lod->ldd_CanvasLeft   = drwFloatToFixed (msg->ddts_Left);
    lod->ldd_CanvasRight  = drwFloatToFixed (msg->ddts_Right);
    lod->ldd_CanvasTop    = drwFloatToFixed (msg->ddts_Top);
    lod->ldd_CanvasBottom = drwFloatToFixed (msg->ddts_Bottom);

    syncHeaderFromInfo (lod);
    lod->ldd_Flags |= LDDF_VIEW_DIRTY;

    return 1;
}


/*****************************************************************************/
/* addObjectMethod -- DDTM_ADDOBJECT                                         */
/*                                                                           */
/* The subclass loader (or external code) supplies an already-fully-built    */
/* DrawObj.  We take ownership and add it to the current target list.        */
/* Note: the supplied DrawObj MUST have been allocated from lod->ldd_Pool    */
/* (or the application is responsible for keeping it alive for the           */
/* lifetime of the object).  Subclass loaders use poolDrawObj() so this      */
/* invariant holds automatically.                                            */
/*****************************************************************************/

ULONG addObjectMethod (struct ClassBase *cb, Class *cl, Object *o,
                       struct drwAddObject *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    if (!msg || !msg->drwa_Obj) return 0;

    /* Make sure bounds are populated; the loader may not have done it.  */
    if (msg->drwa_Obj->do_XMax < msg->drwa_Obj->do_XMin)
        computeObjectBounds (msg->drwa_Obj);

    return drawingForwardDrwm (lod, (Msg) msg);
}


/*****************************************************************************/
/* beginGroupMethod -- DDTM_BEGINGROUP                                       */
/*****************************************************************************/

ULONG beginGroupMethod (struct ClassBase *cb, Class *cl, Object *o,
                        struct drwBeginGroup *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    if (!msg) return 0;
    return drawingForwardDrwm (lod, (Msg) msg);
}


/*****************************************************************************/
/* endGroupMethod -- DDTM_ENDGROUP                                           */
/*****************************************************************************/

ULONG endGroupMethod (struct ClassBase *cb, Class *cl, Object *o,
                      struct drwEndGroup *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    (void) cb;
    (void) o;

    return drawingForwardDrwm (lod, (Msg) msg);
}


/*****************************************************************************/
/* addLayerMethod -- DDTM_ADDLAYER                                           */
/*****************************************************************************/

ULONG addLayerMethod (struct ClassBase *cb, Class *cl, Object *o,
                      struct drwAddLayer *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    (void) cb;
    (void) cl;
    (void) o;

    if (!msg || !msg->drwal_Layer) return 0;

    return drawingForwardDrwm (lod, (Msg) msg);
}


/*****************************************************************************/
/* addColorMethod -- DDTM_ADDCOLOR                                           */
/*                                                                           */
/* Extends the palette table by one RGB triplet.  The class grows the        */
/* table in chunks of 16 entries to amortise the pool allocations.           */
/*****************************************************************************/

ULONG addColorMethod (struct ClassBase *cb, Class *cl, Object *o,
                      struct drwAddColor *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    (void) cb;
    (void) cl;
    (void) o;

    if (!msg) return 0;

    return drawingForwardDrwm (lod, (Msg) msg);
}


/*****************************************************************************/
/* addFontMethod -- DDTM_ADDFONT                                             */
/*****************************************************************************/

ULONG addFontMethod (struct ClassBase *cb, Class *cl, Object *o,
                     struct drwAddFont *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    (void) cb;
    (void) cl;
    (void) o;

    if (!msg || !msg->drwaf_Font) return 0;

    return drawingForwardDrwm (lod, (Msg) msg);
}


/*****************************************************************************/
/* clearDrawingMethod -- DDTM_CLEARDRAWING                                   */
/*****************************************************************************/

ULONG clearDrawingMethod (struct ClassBase *cb, Class *cl, Object *o,
                          struct drwClearDrawing *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    (void) cb;
    (void) cl;
    (void) o;

    return drawingForwardDrwm (lod, (Msg) msg);
}


/*****************************************************************************/
/* setBoundsMethod -- DDTM_SETBOUNDS                                         */
/*****************************************************************************/

ULONG setBoundsMethod (struct ClassBase *cb, Class *cl, Object *o,
                       struct drwSetBounds *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    (void) cb;
    (void) cl;
    (void) o;

    if (!msg) return 0;

    /* Track the canvas unit so computePixelSize() / the layout scale can
     * convert canvas units to pixels correctly.                          */
    lod->ldd_CanvasUnits = msg->drwsb_Units;

    return drawingForwardDrwm (lod, (Msg) msg);
}
