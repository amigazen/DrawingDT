/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* vectorbridge.c -- bridge drawing.datatype to embedded vector.image        */
/*****************************************************************************/

#include "classbase.h"
#include "vectorbridge.h"


static ULONG drwmToVimm (ULONG drwm)
{
    ULONG vimm;

    vimm = 0;
    switch (drwm)
    {
        case DDTM_RENDERDRAWING:  vimm = VIMM_RENDER; break;
        case DDTM_RENDERPIXELS:   vimm = VIMM_RENDERPIXELS; break;
        case DDTM_HITTESTPOINT:   vimm = VIMM_HITTESTPOINT; break;
        case DDTM_GETBOUNDINGBOX: vimm = VIMM_GETBOUNDINGBOX; break;
        case DDTM_FITTOBOX:       vimm = VIMM_FITTOBOX; break;
        case DDTM_ADDOBJECT:      vimm = VIMM_ADDOBJECT; break;
        case DDTM_BEGINGROUP:     vimm = VIMM_BEGINGROUP; break;
        case DDTM_ENDGROUP:       vimm = VIMM_ENDGROUP; break;
        case DDTM_ADDLAYER:       vimm = VIMM_ADDLAYER; break;
        case DDTM_ADDCOLOR:       vimm = VIMM_ADDCOLOR; break;
        case DDTM_ADDFONT:        vimm = VIMM_ADDFONT; break;
        case DDTM_CLEARDRAWING:   vimm = VIMM_CLEAR; break;
        case DDTM_SETBOUNDS:      vimm = VIMM_SETBOUNDS; break;
        default:                  vimm = 0; break;
    }
    return vimm;
}


static ULONG getVectorAttr (struct localData *lod, ULONG attrID,
                            ULONG *storage)
{
    struct opGet msg;

    if (!lod || !lod->ldd_VectorImage || !storage) return 0;

    msg.MethodID = OM_GET;
    msg.opg_AttrID = attrID;
    msg.opg_Storage = storage;

    return DoMethodA (lod->ldd_VectorImage, (Msg) &msg);
}


APTR drawingGetPool (struct localData *lod)
{
    ULONG pool;

    pool = 0;
    getVectorAttr (lod, VIMA_Pool, &pool);
    return (APTR) pool;
}


struct List *drawingGetObjectList (struct localData *lod)
{
    ULONG list;

    list = 0;
    getVectorAttr (lod, VIMA_DisplayList, &list);
    return (struct List *) list;
}


void drawingSyncFromVector (struct localData *lod)
{
    ULONG val;

    if (!lod || !lod->ldd_VectorImage) return;

    getVectorAttr (lod, VIMA_CanvasLeft, &val);
    lod->ldd_CanvasLeft = (LONG) val;
    getVectorAttr (lod, VIMA_CanvasTop, &val);
    lod->ldd_CanvasTop = (LONG) val;
    getVectorAttr (lod, VIMA_CanvasRight, &val);
    lod->ldd_CanvasRight = (LONG) val;
    getVectorAttr (lod, VIMA_CanvasBottom, &val);
    lod->ldd_CanvasBottom = (LONG) val;
    getVectorAttr (lod, VIMA_ObjectCount, &val);
    lod->ldd_ObjectCount = val;
    getVectorAttr (lod, VIMA_LayerCount, &val);
    lod->ldd_LayerCount = val;
    getVectorAttr (lod, VIMA_FixedScale, &val);
    lod->ldd_FixedScale = (LONG) val;
    getVectorAttr (lod, VIMA_Scale, &val);
    lod->ldd_Scale = val;

    lod->ldd_InfoStorage.di_Left   = lod->ldd_CanvasLeft;
    lod->ldd_InfoStorage.di_Top    = lod->ldd_CanvasTop;
    lod->ldd_InfoStorage.di_Right  = lod->ldd_CanvasRight;
    lod->ldd_InfoStorage.di_Bottom = lod->ldd_CanvasBottom;
    getVectorAttr (lod, VIMA_LayerList, &val);
    lod->ldd_InfoStorage.di_Layers = (struct List *) val;
    lod->ldd_InfoStorage.di_Objects = drawingGetObjectList (lod);
    lod->ldd_InfoStorage.di_NumObjects = lod->ldd_ObjectCount;
    getVectorAttr (lod, VIMA_NumColors, &val);
    lod->ldd_NumPaletteColors = val;
    getVectorAttr (lod, VIMA_ColorTable, &val);
    lod->ldd_RGBTable = (UBYTE *) val;
    lod->ldd_InfoStorage.di_NumColors = lod->ldd_NumPaletteColors;
    lod->ldd_InfoStorage.di_RGBTable  = lod->ldd_RGBTable;

    lod->ldd_HeaderStorage.ddth_XLeft  =
        drwFixedToFloat (lod->ldd_CanvasLeft);
    lod->ldd_HeaderStorage.ddth_YTop   =
        drwFixedToFloat (lod->ldd_CanvasTop);
    lod->ldd_HeaderStorage.ddth_XRight =
        drwFixedToFloat (lod->ldd_CanvasRight);
    lod->ldd_HeaderStorage.ddth_YBot   =
        drwFixedToFloat (lod->ldd_CanvasBottom);
}


ULONG drawingForwardDrwm (struct localData *lod, Msg msg)
{
    ULONG oldMid;
    ULONG vimm;
    ULONG rc;

    if (!lod || !lod->ldd_VectorImage || !msg) return 0;

    oldMid = msg->MethodID;
    vimm = drwmToVimm (oldMid);
    if (!vimm) return 0;

    msg->MethodID = vimm;
    rc = DoMethodA (lod->ldd_VectorImage, msg);
    msg->MethodID = oldMid;

    drawingSyncFromVector (lod);
    lod->ldd_Flags |= LDDF_VIEW_DIRTY;

    return rc;
}


