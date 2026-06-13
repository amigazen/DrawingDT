/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

#ifndef DATATYPES_DRAWINGCLASS_H
#define DATATYPES_DRAWINGCLASS_H
/*
**  drawingclass.h
**
**  Public interface for the DataType vector drawing super-class.
**
**  drawing.datatype is a root class for resolution-independent 2-D
**  vector artwork.  It is a sibling of picture.datatype - both inherit
**  directly from datatypesclass - but unlike picture.datatype, the
**  underlying representation is a scalable display list of vector
**  primitives, not a fixed-resolution raster.  The class only
**  rasterises at final draw time, so any zoom factor or render DPI
**  can be applied without quality loss.
**
**  The class is the parent for:
**    dr2d.datatype   (the IFF DR2D format - default subclass)
**    svg.datatype    (Scalable Vector Graphics, as a vector subclass)
**    wmf.datatype    (Windows Metafile, as a vector subclass)
**    eps.datatype    (Encapsulated PostScript)
**    xfig.datatype   (XFig native files)
**  ...and any other vector container an application chooses to
**  register.
**
**  The class deliberately re-uses PDTA_BitMap, PDTA_BitMapHeader and
**  PDTA_ModeID for the cached rasterised view at the current scale,
**  so that legacy applications such as MultiView display a drawing
**  with no source-level changes.
**
**  Backwards compatibility
**  =======================
**  drawing.datatype reproduces the OS4 version "2D structured art
**  super-class" draft API (DDTA_Header, DDTM_ADDPOLY, DDTM_ADDATTR,
**  DDTM_SETSIZE, struct ddtHeader, struct ddtPolygon, struct
**  ddtAttr) so existing subclasses written against the OS4 version
**  header continue to work unmodified.  New code should prefer the
**  richer DDTA_* / DDTM_* / struct DrawObj API documented below.
*/

#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif

#ifndef DATATYPES_DATATYPESCLASS_H
#include <datatypes/datatypesclass.h>
#endif

#ifndef DATATYPES_PICTURECLASS_H
#include <datatypes/pictureclass.h>
#endif

#ifndef EXEC_LISTS_H
#include <exec/lists.h>
#endif

#ifndef EXEC_NODES_H
#include <exec/nodes.h>
#endif

#ifndef INTUITION_INTUITION_H
#include <intuition/intuition.h>
#endif

#ifndef GRAPHICS_GFX_H
#include <graphics/gfx.h>
#endif

#ifndef GRAPHICS_RASTPORT_H
#include <graphics/rastport.h>
#endif

#include <images/vectorclass.h>


/*****************************************************************************/

#define DRAWINGDTCLASS          "drawing.datatype"

/* Group ID under which all drawing.datatype subclasses register.  This is
 * the same constant the OS4 version draft uses, so any datatype installed
 * with DTA_GroupID=GID_DRAWING is recognised by the class. */
#define GID_DRAWING             MAKE_ID('d','r','a','w')


/*****************************************************************************/
/* TAG IDs                                                                   */
/*                                                                           */
/* drawing.datatype IS the drawing class, so it owns the single attribute    */
/* block the OS4 draft reserved for it -- DTA_Dummy + 0x800 (DDTA_Dummy).    */
/* We do NOT fork a separate prefix: every attribute, old and new, is a      */
/* DDTA_* tag inside DTA_Dummy + 0x800 .. 0x8FF.  The handful of tags the    */
/* OS4 draft defined sit at the bottom of the block; this class's own        */
/* additions continue above them from DDTA_Dummy + 0x10, leaving             */
/* DDTA_Dummy + 0x02..0x0F reserved for any further draft tags.              */
/*                                                                           */
/*  DDTA_Dummy + 0x01..0x0F  -- OS4 draft compatibility tags.                */
/*                              Currently only DDTA_Header (DDTA_Dummy + 1).  */
/*                              Match the draft exactly; do not renumber.     */
/*                                                                           */
/*  DDTA_Dummy + 0x10..      -- the full drawing.datatype attribute set.      */
/*                                                                           */
/* The cached raster view is exposed through DDTA_BitMap and                 */
/* DDTA_BitMapHeader, not through picture.datatype PDTA_BitMap /             */
/* PDTA_BitMapHeader.  The root class deliberately does not advertise the    */
/* PDTA_* bitmap fast path, so generic viewers drive it through the          */
/* refreshable gadget path and preserve vector-quality redraws when scale,   */
/* rotation, or render DPI changes.                                          */
/*****************************************************************************/

