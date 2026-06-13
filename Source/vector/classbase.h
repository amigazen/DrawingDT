/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* classbase.h -- internal header for vector.image                           */
/*****************************************************************************/

#ifndef VECTOR_CLASSBASE_H
#define VECTOR_CLASSBASE_H

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
#include <intuition/imageclass.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <graphics/text.h>
#include <diskfont/diskfonttag.h>
#include <diskfont/glyph.h>
#include <diskfont/oterrors.h>
#include <utility/tagitem.h>
#include <string.h>
#include <stddef.h>

#include <clib/macros.h>
#include <clib/dos_protos.h>
#include <clib/exec_protos.h>
#include <clib/intuition_protos.h>
#include <clib/graphics_protos.h>
#include <clib/utility_protos.h>
#include <clib/diskfont_protos.h>
#include <clib/bullet_protos.h>

#include <pragmas/exec_pragmas.h>
#include <pragmas/dos_pragmas.h>
#include <pragmas/intuition_pragmas.h>
#include <pragmas/graphics_pragmas.h>
#include <pragmas/utility_pragmas.h>
#include <pragmas/diskfont_pragmas.h>
#include <pragmas/bullet_pragmas.h>

/* amiga.lib linkage (NewList et al.); link with LIB:amiga.lib. */
#include <proto/alib.h>

#include "vectorclass.h"


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
    struct Library        *cb_DiskfontBase;    /* optional                  */
    struct Library        *cb_BulletBase;      /* optional; rotated glyphs  */

    struct SignalSemaphore cb_Lock;
    Class                 *cb_Class;
};

/* Every library base is reached through the cb pointer, never through a
 * true global.  This keeps the class free of A4/small-data relocation: a
 * romtag library is entered with the caller's A4, so a real global read
 * (e.g. for the diskfont base) would fetch garbage and crash - notably in
 * LibExpunge during an "avail flush".  Routing the diskfont base through
 * cb (as for every other base) means no entry point needs __saveds. */
#define SysBase             cb->cb_SysBase
#define DOSBase             cb->cb_DOSBase
#define IntuitionBase       cb->cb_IntuitionBase
#define GfxBase             cb->cb_GfxBase
#define UtilityBase         cb->cb_UtilityBase
#define DiskfontBase        cb->cb_DiskfontBase
#define BulletBase          cb->cb_BulletBase

#define ASM       __asm
#define REG(x)    register __ ## x

ULONG __stdargs DoMethodA       (Object *obj, Msg message);
ULONG __stdargs DoMethod        (Object *obj, unsigned long MethodID, ...);
ULONG __stdargs DoSuperMethodA  (struct IClass *cl, Object *obj, Msg message);
ULONG __stdargs DoSuperMethod   (struct IClass *cl, Object *obj,
                                 unsigned long MethodID, ...);
ULONG __stdargs CoerceMethod    (struct IClass *cl, Object *obj,
                                 unsigned long MethodID, ...);

#include "classdata.h"
#include "vrport.h"
#include "class_iprotos.h"
#include "glyphengine.h"

#endif /* VECTOR_CLASSBASE_H */
