/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* attrs.c -- OM_GET / OM_SET / notification helpers for drawing.datatype    */
/*                                                                           */
/* Equivalent of picture/dispatch.c's getPictureDTAttr / setPictureDTAttrs   */
/* and document/attrs.c.  Handles the modern DDTA_* tags, the OS4 draft      */
/* DDTA_Header tag, and the picture palette / mode metadata aliases          */
/* (PDTA_ModeID, PDTA_NumColors, PDTA_ColorRegisters, PDTA_Screen, ...) that  */
/* describe the cached rasterised view.  The picture BITMAP tags             */
/* (PDTA_BitMap / PDTA_BitMapHeader) are deliberately NOT advertised: OM_GET  */
/* returns 0 for them so generic viewers such as MultiView drive the         */
/* refreshable gadget path (GM_RENDER) rather than the picture bitmap        */
/* fast-path.  The cached raster view is exposed only through DDTA_BitMap /   */
/* DDTA_BitMapHeader.                                                         */
/*                                                                           */
/* The contract is:                                                          */
/*   * getDrawingAttr returns 1 when it consumed the tag, 0 otherwise.       */
/*     The dispatcher falls through to DoSuperMethodA on a 0 return.         */
/*   * setDrawingAttrs returns non-zero when the change requires a           */
/*     re-render.  The dispatcher then forces a GM_RENDER.                   */
/*****************************************************************************/

#include "classbase.h"


/*****************************************************************************/
/* Forward                                                                    */
/*****************************************************************************/

static void clampScale            (struct localData *lod);
static void updateFixedFromScale  (struct localData *lod);
static void updateScaleFromFixed  (struct localData *lod);


/*****************************************************************************/
/* notifyAttrChanges - OM_NOTIFY wrapper                                     */
/*****************************************************************************/

static ULONG notifyAttrChanges (struct ClassBase *cb, Object *o,
                                struct GadgetInfo *ginfo, ULONG flags,
                                ULONG tag1, ...)
{
    (void) cb;
    return DoMethod (o, OM_NOTIFY, &tag1, ginfo, flags);
}


/*****************************************************************************/
/* doNotifyChanges - called from outside this file when something needs    */
/* the application to be informed of a state change (e.g. async re-render */
/* finished).                                                             */
/*****************************************************************************/

void doNotifyChanges (struct ClassBase *cb, Object *o,
                      struct GadgetInfo *ginfo)
{
    struct localData     *lod;
    struct DTSpecialInfo *si;
    STRPTR                title;

    lod   = (struct localData *) INST_DATA (OCLASS (o), o);
    si    = (struct DTSpecialInfo *) G (o)->SpecialInfo;
    title = lod->ldd_Title;
    if (!title)
        GetAttr (DTA_Title, o, (ULONG *) &title);
    if (!title)
        GetAttr (DTA_Name, o, (ULONG *) &title);

    /* Announce completion like the OS4 drawing.datatype: title plus
     * Busy=FALSE / Sync=TRUE.  The title matters to MultiView because its
     * loading UI replaces the window title with "Loading..." until the
     * datatype reports the finished title again.  Do NOT re-broadcast
     * DTA_NominalHoriz/Vert or DDTA_* scale attributes here: MultiView
     * treats those as layout-changing state and can enter a relayout loop.
     *
     * We do publish the scroller geometry from DTSpecialInfo.  MultiView's
     * window model updates its proportional gadgets from the tags carried
     * by this notification; a bare DTA_Sync=TRUE only tells it to propagate
     * whatever totals it already has.  The matching OM_SET/OM_UPDATE handler
     * deliberately ignores MultiView's echo of these four attributes, so
     * this does not re-arm LDDF_VIEW_DIRTY or restart layout. */
    notifyAttrChanges (cb, o, ginfo, NULL,
                       GA_ID,            G (o)->GadgetID,
                       DTA_Title,        title,
                       DTA_VisibleVert,  (ULONG) si->si_VisVert,
                       DTA_TotalVert,    (ULONG) si->si_TotVert,
                       DTA_VisibleHoriz, (ULONG) si->si_VisHoriz,
                       DTA_TotalHoriz,   (ULONG) si->si_TotHoriz,
                       DTA_Busy,         FALSE,
                       DTA_Sync,         TRUE,
                       TAG_DONE);
}


