/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* stream.c -- streaming I/O abstraction for drawing.datatype                */
/*                                                                           */
/* Where the implementation goes beyond a thin wrapper is in IFF support:    */
/* subclass loaders for DR2D / IFF FORMs use iffparse.library to walk        */
/* chunks.  We allocate the IFFHandle lazily (via getIFFHandle) the first    */
/* time a subclass asks, attach it to the file handle with                   */
/* InitIFFasDOS() and let the subclass call OpenIFF / ParseIFF on it.        */
/*                                                                           */
/* Sources we recognise:          */
/*   DST_FILE       -- DTA_SourceType == DTST_FILE, BPTR file handle         */
/*   DST_RAM        -- DTA_SourceType == DTST_RAM, BPTR or raw buffer        */
/*   DST_CLIPBOARD  -- DTA_SourceType == DTST_CLIPBOARD                      */
/*****************************************************************************/

#include "classbase.h"


#define DRW_DST_NONE         0
#define DRW_DST_FILE         1
#define DRW_DST_RAM          2
#define DRW_DST_CLIPBOARD    3


static void dbgPut (struct ClassBase *cb, STRPTR text)
{
    if (DOSBase && text)
    {
        PutStr ("[drawing.stream] ");
        PutStr (text);
        PutStr ("\n");
    }
}


static void dbgPutLong (struct ClassBase *cb, STRPTR label, LONG value)
{
    if (DOSBase && label)
    {
        Printf ("[drawing.stream] %s%ld\n", (LONG) label, value);
    }
}


/*****************************************************************************/
/* fileStreamRead                                                             */
/*****************************************************************************/

LONG fileStreamRead (struct DrawStream *self, APTR buffer, LONG nbytes)
{
    struct StreamPriv *sp;
    struct ClassBase  *cb;
    BPTR  fh;
    LONG  got;

    sp = (struct StreamPriv *) self->ds_Private;
    if (!sp) return -1;
    cb = sp->sp_CB;
    fh = sp->sp_FileHandle;

    if (!fh) return -1;

    got = Read (fh, buffer, nbytes);
    if (got > 0) self->ds_Position += got;
    return got;
}


/*****************************************************************************/
/* fileStreamSeek                                                             */
/*                                                                            */
/* dos.library/Seek returns OLD position; we recompute the new position    */
/* so the caller can rely on ds_Position.                                 */
/*****************************************************************************/

LONG fileStreamSeek (struct DrawStream *self, LONG offset, LONG mode)
{
    struct StreamPriv *sp;
    struct ClassBase  *cb;
    BPTR  fh;
    LONG  oldpos;
    LONG  newpos;

    sp = (struct StreamPriv *) self->ds_Private;
    if (!sp) return -1;
    cb = sp->sp_CB;
    fh = sp->sp_FileHandle;

    if (!fh) return -1;

    oldpos = Seek (fh, offset, mode);
    if (oldpos < 0) return -1;

    switch (mode)
    {
        case OFFSET_BEGINNING: newpos = offset; break;
        case OFFSET_CURRENT:   newpos = oldpos + offset; break;
        case OFFSET_END:
            if (self->ds_Length >= 0)
                newpos = self->ds_Length + offset;
            else
                newpos = Seek (fh, 0L, OFFSET_CURRENT);
            break;
        default: return -1;
    }

    if (newpos < 0) newpos = 0;
    self->ds_Position = newpos;
    return newpos;
}


/*****************************************************************************/
/* ramStreamRead / ramStreamSeek                                              */
/*****************************************************************************/

LONG ramStreamRead (struct DrawStream *self, APTR buffer, LONG nbytes)
{
    struct StreamPriv *sp;
    struct ClassBase  *cb;
    LONG  avail, got;

    sp = (struct StreamPriv *) self->ds_Private;
    if (!sp || !sp->sp_Buffer) return -1;
    cb = sp->sp_CB;

    if (self->ds_Position >= sp->sp_BufferSize) return 0;

    avail = sp->sp_BufferSize - self->ds_Position;
    got = (nbytes < avail) ? nbytes : avail;

    CopyMem ((BYTE *) sp->sp_Buffer + self->ds_Position, buffer, got);
    self->ds_Position += got;
    return got;
}


