/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* dispatch.c -- BOOPSI dispatcher for vector.image                            */
/*****************************************************************************/

#include "classbase.h"


static Object *newVectorImage (struct ClassBase *cb, Class *cl, Object *o,
                              struct opSet *ops);
static ULONG   disposeVectorImage (struct ClassBase *cb, Class *cl, Object *o,
                                   Msg msg);


static ULONG ASM Dispatch (REG (a0) Class *cl,
                           REG (a2) Object *o,
                           REG (a1) Msg msg)
{
    struct ClassBase *cb;
    struct localData *lod;
    ULONG retval;

    cb = (struct ClassBase *) cl->cl_UserData;
    lod = NULL;
    retval = 0;

    switch (msg->MethodID)
    {
        case OM_NEW:
            retval = (ULONG) newVectorImage (cb, cl, o, (struct opSet *) msg);
            break;

        case OM_DISPOSE:
            disposeVectorImage (cb, cl, o, msg);
            retval = 0;
            break;

        case OM_UPDATE:
        case OM_SET:
            DoSuperMethodA (cl, o, msg);
            setVectorAttrs (cb, cl, o, (struct opSet *) msg);
            retval = 1;
            break;

        case OM_GET:
            retval = getVectorAttr (cb, cl, o, (struct opGet *) msg);
            if (!retval)
                retval = DoSuperMethodA (cl, o, msg);
            break;

        case IM_DRAW:
            retval = vectorIMDraw (cb, cl, o, (struct impDraw *) msg);
            break;

        case IM_DRAWFRAME:
            retval = vectorIMDrawFrame (cb, cl, o, (struct impDraw *) msg);
            break;

        case IM_ERASE:
        case IM_ERASEFRAME:
            retval = vectorIMErase (cb, cl, o, (struct impErase *) msg);
            break;

        case IM_HITTEST:
        case IM_HITFRAME:
            retval = vectorIMHitTest (cb, cl, o, (struct impHitTest *) msg);
            break;

        case IM_DOMAINFRAME:
            retval = vectorIMDomainFrame (cb, cl, o,
                                          (struct impDomainFrame *) msg);
            break;

        case VIMM_RENDER:
            retval = renderVectorMethod (cb, cl, o,
                                         (struct vecRender *) msg);
            break;

        case VIMM_RENDERPIXELS:
            retval = renderVectorPixelsMethod (cb, cl, o,
                                               (struct vecRenderPixels *) msg);
            break;

        case VIMM_HITTESTPOINT:
            retval = hitTestPointMethod (cb, cl, o,
                                         (struct vecHitTestPoint *) msg);
            break;

        case VIMM_GETBOUNDINGBOX:
            retval = getBBoxMethod (cb, cl, o,
                                    (struct vecGetBoundingBox *) msg);
            break;

        case VIMM_FITTOBOX:
            retval = fitToBoxMethod (cb, cl, o, (struct vecFitToBox *) msg);
            break;

        case VIMM_ADDOBJECT:
            retval = addObjectMethod (cb, cl, o, (struct vecAddObject *) msg);
            break;

        case VIMM_BEGINGROUP:
            retval = beginGroupMethod (cb, cl, o,
                                       (struct vecBeginGroup *) msg);
            break;

        case VIMM_ENDGROUP:
            retval = endGroupMethod (cb, cl, o, (struct vecEndGroup *) msg);
            break;

        case VIMM_ADDLAYER:
            retval = addLayerMethod (cb, cl, o, (struct vecAddLayer *) msg);
            break;

        case VIMM_ADDCOLOR:
            retval = addColorMethod (cb, cl, o, (struct vecAddColor *) msg);
            break;

        case VIMM_ADDFONT:
            retval = addFontMethod (cb, cl, o, (struct vecAddFont *) msg);
            break;

        case VIMM_CLEAR:
            retval = clearDrawingMethod (cb, cl, o, (struct vecClear *) msg);
            break;

        case VIMM_SETBOUNDS:
            retval = setBoundsMethod (cb, cl, o,
                                      (struct vecSetBounds *) msg);
            break;

        case VIMM_INVALIDATECACHE:
            lod = INST_DATA (cl, o);
            lod->lvi_Flags |= LVIF_CACHE_DIRTY;
            retval = 1;
            break;

        case VIMM_LOADSOURCE:
            retval = loadSourceMethod (cb, cl, o,
                                       (struct vecLoadSource *) msg);
            break;

        default:
            retval = DoSuperMethodA (cl, o, msg);
            break;
    }

    return retval;
}


