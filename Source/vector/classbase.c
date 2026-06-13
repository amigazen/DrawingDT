/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* classbase.c -- library lifecycle for vector.image                           */
/*****************************************************************************/

#include "classbase.h"


/* This BOOPSI class is a romtag library with no C startup (c.o) linked,  *
 * yet it links LIB:sc.lib for the compiler's helper routines.  sc.lib    *
 * references the C-startup hooks _XCEXIT (program exit) and __chkabort /  *
 * _CXBRK (Ctrl-C poll), which would otherwise be left undefined and make  *
 * slink prompt for a DEFINE.  The DEFINE=@__dummy redirect does not work  *
 * in an sc.lib/scm.lib-only link, so we provide empty stubs in source,    *
 * exactly as the sibling datatype classheaders do.  None are ever reached *
 * on any code path here.  (SAS/C maps the C identifier _XCEXIT to the     *
 * linker symbol __XCEXIT that slink asks for.)                            */
#ifdef __SASC
ASM int _XCEXIT (void)
{
    return 0;
}
void __regargs __chkabort (void)
{
}
void __regargs _CXBRK (void)
{
}
#endif


struct Library *ASM LibInit (REG (d0) struct ClassBase *cb,
                             REG (a0) BPTR seglist,
                             REG (a6) struct Library *sysbase)
{
    cb->cb_SegList = seglist;
    SysBase = sysbase;

    InitSemaphore (&(cb->cb_Lock));

    if (SysBase->lib_Version >= 39)
    {
        DOSBase       = OpenLibrary ("dos.library",       39);
        IntuitionBase = OpenLibrary ("intuition.library", 39);
        GfxBase       = OpenLibrary ("graphics.library",  39);
        UtilityBase   = OpenLibrary ("utility.library",   39);

        /* diskfont.library is OPTIONAL - it is only needed to open a       *
         * scalable font for STXT text.  A NULL base is not a failure; the  *
         * text renderer simply falls back to the rastport's default font.  */
        DiskfontBase  = OpenLibrary ("diskfont.library",  39);

        /* bullet.library is OPTIONAL too.  It drives the outline scaling   *
         * engine directly so text-on-path (TPTH) glyphs can be rotated to  *
         * follow the path tangent - something graphics.library Text()      *
         * cannot do.  When absent the path renderer falls back to upright  *
         * glyphs, so a NULL base is not a failure.                          */
        BulletBase    = OpenLibrary ("bullet.library",    39);

        if (DOSBase && IntuitionBase && GfxBase && UtilityBase)
            return (struct Library *) cb;
    }

    if (BulletBase)
    {
        CloseLibrary (BulletBase);
        BulletBase = NULL;
    }
    if (DiskfontBase)
    {
        CloseLibrary (DiskfontBase);
        DiskfontBase = NULL;
    }

    if (UtilityBase) CloseLibrary (UtilityBase);
    if (GfxBase) CloseLibrary (GfxBase);
    if (IntuitionBase) CloseLibrary (IntuitionBase);
    if (DOSBase) CloseLibrary (DOSBase);

    return NULL;
}


LONG ASM LibOpen (REG (a6) struct ClassBase *cb)
{
    struct ExecBase *eb;
    LONG retval;
    BOOL success;
    BYTE nest;

    eb = (struct ExecBase *) cb->cb_SysBase;
    retval = (LONG) cb;
    success = TRUE;
    nest = 0;

    ObtainSemaphore (&(cb->cb_Lock));
    nest = eb->TDNestCnt;
    Permit ();

    cb->cb_UsageCnt++;
    cb->cb_Lib.lib_Flags &= ~LIBF_DELEXP;

    if (cb->cb_UsageCnt == 1 && cb->cb_Class == NULL)
    {
        if ((cb->cb_Class = initClass (cb)) == NULL)
            success = FALSE;
    }

    if (!success)
    {
        cb->cb_UsageCnt--;
        retval = NULL;
    }

    eb->TDNestCnt = nest;
    ReleaseSemaphore (&(cb->cb_Lock));

    return retval;
}


LONG ASM LibClose (REG (a6) struct ClassBase *cb)
{
    struct ExecBase *eb;
    LONG retval;
    BYTE nest;

    eb = (struct ExecBase *) cb->cb_SysBase;
    retval = NULL;
    nest = 0;

    ObtainSemaphore (&(cb->cb_Lock));
    nest = eb->TDNestCnt;
    Permit ();

    if (cb->cb_UsageCnt)
        cb->cb_UsageCnt--;

    if ((cb->cb_UsageCnt == 0) && cb->cb_Class)
    {
        /* RemoveClass() before FreeClass(): the class was published with     *
         * AddClass() in initClass().  FreeClass() frees the Class node but    *
         * leaves it linked in intuition's public class list, so freeing it    *
         * while still linked corrupts that list on the next NewObject() or    *
         * expunge ("avail flush") -- the corrupt-memory-list panic.  This     *
         * matters most here because vector.image is the leaf class every      *
         * drawing.datatype object instantiates, so it is touched on every     *
         * teardown.  Re-publish with AddClass() if FreeClass() defers.        */
        RemoveClass (cb->cb_Class);
        if (FreeClass (cb->cb_Class))
            cb->cb_Class = NULL;
        else
        {
            AddClass (cb->cb_Class);
            cb->cb_Lib.lib_Flags |= LIBF_DELEXP;
        }
    }

    /* Honour a pending delayed-expunge exactly like the sibling datatype     *
     * classes (drawing/dr2d/picture): the Close vector must return either    *
     * NULL or the seglist handed back by LibExpunge(), never the library     *
     * base.  Returning cb here told exec we had expunged when we had not,    *
     * leaving a live-but-"removed" library that crashes the next time exec   *
     * walks the library list (e.g. on "avail flush").                        */
    if (cb->cb_Lib.lib_Flags & LIBF_DELEXP)
        retval = LibExpunge (cb);

    eb->TDNestCnt = nest;
    ReleaseSemaphore (&(cb->cb_Lock));

    return retval;
}


LONG ASM LibExpunge (REG (a6) struct ClassBase *cb)
{
    BPTR seg;

    seg = cb->cb_SegList;

    if (cb->cb_UsageCnt)
    {
        cb->cb_Lib.lib_Flags |= LIBF_DELEXP;
        return NULL;
    }

    if (cb->cb_Class)
    {
        /* RemoveClass() before FreeClass() -- see LibClose() for rationale. */
        RemoveClass (cb->cb_Class);
        if (!FreeClass (cb->cb_Class))
        {
            AddClass (cb->cb_Class);
            cb->cb_Lib.lib_Flags |= LIBF_DELEXP;
            return NULL;
        }
        cb->cb_Class = NULL;
    }

    Remove ((struct Node *) cb);

    if (BulletBase)
    {
        CloseLibrary (BulletBase);
        BulletBase = NULL;
    }
    if (DiskfontBase)
    {
        CloseLibrary (DiskfontBase);
        DiskfontBase = NULL;
    }
    CloseLibrary (UtilityBase);
    CloseLibrary (GfxBase);
    CloseLibrary (IntuitionBase);
    CloseLibrary (DOSBase);

    FreeMem ((APTR) ((ULONG) cb - (ULONG) cb->cb_Lib.lib_NegSize),
             cb->cb_Lib.lib_NegSize + cb->cb_Lib.lib_PosSize);

    return (LONG) seg;
}
