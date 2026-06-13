/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* classbase.h -- internal header for dr2d.datatype                          */
/*                                                                           */
/* The dr2d.datatype subclass adds an IFF DR2D loader on top of              */
/* drawing.datatype.  All rendering, scaling, layout, hit-testing and        */
/* output are inherited unchanged from the super-class - this file only      */
/* needs to plumb in iffparse.library and the small per-instance state       */
/* the loader keeps (root-FORM stack, palette/layer interim tables, ...).    */
/*****************************************************************************/

#ifndef CLASSBASE_H
#define CLASSBASE_H

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/types.h>
#include <exec/ports.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <exec/semaphores.h>
#include <exec/execbase.h>
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/cghooks.h>
#include <intuition/gadgetclass.h>
#include <intuition/imageclass.h>
#include <intuition/icclass.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <libraries/iffparse.h>
#include <graphics/gfx.h>
#include <utility/tagitem.h>
#include <string.h>
#include <stddef.h>
#include <dos.h>

#include <clib/macros.h>
#include <clib/dos_protos.h>
#include <clib/exec_protos.h>
#include <clib/intuition_protos.h>
#include <clib/graphics_protos.h>
#include <clib/utility_protos.h>
#include <clib/dtclass_protos.h>
#include <clib/datatypes_protos.h>
#include <clib/iffparse_protos.h>

#include <pragmas/dos_pragmas.h>
#include <pragmas/exec_pragmas.h>
#include <pragmas/intuition_pragmas.h>
#include <pragmas/graphics_pragmas.h>
#include <pragmas/utility_pragmas.h>
#include <pragmas/dtclass_pragmas.h>
#include <pragmas/datatypes_pragmas.h>
#include <pragmas/iffparse_pragmas.h>

/* amiga.lib linkage (NewList et al.); link with LIB:amiga.lib. */
#include <proto/alib.h>

#include "dr2dclass.h"

/* The super-class public header -- we need its registered class name      *
 * (DRAWINGDTCLASS) and the full DrawObj/DrawAttr/DrawPoint definitions.    */
struct DrawingClassBase;     /* forward, root class's "ClassBase" type   */


/*****************************************************************************/
/* Library base for dr2d.datatype.                                           */
/*****************************************************************************/

struct ClassBase
{
    struct Library         cb_Lib;
    UWORD                  cb_UsageCnt;
    BPTR                   cb_SegList;

    struct Library        *cb_SysBase;
    struct Library        *cb_DOSBase;
    struct Library        *cb_IntuitionBase;
    struct Library        *cb_UtilityBase;
    struct Library        *cb_GfxBase;
    struct Library        *cb_DataTypesBase;
    struct Library        *cb_IFFParseBase;
    struct Library        *cb_SuperClassBase;  /* drawing.datatype library  */

    struct SignalSemaphore cb_Lock;
    Class                 *cb_Class;       /* dr2d.datatype class          */
};


/*****************************************************************************/
/* SAS/C register-parameter macros and helpers                               */
/*****************************************************************************/

#define ASM       __asm
#define REG(x)    register __ ## x

/*****************************************************************************/
/* Library base aliases                                                      */
/*****************************************************************************/

#define SysBase             cb->cb_SysBase
#define DOSBase             cb->cb_DOSBase
#define UtilityBase         cb->cb_UtilityBase
#define IntuitionBase       cb->cb_IntuitionBase
#define GfxBase             cb->cb_GfxBase
#define DataTypesBase       cb->cb_DataTypesBase
#define IFFParseBase        cb->cb_IFFParseBase
#define SuperClassBase      cb->cb_SuperClassBase


/*****************************************************************************/
/* BOOPSI helpers from amiga.lib                                             */
/*****************************************************************************/

ULONG __stdargs DoMethodA       (Object *obj, Msg message);
ULONG __stdargs DoMethod        (Object *obj, unsigned long MethodID, ...);
ULONG __stdargs DoSuperMethodA  (struct IClass *cl, Object *obj, Msg message);
ULONG __stdargs DoSuperMethod   (struct IClass *cl, Object *obj,
                                 unsigned long MethodID, ...);
ULONG __stdargs CoerceMethodA   (struct IClass *cl, Object *obj, Msg message);
ULONG __stdargs CoerceMethod    (struct IClass *cl, Object *obj,
                                 unsigned long MethodID, ...);
ULONG __stdargs SetSuperAttrs   (struct IClass *cl, Object *obj,
                                 unsigned long Tag1, ...);


/*****************************************************************************/
/* dr2d.datatype per-instance data                                           */
/*****************************************************************************/

#define DR2D_MAX_STACK   64       /* maximum FORM-DR2D nest depth         */
#define DR2D_MAX_COLORS  256      /* matches DR2D CMAP USHORT index range *
                                   * (in practice DR2D files have <= 256) */
#define DR2D_MAX_GRADIENTS 256    /* GRAD table size; GradIDs are small   */


