/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/* dispatch.c
 *
 *  Main BOOPSI method dispatcher for drawing.datatype.
 *
 *  The dispatcher is a single big switch on the incoming MethodID.  We handle:
 *
 *     OM_NEW            -- create a new drawing instance
 *     OM_DISPOSE        -- free everything
 *     OM_SET            -- bulk attribute set
 *     OM_UPDATE         -- like OM_SET but with refresh hint
 *     OM_GET            -- single attribute query
 *     GM_LAYOUT         -- initial layout (delegates to super then async)
 *     GM_HITTEST        -- input.c
 *     GM_GOACTIVE       -- input.c
 *     GM_HANDLEINPUT    -- input.c
 *     GM_RENDER         -- layout.c (drawingRender)
 *     DTM_PROCLAYOUT    -- layout.c
 *     DTM_ASYNCLAYOUT   -- layout.c
 *     DTM_FRAMEBOX      -- layout.c
 *     DTM_GOTO          -- navigate.c
 *     DTM_TRIGGER       -- navigate.c
 *     DTM_SELECT        -- writeobj.c
 *     DTM_CLEARSELECTED -- writeobj.c
 *     DTM_COPY          -- writeobj.c
 *     DTM_PRINT         -- print.c
 *     DTM_WRITE         -- writeobj.c
 *
 *     DDTM_ADDPOLY      -- primitives.c 
 *     DDTM_ADDATTR      -- primitives.c
 *     DDTM_SETSIZE      -- primitives.c
 *
 *     DDTM_*            -- modern drawing-class extension methods
 *
 *  Everything else falls through to DoSuperMethodA so the base
 *  datatypesclass receives unknown messages.
 *
 *  All file-local C declarations sit at the top of their block as
 *  required by C89 / ANSI C.
 */

#include "classbase.h"


/****** drawing.datatype/drawing.datatype *************************************
*
*   NAME
*      drawing.datatype -- root data type for scalable 2-D vector artwork.
*
*   FUNCTION
*      drawing.datatype is the super-class for any datatype that
*      represents a resolution-independent 2-D vector drawing: IFF DR2D,
*      SVG, EPS, XFig, CGM, WMF/EMF, ProDraw etc. 
*      IFF DR2D is the built-in default subclass.
*
*      It maintains a rasterised cached view of the drawing at the
*      current scale / rotation, exposed to callers through DDTA_BitMap
*      and DDTA_BitMapHeader.  It deliberately does NOT advertise the
*      picture.datatype PDTA_BitMap / PDTA_BitMapHeader attributes
*      (OM_GET returns 0 for them) so generic viewers such as MultiView
*      drive it through the refreshable gadget path -- AddDTObject() +
*      GM_RENDER -- rather than the picture bitmap fast-path, which
*      caused palette-remap strobing on overlapping windows.  The cached
*      bitmap is regenerated whenever DDTA_FixedScale / DDTA_Scale /
*      DDTA_Rotation / DDTA_RenderDPI change, so zooming and rotating
*      preserve full vector fidelity (no resampling of an
*      already-rasterised intermediate).
*
*      Subclasses are responsible for:
*         - parsing the source file, populating DrawingInfo and
*           appending DrawObj nodes to the display list in OM_NEW
*           (or asynchronously via DDTM_LOADHEADERS + DDTM_LOADCONTENT)
*         - optionally overriding DDTM_RENDERVECTORS for formats that
*           need higher-fidelity rendering than the root class's
*           polygon-and-bezier rasteriser provides
*
*      The root class handles everything else: scale / rotation
*      computations, the cached current-view BitMap, palette remapping,
*      DTM_COPY/DTM_WRITE to IFF, DTM_PRINT, the OS4 version draft
*      DDTM_ADDPOLY/DDTM_ADDATTR/DDTM_SETSIZE compatibility shims.
*
*   METHODS
*      OM_NEW, OM_DISPOSE, OM_SET, OM_UPDATE, OM_GET,
*      GM_LAYOUT, GM_HITTEST, GM_GOACTIVE, GM_HANDLEINPUT, GM_RENDER,
*      DTM_PROCLAYOUT, DTM_ASYNCLAYOUT, DTM_FRAMEBOX,
*      DTM_GOTO, DTM_TRIGGER,
*      DTM_SELECT, DTM_CLEARSELECTED, DTM_COPY, DTM_WRITE, DTM_PRINT,
*      DDTM_ADDATTR, DDTM_ADDPOLY, DDTM_SETSIZE,
*      DDTM_RENDERDRAWING, DDTM_RENDERPIXELS, DDTM_HITTESTPOINT,
*      DDTM_GETBOUNDINGBOX, DDTM_CANCEL, DDTM_FITTOBOX,
*      DDTM_ADDOBJECT, DDTM_BEGINGROUP, DDTM_ENDGROUP, DDTM_ADDLAYER,
*      DDTM_ADDCOLOR, DDTM_ADDFONT, DDTM_CLEARDRAWING, DDTM_SETBOUNDS,
*      DDTM_LOADHEADERS, DDTM_LOADCONTENT, DDTM_RENDERVECTORS
*
*   SEE ALSO
*      drawingclass.h, pictureclass, datatypesclass
*
*******************************************************************************/


