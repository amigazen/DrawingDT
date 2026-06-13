/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* classbase.h -- internal header for drawing.datatype                       */
/*                                                                           */
/* Mirrors document/classbase.h and picture/classbase.h.  Pulls in every     */
/* OS header the implementation requires, defines the library base struct    */
/* and exposes the convenience macros that map base pointer dereferences     */
/* to the short names (SysBase, DOSBase, IFFParseBase) used throughout the   */
/* rest of the source.                                                       */
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
#include <graphics/gfxmacros.h>
#include <graphics/gfx.h>
#include <graphics/text.h>
#include <graphics/displayinfo.h>
#include <graphics/rastport.h>
#include <devices/printer.h>
#include <devices/prtbase.h>
#include <hardware/blit.h>
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

/* amiga.lib linkage (BOOPSI helpers, NewList et al.); link with LIB:amiga.lib. */
#include <proto/alib.h>

#include "drawingclass.h"
#include "vectorbridge.h"


/*****************************************************************************/
/* Library base for drawing.datatype.                                        */
/*****************************************************************************/

struct ClassBase
{
    struct Library         cb_Lib;
    UWORD                  cb_UsageCnt;
    BPTR                   cb_SegList;

    struct Library        *cb_SysBase;
    struct Library        *cb_DOSBase;
    struct Library        *cb_IntuitionBase;
    struct Library        *cb_GfxBase;
    struct Library        *cb_UtilityBase;
    struct Library        *cb_DataTypesBase;
    struct Library        *cb_IFFParseBase;
    struct Library        *cb_DiskFontBase;     /* optional                 */
    struct Library        *cb_VectorImageBase;

    struct SignalSemaphore cb_Lock;
    Class                 *cb_Class;
};


/*****************************************************************************/
/* SAS/C register-parameter macros and helpers                               */
/*****************************************************************************/

#define MEMORY_FOLLOWING(ptr)        ((void *)((ptr) + 1))
#define MEMORY_N_FOLLOWING(ptr, n)   ((void *)(((ULONG)(ptr)) + (n)))

#define ASM       __asm
#define REG(x)    register __ ## x

/*****************************************************************************/
/* Library-base aliases used by the rest of the C source.                    */
/*****************************************************************************/

#define SysBase             cb->cb_SysBase
#define DOSBase             cb->cb_DOSBase
#define UtilityBase         cb->cb_UtilityBase
#define IntuitionBase       cb->cb_IntuitionBase
#define GfxBase             cb->cb_GfxBase
#define DataTypesBase       cb->cb_DataTypesBase
#define IFFParseBase        cb->cb_IFFParseBase
#define DiskFontBase        cb->cb_DiskFontBase
#define VectorImageBase     cb->cb_VectorImageBase


void kprintf (void *, ...);


/*****************************************************************************/

#include "classdata.h"
#include "class_iprotos.h"

#endif /* CLASSBASE_H */
