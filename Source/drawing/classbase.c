/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* classbase.c -- library lifecycle for drawing.datatype                     */
/*                                                                            */
/* Mirrors document/classbase.c and picture/classbase.c.  exec calls four    */
/* entry points when the library is opened, closed, or expunged:           */
/*                                                                            */
/*    LibInit    - one-shot initialisation when the SegList is loaded       */
/*    LibOpen    - per-OpenLibrary().  First open also opens datatypes /    */
/*                 iffparse and creates the BOOPSI class via initClass().  */
/*    LibClose   - per-CloseLibrary(); last close optionally frees the      */
/*                 BOOPSI class                                              */
/*    LibExpunge - drop the library from memory when no users remain        */
/*                                                                            */
/* Required libraries:                                                       */
/*    intuition, graphics, dos, utility, datatypes, iffparse  (all V39+)    */
/*                                                                            */
/* Optional libraries (used if present, gracefully degraded if absent):     */
/*    diskfont.library  - opening installed fonts at render time            */
/*****************************************************************************/

#include "classbase.h"


/* Romtag library with no C startup (c.o) linked, yet it links LIB:sc.lib   *
 * for the compiler helper routines.  sc.lib references the C-startup hooks  *
 * _XCEXIT and __chkabort / _CXBRK; with no startup to define them slink     *
 * leaves them undefined and prompts for a DEFINE.  Provide empty stubs in   *
 * source (the sibling-datatype convention); none are ever reached here.     *
 * SAS/C maps the C identifier _XCEXIT to the linker symbol __XCEXIT.        */
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


/*****************************************************************************/
/* LibInit                                                                   */
/*                                                                           */
/* Called once when SegList is loaded.  We capture SysBase and the seglist,  */
/* initialise the access semaphore and open the V39 system libraries we      */
/* need at all times.  We require V39 (Workbench 3.0) for the BOOPSI         */
/* gadget/datatype features the class uses.                                  */
/*****************************************************************************/

struct Library *ASM LibInit (REG (d0) struct ClassBase *cb,
                             REG (a0) BPTR seglist,
                             REG (a6) struct Library *sysbase)
{
    cb->cb_SegList = seglist;
    SysBase = sysbase;

    InitSemaphore (&(cb->cb_Lock));

    if (SysBase->lib_Version >= 39)
    {
        IntuitionBase = OpenLibrary ("intuition.library", 39);
        GfxBase       = OpenLibrary ("graphics.library",  39);
        DOSBase       = OpenLibrary ("dos.library",       39);
        UtilityBase   = OpenLibrary ("utility.library",   39);

        if (IntuitionBase && GfxBase && DOSBase && UtilityBase)
            return (struct Library *)cb;
    }

    return NULL;
}


/*****************************************************************************/
/* LibOpen                                                                   */
/*                                                                           */
/* Standard Amiga library ramps.  On the first open we also open the         */
/* datatypes / iffparse libraries (both are required - we use iffparse       */
/* for IFF DR2D parsing and clipboard handling, and datatypes for our        */
/* super-class) and we build the BOOPSI class via initClass().               */
/*                                                                           */
/* Optional libraries (diskfont) are opened too, but the class still         */
/* initialises if they are absent - the subclass renderers must tolerate     */
/* their absence by falling back to topaz or skipping text objects.          */
/*****************************************************************************/