/*****************************************************************************/
/* Forward declarations of file-local helpers                                 */
/*****************************************************************************/

static Object *newDrawing     (struct ClassBase *cb, Class *cl, Object *o,
                               struct opSet *ops);
static ULONG   disposeDrawing (struct ClassBase *cb, Class *cl, Object *o);
static ULONG   dispatchSet    (struct ClassBase *cb, Class *cl, Object *o,
                               struct opSet *ops, BOOL isUpdate);
static void    fireRefresh    (struct ClassBase *cb, Class *cl, Object *o,
                               struct GadgetInfo *ginfo);


#ifdef DRAWING_DEBUG

static void dbgPut (struct ClassBase *cb, STRPTR text)
{
    if (DOSBase && text)
    {
        PutStr ("[drawing] ");
        PutStr (text);
        PutStr ("\n");
    }
}


static void dbgPutLong (struct ClassBase *cb, STRPTR label, LONG value)
{
    if (DOSBase && label)
    {
        Printf ("[drawing] %s%ld\n", (LONG) label, value);
    }
}

#else   /* !DRAWING_DEBUG: helpers compile away to nothing */

#define dbgPut(cb,text)            ((void) 0)
#define dbgPutLong(cb,label,value) ((void) 0)

#endif  /* DRAWING_DEBUG */


/*****************************************************************************/
/* The class entry point.                                                     */
/*                                                                            */
/* Called by exec for every method invocation on an instance of this class.  */
/*****************************************************************************/