/* --- OS4 draft compatibility ------------------------------------------- */
#define DDTA_Dummy              (DTA_Dummy + 0x800)
#define DDTA_Header             (DDTA_Dummy + 0x01)
        /* (struct ddtHeader *) (G) -- canvas extents in DR2D units */

/* --- Canvas geometry --------------------------------------------------- */

#define DDTA_CanvasLeft         (DDTA_Dummy + 0x10)
        /* (LONG)             (G)   -- left edge in 16.16 Fixed   */
#define DDTA_CanvasTop          (DDTA_Dummy + 0x11)
        /* (LONG)             (G)   -- top edge in 16.16 Fixed    */
#define DDTA_CanvasRight        (DDTA_Dummy + 0x12)
        /* (LONG)             (G)   -- right edge in 16.16 Fixed  */
#define DDTA_CanvasBottom       (DDTA_Dummy + 0x13)
        /* (LONG)             (G)   -- bottom edge in 16.16 Fixed */
#define DDTA_CanvasUnits        (DDTA_Dummy + 0x14)
        /* (ULONG)            (ISG) -- DRWU_xxx (default: inches) */
#define DDTA_NativeWidth        (DDTA_Dummy + 0x15)
        /* (LONG)             (G)   -- canvas width in points (1/72 inch) */
#define DDTA_NativeHeight       (DDTA_Dummy + 0x16)
        /* (LONG)             (G)   -- canvas height in points              */

/* --- Rendering controls ----------------------------------------------- */

#define DDTA_NativeDPI          (DDTA_Dummy + 0x20)
        /* (ULONG)            (G)   -- the document's nominal DPI (72)    */
#define DDTA_RenderDPI          (DDTA_Dummy + 0x21)
        /* (ULONG)            (ISG) -- target DPI; default 72             */
#define DDTA_Scale              (DDTA_Dummy + 0x22)
        /* (ULONG)            (ISG) -- zoom factor in integer percent     */
#define DDTA_FixedScale         (DDTA_Dummy + 0x23)
        /* (LONG)             (ISG) -- zoom as 16.16 Fixed; 0x10000=100%  */
#define DDTA_Rotation           (DDTA_Dummy + 0x24)
        /* (ULONG)            (ISG) -- 0, 90, 180, 270 degrees            */
#define DDTA_RenderQuality      (DDTA_Dummy + 0x25)
        /* (ULONG)            (ISG) -- DRWQ_xxx                           */
#define DDTA_RenderFlags        (DDTA_Dummy + 0x26)
        /* (ULONG)            (ISG) -- DRWRF_xxx bitfield                 */
#define DDTA_AntiAlias          (DDTA_Dummy + 0x27)
        /* (BOOL)             (ISG) -- shortcut for DDTA_RenderQuality    */
#define DDTA_ColorMode          (DDTA_Dummy + 0x28)
        /* (ULONG)            (ISG) -- DRWCM_xxx                          */
#define DDTA_BackgroundR        (DDTA_Dummy + 0x29)  /* (UBYTE)  (ISG) */
#define DDTA_BackgroundG        (DDTA_Dummy + 0x2A)  /* (UBYTE)  (ISG) */
#define DDTA_BackgroundB        (DDTA_Dummy + 0x2B)  /* (UBYTE)  (ISG) */
#define DDTA_FitToBox           (DDTA_Dummy + 0x2C)
        /* (BOOL)             (ISG) -- if TRUE, scale fits the gadget    *
         *                            domain rather than honouring        *
         *                            DDTA_Scale                          */