LONG ASM LibOpen (REG (a6) struct ClassBase *cb)
{
    struct ExecBase *eb = (struct ExecBase *) cb->cb_SysBase;
    LONG  retval = (LONG) cb;
    BOOL  success = TRUE;
    BYTE  nest;

    ObtainSemaphore (&(cb->cb_Lock));
    nest = eb->TDNestCnt;
    Permit ();

    cb->cb_UsageCnt++;
    cb->cb_Lib.lib_Flags &= ~LIBF_DELEXP;

    if (cb->cb_UsageCnt == 1)
    {
        if (cb->cb_Class == NULL)
        {
            success = FALSE;
            if ((DataTypesBase = OpenLibrary ("datatypes.library", 0)) != NULL)
            {
                if ((IFFParseBase = OpenLibrary ("iffparse.library", 0)) != NULL)
                {
                    DiskFontBase = OpenLibrary ("diskfont.library", 0);

                    /* vector.image is the rendering engine BOOPSI image class */
                    /* behind this implementation of drawing.datatype.         */
                    VectorImageBase = OpenLibrary ("images/vector.image", 0);

                    if (VectorImageBase && (cb->cb_Class = initClass (cb)) != NULL)
                        success = TRUE;
                }
            }

            if (!success)
            {
                if (DiskFontBase) CloseLibrary (DiskFontBase);
                if (VectorImageBase) CloseLibrary (VectorImageBase);
                if (IFFParseBase) CloseLibrary (IFFParseBase);
                if (DataTypesBase) CloseLibrary (DataTypesBase);
                DiskFontBase = NULL;
                VectorImageBase = NULL;
                IFFParseBase = NULL;
                DataTypesBase = NULL;
            }
        }
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


/*****************************************************************************/
/* LibClose                                                                  */
/*                                                                           */
/* Mirror of LibOpen.  When the use count drops to zero we attempt to        */
/* FreeClass(); if that fails (BOOPSI still has live objects of this class   */
/* outstanding), we leave the class in place and mark the library for        */
/* delayed expunge.                                                          */
/*****************************************************************************/

LONG ASM LibClose (REG (a6) struct ClassBase *cb)
{
    struct ExecBase *eb = (struct ExecBase *) cb->cb_SysBase;
    LONG retval = NULL;
    BYTE nest;

    ObtainSemaphore (&(cb->cb_Lock));
    nest = eb->TDNestCnt;
    Permit ();

    if (cb->cb_UsageCnt)
        cb->cb_UsageCnt--;

    if ((cb->cb_UsageCnt == 0) && cb->cb_Class)
    {
        /* The class was made public with AddClass() in initClass(); it must  *
         * be RemoveClass()'d before FreeClass().  FreeClass() frees the Class *
         * node but does NOT unlink it from intuition's public class list, so  *
         * freeing it while still linked leaves a dangling node that the next  *
         * class-list walk (NewObject / library expunge / "avail flush")       *
         * dereferences -- the classic corrupt-memory-list panic.  If          *
         * FreeClass() defers (objects or subclasses still live) we re-publish *
         * with AddClass() so the Add/Remove pairing stays balanced.           */
        RemoveClass (cb->cb_Class);
        if (FreeClass (cb->cb_Class))
        {
            if (DiskFontBase) CloseLibrary (DiskFontBase);
            if (VectorImageBase) CloseLibrary (VectorImageBase);
            CloseLibrary (IFFParseBase);
            CloseLibrary (DataTypesBase);
            DiskFontBase  = NULL;
            VectorImageBase = NULL;
            IFFParseBase  = NULL;
            DataTypesBase = NULL;
            cb->cb_Class  = NULL;
        }
        else
        {
            AddClass (cb->cb_Class);
            cb->cb_Lib.lib_Flags |= LIBF_DELEXP;
        }
    }

    if (cb->cb_Lib.lib_Flags & LIBF_DELEXP)
        retval = LibExpunge (cb);

    eb->TDNestCnt = nest;
    ReleaseSemaphore (&(cb->cb_Lock));

    return retval;
}


/*****************************************************************************/
/* LibExpunge                                                                */
/*                                                                           */
/* Final teardown.  exec calls this when the last user has gone and the      */
/* system wants to reclaim memory.  We refuse if we still have live users    */
/* (the LIBF_DELEXP path in LibClose will then retry).                       */
/*****************************************************************************/

LONG ASM LibExpunge (REG (a6) struct ClassBase *cb)
{
    BPTR seg = cb->cb_SegList;

    if (cb->cb_UsageCnt)
    {
        cb->cb_Lib.lib_Flags |= LIBF_DELEXP;
        return NULL;
    }

    if (cb->cb_Class)
    {
        /* RemoveClass() before FreeClass() -- see LibClose() for rationale. */
        RemoveClass (cb->cb_Class);
        if (FreeClass (cb->cb_Class))
        {
            if (DiskFontBase) CloseLibrary (DiskFontBase);
            if (VectorImageBase) CloseLibrary (VectorImageBase);
            CloseLibrary (IFFParseBase);
            CloseLibrary (DataTypesBase);
            DiskFontBase  = NULL;
            VectorImageBase = NULL;
            IFFParseBase  = NULL;
            DataTypesBase = NULL;
            cb->cb_Class  = NULL;
        }
        else
        {
            AddClass (cb->cb_Class);
            cb->cb_Lib.lib_Flags |= LIBF_DELEXP;
            return NULL;
        }
    }

    Remove ((struct Node *)cb);

    CloseLibrary (UtilityBase);
    CloseLibrary (DOSBase);
    CloseLibrary (GfxBase);
    CloseLibrary (IntuitionBase);

    FreeMem ((APTR)((ULONG)cb - (ULONG)cb->cb_Lib.lib_NegSize),
             cb->cb_Lib.lib_NegSize + cb->cb_Lib.lib_PosSize);

    return (LONG) seg;
}
