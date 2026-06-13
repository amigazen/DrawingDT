/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* classdata.h -- per-instance state for vector.image                        */
/*****************************************************************************/

#ifndef VECTOR_CLASSDATA_H
#define VECTOR_CLASSDATA_H

#define LVI_GROUP_STACK_DEPTH   32
#define LVI_POLY_INITIAL        32
#define LVI_POLY_GROW           32

struct GroupStack
{
    UWORD    gs_Top;
    UWORD    gs_Pad;
    struct DrawObj *gs_Items[LVI_GROUP_STACK_DEPTH];
};

struct PolyBuilder
{
    struct DrawPoint *pb_Points;
    ULONG    pb_Used;
    ULONG    pb_Allocated;
    ULONG    pb_Type;
    ULONG    pb_LineColor;
    ULONG    pb_FillColor;
    BOOL     pb_Open;
    BOOL     pb_HasBezier;
};

struct localData
{
    ULONG               lvi_ModeID;
    struct BitMap      *lvi_CachedBM;
    UWORD               lvi_CacheW;
    UWORD               lvi_CacheH;
    ULONG               lvi_CacheState;

    LONG                lvi_CanvasLeft;
    LONG                lvi_CanvasTop;
    LONG                lvi_CanvasRight;
    LONG                lvi_CanvasBottom;
    ULONG               lvi_CanvasUnits;

    LONG                lvi_NativeWidth;
    LONG                lvi_NativeHeight;

    ULONG               lvi_NativeDPI;
    ULONG               lvi_RenderDPI;
    ULONG               lvi_Scale;
    LONG                lvi_FixedScale;
    ULONG               lvi_Rotation;

    /* Canvas units per typographic point (16.16).  ProVector text markup    *
     * (\ps point size, \hm/\vm motion) is expressed in absolute points,     *
     * while geometry/CharH are in the drawing's own units.  This factor     *
     * converts points into canvas units so styled text scales with the      *
     * rest of the drawing.  Default 1/72 == 1 canvas unit per inch, which   *
     * matches inch-based DR2D/ProVector documents.                          */
    LONG                lvi_UnitsPerPoint;

    ULONG               lvi_RenderQuality;
    ULONG               lvi_RenderFlags;
    ULONG               lvi_ColorMode;
    ULONG               lvi_DrawingFlags;
    BOOL                lvi_FitToBox;
    BOOL                lvi_PreserveAspect;

    UBYTE               lvi_BackgroundR;
    UBYTE               lvi_BackgroundG;
    UBYTE               lvi_BackgroundB;
    UBYTE               lvi_Pad0;

    STRPTR              lvi_SourceFile;
    ULONG               lvi_SourceFormat;

    struct List         lvi_ObjectsList;
    struct List         lvi_LayerList;
    struct List         lvi_FontList;

    ULONG               lvi_ObjectCount;
    ULONG               lvi_LayerCount;
    ULONG               lvi_FontCount;
    ULONG               lvi_ActiveLayer;

    UBYTE              *lvi_RGBTable;
    ULONG               lvi_NumPaletteColors;

    struct ColorMap    *lvi_ColorMap;

    struct GroupStack   lvi_GroupStack;
    struct PolyBuilder  lvi_PolyBuilder;

    APTR                lvi_Pool;

    ULONG               lvi_Flags;
};

#define LVIF_CACHE_DIRTY    (1L<<0)
#define LVIF_BUILDING       (1L<<1)

#endif /* VECTOR_CLASSDATA_H */