/*****************************************************************************/
/* doNotifyBusy - announce the loading/working state to the application.    */
/*                                                                          */
/* The OS4 drawing.datatype notifies DTA_Busy=TRUE at the start of layout   */
/* and FALSE when finished, which is how MultiView decides when to clear    */
/* its "Loading..." overlay.  We mirror that handshake.                     */
/*****************************************************************************/

void doNotifyBusy (struct ClassBase *cb, Object *o,
                   struct GadgetInfo *ginfo, BOOL busy)
{
    notifyAttrChanges (cb, o, ginfo, NULL,
                       GA_ID,    G (o)->GadgetID,
                       DTA_Busy, busy ? TRUE : FALSE,
                       TAG_DONE);
}


/*****************************************************************************/
/* drawingMethods - the method array reported through DTA_Methods.           */
/*                                                                           */
/* datatypes.library query DTA_Methods to learn which operations an          */
/* object supports - e.g. whether to enable Save-As (DTM_WRITE), Print       */
/* (DTM_PRINT) or clipboard Copy (DTM_COPY).  The OS4 version                */
/* drawing.datatype publishes such a list, so we do the same, but ours       */
/* advertises the full set this root class actually dispatches.  The array   */
/* is a plain ULONG list terminated by ~0, exactly the datatypesclass        */
/* convention, and is static/const so the pointer stays valid after the      */
/* OM_GET returns.                                                           */
/*****************************************************************************/

static const ULONG drawingMethods[] =
{
    OM_NEW,
    OM_DISPOSE,
    OM_SET,
    OM_UPDATE,
    OM_GET,
    GM_LAYOUT,
    GM_HITTEST,
    GM_GOACTIVE,
    GM_HANDLEINPUT,
    GM_RENDER,
    DTM_PROCLAYOUT,
    DTM_ASYNCLAYOUT,
    DTM_FRAMEBOX,
    DTM_SELECT,
    DTM_CLEARSELECTED,
    DTM_COPY,
    DTM_WRITE,
    DTM_PRINT,
    (ULONG) ~0UL
};


/*****************************************************************************/
/* getDrawingAttr -- OM_GET                                                   */
/*****************************************************************************/

