/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* classdata.h -- per-instance state for drawing.datatype                    */
/*                                                                           */
/* Attached to every Object created from drawing.datatype.  Holds:           */
/*                                                                           */
/*   * The scalable display list (modern struct DrawObj nodes) along with    */
/*     auxiliary tables (layers, fonts, colour palette).                     */
/*                                                                           */
/*   * The OS4 version-draft ddtHeader struct, kept in sync with the more      */
/*     general DrawingInfo so existing client code that queries              */
/*     DDTA_Header continues to work.                                        */
/*                                                                           */
/*   * Render-time state (current scale, rotation, render DPI, quality,      */
/*     flags, background colour).                                            */
/*                                                                           */
/*   * The cached rasterised bitmap (picture-datatype compatibility).        */
/*                                                                           */
/*   * A streaming source (DST_FILE / DST_RAM / DST_CLIPBOARD) shared by     */
/*     subclass loaders during DDTM_LOADHEADERS / DDTM_LOADCONTENT.          */
/*                                                                           */
/*   * The memory pool every per-document allocation comes from, so          */
/*     OM_DISPOSE can tear everything down with a single DeletePool().       */
/*****************************************************************************/

#ifndef CLASSDATA_H
#define CLASSDATA_H

#define G(o)    ((struct Gadget *)(o))


/* StreamPriv -- private state for the DrawStream function pointers.
 * The function-pointer entry points have no `cb' parameter, so we keep
 * a back-pointer to the class base in the stream's private area; the
 * read/seek helpers cast ds_Private to this struct and use sp_CB to
 * set up the per-function DOSBase macro.                                */
struct StreamPriv
{
    struct ClassBase *sp_CB;
    BPTR              sp_FileHandle;
    APTR              sp_Buffer;
    LONG              sp_BufferSize;
};


/* GroupStack -- one stack entry for DDTM_BEGINGROUP / DDTM_ENDGROUP.
 *
 * The drawing.datatype display list is hierarchical: groups can nest,
 * fills can contain prototype objects, and the IFF DR2D format expresses
 * "an XTRN object with a single child" through this same mechanism.
 * When a subclass loader (or a DDTM_ADDPOLY-style application) calls
 * DDTM_BEGINGROUP we push a stack frame; subsequent DDTM_ADDOBJECT calls
 * append to the group's child list rather than the root list.  An
 * arbitrary stack depth would require dynamic allocation; we cap at 32
 * which matches the deepest sane DR2D / SVG nesting seen in practice. */
#define LDD_GROUP_STACK_DEPTH   32

struct GroupStack
{
    UWORD    gs_Top;
    UWORD    gs_Pad;
    struct DrawObj *gs_Items[LDD_GROUP_STACK_DEPTH];
};


/* PolyBuilder -- state for the OS4 version DDTM_ADDPOLY contract.
 *
 * That contract is fundamentally streaming: an application calls
 * DDTM_ADDPOLY repeatedly with DDTP_MOVETO / DDTP_LINE / DDTP_BEZIER
 * messages, and finally a DDTP_CLOSE or DDTP_END to commit the
 * polygon to the display list.  We accumulate the points here and
 * spawn a DrawObj when CLOSE / END arrives.                              */
#define LDD_POLY_INITIAL        32
#define LDD_POLY_GROW           32

struct PolyBuilder
{
    struct DrawPoint *pb_Points;
    ULONG    pb_Used;
    ULONG    pb_Allocated;
    ULONG    pb_Type;       /* the DDTM_ADDATTR colour pair carries here */
    ULONG    pb_LineColor;
    ULONG    pb_FillColor;
    BOOL     pb_Open;       /* TRUE between first MOVETO and CLOSE/END   */
    BOOL     pb_HasBezier;
};


struct localData
{
    /* ------------------------------------------------------------------ */
    /* Cached rasterised view (mirrors picture.datatype layout for        */
    /* PDTA_BitMap / PDTA_BitMapHeader compatibility).                    */
    /* ------------------------------------------------------------------ */

    ULONG               ldd_ModeID;
    struct BitMapHeader ldd_BMHD;
    Point               ldd_Point;
    struct BitMap      *ldd_OurBMap;
    struct BitMap      *ldd_SourceBMap;
    struct BitMap      *ldd_BMap;       /* cached rasterised drawing      */
    struct Screen      *ldd_Screen;

    /* Colour / remap bookkeeping (palette screen output) */
    struct ColorRegister *ldd_Colors;
    LONG               *ldd_CRegs;
    LONG               *ldd_GRegs;
    UBYTE              *ldd_ColorTable;
    UBYTE              *ldd_ColorTable2;
    UBYTE              *ldd_Allocated;
    WORD                ldd_NumColors;
    WORD                ldd_NumAlloc;
    struct ColorMap    *ldd_ColorMap;
    ULONG               ldd_Precision;