#define DDTA_PreserveAspect     (DDTA_Dummy + 0x2D)
        /* (BOOL)             (ISG) -- preserve aspect ratio when fitting */

#define DDTA_BitMap             (DDTA_Dummy + 0x2E)
        /* (struct BitMap *)  (G)   -- rasterised view at current scale.   */
#define DDTA_BitMapHeader       (DDTA_Dummy + 0x2F)
        /* (struct BitMapHeader *) (G) -- header for DDTA_BitMap           */

/* --- Async progress / cancellation ------------------------------------ */

#define DDTA_RenderProgress     (DDTA_Dummy + 0x30)
        /* (ULONG *)          (IS)  -- progress sink, 0..100             */
#define DDTA_AbortRender        (DDTA_Dummy + 0x31)
        /* (ULONG *)          (IS)  -- poll non-zero to abort            */

/* --- Metadata --------------------------------------------------------- */

#define DDTA_Title              (DDTA_Dummy + 0x40)  /* (STRPTR)   (ISG) */
#define DDTA_Author             (DDTA_Dummy + 0x41)  /* (STRPTR)   (ISG) */
#define DDTA_Subject            (DDTA_Dummy + 0x42)  /* (STRPTR)   (ISG) */
#define DDTA_Keywords           (DDTA_Dummy + 0x43)  /* (STRPTR)   (ISG) */
#define DDTA_Producer           (DDTA_Dummy + 0x44)  /* (STRPTR)   (ISG) */
#define DDTA_Creator            (DDTA_Dummy + 0x45)  /* (STRPTR)   (ISG) */
#define DDTA_CreationDate       (DDTA_Dummy + 0x46)  /* (struct DateStamp *)(ISG)*/
#define DDTA_ModificationDate   (DDTA_Dummy + 0x47)  /* (struct DateStamp *)(ISG)*/
#define DDTA_DrawingFlags       (DDTA_Dummy + 0x48)
        /* (ULONG)            (G)   -- DRWF_xxx bitfield */

/* --- Display list ----------------------------------------------------- */

#define DDTA_DisplayList        (DDTA_Dummy + 0x50)
        /* (struct List *)    (G)   -- root list of DrawObj                 */
#define DDTA_ObjectCount        (DDTA_Dummy + 0x51)
        /* (ULONG)            (G)   -- count of top-level objects           */
#define DDTA_LayerCount         (DDTA_Dummy + 0x52)
        /* (ULONG)            (G)   -- count of layers                      */
#define DDTA_LayerList          (DDTA_Dummy + 0x53)
        /* (struct List *)    (G)   -- list of DrawLayer                    */
#define DDTA_ActiveLayer        (DDTA_Dummy + 0x54)
        /* (ULONG)            (ISG) -- index of currently active layer     */
#define DDTA_ColorTable         (DDTA_Dummy + 0x55)
        /* (UBYTE *)          (G)   -- RGB triplets (3*NumColors bytes)    */
#define DDTA_NumColors          (DDTA_Dummy + 0x56)
        /* (ULONG)            (G)   -- entries in the RGB table             */
#define DDTA_DrawingInfo        (DDTA_Dummy + 0x57)
        /* (struct DrawingInfo *) (G) -- top-level descriptor               */

/* --- Streaming I/O ---------------------------------------------------- */

#define DDTA_Stream             (DDTA_Dummy + 0x60)
        /* (struct DrawStream *) (G) -- handle for lazy reads              */
#define DDTA_StreamSize         (DDTA_Dummy + 0x61)
        /* (LONG)             (G)   -- stream length, -1 unknown           */
#define DDTA_StreamPosition     (DDTA_Dummy + 0x62)
        /* (LONG)             (G)   -- current stream position             */
#define DDTA_IFFHandle          (DDTA_Dummy + 0x63)
        /* (struct IFFHandle *) (G) -- IFF handle for the source if any   */