ULONG getDrawingAttr (struct ClassBase *cb, Class *cl, Object *o,
                      struct opGet *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    (void) cb;

    switch (msg->opg_AttrID)
    {
        /* -- datatype standard ---------------------------------- */
        case DTA_Methods:
            *msg->opg_Storage = (ULONG) drawingMethods;
            break;

        /* -- OS4 drawing.datatype compatibility ---------------------------- */
        case DDTA_Header:
            *msg->opg_Storage = (ULONG) &lod->ldd_HeaderStorage;
            break;

        /* -- canvas --------------------------------------------- */
        case DDTA_CanvasLeft:
            *msg->opg_Storage = (ULONG) lod->ldd_CanvasLeft;
            break;
        case DDTA_CanvasTop:
            *msg->opg_Storage = (ULONG) lod->ldd_CanvasTop;
            break;
        case DDTA_CanvasRight:
            *msg->opg_Storage = (ULONG) lod->ldd_CanvasRight;
            break;
        case DDTA_CanvasBottom:
            *msg->opg_Storage = (ULONG) lod->ldd_CanvasBottom;
            break;
        case DDTA_CanvasUnits:
            *msg->opg_Storage = lod->ldd_CanvasUnits;
            break;
        case DDTA_NativeWidth:
            *msg->opg_Storage = (ULONG) lod->ldd_NativeWidth;
            break;
        case DDTA_NativeHeight:
            *msg->opg_Storage = (ULONG) lod->ldd_NativeHeight;
            break;

        /* -- rendering ------------------------------------------ */
        case DDTA_NativeDPI:
            *msg->opg_Storage = lod->ldd_NativeDPI;
            break;
        case DDTA_RenderDPI:
            *msg->opg_Storage = lod->ldd_RenderDPI;
            break;
        case DDTA_Scale:
            *msg->opg_Storage = lod->ldd_Scale;
            break;
        case DDTA_FixedScale:
            *msg->opg_Storage = (ULONG) lod->ldd_FixedScale;
            break;
        case DDTA_Rotation:
            *msg->opg_Storage = lod->ldd_Rotation;
            break;
        case DDTA_RenderQuality:
            *msg->opg_Storage = lod->ldd_RenderQuality;
            break;
        case DDTA_RenderFlags:
            *msg->opg_Storage = lod->ldd_RenderFlags;
            break;
        case DDTA_AntiAlias:
            *msg->opg_Storage = (lod->ldd_RenderQuality >= DRWQ_NORMAL)
                                    ? TRUE : FALSE;
            break;
        case DDTA_ColorMode:
            *msg->opg_Storage = lod->ldd_ColorMode;
            break;
        case DDTA_BackgroundR:
            *msg->opg_Storage = (ULONG) lod->ldd_BackgroundR;
            break;
        case DDTA_BackgroundG:
            *msg->opg_Storage = (ULONG) lod->ldd_BackgroundG;
            break;
        case DDTA_BackgroundB:
            *msg->opg_Storage = (ULONG) lod->ldd_BackgroundB;
            break;
        case DDTA_FitToBox:
            *msg->opg_Storage = (ULONG) lod->ldd_FitToBox;
            break;
        case DDTA_PreserveAspect:
            *msg->opg_Storage = (ULONG) lod->ldd_PreserveAspect;
            break;

        /* -- bitmap aliases (picture.datatype compatible) ------- */
        case DDTA_BitMap:
            *msg->opg_Storage = (ULONG) lod->ldd_BMap;
            break;

        case DDTA_BitMapHeader:
            *msg->opg_Storage = (ULONG) &lod->ldd_BMHD;
            break;

        /* Do not present drawing.datatype as a picture.datatype bitmap to
         * generic viewers.  MultiView probes PDTA_BitMapHeader during
         * OpenEnvironment(); if it succeeds, it calls DTM_PROCLAYOUT and
         * opens a custom screen with our cached bitmap as SA_BitMap.  That
         * picture fast-path is not a refreshable vector gadget path and
         * causes the strobing / dirty-pixel behaviour seen with overlapping
         * windows.  The drawing class keeps DDTA_BitMap/DDTA_BitMapHeader
         * for explicit raster-cache callers, but lets PDTA_* fall through
         * so MultiView uses AddDTObject()/GM_RENDER like the OS4 drawing
         * datatype.                                                       */
        case PDTA_BitMap:
#ifdef PDTA_DestBitMap
        case PDTA_DestBitMap:
#endif
        case PDTA_ClassBitMap:
        case PDTA_BitMapHeader:
            return 0;

        case PDTA_ModeID:
            *msg->opg_Storage = lod->ldd_ModeID;
            break;
        case PDTA_NumColors:
            *msg->opg_Storage = (ULONG) lod->ldd_NumColors;
            break;
        case PDTA_NumAlloc:
            *msg->opg_Storage = (ULONG) lod->ldd_NumAlloc;
            break;
        case PDTA_ColorRegisters:
            *msg->opg_Storage = (ULONG) lod->ldd_Colors;
            break;
        case PDTA_CRegs:
            *msg->opg_Storage = (ULONG) lod->ldd_CRegs;
            break;
        case PDTA_GRegs:
            *msg->opg_Storage = (ULONG) lod->ldd_GRegs;
            break;
        case PDTA_ColorTable:
            *msg->opg_Storage = (ULONG) lod->ldd_ColorTable;
            break;
        case PDTA_ColorTable2:
            *msg->opg_Storage = (ULONG) lod->ldd_ColorTable2;
            break;
        case PDTA_Grab:
            *msg->opg_Storage = (ULONG) &lod->ldd_Point;
            break;
        case PDTA_Screen:
            *msg->opg_Storage = (ULONG) lod->ldd_Screen;
            break;

        /* -- metadata ------------------------------------------- */
        case DDTA_Title:
            *msg->opg_Storage = (ULONG) lod->ldd_Title;
            break;
        case DDTA_Author:
            *msg->opg_Storage = (ULONG) lod->ldd_Author;
            break;
        case DDTA_Subject:
            *msg->opg_Storage = (ULONG) lod->ldd_Subject;
            break;
        case DDTA_Keywords:
            *msg->opg_Storage = (ULONG) lod->ldd_Keywords;
            break;
        case DDTA_Producer:
            *msg->opg_Storage = (ULONG) lod->ldd_Producer;
            break;
        case DDTA_Creator:
            *msg->opg_Storage = (ULONG) lod->ldd_Creator;
            break;
        case DDTA_DrawingFlags:
            *msg->opg_Storage = lod->ldd_DrawingFlags;
            break;

        /* -- display list --------------------------------------- */
        case DDTA_DisplayList:
            *msg->opg_Storage = (ULONG) drawingGetObjectList (lod);
            break;
        case DDTA_ObjectCount:
            *msg->opg_Storage = lod->ldd_ObjectCount;
            break;
        case DDTA_LayerCount:
            *msg->opg_Storage = lod->ldd_LayerCount;
            break;
        case DDTA_LayerList:
            *msg->opg_Storage = (ULONG) lod->ldd_InfoStorage.di_Layers;
            break;
        case DDTA_ActiveLayer:
            *msg->opg_Storage = lod->ldd_ActiveLayer;
            break;
        case DDTA_ColorTable:
            *msg->opg_Storage = (ULONG) lod->ldd_RGBTable;
            break;
        case DDTA_NumColors:
            *msg->opg_Storage = lod->ldd_NumPaletteColors;
            break;
        case DDTA_DrawingInfo:
            *msg->opg_Storage = (ULONG) &lod->ldd_InfoStorage;
            break;

        /* -- streaming ------------------------------------------ */
        case DDTA_Stream:
            *msg->opg_Storage = (ULONG) &lod->ldd_Stream;
            break;
        case DDTA_StreamSize:
            *msg->opg_Storage = (ULONG) lod->ldd_Stream.ds_Length;
            break;
        case DDTA_StreamPosition:
            *msg->opg_Storage = (ULONG) lod->ldd_Stream.ds_Position;
            break;
        case DDTA_IFFHandle:
            *msg->opg_Storage = (ULONG) getIFFHandle (cb, lod);
            break;

        case OBP_Precision:
            *msg->opg_Storage = lod->ldd_Precision;
            break;

        default:
            return 0;
    }

    return 1;
}


