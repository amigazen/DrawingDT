/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* classbase.h -- internal header for ilus.datatype                          */
/*                                                                           */
/* The ilus.datatype subclass adds an IFF ILUS loader on top of              */
/* drawing.datatype.  All rendering, scaling, layout, hit-testing and        */
/* output are inherited unchanged from the super-class - this file only      */
/* needs to plumb in iffparse.library and the small per-instance state       */
/* the loader keeps (named-colour table, group-nest stack, canvas union).    */
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

#include "ilusclass.h"

/*****************************************************************************/
/* Debug output toggle.                                                      */
/*                                                                           */
/* The loader and dispatcher emit progress / diagnostic lines through the    */
/* dbgPut() / dbgPutLong() helpers.  This output is OFF by default: build    */
/* with -DILUS_DEBUG (smake / SCOPTIONS) to compile the bodies of those      */
/* helpers in; otherwise they expand to nothing and the compiler drops the   */
/* call sites entirely.  Leave it undefined for release builds.              */
/*****************************************************************************/

/* #define ILUS_DEBUG */

/* The super-class public header -- we need its registered class name      *
 * (DRAWINGDTCLASS) and the full DrawObj/DrawAttr/DrawPoint definitions.   */
struct DrawingClassBase;     /* forward, root class's "ClassBase" type     */


/*****************************************************************************/
/* Library base for ilus.datatype.                                           */
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
    Class                 *cb_Class;       /* ilus.datatype class           */
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
/* ilus.datatype per-instance data                                           */
/*****************************************************************************/

#define ILUS_MAX_STACK   64       /* maximum group nest depth              */
#define ILUS_MAX_COLORS  256      /* named CTAG colour entries cached      */
#define ILUS_NAME_LEN    32       /* longest colour tag name we store      */


/*
 * One cached CTAG colour: its tag name (as referenced from the AT
 * attribute-data STYLETAG_COLOR command) and the packed 0xAARRGGBB
 * value the COLOR_STYLE CMYK fields decode to.
 */
struct ILUSColor
{
    UBYTE   ic_Name[ILUS_NAME_LEN];
    ULONG   ic_ARGB;
};


/*
 * One entry of the group-nest stack.  The loader pushes a frame each
 * time it enters the FORM WRAP that carries a group's children, and
 * pops it on the matching end-of-context.  dg_Owner is the DROT_GROUP
 * DrawObj the children belong to.
 */
struct ILUSGroup
{
    UWORD              ig_Type;     /* DROT_GROUP                            */
    UWORD              ig_Pad;
    struct DrawObj    *ig_Owner;    /* the group DrawObj                     */
};


/*
 * IFF context-stack frame.  ilusParseContent walks the file in
 * IFFPARSE_STEP mode and pushes one of these for every FORM/LIST/CAT it
 * enters, popping on each end-of-context, so it always knows the type of
 * the container it is currently inside (in particular whether the just-
 * entered context is a WRAP that should open a group).
 */
struct ILUSCtx
{
    ULONG   ix_Type;        /* the FORM/LIST/CAT type, e.g. ILUS_ID_WRAP   */
    BOOL    ix_OpenedGroup; /* TRUE if we issued DDTM_BEGINGROUP on entry  */
};


struct localData
{
    /* Object being built. */
    Object                *ld_Object;

    /* Convenience: shared pool from the root class.  Not owned by us.  */
    APTR                   ld_Pool;

    /* IFF state. */
    struct IFFHandle      *ld_IFF;        /* belongs to drawing.datatype     */
    BOOL                   ld_OwnsIFF;    /* TRUE = we should CloseIFF/FreeIFF*/

    /* Group nest stack (display-list construction). */
    struct ILUSGroup       ld_Stack[ILUS_MAX_STACK];
    LONG                   ld_StackDepth;

    /* IFF container-context stack (parse bookkeeping). */
    struct ILUSCtx         ld_Ctx[ILUS_MAX_STACK];
    LONG                   ld_CtxDepth;

    /* Named colour table, built from the CTAG chunk before any object is
     * parsed.  AT attribute-data references colours by name through these.*/
    struct ILUSColor       ld_Colors[ILUS_MAX_COLORS];
    UWORD                  ld_NumColors;

    /* Canvas extents.  ILUS PGDM is frequently all-zero, so the loader
     * accumulates the union of every OB bounding box (in COORDs) and uses
     * that as the canvas when PGDM carries no usable extent. */
    LONG                   ld_BBoxX1, ld_BBoxY1;   /* COORD union of OB bbox */
    LONG                   ld_BBoxX2, ld_BBoxY2;
    BOOL                   ld_HaveBBox;
    LONG                   ld_PGDM[4];             /* COORD L,T,R,B          */
    BOOL                   ld_GotPGDM;

    /* The DROT_GROUP just created by an IOBJ chunk, waiting for its FORM
     * WRAP of children to open it.  Cleared once the WRAP is entered.   */
    struct DrawObj        *ld_GroupForWrap;

    /* The DROT_TEXT object just created by an IOBJ chunk, waiting for its
     * sibling CTXT chunk to supply the displayed string.  Cleared once
     * filled or when the next object is created.                        */
    struct DrawObj        *ld_PendingText;
};


/*****************************************************************************/

#include "class_iprotos.h"

#endif /* CLASSBASE_H */