/*****************************************************************************/
/* METHOD IDs                                                                */
/*                                                                           */
/* As with the attributes, drawing.datatype owns the single method block the */
/* OS4 draft reserved (DDTM_Dummy = 0x800) and expands on it rather than     */
/* forking a new prefix, so every method ID is a DDTM_* method inside        */
/* 0x800 .. 0x8FF.  The OS4 draft methods sit at the bottom of the block;    */
/* this class's own methods continue above them from DDTM_Dummy + 0x10,      */
/* leaving DDTM_Dummy + 0x04..0x0F reserved for any further draft methods:   */
/*                                                                           */
/*  DDTM_Dummy + 0x01..0x0F  -- OS4 draft compatibility methods              */
/*                              (DDTM_ADDATTR, DDTM_ADDPOLY, DDTM_SETSIZE).  */
/*                              Match the draft exactly; do not renumber.    */
/*                                                                           */
/*  DDTM_Dummy + 0x10..      -- the full drawing.datatype method set.        */
/*****************************************************************************/

/* --- OS4 draft compatibility methods --- */
#define DDTM_Dummy              (0x800)
#define DDTM_ADDATTR            (DDTM_Dummy + 0x01)  /* struct ddtAttr     */
#define DDTM_ADDPOLY            (DDTM_Dummy + 0x02)  /* struct ddtPolygon  */
#define DDTM_SETSIZE            (DDTM_Dummy + 0x03)  /* struct ddtSize     */

/* --- Application-facing methods --- */
#define DDTM_RENDERDRAWING      (DDTM_Dummy + 0x10)  /* render into RastPort */
#define DDTM_RENDERPIXELS       (DDTM_Dummy + 0x11)  /* render into buffer  */
#define DDTM_HITTESTPOINT       (DDTM_Dummy + 0x12)  /* find object at xy   */
#define DDTM_GETBOUNDINGBOX     (DDTM_Dummy + 0x13)  /* extent of selection */
#define DDTM_CANCEL             (DDTM_Dummy + 0x14)  /* abort rendering     */
#define DDTM_FITTOBOX           (DDTM_Dummy + 0x15)  /* compute fit scale   */

/* --- Display-list construction (used by subclass loaders, app code,
 * and the OS4 draft DDTM_ADDPOLY/ADDATTR compatibility shims) --- */
#define DDTM_ADDOBJECT          (DDTM_Dummy + 0x20)  /* append DrawObj      */
#define DDTM_BEGINGROUP         (DDTM_Dummy + 0x21)  /* push group context */
#define DDTM_ENDGROUP           (DDTM_Dummy + 0x22)  /* pop group context  */
#define DDTM_ADDLAYER           (DDTM_Dummy + 0x23)  /* declare layer      */
#define DDTM_ADDCOLOR           (DDTM_Dummy + 0x24)  /* declare palette    */
#define DDTM_ADDFONT            (DDTM_Dummy + 0x25)  /* declare font       */
#define DDTM_CLEARDRAWING       (DDTM_Dummy + 0x26)  /* drop all objects   */
#define DDTM_SETBOUNDS          (DDTM_Dummy + 0x27)  /* set canvas extents */

/* --- Subclass content provision (root sends these DOWN to the subclass) --- */
#define DDTM_LOADHEADERS        (DDTM_Dummy + 0x30)
#define DDTM_LOADCONTENT        (DDTM_Dummy + 0x31)
#define DDTM_RENDERVECTORS      (DDTM_Dummy + 0x32)


/*****************************************************************************/
/* FLAGS (render/canvas flags: see vectorclass.h)                            */
/*****************************************************************************/

/* DDTM_CANCEL.drwc_What */
#define DRWCW_ANY               0
#define DRWCW_RENDER            1
#define DRWCW_LOAD              2

/*****************************************************************************/
/* STRUCTURES vector model is found in images/vectorclass.h)                 */
/*****************************************************************************/