static ULONG ASM Dispatch (REG (a0) Class *cl,
                           REG (a2) Object *o,
                           REG (a1) Msg msg)
{
    struct ClassBase *cb     = (struct ClassBase *) cl->cl_UserData;
    struct localData *lod    = NULL;
    ULONG retval = 0;

    /* localData only exists after OM_NEW has succeeded.                   */
    if (msg->MethodID != OM_NEW)
        lod = (struct localData *) INST_DATA (cl, o);

    switch (msg->MethodID)
    {
        case OM_NEW:
            dbgPut (cb, "dispatch OM_NEW");
            retval = (ULONG) newDrawing (cb, cl, o, (struct opSet *) msg);
            dbgPut (cb, "dispatch OM_NEW returned");
            break;

        case OM_DISPOSE:
            retval = disposeDrawing (cb, cl, o);
            break;

        case OM_SET:
            retval = dispatchSet (cb, cl, o, (struct opSet *) msg, FALSE);
            break;

        case OM_UPDATE:
            retval = dispatchSet (cb, cl, o, (struct opSet *) msg, TRUE);
            break;

        case OM_GET:
            retval = getDrawingAttr (cb, cl, o, (struct opGet *) msg);
            if (!retval)
                retval = DoSuperMethodA (cl, o, msg);
            break;

        /* ---- gadget ---- */
        case GM_LAYOUT:
            {
                struct gpLayout *gpl = (struct gpLayout *) msg;
                BOOL willRender;

                /* Only (re)rasterise on the initial layout or when an actual *
                 * attribute change marked the view dirty.  MultiView re-     *
                 * issues GM_LAYOUT for non-content events (resize, window    *
                 * activation, refresh); forcing a re-render on each one frees*
                 * and re-obtains the screen pens every time -- the continuous*
                 * palette-remap flicker.  The cached bitmap is canvas-sized, *
                 * so a resize needs no re-render: GM_RENDER re-blits the      *
                 * visible part.                                              */
                willRender = gpl->gpl_Initial ||
                             (lod->ldd_Flags & LDDF_VIEW_DIRTY);

                /* Announce "busy" like the OS4 reference (only when we are    *
                 * actually about to work) so MultiView shows the loading     *
                 * state and clears it on the DTA_Busy=FALSE we send below.   */
                if (willRender)
                    doNotifyBusy (cb, o, gpl->gpl_GInfo, TRUE);

                retval = DoSuperMethodA (cl, o, msg);
                retval += drawingLayout (cb, cl, o, gpl);

                /* drawingLayout sets LDDF_NEED_REFRESH only when it produced *
                 * a fresh bitmap.  Repaint and notify completion only then -- *
                 * a bare (non-initial, non-dirty) layout pass must neither     *
                 * repaint nor re-notify, otherwise MultiView re-lays-out in a *
                 * loop.                                                        *
                 *                                                             *
                 * We DO paint here through ObtainGIRPort()/GM_RENDER, exactly *
                 * like the OS4 drawing.datatype.  MultiView's live path opens *
                 * the window first and then calls AddDTObject(), which merely *
                 * AddGList()s our gadget and sends this GM_LAYOUT with         *
                 * gpl_Initial=1 -- it never issues a GM_RENDER and never       *
                 * calls RefreshDTObjects().  So nothing draws unless we paint  *
                 * ourselves.  This no longer fights the picture fast-path      *
                 * because OM_GET no longer advertises PDTA_BitMap[Header], so  *
                 * MultiView cannot drive us through the SA_BitMap screen path. */
                if (lod->ldd_Flags & LDDF_NEED_REFRESH)
                {
                    struct RastPort *grp;

                    lod->ldd_Flags &= ~LDDF_NEED_REFRESH;

                    if (gpl->gpl_GInfo &&
                        (grp = ObtainGIRPort (gpl->gpl_GInfo)) != NULL)
                    {
                        struct gpRender gpr;

                        gpr.MethodID   = GM_RENDER;
                        gpr.gpr_GInfo  = gpl->gpl_GInfo;
                        gpr.gpr_RPort  = grp;
                        gpr.gpr_Redraw = GREDRAW_REDRAW;
                        DoMethodA (o, (Msg) &gpr);

                        ReleaseGIRPort (grp);
                    }

                    fireRefresh (cb, cl, o, gpl->gpl_GInfo);
                }
            }
            break;

        case GM_HITTEST:
            retval = hitTestMethod (cb, cl, o, (struct gpHitTest *) msg);
            break;

        case GM_GOACTIVE:
            retval = goActiveMethod (cb, cl, o, (struct gpInput *) msg);
            break;

        case GM_HANDLEINPUT:
            retval = handleInputMethod (cb, cl, o, (struct gpInput *) msg);
            break;

        case GM_RENDER:
            retval = drawingRender (cb, cl, o, (struct gpRender *) msg);
            break;

        /* ---- datatypes ---- */
        case DTM_PROCLAYOUT:
        case DTM_ASYNCLAYOUT:
            retval = drawingLayout (cb, cl, o, (struct gpLayout *) msg);
            if (retval)
                fireRefresh (cb, cl, o,
                             ((struct gpLayout *) msg)->gpl_GInfo);
            break;

        case DTM_FRAMEBOX:
            retval = drawingFrameBox (cb, cl, o, (struct dtFrameBox *) msg);
            if (!retval)
                retval = DoSuperMethodA (cl, o, msg);
            break;

        case DTM_GOTO:
            retval = gotoMethod (cb, cl, o, (struct dtGoto *) msg);
            break;

        case DTM_TRIGGER:
            retval = triggerMethod (cb, cl, o, (struct dtTrigger *) msg);
            break;

        case DTM_SELECT:
            retval = selectMethod (cb, cl, o, (struct dtSelect *) msg);
            break;

        case DTM_CLEARSELECTED:
            retval = clearSelectedMethod (cb, cl, o,
                                          (struct dtGeneral *) msg);
            break;

        case DTM_COPY:
            retval = copyMethod (cb, cl, o, (struct dtGeneral *) msg);
            break;

        case DTM_WRITE:
            retval = writeMethod (cb, cl, o, (struct dtWrite *) msg);
            break;

        case DTM_PRINT:
            retval = printMethod (cb, cl, o, (struct dtPrint *) msg);
            break;

        /* ---- OS4 drawing.datatype compatibility methods-------------- *
         * The OS4 drawing.datatype defines DDTM_ADDPOLY,                *
         * DDTM_ADDATTR and DDTM_SETSIZE as returning 0 (see the         *
         * reference setsize/addattr/addpoly in drawing_dt).  Match that *
         * return contract exactly so subclasses written against the     *
         * original see identical results; the helpers still build the   *
         * display list as their side effect.                            */
        case DDTM_ADDPOLY:
            addPolyMethod (cb, cl, o, (struct ddtPolygon *) msg);
            retval = 0;
            break;

        case DDTM_ADDATTR:
            addAttrMethod (cb, cl, o, (struct ddtAttr *) msg);
            retval = 0;
            break;

        case DDTM_SETSIZE:
            setSizeMethod (cb, cl, o, (struct ddtSize *) msg);
            retval = 0;
            break;

        /* ---- modern DDTM_* methods ------------------------------- */
        case DDTM_RENDERDRAWING:
            retval = drawingRenderDrawing (lod,
                                           (struct drwRenderDrawing *) msg);
            break;

        case DDTM_RENDERPIXELS:
            retval = drawingRenderPixels (lod,
                                          (struct drwRenderPixels *) msg);
            break;

        case DDTM_HITTESTPOINT:
            retval = drawingForwardDrwm (lod, msg);
            break;

        case DDTM_GETBOUNDINGBOX:
            retval = drawingForwardDrwm (lod, msg);
            break;

        case DDTM_FITTOBOX:
            retval = drawingForwardDrwm (lod, msg);
            break;

        case DDTM_CANCEL:
            retval = cancelMethod (cb, cl, o, (struct drwCancel *) msg);
            break;

        case DDTM_ADDOBJECT:
            retval = addObjectMethod (cb, cl, o,
                                      (struct drwAddObject *) msg);
            break;

        case DDTM_BEGINGROUP:
            retval = beginGroupMethod (cb, cl, o,
                                       (struct drwBeginGroup *) msg);
            break;

        case DDTM_ENDGROUP:
            retval = endGroupMethod (cb, cl, o,
                                     (struct drwEndGroup *) msg);
            break;

        case DDTM_ADDLAYER:
            retval = addLayerMethod (cb, cl, o,
                                     (struct drwAddLayer *) msg);
            break;

        case DDTM_ADDCOLOR:
            retval = addColorMethod (cb, cl, o,
                                     (struct drwAddColor *) msg);
            break;

        case DDTM_ADDFONT:
            retval = addFontMethod (cb, cl, o,
                                    (struct drwAddFont *) msg);
            break;

        case DDTM_CLEARDRAWING:
            retval = clearDrawingMethod (cb, cl, o,
                                         (struct drwClearDrawing *) msg);
            break;

        case DDTM_SETBOUNDS:
            retval = setBoundsMethod (cb, cl, o,
                                      (struct drwSetBounds *) msg);
            break;

        /* Subclass content-provision methods - default implementations.
         * Subclasses are expected to override DDTM_LOADHEADERS and
         * DDTM_LOADCONTENT to parse a source.                            */
        case DDTM_LOADHEADERS:
        case DDTM_LOADCONTENT:
            retval = 1;
            break;

        case DDTM_RENDERVECTORS:
            /* Default vector rendering: the root class supplies a generic
             * polygon-and-bezier implementation that handles every
             * DROT_* primitive.  Subclasses with format-specific needs
             * (SVG gradients, EPS PostScript) override this method.    */
            {
                struct drwRenderVectors *rv =
                    (struct drwRenderVectors *) msg;
                struct localData *ldlocal = INST_DATA (cl, o);

                drawingRenderVectors (cb, ldlocal,
                                      rv->drwrv_RPort,
                                      rv->drwrv_DestX, rv->drwrv_DestY,
                                      rv->drwrv_DestWidth,
                                      rv->drwrv_DestHeight,
                                      rv->drwrv_FixedScaleX,
                                      rv->drwrv_FixedScaleY,
                                      rv->drwrv_CanvasX,
                                      rv->drwrv_CanvasY,
                                      rv->drwrv_Rotation,
                                      rv->drwrv_RenderQuality,
                                      rv->drwrv_RenderFlags,
                                      rv->drwrv_AbortPtr,
                                      rv->drwrv_ProgressPtr);
                retval = 1;
            }
            break;

        default:
            retval = DoSuperMethodA (cl, o, msg);
            break;
    }

    return retval;
}


