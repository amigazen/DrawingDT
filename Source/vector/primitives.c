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

    pb = &lod->lvi_PolyBuilder;
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
        lod->lvi_DrawingFlags |= DRWF_HAS_BEZIER;

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
    struct PolyBuilder *pb = &lod->lvi_PolyBuilder;
    ULONG newCap;
    struct DrawPoint *newPts;
    ULONG  i;

    if (needed <= pb->pb_Allocated) return TRUE;

    newCap = pb->pb_Allocated ? pb->pb_Allocated : LVI_POLY_INITIAL;
    while (newCap < needed) newCap += LVI_POLY_GROW;

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
/* addObjectMethod -- VIMM_ADDOBJECT                                         */
/*                                                                           */
/* The subclass loader (or external code) supplies an already-fully-built    */
/* DrawObj.  We take ownership and add it to the current target list.        */
/* Note: the supplied DrawObj MUST have been allocated from lod->lvi_Pool    */
/* (or the application is responsible for keeping it alive for the           */
/* lifetime of the object).  Subclass loaders use poolDrawObj() so this      */
/* invariant holds automatically.                                            */
/*****************************************************************************/

ULONG addObjectMethod (struct ClassBase *cb, Class *cl, Object *o,
                       struct vecAddObject *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    if (!msg || !msg->veca_Obj) return 0;

    if (msg->veca_Obj->do_Type == DROT_TEXT ||
        msg->veca_Obj->do_Type == DROT_TEXTPATH)
    {
        if (!parseProVectorText (cb, lod, msg->veca_Obj))
            return 0;
    }

    /* Make sure bounds are populated; the loader may not have done it.  */
    if (msg->veca_Obj->do_XMax < msg->veca_Obj->do_XMin)
        computeObjectBounds (msg->veca_Obj);

    return appendObject (cb, lod, msg->veca_Obj);
}


/*****************************************************************************/
/* beginGroupMethod -- DDTM_BEGINGROUP                                       */
/*****************************************************************************/

ULONG beginGroupMethod (struct ClassBase *cb, Class *cl, Object *o,
                        struct vecBeginGroup *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    if (!msg) return 0;
    return beginGroupObject (cb, lod, msg->vecbg_Type, msg->vecbg_ExpectCount,
                             msg->vecbg_FillID);
}


/*****************************************************************************/
/* endGroupMethod -- DDTM_ENDGROUP                                           */
/*****************************************************************************/

ULONG endGroupMethod (struct ClassBase *cb, Class *cl, Object *o,
                      struct vecEndGroup *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    (void) msg;

    return endGroupObject (cb, lod);
}


/*****************************************************************************/
/* addLayerMethod -- DDTM_ADDLAYER                                           */
/*****************************************************************************/

ULONG addLayerMethod (struct ClassBase *cb, Class *cl, Object *o,
                      struct vecAddLayer *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    (void) cb;

    if (!msg || !msg->vecal_Layer) return 0;

    AddTail (&lod->lvi_LayerList, (struct Node *) msg->vecal_Layer);
    lod->lvi_LayerCount++;
    lod->lvi_DrawingFlags |= DRWF_HAS_LAYERS;
    return 1;
}


/*****************************************************************************/
/* addColorMethod -- DDTM_ADDCOLOR                                           */
/*                                                                           */
/* Extends the palette table by one RGB triplet.  The class grows the        */
/* table in chunks of 16 entries to amortise the pool allocations.           */
/*****************************************************************************/

ULONG addColorMethod (struct ClassBase *cb, Class *cl, Object *o,
                      struct vecAddColor *msg)
{
    struct localData *lod = INST_DATA (cl, o);
    ULONG n;
    UBYTE *fresh;
    ULONG newCap;
    ULONG  i;

    if (!msg) return 0;

    n = lod->lvi_NumPaletteColors;

    if ((n & 15) == 0)
    {
        newCap = (n + 16) * 3;
        fresh = (UBYTE *) poolAlloc (cb, lod, newCap);
        if (!fresh) return 0;
        if (lod->lvi_RGBTable)
        {
            for (i = 0; i < n * 3; i++) fresh[i] = lod->lvi_RGBTable[i];
        }
        lod->lvi_RGBTable = fresh;
    }

    lod->lvi_RGBTable[3 * n + 0] = msg->vecac_R;
    lod->lvi_RGBTable[3 * n + 1] = msg->vecac_G;
    lod->lvi_RGBTable[3 * n + 2] = msg->vecac_B;
    lod->lvi_NumPaletteColors++;

    return 1;
}


/*****************************************************************************/
/* addFontMethod -- DDTM_ADDFONT                                             */
/*****************************************************************************/

ULONG addFontMethod (struct ClassBase *cb, Class *cl, Object *o,
                     struct vecAddFont *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    (void) cb;

    if (!msg || !msg->vecaf_Font) return 0;

    AddTail (&lod->lvi_FontList, (struct Node *) msg->vecaf_Font);
    lod->lvi_FontCount++;
    lod->lvi_DrawingFlags |= DRWF_HAS_FONTS;
    return 1;
}


/*****************************************************************************/
/* clearDrawingMethod -- DDTM_CLEARDRAWING                                   */
/*****************************************************************************/

ULONG clearDrawingMethod (struct ClassBase *cb, Class *cl, Object *o,
                          struct vecClear *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    (void) msg;

    clearDisplayList (cb, lod);
    return 1;
}


/*****************************************************************************/
/* setBoundsMethod -- DDTM_SETBOUNDS                                         */
/*****************************************************************************/

ULONG setBoundsMethod (struct ClassBase *cb, Class *cl, Object *o,
                       struct vecSetBounds *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    (void) cb;

    if (!msg) return 0;

    lod->lvi_CanvasLeft   = msg->vecsb_Left;
    lod->lvi_CanvasTop    = msg->vecsb_Top;
    lod->lvi_CanvasRight  = msg->vecsb_Right;
    lod->lvi_CanvasBottom = msg->vecsb_Bottom;
    if (msg->vecsb_Units != (ULONG) ~0UL)
        lod->lvi_CanvasUnits = msg->vecsb_Units;

    syncCanvasExtents (lod);
    return 1;
}