/*****************************************************************************/
/* setDrawingAttrs -- OM_SET / OM_UPDATE                                      */
/*                                                                            */
/* The return value is "refresh requested" - the dispatcher uses this to    */
/* drive GM_RENDER and DTM_ASYNCLAYOUT as needed.                           */
/*****************************************************************************/

ULONG setDrawingAttrs (struct ClassBase *cb, Class *cl, Object *o,
                       struct opSet *msg)
{
    struct localData *lod = INST_DATA (cl, o);
    struct TagItem   *tstate = msg->ops_AttrList;
    struct TagItem   *tag;
    ULONG  refresh = 0;
    ULONG  scaleWas = lod->ldd_Scale;
    LONG   fixedWas = lod->ldd_FixedScale;
    ULONG  rotWas  = lod->ldd_Rotation;
    ULONG  qualityWas = lod->ldd_RenderQuality;
    ULONG  flagsWas = lod->ldd_RenderFlags;
    ULONG  dpiWas   = lod->ldd_RenderDPI;
    BOOL   fitWas   = lod->ldd_FitToBox;

    (void) cb;

    while ((tag = NextTagItem (&tstate)) != NULL)
    {
        ULONG tidata = tag->ti_Data;

        switch (tag->ti_Tag)
        {
            /* -- rendering ---------------------------------------- */
            case DDTA_RenderDPI:
                if (tidata == 0) tidata = 72;
                if (tidata != lod->ldd_RenderDPI)
                {
                    lod->ldd_RenderDPI = tidata;
                    lod->ldd_Flags |= LDDF_VIEW_DIRTY;
                    refresh = 1;
                }
                break;

            case DDTA_Scale:
                if (tidata != lod->ldd_Scale)
                {
                    lod->ldd_Scale = tidata;
                    clampScale (lod);
                    updateFixedFromScale (lod);
                    lod->ldd_Flags |= LDDF_VIEW_DIRTY;
                    refresh = 1;
                }
                break;

            case DDTA_FixedScale:
                if ((LONG) tidata != lod->ldd_FixedScale)
                {
                    lod->ldd_FixedScale = (LONG) tidata;
                    updateScaleFromFixed (lod);
                    clampScale (lod);
                    lod->ldd_Flags |= LDDF_VIEW_DIRTY;
                    refresh = 1;
                }
                break;

            case DDTA_Rotation:
                tidata = (tidata + 360) % 360;
                tidata = (tidata / 90) * 90;
                if (tidata != lod->ldd_Rotation)
                {
                    lod->ldd_Rotation = tidata;
                    lod->ldd_Flags |= LDDF_VIEW_DIRTY;
                    refresh = 1;
                }
                break;

            case DDTA_RenderQuality:
                if (tidata != lod->ldd_RenderQuality)
                {
                    lod->ldd_RenderQuality = tidata;
                    lod->ldd_Flags |= LDDF_VIEW_DIRTY;
                    refresh = 1;
                }
                break;

            case DDTA_RenderFlags:
                if (tidata != lod->ldd_RenderFlags)
                {
                    lod->ldd_RenderFlags = tidata;
                    lod->ldd_Flags |= LDDF_VIEW_DIRTY;
                    refresh = 1;
                }
                break;

            case DDTA_AntiAlias:
                {
                    ULONG newQ = tidata ? DRWQ_NORMAL : DRWQ_DRAFT;
                    if (newQ != lod->ldd_RenderQuality)
                    {
                        lod->ldd_RenderQuality = newQ;
                        lod->ldd_Flags |= LDDF_VIEW_DIRTY;
                        refresh = 1;
                    }
                }
                break;

            case DDTA_ColorMode:
                if (tidata != lod->ldd_ColorMode)
                {
                    lod->ldd_ColorMode = tidata;
                    lod->ldd_Flags |= LDDF_VIEW_DIRTY;
                    refresh = 1;
                }
                break;

            case DDTA_BackgroundR:
                lod->ldd_BackgroundR = (UBYTE) tidata;
                lod->ldd_Flags |= LDDF_VIEW_DIRTY;
                refresh = 1;
                break;
            case DDTA_BackgroundG:
                lod->ldd_BackgroundG = (UBYTE) tidata;
                lod->ldd_Flags |= LDDF_VIEW_DIRTY;
                refresh = 1;
                break;
            case DDTA_BackgroundB:
                lod->ldd_BackgroundB = (UBYTE) tidata;
                lod->ldd_Flags |= LDDF_VIEW_DIRTY;
                refresh = 1;
                break;

            case DDTA_FitToBox:
                lod->ldd_FitToBox = tidata ? TRUE : FALSE;
                lod->ldd_Flags |= LDDF_VIEW_DIRTY;
                refresh = 1;
                break;

            case DDTA_PreserveAspect:
                lod->ldd_PreserveAspect = tidata ? TRUE : FALSE;
                lod->ldd_Flags |= LDDF_VIEW_DIRTY;
                refresh = 1;
                break;

            case DDTA_CanvasUnits:
                lod->ldd_CanvasUnits = tidata;
                break;

            case DDTA_ActiveLayer:
                lod->ldd_ActiveLayer = tidata;
                lod->ldd_Flags |= LDDF_VIEW_DIRTY;
                refresh = 1;
                break;

            /* -- async progress / cancel -------------------------- */
            case DDTA_RenderProgress:
                lod->ldd_ProgressPtr = (ULONG *) tidata;
                break;
            case DDTA_AbortRender:
                lod->ldd_AbortPtr = (ULONG *) tidata;
                break;

            /* -- metadata (settable) ---------------------------- */
            case DDTA_Title:
                lod->ldd_Title = (STRPTR) tidata;
                SetSuperAttrs (cl, o, DTA_ObjName, tidata,
                                       DTA_Title,   tidata,
                                       TAG_DONE);
                break;
            case DDTA_Author:
                lod->ldd_Author = (STRPTR) tidata;
                SetSuperAttrs (cl, o, DTA_ObjAuthor, tidata, TAG_DONE);
                break;
            case DDTA_Subject:
                lod->ldd_Subject = (STRPTR) tidata;
                SetSuperAttrs (cl, o, DTA_ObjAnnotation, tidata, TAG_DONE);
                break;
            case DDTA_Keywords:
                lod->ldd_Keywords = (STRPTR) tidata;
                break;
            case DDTA_Producer:
                lod->ldd_Producer = (STRPTR) tidata;
                break;
            case DDTA_Creator:
                lod->ldd_Creator = (STRPTR) tidata;
                break;

            /* -- picture aliases: subclass might still use these for
             * the cached bitmap (back-compat with applications that
             * SetGadgetAttrs(PDTA_BitMap)).                       */
            case PDTA_BitMap:
                lod->ldd_OurBMap = (struct BitMap *) tidata;
                lod->ldd_SourceBMap = (struct BitMap *) tidata;
                break;
            case PDTA_ClassBitMap:
                lod->ldd_SourceBMap = (struct BitMap *) tidata;
                break;
            case PDTA_ModeID:
                lod->ldd_ModeID = tidata;
                break;
            case PDTA_NumColors:
                lod->ldd_NumColors = (WORD) tidata;
                break;
            case PDTA_Remap:
                if (tidata) lod->ldd_Flags |=  LDDF_REMAP;
                else        lod->ldd_Flags &= ~LDDF_REMAP;
                break;
            case PDTA_Screen:
                lod->ldd_Screen = (struct Screen *) tidata;
                if (lod->ldd_Screen)
                    lod->ldd_ColorMap = lod->ldd_Screen->ViewPort.ColorMap;
                break;
            case PDTA_FreeSourceBitMap:
                if (tidata) lod->ldd_Flags |=  LDDF_FREESRC;
                else        lod->ldd_Flags &= ~LDDF_FREESRC;
                break;
            case PDTA_Grab:
                if (tidata)
                    lod->ldd_Point = *((Point *) tidata);
                break;

            case OBP_Precision:
                lod->ldd_Precision = tidata;
                break;

            /* -- scrolling geometry ------------------------------- *
             * DTA_VisibleVert / DTA_TotalVert / DTA_VisibleHoriz /  *
             * DTA_TotalHoriz are scroller extents that WE publish    *
             * (via drawingLayout -> si_Vis/Tot* and the super class) *
             * and that the application -- MultiView in particular -- *
             * echoes straight back to us after every DTA_Sync=TRUE   *
             * notification.  Treating that echo as a content change  *
             * (refresh=1) re-arms LDDF_VIEW_DIRTY, re-runs           *
             * DTM_ASYNCLAYOUT, re-notifies DTA_Sync=TRUE, and so on   *
             * forever -- the visible "strobing".  These attributes    *
             * never alter the canvas-sized cached bitmap, and the     *
             * scroll position itself arrives via DTA_TopVert /        *
             * DTA_TopHoriz (handled by the super class) and is        *
             * honoured by GM_RENDER's re-blit.  So we deliberately    *
             * do NOT force a refresh here, breaking the loop.         */
            case DTA_VisibleVert:
            case DTA_TotalVert:
            case DTA_VisibleHoriz:
            case DTA_TotalHoriz:
                break;
        }
    }