/*****************************************************************************/
/* initClass                                                                 */
/*                                                                           */
/* Build the BOOPSI class.  Called once from LibOpen on the first user.      */
/* We are a subclass of datatypesclass[].                                    */
/*****************************************************************************/

Class *initClass (struct ClassBase *cb)
{
    Class *cl;

    cl = MakeClass (DRAWINGDTCLASS,
                    DATATYPESCLASS,
                    NULL,
                    sizeof (struct localData),
                    0);
    if (!cl) return NULL;

    cl->cl_UserData = (ULONG) cb;
    cl->cl_Dispatcher.h_Entry = (ULONG (*)()) Dispatch;
    cl->cl_Dispatcher.h_SubEntry = NULL;

    AddClass (cl);
    return cl;
}


/*****************************************************************************/
/* dispatchSet                                                               */
/*                                                                           */
/* OM_SET / OM_UPDATE handler.  Calls setDrawingAttrs in attrs.c and         */
/* arranges for refresh after the super-class has propagated the changes.    */
/*****************************************************************************/

static ULONG dispatchSet (struct ClassBase *cb, Class *cl, Object *o,
                          struct opSet *ops, BOOL isUpdate)
{
    struct localData *lod = INST_DATA (cl, o);
    ULONG refresh;

    DoSuperMethodA (cl, o, (Msg) ops);

