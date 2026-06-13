/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* pool.c -- memory pool helpers for vector.image                            */
/*****************************************************************************/

#include "classbase.h"


APTR poolAlloc (struct ClassBase *cb, struct localData *lod, ULONG size)
{
    (void) cb;
    if (!lod || !lod->lvi_Pool) return NULL;
    return AllocPooled (lod->lvi_Pool, size);
}


APTR poolAllocClear (struct ClassBase *cb, struct localData *lod, ULONG size)
{
    APTR p;

    p = poolAlloc (cb, lod, size);
    if (p) memset (p, 0, size);
    return p;
}


STRPTR poolStrDup (struct ClassBase *cb, struct localData *lod,
                   CONST_STRPTR src)
{
    ULONG len;
    STRPTR dst;

    if (!src) return NULL;

    len = 0;
    while (src[len]) len++;

    dst = (STRPTR) poolAlloc (cb, lod, len + 1);
    if (!dst) return NULL;

    CopyMem ((APTR) src, (APTR) dst, len + 1);
    return dst;
}
