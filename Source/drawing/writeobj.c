/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* writeobj.c -- DTM_SELECT / DTM_CLEARSELECTED / DTM_COPY / DTM_WRITE       */
/*                                                                           */
/* The class supports two output formats for DTM_WRITE:                      */
/*                                                                           */
/*   DTWM_IFF       -- write the cached rasterised view as an IFF ILBM       */
/*                     (delegated to picture.datatype via the cached         */
/*                     bitmap), so DTM_COPY-then-paste from MultiView        */
/*                     remains compatible with old workflows.                */
/*                                                                           */
/*   DTWM_RAW       -- write the source bytes verbatim by re-reading from    */
/*                     the streaming source.  This is the format the         */
/*                     application gets when it asks for a "Save As..."      */
/*                     in the drawing's native format.                       */
/*                                                                           */
/* DTM_COPY copies the rasterised view to the clipboard as ILBM.             */
/*****************************************************************************/

#include "classbase.h"


/*****************************************************************************/
/* writeRawSource                                                            */
/*                                                                           */
/* Re-read the streaming source from the start and write it byte-for-byte    */
/* into the destination IFF stream.  Returns the number of bytes written.    */
/*****************************************************************************/

static LONG writeRawSource (struct ClassBase *cb, struct localData *lod,
                            struct IFFHandle *iff)
{
    UBYTE  buf[2048];
    LONG   total = 0;
    LONG   got;

    if (!lod->ldd_Stream.ds_Read) return 0;

    if (lod->ldd_Stream.ds_Seek)
        lod->ldd_Stream.ds_Seek (&lod->ldd_Stream, 0L, OFFSET_BEGINNING);

    while ((got = lod->ldd_Stream.ds_Read (&lod->ldd_Stream,
                                           buf, (LONG) sizeof (buf))) > 0)
    {
        if (WriteChunkBytes (iff, buf, got) != got) return -1;
        total += got;
    }
    return total;
}


/*****************************************************************************/
/* DTM_SELECT                                                                */
/*****************************************************************************/

ULONG selectMethod (struct ClassBase *cb, Class *cl, Object *o,
                    struct dtSelect *msg)
{
    (void) cb;
    return DoSuperMethodA (cl, o, (Msg) msg);
}


/*****************************************************************************/
/* DTM_CLEARSELECTED                                                         */
/*****************************************************************************/

ULONG clearSelectedMethod (struct ClassBase *cb, Class *cl, Object *o,
                           struct dtGeneral *msg)
{
    (void) cb;
    return DoSuperMethodA (cl, o, (Msg) msg);
}


/*****************************************************************************/
/* DTM_COPY                                                                  */
/*                                                                           */
/* Lets the base class handle copy-to-clipboard via the cached rasterised    */
/* bitmap; that produces ILBM data which any picture-aware application       */
/* can paste.                                                                */
/*****************************************************************************/

ULONG copyMethod (struct ClassBase *cb, Class *cl, Object *o,
                  struct dtGeneral *msg)
{
    (void) cb;
    return DoSuperMethodA (cl, o, (Msg) msg);
}


/*****************************************************************************/
/* DTM_WRITE                                                                 */
/*                                                                           */
/* DTWM_IFF: defer to the base class (it copies the rasterised bitmap as     */
/*           an ILBM).                                                       */
/* DTWM_RAW: stream the source bytes through verbatim.                       */
/*****************************************************************************/

ULONG writeMethod (struct ClassBase *cb, Class *cl, Object *o,
                   struct dtWrite *msg)
{
    struct localData *lod = INST_DATA (cl, o);

    if (!msg) return 0;

    switch (msg->dtw_Mode)
    {
        case DTWM_RAW:
            if (lod->ldd_Stream.ds_Read && msg->dtw_FileHandle)
            {
                LONG written;
                written = writeRawSource (cb, lod,
                                          (struct IFFHandle *) msg->dtw_FileHandle);
                if (written < 0) return 0;
                return 1;
            }
            return 0;

        case DTWM_IFF:
        default:
            return DoSuperMethodA (cl, o, (Msg) msg);
    }
}