    refresh = setDrawingAttrs (cb, cl, o, ops);

    if (refresh)
    {
        lod->ldd_Flags |= LDDF_VIEW_DIRTY;

        if (G (o)->Flags & GFLG_EXTENDED)
        {
            struct DTSpecialInfo *si =
                (struct DTSpecialInfo *) G (o)->SpecialInfo;
            if (si && !(si->si_Flags & DTSIF_LAYOUT))
            {
                if (ops->ops_GInfo)
                {
                    struct gpLayout gpl;
                    gpl.MethodID  = DTM_ASYNCLAYOUT;
                    gpl.gpl_GInfo = ops->ops_GInfo;
                    gpl.gpl_Initial = 0;
                    DoAsyncLayout (o, &gpl);
                }
            }
        }
    }

    (void) isUpdate;
    return refresh;
}


/*****************************************************************************/
/* fireRefresh                                                               */
/*                                                                           */
/* Called after a successful DTM_ASYNCLAYOUT to notify the application.      */
/*****************************************************************************/

static void fireRefresh (struct ClassBase *cb, Class *cl, Object *o,
                         struct GadgetInfo *ginfo)
{
    (void) cl;
    doNotifyChanges (cb, o, ginfo);
}


/*****************************************************************************/
/* newDrawing -- OM_NEW                                                      */
/*                                                                           */
/* Build a new drawing instance:                                             */
/*                                                                           */
/*  1. Let the super class build the bare object.                            */
/*  2. Set instance defaults (scale 100%, DPI 72, rotation 0, etc.).         */
/*  3. Initialise the display list, the layer / colour tables and the        */
/*     memory pool.                                                          */
/*  4. Apply the caller's tag list.                                          */
/*  5. Open the streaming source.                                            */
/*  6. Ask the subclass to parse the headers via DDTM_LOADHEADERS,           */
/*     then the content via DDTM_LOADCONTENT.                                */
/*                                                                           */
/* On failure we send OM_DISPOSE to ourselves and return NULL.               */
/*****************************************************************************/

static Object *newDrawing (struct ClassBase *cb, Class *cl, Object *o,
                           struct opSet *ops)
{
    Object *newObj;
    struct localData *lod;
    struct drwLoadHeaders lhMsg;
    struct drwLoadContent lcMsg;
    struct drwSetBounds   sbMsg;