/*
 * One entry of the GRAD (gradient) table, captured during the parse.
 * The colour indices are resolved against the CMAP cache lazily inside
 * readATTRChunk (by then the CMAP chunk has always been read, since GRAD
 * follows CMAP in every ProVector file).  Geometry is stored as 16.16
 * fixed: gr_Angle in degrees, gr_CX/gr_CY as a 0..1 fraction of the
 * filled object's bounding box.
 */
struct DR2DGradient
{
    LONG   gr_Angle;     /* 16.16 fixed degrees (GradAngle)                */
    LONG   gr_CX;        /* 16.16 fixed fraction 0..1 (GradX centre)       */
    LONG   gr_CY;        /* 16.16 fixed fraction 0..1 (GradY centre)       */
    UWORD  gr_RawType;   /* raw GRAD Type byte (G_CONIC/LINEAR/RADIAL/SHAPE) */
    UWORD  gr_Interp;    /* raw GRAD Interp byte (I_LINEAR .. I_ARCSINE)   */
    UWORD  gr_NumStops;  /* number of colour stops captured in gr_Stops    */
    UWORD  gr_Start;     /* CMAP index of first stop (2-colour fallback)   */
    UWORD  gr_End;       /* CMAP index of last stop (2-colour fallback)    */
    UWORD  gr_Valid;     /* TRUE once a GRAD chunk filled this slot        */
    ULONG *gr_Stops;     /* pool array of gr_NumStops resolved ARGB values *
                          * (NULL when only the fallback pair is known)    */
};

/*
 * The loader keeps a tiny stack of "current parent" Obj pointers so it
 * can push children into GRUPs / FILLs / XTRNs as it goes.  We do not
 * try to mirror the entire example obj2d.h here - the super class
 * already provides DrawObj.  Instead we keep an Obj2DState that points
 * at the drawing.datatype object currently receiving children.
 */
struct DR2DGroup
{
    UWORD              dg_Type;     /* DROT_GROUP / DROT_FILL / DROT_EXTERNAL */
    UWORD              dg_Pad;
    ULONG              dg_Expect;   /* number of children declared by DR2D    */
    ULONG              dg_Received; /* number actually pushed so far          */
    struct DrawObj    *dg_Owner;    /* the GROUP/FILL/EXTERNAL DrawObj        */
};


struct localData
{
    /* Object being built. */
    Object                *ld_Object;

    /* Convenience: shared pool from the root class.  Not owned by us.  */
    APTR                   ld_Pool;

    /* IFF state. */
    struct IFFHandle      *ld_IFF;        /* belongs to drawing.datatype       */
    BOOL                   ld_OwnsIFF;    /* TRUE = we should CloseIFF/FreeIFF */

    /* Group nest stack. */
    struct DR2DGroup       ld_Stack[DR2D_MAX_STACK];
    LONG                   ld_StackDepth;

    /* Most-recent ATTR / BBOX property snapshot - the IFF parser updates *
     * this every time it leaves a PROP chunk.                            */
    struct DrawAttr        ld_LastAttr;
    BOOL                   ld_HasAttr;

    /* Layer index lookup -- DR2D ids can be sparse, drawing.datatype     *
     * IDs are sequential.                                                */
    UWORD                  ld_NumLayersSeen;

    /* Local CMAP cache.  DR2D's ATTR chunk references colours by a       *
     * 16-bit palette index into the CMAP chunk; we cache the RGB triples *
     * here so absorbATTRProp() can resolve indexes to packed ARGB at     *
     * load time and store ARGB (not indexes) in DrawAttr.da_FillColor/   *
     * da_EdgeColor.  Without this cache the renderer would treat the     *
     * palette index as if it were a colour.                              */
    UBYTE                  ld_CMap[DR2D_MAX_COLORS][3];
    UWORD                  ld_NumColors;

    /* GRAD gradient table. Indexed directly by GradID; readATTRChunk     *
     * resolves an ATTR with FillType==FT_GRADIENT through this table.    */
    struct DR2DGradient    ld_Gradients[DR2D_MAX_GRADIENTS];

    /* DRHD canvas extents, captured during the parse loop while the top   *
     * FORM DR2D context is still current.  We must NOT rely on            *
     * FindProp(DRHD) after the parse completes: by then iffparse has      *
     * popped the FORM context and freed the stored property, so the query *
     * returns NULL.  ld_DRHD holds the raw IEEE-single words (UL.x, UL.y, *
     * LR.x, LR.y); ld_GotDRHD says whether they were seen.                */
    ULONG                  ld_DRHD[4];
    BOOL                   ld_GotDRHD;

    /* Pending BBOX, captured inline.  DR2D emits an optional BBOX chunk    *
     * immediately before the object it describes; we stash its raw IEEE    *
     * words here and apply them to the next object created.  Like ATTR and *
     * DRHD, BBOX is interleaved with object data and so cannot be tracked  *
     * with FindProp/PropChunks (IFF only collects properties that lead a   *
     * FORM); it must be read inline as a stop chunk.                       */
    ULONG                  ld_PendingBBox[4];
    BOOL                   ld_HasPendingBBox;
};


/*****************************************************************************/

#include "class_iprotos.h"

#endif /* CLASSBASE_H */