    if (lod->ldd_Scale     != scaleWas ||
        lod->ldd_FixedScale != fixedWas ||
        lod->ldd_Rotation   != rotWas ||
        lod->ldd_RenderQuality != qualityWas ||
        lod->ldd_RenderFlags != flagsWas ||
        lod->ldd_RenderDPI != dpiWas ||
        lod->ldd_FitToBox != fitWas)
    {
        lod->ldd_Flags |= LDDF_VIEW_DIRTY;
        refresh = 1;
    }

    if (lod->ldd_VectorImage)
    {
        SetAttrs (lod->ldd_VectorImage,
                  VIMA_CanvasUnits, lod->ldd_CanvasUnits,
                  VIMA_NativeDPI,   lod->ldd_NativeDPI,
                  VIMA_RenderDPI,   lod->ldd_RenderDPI,
                  VIMA_Scale,       lod->ldd_Scale,
                  VIMA_FixedScale,  lod->ldd_FixedScale,
                  VIMA_Rotation,    lod->ldd_Rotation,
                  VIMA_RenderQuality, lod->ldd_RenderQuality,
                  VIMA_RenderFlags, lod->ldd_RenderFlags,
                  VIMA_ColorMap,    (ULONG) lod->ldd_ColorMap,
                  TAG_DONE);
    }

    return refresh;
}


/*****************************************************************************/
/* Helpers                                                                    */
/*****************************************************************************/

static void clampScale (struct localData *lod)
{
    if (lod->ldd_Scale < 1)     lod->ldd_Scale = 1;
    if (lod->ldd_Scale > 6400)  lod->ldd_Scale = 6400;
}


static void updateFixedFromScale (struct localData *lod)
{
    lod->ldd_FixedScale = (LONG)((lod->ldd_Scale * 65536UL) / 100UL);
}


static void updateScaleFromFixed (struct localData *lod)
{
    lod->ldd_Scale = (ULONG)((lod->ldd_FixedScale * 100UL) / 65536UL);
}