    dbgPut (cb, "newDrawing entered");
    dbgPut (cb, "DoSuperMethodA(OM_NEW -> datatypesclass) ...");
    newObj = (Object *) DoSuperMethodA (cl, o, (Msg) ops);
    if (!newObj)
    {
        dbgPut (cb, "DoSuperMethodA returned NULL");
        return NULL;
    }
    dbgPut (cb, "DoSuperMethodA returned object");

    lod = INST_DATA (cl, newObj);
    dbgPut (cb, "INST_DATA ok");

    /* Defaults.  MakeClass() zero-fills, so we only set non-zero values. */
    dbgPut (cb, "setting defaults");
    lod->ldd_Scale           = 100;
    lod->ldd_FixedScale      = DRW_FIXED_ONE;
    lod->ldd_NativeDPI       = 72;
    lod->ldd_RenderDPI       = 72;
    lod->ldd_Rotation        = 0;
    lod->ldd_RenderQuality   = DRWQ_NORMAL;
    lod->ldd_RenderFlags     = DRWRF_HINTING;
    lod->ldd_ColorMode       = DRWCM_TRUECOLOR;
    lod->ldd_BackgroundR     = 255;
    lod->ldd_BackgroundG     = 255;
    lod->ldd_BackgroundB     = 255;
    lod->ldd_FitToBox        = FALSE;
    lod->ldd_PreserveAspect  = TRUE;
    lod->ldd_Precision       = PRECISION_IMAGE;
    lod->ldd_CanvasUnits     = DRWU_POINTS;
    lod->ldd_CanvasLeft      = DRW_FIXED_ZERO;
    lod->ldd_CanvasTop       = DRW_FIXED_ZERO;
    lod->ldd_CanvasRight     = DRW_INT_TO_FIXED (576); /* 8x8" at 72 dip */
    lod->ldd_CanvasBottom    = DRW_INT_TO_FIXED (576);

    dbgPut (cb, "NewObject(vector.image) ...");
    lod->ldd_VectorImage = NewObject (NULL, VECTORIMAGECLASS,
                                      IA_Width,  (ULONG) 64,
                                      IA_Height, (ULONG) 64,
                                      TAG_DONE);
    if (!lod->ldd_VectorImage)
    {
        struct opSet disposeMsg;
        dbgPut (cb, "NewObject(vector.image) failed");
        disposeMsg.MethodID    = OM_DISPOSE;
        disposeMsg.ops_AttrList = NULL;
        disposeMsg.ops_GInfo    = NULL;
        DoMethodA (newObj, (Msg) &disposeMsg);
        return NULL;
    }
    dbgPut (cb, "NewObject(vector.image) returned");

    dbgPut (cb, "initDisplayList ...");
    initDisplayList (cb, lod);
    dbgPut (cb, "initDisplayList returned");

    /* Stream defaults */
    lod->ldd_Stream.ds_Type     = 0;
    lod->ldd_Stream.ds_Length   = -1;
    lod->ldd_Stream.ds_Position = 0;

    /* Apply caller's tag list now so DDTA_* settings land before load.   */
    dbgPut (cb, "setDrawingAttrs(initial tags) ...");
    {
        struct opSet setMsg;
        setMsg.MethodID    = OM_SET;
        setMsg.ops_AttrList = ops->ops_AttrList;
        setMsg.ops_GInfo    = ops->ops_GInfo;
        setDrawingAttrs (cb, cl, newObj, &setMsg);
    }
    dbgPut (cb, "setDrawingAttrs returned");

    /* Keep scale and fixed scale in sync if the caller set only one.    */
    if (lod->ldd_FixedScale == DRW_FIXED_ONE && lod->ldd_Scale != 100)
        lod->ldd_FixedScale = (LONG)((lod->ldd_Scale * 65536UL) / 100UL);
    else if (lod->ldd_Scale == 100 && lod->ldd_FixedScale != DRW_FIXED_ONE)
        lod->ldd_Scale = (ULONG)((lod->ldd_FixedScale * 100UL) / 65536UL);

    /* Open the source as a streaming DrawStream.                         */
    dbgPut (cb, "openStream ...");
    if (openStream (cb, lod, ops->ops_AttrList))
    {
        dbgPut (cb, "openStream returned TRUE");
        lod->ldd_Flags |= LDDF_HAS_SOURCE;
    }
    else
    {
        dbgPut (cb, "openStream returned FALSE");
    }