ULONG drawingRenderVectors (struct ClassBase *cb, struct localData *lod,
                            struct RastPort *rp,
                            LONG destX, LONG destY,
                            LONG destW, LONG destH,
                            LONG fixedScaleX, LONG fixedScaleY,
                            LONG canvasOriginX, LONG canvasOriginY,
                            UWORD rotation, UWORD quality,
                            ULONG flags,
                            ULONG *abortPtr, ULONG *progressPtr)
{
    struct vecRender rmsg;

    (void) cb;

    if (!lod || !lod->ldd_VectorImage || !rp) return 0;

    rmsg.MethodID         = VIMM_RENDER;
    rmsg.vecr_RPort       = rp;
    rmsg.vecr_Left        = destX;
    rmsg.vecr_Top         = destY;
    rmsg.vecr_Width       = destW;
    rmsg.vecr_Height      = destH;
    rmsg.vecr_FixedScaleX = fixedScaleX;
    rmsg.vecr_FixedScaleY = fixedScaleY;
    rmsg.vecr_Rotation    = rotation;
    rmsg.vecr_RenderQuality = quality;
    rmsg.vecr_RenderFlags = flags;
    rmsg.vecr_AbortPtr    = abortPtr;
    rmsg.vecr_ProgressPtr = progressPtr;
    rmsg.vecr_ColorMap    = lod->ldd_ColorMap;

    return DoMethodA (lod->ldd_VectorImage, (Msg) &rmsg);
}


ULONG drawingRenderDrawing (struct localData *lod,
                            struct drwRenderDrawing *msg)
{
    struct vecRender rmsg;
    LONG destW;
    LONG destH;
    struct ColorMap *cmap;

    if (!lod || !lod->ldd_VectorImage || !msg) return 0;

    destW = msg->drwrd_Width;
    destH = msg->drwrd_Height;
    if (destW < 1) destW = 1;
    if (destH < 1) destH = 1;

    /* Resolve a destination ColorMap so vector.image can map the drawing's *
     * 24-bit colours to real screen pens via ObtainBestPen.  Prefer the    *
     * ColorMap captured at layout time; otherwise derive it from the       *
     * caller-supplied GadgetInfo's screen (the direct-render path used by  *
     * applications and the test harness).  Without one the renderer falls  *
     * back to raw palette indices, which paint as arbitrary screen pens.   */
    cmap = lod->ldd_ColorMap;
    if (!cmap && msg->drwrd_GInfo && msg->drwrd_GInfo->gi_Screen)
        cmap = msg->drwrd_GInfo->gi_Screen->ViewPort.ColorMap;

    rmsg.MethodID         = VIMM_RENDER;
    rmsg.vecr_RPort       = msg->drwrd_RPort;
    rmsg.vecr_Left        = msg->drwrd_Left;
    rmsg.vecr_Top         = msg->drwrd_Top;
    rmsg.vecr_Width       = destW;
    rmsg.vecr_Height      = destH;
    rmsg.vecr_FixedScaleX = msg->drwrd_FixedScaleX;
    rmsg.vecr_FixedScaleY = msg->drwrd_FixedScaleY;
    rmsg.vecr_Rotation    = msg->drwrd_Rotation;
    rmsg.vecr_RenderQuality = msg->drwrd_RenderQuality;
    rmsg.vecr_RenderFlags = msg->drwrd_RenderFlags;
    rmsg.vecr_AbortPtr    = msg->drwrd_AbortPtr;
    rmsg.vecr_ProgressPtr = msg->drwrd_ProgressPtr;
    rmsg.vecr_ColorMap    = cmap;

    return DoMethodA (lod->ldd_VectorImage, (Msg) &rmsg);
}


ULONG drawingRenderPixels (struct localData *lod,
                           struct drwRenderPixels *msg)
{
    struct vecRenderPixels rmsg;

    if (!lod || !lod->ldd_VectorImage || !msg) return 0;

    rmsg.MethodID         = VIMM_RENDERPIXELS;
    rmsg.vecrp_PixelData  = msg->drwrp_PixelData;
    rmsg.vecrp_PixelFormat = msg->drwrp_PixelFormat;
    rmsg.vecrp_PixelArrayMod = msg->drwrp_PixelArrayMod;
    rmsg.vecrp_Width      = msg->drwrp_Width;
    rmsg.vecrp_Height     = msg->drwrp_Height;
    rmsg.vecrp_FixedScaleX = msg->drwrp_FixedScaleX;
    rmsg.vecrp_FixedScaleY = msg->drwrp_FixedScaleY;
    rmsg.vecrp_Rotation   = msg->drwrp_Rotation;
    rmsg.vecrp_RenderQuality = msg->drwrp_RenderQuality;
    rmsg.vecrp_RenderFlags = msg->drwrp_RenderFlags;
    rmsg.vecrp_AbortPtr   = msg->drwrp_AbortPtr;
    rmsg.vecrp_ProgressPtr = msg->drwrp_ProgressPtr;
    /* Pixel rendering targets a caller-supplied chunky buffer with no       *
     * destination ColorMap; the renderer falls back to the drawing's own   *
     * RGBTable for 8-bit output.                                            */
    rmsg.vecrp_ColorMap   = lod->ldd_ColorMap;

    return DoMethodA (lod->ldd_VectorImage, (Msg) &rmsg);
}
