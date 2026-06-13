/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* classbase.c -- library lifecycle for dr2d.datatype                        */
/*                                                                           */
/* dr2d.datatype is a thin subclass of drawing.datatype.  Lifecycle is the   */
/* same pattern - LibInit / LibOpen / LibClose / LibExpunge - except that   */
/* on first open we also need to open drawing.datatype itself so it is      */
/* resident and registered as a public BOOPSI class; initClass() then names */
/* it as the super-class in MakeClass().                                    */
/*****************************************************************************/

#include "classbase.h"


/* Romtag library with no C startup (c.o) linked, yet it links LIB:sc.lib    *
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
            return (struct Library *) cb;
    }
    return NULL;
}


/*****************************************************************************/
/* LibOpen                                                                   */
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
                    if ((SuperClassBase =
                            OpenLibrary ("datatypes/drawing.datatype", 0)) != NULL)
                    {
                        cb->cb_Class = initClass (cb);
                        if (cb->cb_Class)
                            success = TRUE;
                    }
                }
            }

            if (!success)
            {
                if (SuperClassBase) CloseLibrary (SuperClassBase);
                if (IFFParseBase) CloseLibrary (IFFParseBase);
                if (DataTypesBase) CloseLibrary (DataTypesBase);
                SuperClassBase = NULL;
                IFFParseBase  = NULL;
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
        /* The class was made public with AddClass() in initClass(); it must   *
         * be RemoveClass()'d before FreeClass().  FreeClass() frees the Class *
         * node without unlinking it from intuition's public class list, so    *
         * freeing it while still linked leaves a dangling node that the next  *
         * class-list walk (NewObject / expunge / "avail flush") dereferences  *
         * -- the corrupt-memory-list panic.  Re-publish with AddClass() if    *
         * FreeClass() defers (live objects or subclasses remain).             */
        RemoveClass (cb->cb_Class);
        if (FreeClass (cb->cb_Class))
        {
            CloseLibrary (SuperClassBase);
            CloseLibrary (IFFParseBase);
            CloseLibrary (DataTypesBase);
            SuperClassBase = NULL;
            IFFParseBase  = NULL;
            DataTypesBase = NULL;
            cb->cb_Class      = NULL;
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
            CloseLibrary (SuperClassBase);
            CloseLibrary (IFFParseBase);
            CloseLibrary (DataTypesBase);
            SuperClassBase = NULL;
            IFFParseBase  = NULL;
            DataTypesBase = NULL;
            cb->cb_Class      = NULL;
        }
        else
        {
            AddClass (cb->cb_Class);
            cb->cb_Lib.lib_Flags |= LIBF_DELEXP;
            return NULL;
        }
    }

    Remove ((struct Node *) cb);

    CloseLibrary (UtilityBase);
    CloseLibrary (DOSBase);
    CloseLibrary (GfxBase);
    CloseLibrary (IntuitionBase);

    FreeMem ((APTR)((ULONG) cb - (ULONG) cb->cb_Lib.lib_NegSize),
             cb->cb_Lib.lib_NegSize + cb->cb_Lib.lib_PosSize);

    return (LONG) seg;
}