    /* Ask subclass to parse headers.                                     */
    dbgPut (cb, "DoMethodA(DDTM_LOADHEADERS) ...");
    lhMsg.MethodID    = DDTM_LOADHEADERS;
    lhMsg.drwlh_Stream = &lod->ldd_Stream;
    lhMsg.drwlh_Info   = &lod->ldd_InfoStorage;
    lhMsg.drwlh_Pool   = drawingGetPool (lod);
    DoMethodA (newObj, (Msg) &lhMsg);
    dbgPut (cb, "DoMethodA(DDTM_LOADHEADERS) returned");

    /* Push the canvas extents the subclass reported in DrawingInfo down   *
     * into vector.image.  DDTM_LOADHEADERS only fills ldd_InfoStorage;    *
     * without this DDTM_SETBOUNDS the engine keeps its default canvas and *
     * every scale / fit / origin calculation is wrong.  Only push a       *
     * non-degenerate box so a subclass that omits a header leaves the     *
     * engine default in place.                                            */
    if (lod->ldd_InfoStorage.di_Right > lod->ldd_InfoStorage.di_Left &&
        lod->ldd_InfoStorage.di_Bottom > lod->ldd_InfoStorage.di_Top)
    {
        sbMsg.MethodID     = DDTM_SETBOUNDS;
        sbMsg.drwsb_Left   = lod->ldd_InfoStorage.di_Left;
        sbMsg.drwsb_Top    = lod->ldd_InfoStorage.di_Top;
        sbMsg.drwsb_Right  = lod->ldd_InfoStorage.di_Right;
        sbMsg.drwsb_Bottom = lod->ldd_InfoStorage.di_Bottom;
        sbMsg.drwsb_Units  = lod->ldd_InfoStorage.di_Units;
        dbgPut (cb, "DDTM_SETBOUNDS push DRHD bounds ...");
        drawingForwardDrwm (lod, (Msg) &sbMsg);
        /* Remember the unit the subclass measured the canvas in (DR2D =      *
         * inches, SVG = pixels, ...).  computePixelSize() and the layout     *
         * scale need it; drawingSyncFromVector() does not copy it back.      */
        lod->ldd_CanvasUnits = lod->ldd_InfoStorage.di_Units;
        dbgPutLong (cb, "canvas right after push=", lod->ldd_CanvasRight);
        dbgPutLong (cb, "canvas bottom after push=", lod->ldd_CanvasBottom);
    }

    /* Mirror the canvas extents from DrawingInfo into our convenience
     * fields and the OS4 version ddtHeader.                               */
    dbgPut (cb, "syncHeaderFromInfo ...");
    syncHeaderFromInfo (lod);
    dbgPut (cb, "syncHeaderFromInfo returned");

    lod->ldd_Flags |= LDDF_HEADERS_LOADED;

    /* Ask subclass to load the content.  For small drawings (typical    *
     * DR2D) the subclass does this synchronously inside this call.      *
     * Large drawings may defer to DTM_ASYNCLAYOUT instead by returning 0. */
    dbgPut (cb, "DoMethodA(DDTM_LOADCONTENT) ...");
    lcMsg.MethodID         = DDTM_LOADCONTENT;
    lcMsg.drwlc_Stream     = &lod->ldd_Stream;
    lcMsg.drwlc_DisplayList = drawingGetObjectList (lod);
    lcMsg.drwlc_Pool       = drawingGetPool (lod);
    lcMsg.drwlc_AbortPtr   = lod->ldd_AbortPtr
                                ? lod->ldd_AbortPtr
                                : &lod->ldd_AbortStorage;
    lcMsg.drwlc_ProgressPtr = lod->ldd_ProgressPtr
                                 ? lod->ldd_ProgressPtr
                                 : &lod->ldd_ProgressStorage;
    DoMethodA (newObj, (Msg) &lcMsg);
    dbgPut (cb, "DoMethodA(DDTM_LOADCONTENT) returned");

    /* Update the object count and recompute bounds if the subclass         *
     * populated the list.                                                  */
    dbgPut (cb, "recomputeDrawingBounds ...");
    recomputeDrawingBounds (lod);
    dbgPutLong (cb, "object count=", (LONG) lod->ldd_ObjectCount);
    lod->ldd_Flags |= LDDF_CONTENT_LOADED;