LONG ramStreamSeek (struct DrawStream *self, LONG offset, LONG mode)
{
    struct StreamPriv *sp;
    LONG newpos;

    sp = (struct StreamPriv *) self->ds_Private;
    if (!sp || !sp->sp_Buffer) return -1;

    switch (mode)
    {
        case OFFSET_BEGINNING: newpos = offset; break;
        case OFFSET_CURRENT:   newpos = self->ds_Position + offset; break;
        case OFFSET_END:       newpos = sp->sp_BufferSize + offset; break;
        default: return -1;
    }

    if (newpos < 0) newpos = 0;
    if (newpos > sp->sp_BufferSize) newpos = sp->sp_BufferSize;
    self->ds_Position = newpos;
    return newpos;
}


/*****************************************************************************/
/* openStream                                                                 */
/*                                                                            */
/* Inspects DTA_Handle / DTA_SourceType / DTA_Name in the caller's tag    */
/* list and wires up an appropriate DrawStream attached to lod->ldd_Stream. */
/* Returns TRUE on success.  Failure does not prevent OM_NEW from         */
/* succeeding - applications sometimes create empty drawing objects and  */
/* populate them via DDTM_ADDPOLY.                                       */
/*****************************************************************************/

BOOL openStream (struct ClassBase *cb, struct localData *lod,
                 struct TagItem *attrList)
{
    ULONG  sourceType;
    APTR   handle;
    STRPTR name;
    BPTR   fh;
    BPTR   lock;
    LONG   length;
    LONG   oldpos;
    struct FileInfoBlock *fib;
    UBYTE  lockName[256];

    lod->ldd_Stream.ds_Type     = DRW_DST_NONE;
    lod->ldd_Stream.ds_Length   = -1;
    lod->ldd_Stream.ds_Position = 0;
    lod->ldd_Stream.ds_Read     = NULL;
    lod->ldd_Stream.ds_Seek     = NULL;
    lod->ldd_Stream.ds_Private  = NULL;

    lod->ldd_StreamPriv.sp_CB         = cb;
    lod->ldd_StreamPriv.sp_FileHandle = 0;
    lod->ldd_StreamPriv.sp_Buffer     = NULL;
    lod->ldd_StreamPriv.sp_BufferSize = 0;

    sourceType = GetTagData (DTA_SourceType, DTST_FILE, attrList);
    handle     = (APTR)   GetTagData (DTA_Handle, 0,    attrList);
    name       = (STRPTR) GetTagData (DTA_Name,   0,    attrList);

    dbgPutLong (cb, "openStream sourceType=", (LONG) sourceType);
    dbgPutLong (cb, "openStream handle=", (LONG) handle);
    if (name)
        dbgPut (cb, "openStream has DTA_Name");
    else
        dbgPut (cb, "openStream name=NULL");

    lod->ldd_SourceType = sourceType;