/* OS4 version compatibility: the canvas header used by DDTA_Header.
 *
 * NOTE: the OS4 version draft declared the four extents as 'float'.  Because
 * float is rarely passed through SetGadgetAttrs() / GetAttr() without
 * dropping bytes (datatypes.library uses ULONG storage), the modern
 * implementation also exposes the same values through the DDTA_Canvas*
 * Fixed-point tags.  Subclasses that need bit-exact source compatibility
 * with code written against the OS4 version header receive the struct
 * unchanged; new code should use DDTA_Canvas*.
 */
struct ddtHeader
{
    float   ddth_XLeft;
    float   ddth_YTop;
    float   ddth_XRight;
    float   ddth_YBot;
};

/* OS4 version compatibility: DDTM_SETSIZE message */
struct ddtSize
{
    ULONG   MethodID;       /* DDTM_SETSIZE                  */
    float   ddts_Left;
    float   ddts_Right;
    float   ddts_Top;
    float   ddts_Bottom;
};

/* OS4 version compatibility: DDTM_ADDATTR message */
struct ddtAttr
{
    ULONG   MethodID;       /* DDTM_ADDATTR                  */
    ULONG   ddta_Type;      /* DDTA_COLOURS or other         */
    ULONG   ddta_Data;
    ULONG   ddta_Data2;
};

/* OS4 version compatibility: DDTM_ADDPOLY message */
struct ddtPolygon
{
    ULONG   MethodID;       /* DDTM_ADDPOLY                  */
    ULONG   ddtp_Type;      /* DDTP_LINE / DDTP_BEZIER / ... */
    float   ddtp_PosX;
    float   ddtp_PosY;
    float   ddtp_PosX2;     /* DDTP_BEZIER only              */
    float   ddtp_PosY2;     /* DDTP_BEZIER only              */
    float   ddtp_PosX3;     /* DDTP_BEZIER only              */
    float   ddtp_PosY3;     /* DDTP_BEZIER only              */
};

/* Polygon types, OS4 version */
#define DDTP_LINE               0x0
#define DDTP_BEZIER             0x1
#define DDTP_MOVETO             0x2
#define DDTP_CLOSE              0x4
#define DDTP_END                0x8

/* Attribute types, OS4 version (passed via ddta_Type) */
#define DDTA_COLOURS            0x0   /* Data = line ARGB, Data2 = fill ARGB */

/* Special "no paint" sentinel colour.  Declared here for source            *
 * compatibility with the OS4 version drawingclass.h, which defined it      *
 * in this header.                                                          */
#ifndef DDTA_TRANSPARENT
#define DDTA_TRANSPARENT        0x01000000   /* compatible no-paint colour  */
#endif


/* DrawingInfo -- top-level descriptor populated by the subclass loader.
 * The application reads this via DDTA_DrawingInfo to learn what the
 * file contained without having to walk every list itself.  All lists
 * remain owned by the class's per-instance memory pool.               */
struct DrawingInfo
{
    LONG    di_Left, di_Top;        /* canvas extents in 16.16 Fixed   */
    LONG    di_Right, di_Bottom;
    ULONG   di_Units;               /* DRWU_xxx                        */
    ULONG   di_Flags;               /* DRWF_xxx                        */
    ULONG   di_NumObjects;
    ULONG   di_NumLayers;
    ULONG   di_NumColors;
    struct List *di_Objects;        /* list of DrawObj                  */
    struct List *di_Layers;         /* list of DrawLayer                */
    struct List *di_Fonts;          /* list of DrawFont                 */
    UBYTE      *di_RGBTable;        /* 3*di_NumColors bytes             */
    UBYTE      *di_CMYKTable;       /* 4*di_NumColors bytes (optional)  */
    APTR        di_Private;         /* subclass-private                 */
};

/* --- Streaming I/O ----------------------------------------------------- */
/*
 * We expose the stream so that subclass loaders that prefer to read 
 * incrementally can do so (this matters for the DR2D parser, which walks 
 * IFF chunks).
 */

