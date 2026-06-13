/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* attrs.c -- OM_GET / OM_SET for vector.image                                 */
/*****************************************************************************/

#include "classbase.h"


ULONG getVectorAttr (struct ClassBase *cb, Class *cl, Object *o,
                     struct opGet *msg)
{
    struct localData *lod;
    ULONG data;

    (void) cb;
    (void) cl;

    lod = INST_DATA (cl, o);
    data = 0;

    switch (msg->opg_AttrID)
    {
        case VIMA_CanvasLeft:
            data = (ULONG) lod->lvi_CanvasLeft;
            break;
        case VIMA_CanvasTop:
            data = (ULONG) lod->lvi_CanvasTop;
            break;
        case VIMA_CanvasRight:
            data = (ULONG) lod->lvi_CanvasRight;
            break;
        case VIMA_CanvasBottom:
            data = (ULONG) lod->lvi_CanvasBottom;
            break;
        case VIMA_CanvasUnits:
            data = lod->lvi_CanvasUnits;
            break;
        case VIMA_NativeDPI:
            data = lod->lvi_NativeDPI;
            break;
        case VIMA_RenderDPI:
            data = lod->lvi_RenderDPI;
            break;
        case VIMA_Scale:
            data = lod->lvi_Scale;
            break;
        case VIMA_FixedScale:
            data = (ULONG) lod->lvi_FixedScale;
            break;
        case VIMA_UnitsPerPoint:
            data = (ULONG) lod->lvi_UnitsPerPoint;
            break;
        case VIMA_Rotation:
            data = lod->lvi_Rotation;
            break;
        case VIMA_RenderQuality:
            data = lod->lvi_RenderQuality;
            break;
        case VIMA_RenderFlags:
            data = lod->lvi_RenderFlags;
            break;
        case VIMA_DisplayList:
            data = (ULONG) &lod->lvi_ObjectsList;
            break;
        case VIMA_ObjectCount:
            data = lod->lvi_ObjectCount;
            break;
        case VIMA_LayerCount:
            data = lod->lvi_LayerCount;
            break;
        case VIMA_LayerList:
            data = (ULONG) &lod->lvi_LayerList;
            break;
        case VIMA_NumColors:
            data = lod->lvi_NumPaletteColors;
            break;
        case VIMA_ColorTable:
            data = (ULONG) lod->lvi_RGBTable;
            break;
        case VIMA_ColorMap:
            data = (ULONG) lod->lvi_ColorMap;
            break;
        case VIMA_Pool:
            data = (ULONG) lod->lvi_Pool;
            break;
        case VIMA_CachedBitMap:
            data = (ULONG) lod->lvi_CachedBM;
            break;
        case VIMA_DrawingFlags:
            data = lod->lvi_DrawingFlags;
            break;
        case VIMA_SourceFile:
            data = (ULONG) lod->lvi_SourceFile;
            break;
        case VIMA_SourceFormat:
            data = lod->lvi_SourceFormat;
            break;
        case IA_Scalable:
            data = TRUE;
            break;
        default:
            return 0;
    }

    *(msg->opg_Storage) = data;
    return 1;
}


ULONG setVectorAttrs (struct ClassBase *cb, Class *cl, Object *o,
                      struct opSet *msg)
{
    struct localData *lod;
    struct TagItem *ti;
    struct TagItem *tagState;
    ULONG refresh;

    (void) cb;
    (void) cl;

    lod = INST_DATA (cl, o);
    refresh = 0;
    tagState = msg->ops_AttrList;

    while ((ti = NextTagItem (&tagState)) != NULL)
    {
        switch (ti->ti_Tag)
        {
            case VIMA_CanvasLeft:
                lod->lvi_CanvasLeft = (LONG) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_CanvasTop:
                lod->lvi_CanvasTop = (LONG) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_CanvasRight:
                lod->lvi_CanvasRight = (LONG) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_CanvasBottom:
                lod->lvi_CanvasBottom = (LONG) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_CanvasUnits:
                lod->lvi_CanvasUnits = (ULONG) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_NativeDPI:
                lod->lvi_NativeDPI = (ULONG) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_RenderDPI:
                lod->lvi_RenderDPI = (ULONG) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_Scale:
                lod->lvi_Scale = (ULONG) ti->ti_Data;
                if (lod->lvi_Scale)
                    lod->lvi_FixedScale =
                        (LONG) ((lod->lvi_Scale * 65536UL) / 100UL);
                refresh = 1;
                break;
            case VIMA_FixedScale:
                lod->lvi_FixedScale = (LONG) ti->ti_Data;
                lod->lvi_Scale = (ULONG)
                    ((lod->lvi_FixedScale * 100UL) / 65536UL);
                refresh = 1;
                break;
            case VIMA_Rotation:
                lod->lvi_Rotation = (ULONG) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_UnitsPerPoint:
                if ((LONG) ti->ti_Data > 0)
                    lod->lvi_UnitsPerPoint = (LONG) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_RenderQuality:
                lod->lvi_RenderQuality = (ULONG) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_RenderFlags:
                lod->lvi_RenderFlags = (ULONG) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_BackgroundR:
                lod->lvi_BackgroundR = (UBYTE) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_BackgroundG:
                lod->lvi_BackgroundG = (UBYTE) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_BackgroundB:
                lod->lvi_BackgroundB = (UBYTE) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_ColorMap:
                lod->lvi_ColorMap = (struct ColorMap *) ti->ti_Data;
                refresh = 1;
                break;
            case VIMA_SourceFile:
                lod->lvi_SourceFile =
                    poolStrDup (cb, lod, (CONST_STRPTR) ti->ti_Data);
                break;
            case VIMA_SourceFormat:
                lod->lvi_SourceFormat = (ULONG) ti->ti_Data;
                break;
            case IA_Width:
            case IA_Height:
                /* imageclass has already updated the embedded Image field
                 * before this method is called from OM_SET/OM_UPDATE.  Mark
                 * our cached bitmap dirty so a subsequent IM_DRAW is rebuilt
                 * at the new image size instead of blitting stale pixels. */
                refresh = 1;
                break;
            default:
                break;
        }
    }

    if (refresh)
        lod->lvi_Flags |= LVIF_CACHE_DIRTY;

    return refresh;
}