Class *initClass (struct ClassBase *cb)
{
    Class *cl;

    cl = MakeClass (VECTORIMAGECLASS,
                    IMAGECLASS,
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


static Object *newVectorImage (struct ClassBase *cb, Class *cl, Object *o,
                              struct opSet *ops)
{
    Object *newObj;
    struct localData *lod;
    struct opSet setMsg;
    struct vecLoadSource loadMsg;

    newObj = (Object *) DoSuperMethodA (cl, o, (Msg) ops);
    if (!newObj) return NULL;

    lod = INST_DATA (cl, newObj);

    lod->lvi_Scale         = 100;
    lod->lvi_FixedScale    = DRW_FIXED_ONE;
    lod->lvi_UnitsPerPoint = DRW_FIXED_ONE / 72;
    lod->lvi_NativeDPI     = 72;
    lod->lvi_RenderDPI     = 72;
    lod->lvi_Rotation      = 0;
    lod->lvi_RenderQuality = DRWQ_NORMAL;
    lod->lvi_RenderFlags   = DRWRF_HINTING;
    lod->lvi_ColorMode     = DRWCM_TRUECOLOR;
    lod->lvi_BackgroundR   = 255;
    lod->lvi_BackgroundG   = 255;
    lod->lvi_BackgroundB   = 255;
    lod->lvi_CanvasUnits   = DRWU_POINTS;
    lod->lvi_CanvasLeft    = DRW_FIXED_ZERO;
    lod->lvi_CanvasTop     = DRW_FIXED_ZERO;
    lod->lvi_CanvasRight   = DRW_INT_TO_FIXED (576);
    lod->lvi_CanvasBottom  = DRW_INT_TO_FIXED (576);

    lod->lvi_Pool = CreatePool (MEMF_ANY | MEMF_CLEAR, 8192, 4096);
    if (!lod->lvi_Pool)
    {
        CoerceMethod (cl, newObj, OM_DISPOSE);
        return NULL;
    }

    initDisplayList (cb, lod);
    lod->lvi_Flags |= LVIF_CACHE_DIRTY;

    if (ops && ops->ops_AttrList)
    {
        setMsg.MethodID    = OM_SET;
        setMsg.ops_AttrList = ops->ops_AttrList;
        setMsg.ops_GInfo    = ops->ops_GInfo;
        setVectorAttrs (cb, cl, newObj, &setMsg);

        if (lod->lvi_SourceFile)
        {
            loadMsg.MethodID = VIMM_LOADSOURCE;
            loadMsg.vecls_FileName = lod->lvi_SourceFile;
            loadMsg.vecls_Format = lod->lvi_SourceFormat;
            if (!loadSourceMethod (cb, cl, newObj, &loadMsg))
            {
                CoerceMethod (cl, newObj, OM_DISPOSE);
                return NULL;
            }
        }
    }

    return newObj;
}


static ULONG disposeVectorImage (struct ClassBase *cb, Class *cl, Object *o,
                                 Msg msg)
{
    struct localData *lod;
    struct Image *im;

    lod = INST_DATA (cl, o);
    im  = (struct Image *) o;

    freeVectorCache (cb, lod);
    im->ImageData = NULL;

    if (lod->lvi_Pool)
    {
        DeletePool (lod->lvi_Pool);
        lod->lvi_Pool = NULL;
    }

    return DoSuperMethodA (cl, o, msg);
}