struct DrawStream
{
    ULONG   ds_Type;        /* same DST_xxx values as docstream      */
    LONG    ds_Length;
    LONG    ds_Position;
    LONG  (*ds_Read)(struct DrawStream *self, APTR buf, LONG n);
    LONG  (*ds_Seek)(struct DrawStream *self, LONG offset, LONG mode);
    APTR    ds_Private;
};

#define DrawStreamRead(s, buf, n)   ((s)->ds_Read ((s), (buf), (n)))
#define DrawStreamSeek(s, off, m)   ((s)->ds_Seek ((s), (off), (m)))


/*****************************************************************************/
/* METHOD MESSAGES                                                           */
/*****************************************************************************/

/* DDTM_RENDERDRAWING -- render the drawing into a caller-supplied RastPort
 * at a caller-supplied scale.  The root class implementation walks the
 * display list and rasterises it directly into the rastport using
 * graphics.library Move/Draw/PolyDraw/AreaEnd calls; subclasses with
 * format-specific render contracts (SVG with gradients, EPS via a
 * PostScript interpreter, etc.) override this method.                    */

struct drwRenderDrawing
{
    ULONG   MethodID;               /* DDTM_RENDERDRAWING               */
    struct GadgetInfo *drwrd_GInfo;
    struct RastPort   *drwrd_RPort;
    LONG    drwrd_Left;             /* destination position             */
    LONG    drwrd_Top;
    LONG    drwrd_Width;
    LONG    drwrd_Height;
    LONG    drwrd_FixedScaleX;      /* 16.16 Fixed                      */
    LONG    drwrd_FixedScaleY;
    UWORD   drwrd_Rotation;
    UWORD   drwrd_RenderQuality;
    ULONG   drwrd_RenderFlags;
    ULONG  *drwrd_AbortPtr;
    ULONG  *drwrd_ProgressPtr;
};

/* DDTM_RENDERPIXELS -- render into a packed pixel buffer.               */
struct drwRenderPixels
{
    ULONG   MethodID;
    struct GadgetInfo *drwrp_GInfo;
    APTR    drwrp_PixelData;
    ULONG   drwrp_PixelFormat;     /* PBPAFMT_xxx from pictureclass.h    */
    LONG    drwrp_PixelArrayMod;
    LONG    drwrp_Width;
    LONG    drwrp_Height;
    LONG    drwrp_FixedScaleX;
    LONG    drwrp_FixedScaleY;
    UWORD   drwrp_Rotation;
    UWORD   drwrp_RenderQuality;
    ULONG   drwrp_RenderFlags;
    ULONG  *drwrp_AbortPtr;
    ULONG  *drwrp_ProgressPtr;
};

/* DDTM_HITTESTPOINT */
struct drwHitTestPoint
{
    ULONG   MethodID;
    LONG    drwh_X;                 /* in canvas units, 16.16 Fixed     */
    LONG    drwh_Y;
    struct DrawObj **drwh_Result;
};

/* DDTM_GETBOUNDINGBOX */
struct drwGetBoundingBox
{
    ULONG   MethodID;
    LONG    *drwbb_XMin;
    LONG    *drwbb_YMin;
    LONG    *drwbb_XMax;
    LONG    *drwbb_YMax;
};

/* DDTM_CANCEL */
struct drwCancel
{
    ULONG   MethodID;
    ULONG   drwc_What;              /* DRWCW_xxx                        */
};

/* DDTM_FITTOBOX */
struct drwFitToBox
{
    ULONG   MethodID;
    LONG    drwf_BoxWidth;          /* pixels                           */
    LONG    drwf_BoxHeight;
    BOOL    drwf_PreserveAspect;
    LONG   *drwf_FixedScaleX;       /* OUT: 16.16                       */
    LONG   *drwf_FixedScaleY;
};

/* DDTM_ADDOBJECT */
struct drwAddObject
{
    ULONG   MethodID;
    struct DrawObj *drwa_Obj;       /* class takes ownership            */
};