    /* ------------------------------------------------------------------ */
    /* Drawing state.                                                     */
    /* ------------------------------------------------------------------ */

    struct DrawingInfo  ldd_InfoStorage;   /* always present              */
    struct ddtHeader    ldd_HeaderStorage; /* OS4 version compatibility     */

    /* Canvas extents in 16.16 Fixed (canonical) */
    LONG                ldd_CanvasLeft;
    LONG                ldd_CanvasTop;
    LONG                ldd_CanvasRight;
    LONG                ldd_CanvasBottom;
    ULONG               ldd_CanvasUnits;

    /* Native pixel extents derived from canvas at lod_NativeDPI          */
    LONG                ldd_NativeWidth;
    LONG                ldd_NativeHeight;

    ULONG               ldd_NativeDPI;
    ULONG               ldd_RenderDPI;
    ULONG               ldd_Scale;         /* integer percent             */
    LONG                ldd_FixedScale;    /* 16.16, kept in sync         */
    ULONG               ldd_Rotation;

    ULONG               ldd_RenderQuality;
    ULONG               ldd_RenderFlags;
    ULONG               ldd_ColorMode;
    ULONG               ldd_DrawingFlags;
    BOOL                ldd_FitToBox;
    BOOL                ldd_PreserveAspect;

    UBYTE               ldd_BackgroundR;
    UBYTE               ldd_BackgroundG;
    UBYTE               ldd_BackgroundB;
    UBYTE               ldd_Pad0;

    /* Embedded vector renderer (vector.image).  Display list and pool live *
     * inside this object; drawing.datatype forwards DDTM_* to it.          */
    Object             *ldd_VectorImage;

    ULONG               ldd_ObjectCount;
    ULONG               ldd_LayerCount;
    ULONG               ldd_FontCount;
    ULONG               ldd_ActiveLayer;

    UBYTE              *ldd_RGBTable;      /* 3 * ldd_NumColors           */
    UBYTE              *ldd_CMYKTable;     /* 4 * ldd_NumColors           */
    ULONG               ldd_NumPaletteColors;

    /* Metadata strings, all in ldd_Pool */
    STRPTR              ldd_Title;
    STRPTR              ldd_Author;
    STRPTR              ldd_Subject;
    STRPTR              ldd_Keywords;
    STRPTR              ldd_Producer;
    STRPTR              ldd_Creator;

    /* ------------------------------------------------------------------ */
    /* Async progress / cancellation                                      */
    /* ------------------------------------------------------------------ */

    ULONG              *ldd_AbortPtr;
    ULONG               ldd_AbortStorage;
    ULONG              *ldd_ProgressPtr;
    ULONG               ldd_ProgressStorage;

    /* ------------------------------------------------------------------ */
    /* Streaming source                                                   */
    /* ------------------------------------------------------------------ */

    struct DrawStream   ldd_Stream;
    struct StreamPriv   ldd_StreamPriv;
    BPTR                ldd_FileHandle;
    APTR                ldd_RamBuffer;
    LONG                ldd_RamBufferSize;
    BOOL                ldd_OwnsBuffer;
    STRPTR              ldd_Filename;
    ULONG               ldd_SourceType;

    /* IFF handle managed by the root class when the subclass needs to
     * walk IFF chunks.  Subclasses request one via DDTA_IFFHandle (G).  
     * The root class lazily allocates on first read and tears down in closeStream(). */
    struct IFFHandle   *ldd_IFFHandle;
    BOOL                ldd_OwnsIFF;

    /* ------------------------------------------------------------------ */
    /* Display-list construction support                                  */
    /* ------------------------------------------------------------------ */

    struct GroupStack   ldd_GroupStack;
    struct PolyBuilder  ldd_PolyBuilder;

    /* ------------------------------------------------------------------ */
    /* Free-running state                                                 */
    /* ------------------------------------------------------------------ */

    LONG                ldd_OTopVert;
    LONG                ldd_OTopHoriz;

    ULONG               ldd_Flags;
};


/*****************************************************************************/
/* ldd_Flags                                                                  */
/*****************************************************************************/

#define LDDF_REMAP          (1L<< 0)
#define LDDF_FREESRC        (1L<< 1)
#define LDDF_SAMEBM         (1L<< 2)
#define LDDF_HEADERS_LOADED (1L<< 3)
#define LDDF_CONTENT_LOADED (1L<< 4)
#define LDDF_VIEW_DIRTY     (1L<< 5)   /* cached bitmap stale            */
#define LDDF_RENDERING      (1L<< 6)
#define LDDF_CANCELLED      (1L<< 7)
#define LDDF_NEED_REFRESH   (1L<< 8)
#define LDDF_HAS_SOURCE     (1L<< 9)
#define LDDF_BUILDING       (1L<<10)   /* DDTM_ADDPOLY in flight         */

#endif /* CLASSDATA_H */