    switch (sourceType)
    {
        case DTST_FILE:
        case DTST_HOTLINK:
        case DTST_CLIPBOARD:
            fh = 0;
            if (sourceType == DTST_CLIPBOARD && handle)
            {
                struct IFFHandle *iff = (struct IFFHandle *) handle;
                fh = (BPTR) iff->iff_Stream;
            }
            else
            {
                fh = 0;
                lock = (BPTR) handle;
                if (lock)
                {
                    dbgPut (cb, "NameFromLock(DTA_Handle) ...");
                    if (NameFromLock (lock, (STRPTR) lockName,
                                      (LONG) sizeof (lockName)))
                    {
                        dbgPut (cb, "NameFromLock returned a path");
                        lod->ldd_Filename =
                            poolStrDup (cb, lod, (STRPTR) lockName);
                        dbgPut (cb, "Open(NameFromLock path) ...");
                        fh = Open ((STRPTR) lockName, MODE_OLDFILE);
                        if (fh) lod->ldd_FileHandle = fh;
                        dbgPutLong (cb, "Open lock path returned fh=",
                                    (LONG) fh);
                    }
                    else
                    {
                        dbgPutLong (cb, "NameFromLock failed, IoErr=",
                                    IoErr ());
                    }
                }
            }

            if (!fh && name)
            {
                lod->ldd_Filename = poolStrDup (cb, lod, name);
                dbgPut (cb, "Open(name, MODE_OLDFILE) ...");
                fh = Open (name, MODE_OLDFILE);
                if (fh) lod->ldd_FileHandle = fh;
                dbgPutLong (cb, "Open returned fh=", (LONG) fh);
            }

            if (!fh)
            {
                dbgPutLong (cb, "openStream failed, IoErr=", IoErr ());
                return FALSE;
            }

            length = -1;
            dbgPut (cb, "AllocDosObject(DOS_FIB) ...");
            fib = AllocDosObject (DOS_FIB, NULL);
            if (fib)
            {
                dbgPut (cb, "ExamineFH ...");
                if (ExamineFH (fh, fib)) length = fib->fib_Size;
                FreeDosObject (DOS_FIB, fib);
                dbgPutLong (cb, "ExamineFH length=", length);
            }
            if (length < 0)
            {
                dbgPut (cb, "Seek EOF for length ...");
                oldpos = Seek (fh, 0L, OFFSET_END);
                if (oldpos >= 0)
                {
                    length = Seek (fh, 0L, OFFSET_CURRENT);
                    Seek (fh, oldpos, OFFSET_BEGINNING);
                }
                dbgPutLong (cb, "Seek length=", length);
            }
            dbgPut (cb, "Seek beginning ...");
            Seek (fh, 0L, OFFSET_BEGINNING);

            lod->ldd_StreamPriv.sp_FileHandle = fh;
            lod->ldd_Stream.ds_Type     = (sourceType == DTST_CLIPBOARD)
                                              ? DRW_DST_CLIPBOARD
                                              : DRW_DST_FILE;
            lod->ldd_Stream.ds_Length   = length;
            lod->ldd_Stream.ds_Position = 0;
            lod->ldd_Stream.ds_Read     = fileStreamRead;
            lod->ldd_Stream.ds_Seek     = fileStreamSeek;
            lod->ldd_Stream.ds_Private  = (APTR) &lod->ldd_StreamPriv;

            /* ldd_FileHandle records the handle WE opened and therefore own;  *
             * closeStream() unconditionally Close()s it.  Only the lock and   *
             * name branches above (which call Open()) set it.  A clipboard    *
             * source borrows the application's IFFHandle stream (fh =         *
             * iff->iff_Stream) which we must NOT close, so deliberately leave  *
             * ldd_FileHandle untouched here -- adopting the borrowed stream    *
             * made closeStream() Close() a handle owned by the caller, which  *
             * corrupts that handle and faults later (e.g. on "avail flush").  */
            dbgPutLong (cb, "openStream final length=", length);
            return TRUE;

        case DTST_RAM:
            if (lod->ldd_RamBuffer && lod->ldd_RamBufferSize > 0)
            {
                lod->ldd_StreamPriv.sp_Buffer     = lod->ldd_RamBuffer;
                lod->ldd_StreamPriv.sp_BufferSize = lod->ldd_RamBufferSize;

                lod->ldd_Stream.ds_Type     = DRW_DST_RAM;
                lod->ldd_Stream.ds_Length   = lod->ldd_RamBufferSize;
                lod->ldd_Stream.ds_Position = 0;
                lod->ldd_Stream.ds_Read     = ramStreamRead;
                lod->ldd_Stream.ds_Seek     = ramStreamSeek;
                lod->ldd_Stream.ds_Private  = (APTR) &lod->ldd_StreamPriv;
                return TRUE;
            }
            fh = (BPTR) handle;
            if (!fh) return FALSE;

            lod->ldd_StreamPriv.sp_FileHandle = fh;
            lod->ldd_Stream.ds_Type     = DRW_DST_RAM;
            lod->ldd_Stream.ds_Length   = -1;
            lod->ldd_Stream.ds_Position = 0;
            lod->ldd_Stream.ds_Read     = fileStreamRead;
            lod->ldd_Stream.ds_Seek     = fileStreamSeek;
            lod->ldd_Stream.ds_Private  = (APTR) &lod->ldd_StreamPriv;
            return TRUE;

        default:
            dbgPut (cb, "openStream unsupported source type");
            return FALSE;
    }
}