/* DDTM_BEGINGROUP */
struct drwBeginGroup
{
    ULONG   MethodID;
    UWORD   drwbg_Type;             /* DROT_GROUP / DROT_FILL / etc.    */
    UWORD   drwbg_Pad;
    ULONG   drwbg_ExpectCount;      /* hint, 0 = unknown                */
    ULONG   drwbg_FillID;           /* DROT_FILL pattern id (else 0)    */
};

/* DDTM_ENDGROUP */
struct drwEndGroup
{
    ULONG   MethodID;
};

/* DDTM_ADDLAYER */
struct drwAddLayer
{
    ULONG   MethodID;
    struct DrawLayer *drwal_Layer;
};

/* DDTM_ADDCOLOR */
struct drwAddColor
{
    ULONG   MethodID;
    UBYTE   drwac_R;
    UBYTE   drwac_G;
    UBYTE   drwac_B;
    UBYTE   drwac_Pad;
};

/* DDTM_ADDFONT */
struct drwAddFont
{
    ULONG   MethodID;
    struct DrawFont *drwaf_Font;
};

/* DDTM_CLEARDRAWING */
struct drwClearDrawing
{
    ULONG   MethodID;
};

/* DDTM_SETBOUNDS */
struct drwSetBounds
{
    ULONG   MethodID;
    LONG    drwsb_Left;             /* 16.16 Fixed                       */
    LONG    drwsb_Top;
    LONG    drwsb_Right;
    LONG    drwsb_Bottom;
    ULONG   drwsb_Units;            /* DRWU_xxx                          */
};

/* --- Subclass content provision messages -------------------------------- */

/* DDTM_LOADHEADERS -- the root class sends this to the subclass during
 * OM_NEW.  The subclass MUST parse just enough of the source to populate
 * the DrawingInfo (canvas extents, layer table, colour table).  The
 * content (the actual display list) follows in DDTM_LOADCONTENT.  This
 * two-phase load lets the application reserve scroller geometry before
 * the slow path runs.                                                      */
struct drwLoadHeaders
{
    ULONG   MethodID;
    struct DrawStream *drwlh_Stream;
    struct DrawingInfo *drwlh_Info;
    APTR    drwlh_Pool;
};

/* DDTM_LOADCONTENT -- the root class sends this after DDTM_LOADHEADERS,
 * either synchronously inside OM_NEW or asynchronously from
 * DTM_ASYNCLAYOUT, depending on the file size.  The subclass appends
 * DrawObj nodes to drwlc_DisplayList using AllocPooled() against the
 * supplied pool.                                                           */
struct drwLoadContent
{
    ULONG   MethodID;
    struct DrawStream *drwlc_Stream;
    struct List *drwlc_DisplayList;
    APTR    drwlc_Pool;
    ULONG  *drwlc_AbortPtr;
    ULONG  *drwlc_ProgressPtr;
};

/* DDTM_RENDERVECTORS -- the root class sends this when its default
 * line-and-fill rasteriser would produce inadequate output (e.g. an
 * SVG with gradients, or an EPS that needs PostScript).  The subclass
 * is given a fresh RastPort already clipped to the rendering target
 * and asked to draw the supplied object list into it.                      */
struct drwRenderVectors
{
    ULONG   MethodID;
    struct GadgetInfo *drwrv_GInfo;
    struct RastPort   *drwrv_RPort;
    struct List       *drwrv_Objects;
    LONG    drwrv_DestX;
    LONG    drwrv_DestY;
    LONG    drwrv_DestWidth;
    LONG    drwrv_DestHeight;
    LONG    drwrv_FixedScaleX;
    LONG    drwrv_FixedScaleY;
    LONG    drwrv_CanvasX;          /* canvas-coord origin               */
    LONG    drwrv_CanvasY;
    UWORD   drwrv_Rotation;
    UWORD   drwrv_RenderQuality;
    ULONG   drwrv_RenderFlags;
    ULONG  *drwrv_AbortPtr;
    ULONG  *drwrv_ProgressPtr;
};


#endif /* DATATYPES_DRAWINGCLASS_H */