    /* Compute native pixel size for the application's scroller geometry. */
    dbgPut (cb, "computePixelSize/SetSuperAttrs ...");
    {
        LONG pw, ph;
        computePixelSize (lod, &pw, &ph);
        lod->ldd_NativeWidth  = pw;
        lod->ldd_NativeHeight = ph;
        SetSuperAttrs (cl, newObj,
                       DTA_NominalHoriz, (ULONG) pw,
                       DTA_NominalVert,  (ULONG) ph,
                       TAG_DONE);
    }
    dbgPut (cb, "computePixelSize/SetSuperAttrs returned");

    lod->ldd_Flags |= LDDF_VIEW_DIRTY;

    dbgPut (cb, "newDrawing leaving OK");
    return newObj;
}


/*****************************************************************************/
/* disposeDrawing -- OM_DISPOSE                                              */
/*****************************************************************************/

static ULONG disposeDrawing (struct ClassBase *cb, Class *cl, Object *o)
{
    struct localData     *lod = INST_DATA (cl, o);
    struct DTSpecialInfo *si  = (struct DTSpecialInfo *) G (o)->SpecialInfo;

    /* Tell any in-flight async layout to abort, then serialise with the    *
     * datatypes layout task by taking si_Lock exclusively: drawingLayout   *
     * holds it for the duration of a render, so once we own it the task    *
     * cannot still be touching the vector.image, its display list or the   *
     * cached bitmap we are about to free.  Freeing those out from under a  *
     * running layout task is what crashes when MultiView exits.  Once      *
     * ldd_VectorImage is NULL, drawingLayout() bails immediately (see its  *
     * guard) so a layout request queued after we release the lock but      *
     * before the super class tears the task down is harmless.              */
    if (lod->ldd_AbortPtr) *lod->ldd_AbortPtr = 1;
    lod->ldd_AbortStorage = 1;
    lod->ldd_Flags |= LDDF_CANCELLED;

    if (si)
        ObtainSemaphore (&si->si_Lock);

    closeStream (cb, lod);
    freeCachedBitMap (cb, lod);

    if (lod->ldd_VectorImage)
    {
        DisposeObject (lod->ldd_VectorImage);
        lod->ldd_VectorImage = NULL;
    }

    lod->ldd_ObjectCount = 0;
    lod->ldd_LayerCount  = 0;
    lod->ldd_FontCount   = 0;

    if (si)
        ReleaseSemaphore (&si->si_Lock);

    {
        struct opSet disposeMsg;
        disposeMsg.MethodID    = OM_DISPOSE;
        disposeMsg.ops_AttrList = NULL;
        disposeMsg.ops_GInfo    = NULL;
        return DoSuperMethodA (cl, o, (Msg) &disposeMsg);
    }
}


/*****************************************************************************/
/* cancelMethod -- DDTM_CANCEL                                                */
/*****************************************************************************/

ULONG cancelMethod (struct ClassBase *cb, Class *cl, Object *o,
                    struct drwCancel *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    (void) cb;
    (void) msg;

    if (lod->ldd_AbortPtr) *lod->ldd_AbortPtr = 1;
    lod->ldd_AbortStorage = 1;
    lod->ldd_Flags |= LDDF_CANCELLED;
    return 1;
}


/*****************************************************************************/
/* Pool helpers                                                               */
/*****************************************************************************/

APTR poolAlloc (struct ClassBase *cb, struct localData *lod, ULONG size)
{
    APTR pool;

    (void) cb;
    pool = drawingGetPool (lod);
    if (!pool) return NULL;
    return AllocPooled (pool, size);
}


APTR poolAllocClear (struct ClassBase *cb, struct localData *lod, ULONG size)
{
    APTR p = poolAlloc (cb, lod, size);
    if (p) memset (p, 0, size);
    return p;
}


STRPTR poolStrDup (struct ClassBase *cb, struct localData *lod,
                   CONST_STRPTR src)
{
    ULONG  len;
    STRPTR dst;

    if (!src) return NULL;
    len = 0;
    while (src[len]) len++;
    dst = (STRPTR) poolAlloc (cb, lod, len + 1);
    if (!dst) return NULL;
    CopyMem ((APTR) src, dst, len + 1);
    return dst;
}