/*****************************************************************************/
/* closeStream                                                                */
/*****************************************************************************/

void closeStream (struct ClassBase *cb, struct localData *lod)
{
    if (lod->ldd_OwnsIFF && lod->ldd_IFFHandle)
    {
        CloseIFF (lod->ldd_IFFHandle);
        FreeIFF (lod->ldd_IFFHandle);
        lod->ldd_IFFHandle = NULL;
        lod->ldd_OwnsIFF = FALSE;
    }

    if (lod->ldd_FileHandle)
    {
        Close (lod->ldd_FileHandle);
    }
    lod->ldd_FileHandle = 0;
    lod->ldd_StreamPriv.sp_FileHandle = 0;

    if (lod->ldd_RamBuffer && lod->ldd_OwnsBuffer)
    {
        FreeMem (lod->ldd_RamBuffer, lod->ldd_RamBufferSize);
        lod->ldd_RamBuffer = NULL;
        lod->ldd_RamBufferSize = 0;
        lod->ldd_OwnsBuffer = FALSE;
    }

    lod->ldd_Stream.ds_Type     = DRW_DST_NONE;
    lod->ldd_Stream.ds_Length   = -1;
    lod->ldd_Stream.ds_Position = 0;
    lod->ldd_Stream.ds_Read     = NULL;
    lod->ldd_Stream.ds_Seek     = NULL;
    lod->ldd_Stream.ds_Private  = NULL;
}


/*****************************************************************************/
/* getIFFHandle                                                               */
/*                                                                            */
/* Subclasses that need to walk IFF chunks (DR2D, AGA-IFF FORMs, picture-  */
/* embedded FORM ANIMs) call this to obtain a shared IFFHandle attached  */
/* to the streaming source.  The root class allocates the handle lazily */
/* on first call and tears it down in closeStream().                    */
/*                                                                            */
/* The subclass calls AllocIFF / OpenIFF on the returned handle once and */
/* must call CloseIFF / FreeIFF only via closeStream (so this routine    */
/* is the SOLE owner of the handle).                                    */
/*****************************************************************************/

struct IFFHandle *getIFFHandle (struct ClassBase *cb, struct localData *lod)
{
    if (lod->ldd_IFFHandle)
    {
        dbgPut (cb, "getIFFHandle reused existing handle");
        return lod->ldd_IFFHandle;
    }

    if (lod->ldd_Stream.ds_Type != DRW_DST_FILE &&
        lod->ldd_Stream.ds_Type != DRW_DST_CLIPBOARD &&
        lod->ldd_Stream.ds_Type != DRW_DST_RAM)
    {
        dbgPutLong (cb, "getIFFHandle unsupported stream type=",
                    (LONG) lod->ldd_Stream.ds_Type);
        return NULL;
    }

    if (!lod->ldd_StreamPriv.sp_FileHandle)
    {
        dbgPut (cb, "getIFFHandle has no file handle");
        return NULL;
    }

    dbgPut (cb, "AllocIFF ...");
    lod->ldd_IFFHandle = AllocIFF ();
    if (!lod->ldd_IFFHandle)
    {
        dbgPut (cb, "AllocIFF failed");
        return NULL;
    }

    lod->ldd_IFFHandle->iff_Stream = (ULONG) lod->ldd_StreamPriv.sp_FileHandle;
    dbgPut (cb, "InitIFFasDOS ...");
    InitIFFasDOS (lod->ldd_IFFHandle);

    dbgPut (cb, "OpenIFF(IFFF_READ) ...");
    if (OpenIFF (lod->ldd_IFFHandle, IFFF_READ) != 0)
    {
        dbgPut (cb, "OpenIFF failed");
        FreeIFF (lod->ldd_IFFHandle);
        lod->ldd_IFFHandle = NULL;
        return NULL;
    }

    lod->ldd_OwnsIFF = TRUE;
    dbgPut (cb, "OpenIFF returned OK");
    return lod->ldd_IFFHandle;
}
