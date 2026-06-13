/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* load.c -- IFF DR2D parser for dr2d.datatype                               */
/*                                                                           */
/* drawing.datatype sends DDTM_LOADHEADERS during OM_NEW so the subclass     */
/* can populate the DrawingInfo (canvas bounds, layer table, colour table)   */
/* before scroller geometry is computed; it then sends DDTM_LOADCONTENT      */
/* either inline or from DTM_ASYNCLAYOUT to fetch the actual display list.   */
/*                                                                           */
/* We do both passes against a single IFFHandle obtained from the root       */
/* class via the DDTA_IFFHandle attribute.  The handle is opened in          */
/* IFFF_READ mode and attached to the streaming source by the root           */
/* class's getIFFHandle() helper, so we just need to seek back to 0 at       */
/* the start of each pass and StopChunk() on the chunks of interest.         */
/*                                                                           */
/* Per the IFF DR2D spec (Ross Cunniff, Stylus 1991), valid FORM DR2D        */
/* files contain:                                                            */
/*                                                                           */
/*   - DRHD  (PROP)   document header (drawing extents)                      */
/*   - PPRF  (PROP)   page preferences                                       */
/*   - DASH  (CHUNK)  dash-pattern table entry                               */
/*   - AROW  (CHUNK)  arrow-head table entry                                 */
/*   - FILL  (CHUNK)  fill definition table entry                            */
/*   - LAYR  (CHUNK)  layer table entry                                      */
/*   - ATTR  (PROP)   object attributes (shared by all subsequent objs)      */
/*   - BBOX  (PROP)   object bounding box                                    */
/*   - CPLY  (CHUNK)  closed polygon                                         */
/*   - OPLY  (CHUNK)  open polygon (polyline)                                */
/*   - GRUP  (FORM)   group of objects (nested FORM DR2D)                    */
/*   - XTRN  (FORM)   externally controlled object                           */
/*   - TPTH  (CHUNK)  text along a path                                      */
/*   - STXT  (CHUNK)  simple text string                                     */
/*   - VBM   (CHUNK)  virtually positioned bitmap                            */
/*   - CMAP  (CHUNK)  RGB colour map                                         */
/*   - CMYK  (CHUNK)  CMYK colour map                                        */
/*   - CNAM  (CHUNK)  colour names                                           */
/*   - FONS  (CHUNK)  font-table entry                                       */
/*****************************************************************************/

#include "classbase.h"


/* IEEE conversion shorthands.                                               *
 *   IEEE_TO_FIXED   - spatial inches -> 16.16 Fixed POINTS (dip), scaled 72 *
 *   IEEE_ANGLE_FIXED - raw IEEE -> 16.16 Fixed, no scaling (radians etc.)   */
#define IEEE_TO_FIXED(x)     dr2dCoordToFixed ((ULONG)(x))
#define IEEE_ANGLE_FIXED(x)  dr2dIEEEToFixed ((ULONG)(x))


static void dbgPut (struct ClassBase *cb, STRPTR text)
{
    if (DOSBase && text)
    {
        PutStr ("[dr2d] ");
        PutStr (text);
        PutStr ("\n");
    }
}


static void dbgPutLong (struct ClassBase *cb, STRPTR label, LONG value)
{
    if (DOSBase && label)
    {
        Printf ("[dr2d] %s%ld\n", (LONG) label, value);
    }
}


static void dbgPutID (struct ClassBase *cb, STRPTR label, LONG id)
{
    UBYTE buf[5];

    if (DOSBase && label)
    {
        buf[0] = (UBYTE) ((id >> 24) & 0xff);
        buf[1] = (UBYTE) ((id >> 16) & 0xff);
        buf[2] = (UBYTE) ((id >>  8) & 0xff);
        buf[3] = (UBYTE) ( id        & 0xff);
        buf[4] = '\0';
        Printf ("[dr2d] %s%s\n", (LONG) label, (LONG) buf);
    }
}


static LONG declareStopChunk (struct ClassBase *cb, struct IFFHandle *iff,
                              LONG id)
{
    LONG err;

    dbgPutID (cb, "StopChunk id=", id);
    err = StopChunk (iff, DR2D_ID_DR2D, id);
    dbgPutLong (cb, "StopChunk rc=", err);
    return err;
}


/* Local working union for ReadChunkRecords.  All members fit inside a
 * single 256-byte scratch buffer; the union is allocated on the C
 * stack inside each parsing helper.                                     */
typedef union
{
    UBYTE  bytes[256];

    /* DRHD - drawing header */
    struct {
        ULONG ul_x, ul_y;
        ULONG lr_x, lr_y;
    } drhd;

    /* LAYR */
    struct {
        UWORD LayerID;
        UBYTE LayerName[16];
        UBYTE Flags;
        UBYTE Pad0;
    } layr;

    /* ATTR */
    struct {
        UBYTE FillType;
        UBYTE JoinType;
        UBYTE EdgePattern;
        UBYTE ArrowHeads;
        UWORD FillValue;
        UWORD EdgeValue;
        UWORD WhichLayer;
        ULONG EdgeThick;   /* IEEE */
    } attr;

    /* BBOX */
    struct {
        ULONG XMin, YMin;
        ULONG XMax, YMax;
    } bbox;

    /* POLY header */
    struct {
        UWORD NumPoints;
    } poly;

    /* GRUP */
    struct {
        UWORD NumObjs;
    } grup;

    /* XTRN */
    struct {
        UWORD ApplCallBacks;
        UWORD ApplNameLength;
    } xtrn;

    /* STXT */
    struct {
        WORD  WhichFont;
        ULONG CharW, CharH;
        ULONG BaseX, BaseY;
        ULONG Rotation;
        UWORD NumChars;
    } stxt;

    /* TPTH */
    struct {
        UWORD WhichFont;
        ULONG CharW, CharH;
        UWORD NumChars;
        UWORD NumPoints;
    } tpth;

    /* VBM */
    struct {
        ULONG XPos, YPos;
        ULONG XSize, YSize;
        ULONG Rotation;
        UWORD PathLen;
    } vbm;

    /* FONS */
    struct {
        UBYTE FontID;
        UBYTE Pad1;
        UBYTE Proportional;
        UBYTE Serif;
    } fons;
} ChunkScratch;


/*****************************************************************************/
/* Forward declarations                                                       */
/*****************************************************************************/

static LONG readPolyChunk    (struct ClassBase *cb, struct localData *ld,
                              struct IFFHandle *iff,
                              struct ContextNode *cn,
                              struct DrawObj *obj, BOOL closed);
static LONG readSTXTChunk    (struct ClassBase *cb, struct localData *ld,
                              struct IFFHandle *iff,
                              struct ContextNode *cn,
                              struct DrawObj *obj);
static LONG readTPTHChunk    (struct ClassBase *cb, struct localData *ld,
                              struct IFFHandle *iff,
                              struct ContextNode *cn,
                              struct DrawObj *obj);
static LONG readVBMChunk     (struct ClassBase *cb, struct localData *ld,
                              struct IFFHandle *iff,
                              struct ContextNode *cn,
                              struct DrawObj *obj);
static LONG readFONSChunk    (struct ClassBase *cb, Object *o,
                              struct localData *ld,
                              struct IFFHandle *iff,
                              struct ContextNode *cn);
static LONG readLAYRChunk    (struct ClassBase *cb, Object *o,
                              struct localData *ld,
                              struct IFFHandle *iff);
static LONG readCMAPChunk    (struct ClassBase *cb, Object *o,
                              struct localData *ld,
                              struct IFFHandle *iff,
                              struct ContextNode *cn);
static LONG readATTRChunk    (struct ClassBase *cb, struct localData *ld,
                              struct IFFHandle *iff,
                              struct ContextNode *cn);
static LONG readGRADChunk    (struct ClassBase *cb, struct localData *ld,
                              struct IFFHandle *iff,
                              struct ContextNode *cn);
static void absorbATTRProp   (struct ClassBase *cb, struct localData *ld,
                              struct IFFHandle *iff);
static void absorbBBOXProp   (struct ClassBase *cb, struct localData *ld,
                              struct DrawObj *obj);


/*****************************************************************************/
/* declareInterestingChunks                                                    */
/*                                                                            */
/* Tell iffparse.library which chunks we want to stop at and which to       */
/* track as PROPs.                                                          */
/*****************************************************************************/

static LONG declareInterestingChunks (struct ClassBase *cb,
                                      struct IFFHandle *iff)
{
    /* Stop-chunk registration. Register the entries so no compiler/preprocessor 
     * quirk can terminate the list early. ID_FORM is first so SCAN stops when it
     * enters the top-level FORM DR2D. */
    /* DRHD, ATTR and BBOX are NOT registered as PROP chunks.  Although     *
     * they behave like "current state" properties, DR2D interleaves them   *
     * with object chunks, whereas IFF only collects a property when it     *
     * leads a FORM (before the first data chunk) or sits inside a PROP.    *
     * PropChunks()/FindProp() therefore return NULL for them mid-stream.   *
     * We read them inline as stop chunks, maintaining the running ATTR     *
     * state and the pending BBOX/DRHD in localData.                        */
    static LONG propChunks[] = {
        DR2D_ID_DR2D, DR2D_ID_PPRF
    };
    LONG err;

    dbgPutLong (cb, "declare: iff ptr=", (LONG) iff);

    err = declareStopChunk (cb, iff, ID_FORM);       if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_DRHD);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_ATTR);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_BBOX);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_DASH);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_GRAD);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_FILL);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_LAYR);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_CMAP);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_CMYK);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_CNAM);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_FONS);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_CPLY);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_OPLY);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_GRUP);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_XTRN);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_TPTH);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_STXT);  if (err) return err;
    err = declareStopChunk (cb, iff, DR2D_ID_VBM);   if (err) return err;

    err = PropChunks (iff, propChunks,
                      (LONG) (sizeof (propChunks) / sizeof (propChunks[0]) / 2));
    dbgPutLong (cb, "PropChunks rc=", err);
    if (err) return err;

    err = StopOnExit (iff, DR2D_ID_DR2D, ID_FORM);
    dbgPutLong (cb, "StopOnExit rc=", err);
    if (err) return err;

    return 0;
}


/*****************************************************************************/
/* DDTM_LOADHEADERS                                                          */
/*                                                                           */
/* First pass: parse enough of the IFF stream to learn the drawing's         */
/* canvas extents, layer count and colour map.  We do not build the          */
/* display list here - that comes in DDTM_LOADCONTENT.                       */
/*****************************************************************************/

ULONG loadHeadersMethod (struct ClassBase *cb, Class *cl, Object *o,
                         struct drwLoadHeaders *msg)
{
    struct localData      *ld;
    struct IFFHandle      *iff;
    LONG  err;

    dbgPut (cb, "DDTM_LOADHEADERS entered");
    if (!msg) return 0;
    ld = (struct localData *) INST_DATA (cl, o);

    iff = NULL;
    dbgPut (cb, "GetAttr(DDTA_IFFHandle) ...");
    GetAttr (DDTA_IFFHandle, o, (ULONG *) &iff);
    if (!iff)
    {
        dbgPut (cb, "no IFF handle from drawing.datatype");
        return 0;
    }
    dbgPut (cb, "got IFF handle");
    ld->ld_IFF = iff;
    ld->ld_Pool = msg->drwlh_Pool;

    /* The root class opens the IFFHandle with IFFF_READ before sending  *
     * us the message.  We just have to declare what we want.            */
    dbgPut (cb, "declareInterestingChunks ...");
    err = declareInterestingChunks (cb, iff);
    if (err)
    {
        dbgPutLong (cb, "declareInterestingChunks err=", err);
        return 0;
    }
    dbgPut (cb, "declareInterestingChunks returned");

    /* Single-pass parse: DR2D files are typically a few KB to a few    *
     * hundred KB.  We walk the file once, building the entire display  *
     * list as we go.  DDTM_LOADCONTENT then becomes a no-op.           */
    dbgPut (cb, "dr2dParseContent ...");
    err = dr2dParseContent (cb, o, ld, iff,
                            NULL,           /* root owns the list      */
                            NULL, NULL);    /* sync, no progress       */
    dbgPutLong (cb, "dr2dParseContent err=", err);
    if (err != 0 && err != IFFERR_EOF) return 0;

    /* Populate the caller's DrawingInfo from the DRHD extents captured     *
     * during the parse (see ld_DRHD).  We must use the captured copy and   *
     * NOT FindProp here: by this point the parse has run to EOF/EOC and    *
     * iffparse has popped the top FORM context, freeing the stored DRHD    *
     * property, so FindProp would return NULL.                             *
     *                                                                      *
     * Coordinates are kept in the file's native units (no inches->points   *
     * conversion).  DR2D files do not consistently use inches: tiger.dr2d  *
     * for example defines a 1000x800 user-unit canvas.  Keeping native     *
     * units and letting DDTM_FITTOBOX scale the DRHD canvas to the output  *
     * box is unit-agnostic and correct for both inch- and user-unit files. */
    if (ld->ld_GotDRHD && msg->drwlh_Info)
    {
        msg->drwlh_Info->di_Left   = dr2dIEEEToFixed (ld->ld_DRHD[0]);
        msg->drwlh_Info->di_Top    = dr2dIEEEToFixed (ld->ld_DRHD[1]);
        msg->drwlh_Info->di_Right  = dr2dIEEEToFixed (ld->ld_DRHD[2]);
        msg->drwlh_Info->di_Bottom = dr2dIEEEToFixed (ld->ld_DRHD[3]);
        msg->drwlh_Info->di_Units  = DRWU_INCHES;
        dbgPutLong (cb, "DRHD raw ulx=", (LONG) ld->ld_DRHD[0]);
        dbgPutLong (cb, "DRHD raw lrx=", (LONG) ld->ld_DRHD[2]);
        dbgPutLong (cb, "DRHD di_Right fixed=", msg->drwlh_Info->di_Right);
        dbgPutLong (cb, "DRHD di_Bottom fixed=", msg->drwlh_Info->di_Bottom);
    }
    else
    {
        dbgPut (cb, "no DRHD captured during parse");
    }

    dbgPut (cb, "DDTM_LOADHEADERS leaving OK");
    return 1;
}


/*****************************************************************************/
/* DDTM_LOADCONTENT                                                          */
/*                                                                           */
/* No-op: everything was parsed during DDTM_LOADHEADERS.                     */
/*****************************************************************************/

ULONG loadContentMethod (struct ClassBase *cb, Class *cl, Object *o,
                         struct drwLoadContent *msg)
{
    (void) cb;
    (void) cl;
    (void) o;
    (void) msg;
    return 1;
}


/*****************************************************************************/
/* dr2dParseContent                                                          */
/*                                                                           */
/* Walk the IFF stream the second time, building DrawObj nodes and pushing   */
/* them onto the display list owned by the root class.                       */
/*****************************************************************************/

LONG dr2dParseContent (struct ClassBase *cb, Object *o,
                       struct localData *ld,
                       struct IFFHandle *iff,
                       struct List *displayList,
                       ULONG *abortPtr, ULONG *progressPtr)
{
    struct ContextNode    *cn;
    struct DrawObj        *obj;
    ChunkScratch       data;
    UWORD              fillID;
    UBYTE             *nameBuf;
    LONG               namelen;
    BOOL               closed;
    LONG  err;
    LONG  done = 0;
    LONG  newNest = 0;
    LONG  nestLevel = 0;
    LONG  iterations = 0;

    (void) displayList;     /* root class owns the list                  */
    (void) progressPtr;
    obj = NULL;

    /* Because ID_FORM is a registered stop chunk, the very first SCAN      *
     * stops upon ENTERING the top-level FORM DR2D (CurrentChunk == FORM),  *
     * which bumps nestLevel from 0 to 1; the loop then keeps scanning the  *
     * children until the matching IFFERR_EOC drops nestLevel back to 0.    */
    dbgPut (cb, "parse loop entered");

    while (!done)
    {
        if (abortPtr && *abortPtr) return -1;

        iterations++;
        if (iterations > 100000L)
        {
            dbgPut (cb, "parse iteration guard tripped");
            return IFFERR_MANGLED;
        }

        if ((iterations < 40L) || ((iterations % 100L) == 0L))
            dbgPutLong (cb, "ParseIFF iteration ", iterations);

        err = ParseIFF (iff, IFFPARSE_SCAN);
        if ((iterations < 40L) || ((iterations % 100L) == 0L))
            dbgPutLong (cb, "ParseIFF err=", err);

        if (err == IFFERR_EOF)
        {
            dbgPut (cb, "ParseIFF EOF");
            done = 1;
            break;
        }
        if (err == IFFERR_EOC)
        {
            dbgPut (cb, "ParseIFF EOC");
            /* Leaving a FORM.  If we were inside a GRUP/FILL/XTRN, pop. */
            if (ld->ld_StackDepth > 0)
                dr2dEndGroup (cb, o, ld);
            nestLevel--;
            dbgPutLong (cb, "nestLevel after EOC=", nestLevel);
            if (nestLevel <= 0) done = 1;
            continue;
        }
        if (err != 0)
        {
            dbgPutLong (cb, "ParseIFF returned fatal err=", err);
            return err;
        }

        cn = CurrentChunk (iff);
        if (!cn)
        {
            dbgPut (cb, "CurrentChunk returned NULL");
            continue;
        }

        if ((iterations < 40L) || ((iterations % 100L) == 0L))
        {
            dbgPutID (cb, "chunk id=", cn->cn_ID);
            dbgPutID (cb, "chunk type=", cn->cn_Type);
            dbgPutLong (cb, "chunk size=", (LONG) cn->cn_Size);
            dbgPutLong (cb, "chunk scan=", (LONG) cn->cn_Scan);
        }

        switch (cn->cn_ID)
        {
            case ID_FORM:
                /* The top-level FORM DR2D (nestLevel == 0) just opens    *
                 * the document.  A FORM seen while already nested is a   *
                 * GRUP / FILL / XTRN whose inner chunk follows on the    *
                 * next ParseIFF call and creates the matching DrawObj.   *
                 * Match the reference: only flag newNest when already    *
                 * inside the document.                                   */
                if (nestLevel > 0)
                    newNest = 1;
                nestLevel++;
                dbgPutLong (cb, "entered FORM, nestLevel=", nestLevel);
                break;

            case DR2D_ID_DRHD:
                /* Page extents.  Read inline (see declareInterestingChunks *
                 * for why DRHD cannot be a PROP) and stash the raw IEEE    *
                 * words; loadHeadersMethod converts them to the canvas.    */
                dbgPut (cb, "read DRHD");
                err = ReadChunkRecords (iff, (APTR) ld->ld_DRHD,
                                        (LONG) sizeof (ld->ld_DRHD), 1);
                if (err != 1) return IFFERR_MANGLED;
                ld->ld_GotDRHD = TRUE;
                dbgPutLong (cb, "DRHD ulx raw=", (LONG) ld->ld_DRHD[0]);
                dbgPutLong (cb, "DRHD lrx raw=", (LONG) ld->ld_DRHD[2]);
                break;

            case DR2D_ID_ATTR:
                /* Running attribute state inherited by every following     *
                 * object until the next ATTR.  Read inline and resolve     *
                 * CMAP indices to ARGB now (CMAP precedes the objects).    */
                dbgPut (cb, "read ATTR");
                err = readATTRChunk (cb, ld, iff, cn);
                if (err) return err;
                break;

            case DR2D_ID_BBOX:
                /* Bounding box for the next object.  Stash raw IEEE words; *
                 * absorbBBOXProp applies them when the object is built.    */
                dbgPut (cb, "read BBOX");
                err = ReadChunkRecords (iff, (APTR) ld->ld_PendingBBox,
                                        (LONG) sizeof (ld->ld_PendingBBox), 1);
                if (err != 1) return IFFERR_MANGLED;
                ld->ld_HasPendingBBox = TRUE;
                break;

            case DR2D_ID_GRUP:
                dbgPut (cb, "read GRUP");
                err = ReadChunkRecords (iff, (APTR) &data.grup,
                                        (LONG) sizeof (data.grup), 1);
                if (err != 1) return IFFERR_MANGLED;

                obj = dr2dMakeDrawObj (cb, o, ld, DROT_GROUP);
                if (!obj) return IFFERR_NOMEM;
                obj->do_Data.Group.do_NumChildren = 0;
                NewList (&obj->do_Data.Group.do_Children);

                absorbBBOXProp (cb, ld, obj);
                absorbATTRProp (cb, ld, iff);
                if (ld->ld_HasAttr)
                    obj->do_Attrs = ld->ld_LastAttr;

                err = dr2dPushChild (cb, o, ld, obj);
                if (err) return err;
                err = dr2dBeginGroup (cb, o, ld, DROT_GROUP,
                                       data.grup.NumObjs, obj);
                if (err) return err;
                newNest = 0;
                break;

            case DR2D_ID_GRAD:
                dbgPut (cb, "read GRAD");
                err = readGRADChunk (cb, ld, iff, cn);
                if (err) return err;
                break;

            case DR2D_ID_FILL:
                dbgPut (cb, "read FILL");
                fillID = 0;
                err = ReadChunkRecords (iff, (APTR) &fillID,
                                        (LONG) sizeof (fillID), 1);
                if (err != 1) return IFFERR_MANGLED;

                obj = dr2dMakeDrawObj (cb, o, ld, DROT_FILL);
                if (!obj) return IFFERR_NOMEM;
                obj->do_Data.Fill.do_FillID = fillID;
                NewList (&obj->do_Data.Fill.do_Children);

                err = dr2dBeginGroup (cb, o, ld, DROT_FILL, 1, obj);
                if (err) return err;
                newNest = 0;
                break;

            case DR2D_ID_XTRN:
                dbgPut (cb, "read XTRN");
                err = ReadChunkRecords (iff, (APTR) &data.xtrn,
                                        (LONG) sizeof (data.xtrn), 1);
                if (err != 1) return IFFERR_MANGLED;

                obj = dr2dMakeDrawObj (cb, o, ld, DROT_EXTERNAL);
                if (!obj) return IFFERR_NOMEM;
                obj->do_Data.External.do_CallBacks = data.xtrn.ApplCallBacks;
                obj->do_Data.External.do_NameLen   = data.xtrn.ApplNameLength;

                namelen = (LONG) data.xtrn.ApplNameLength;
                if (namelen > 0 && namelen < 1024)
                {
                    nameBuf = (UBYTE *) AllocPooled (ld->ld_Pool,
                                                     (ULONG) (namelen + 1));
                    if (!nameBuf) return IFFERR_NOMEM;
                    err = ReadChunkRecords (iff, nameBuf, namelen, 1);
                    if (err != 1) return IFFERR_MANGLED;
                    nameBuf[namelen] = '\0';
                    obj->do_Data.External.do_Name = (STRPTR) nameBuf;
                }

                err = dr2dBeginGroup (cb, o, ld, DROT_EXTERNAL, 1, obj);
                if (err) return err;
                newNest = 0;
                break;

            case DR2D_ID_CPLY:
            case DR2D_ID_OPLY:
                dbgPutID (cb, "read polygon chunk ", cn->cn_ID);
                closed = (cn->cn_ID == DR2D_ID_CPLY) ? TRUE : FALSE;
                obj = dr2dMakeDrawObj (cb, o, ld,
                                       closed ? DROT_POLYGON : DROT_POLYLINE);
                if (!obj) return IFFERR_NOMEM;
                absorbBBOXProp (cb, ld, obj);
                absorbATTRProp (cb, ld, iff);
                if (ld->ld_HasAttr)
                    obj->do_Attrs = ld->ld_LastAttr;

                err = readPolyChunk (cb, ld, iff, cn, obj, closed);
                if (err) return err;
                err = dr2dPushChild (cb, o, ld, obj);
                if (err) return err;
                break;

            case DR2D_ID_STXT:
                dbgPut (cb, "read STXT");
                obj = dr2dMakeDrawObj (cb, o, ld, DROT_TEXT);
                if (!obj) return IFFERR_NOMEM;
                absorbBBOXProp (cb, ld, obj);
                absorbATTRProp (cb, ld, iff);
                if (ld->ld_HasAttr)
                    obj->do_Attrs = ld->ld_LastAttr;

                err = readSTXTChunk (cb, ld, iff, cn, obj);
                if (err) return err;
                err = dr2dPushChild (cb, o, ld, obj);
                if (err) return err;
                break;

            case DR2D_ID_TPTH:
                dbgPut (cb, "read TPTH");
                obj = dr2dMakeDrawObj (cb, o, ld, DROT_TEXTPATH);
                if (!obj) return IFFERR_NOMEM;
                absorbBBOXProp (cb, ld, obj);
                absorbATTRProp (cb, ld, iff);
                if (ld->ld_HasAttr)
                    obj->do_Attrs = ld->ld_LastAttr;

                err = readTPTHChunk (cb, ld, iff, cn, obj);
                if (err) return err;
                err = dr2dPushChild (cb, o, ld, obj);
                if (err) return err;
                break;

            case DR2D_ID_VBM:
                dbgPut (cb, "read VBM");
                obj = dr2dMakeDrawObj (cb, o, ld, DROT_BITMAP);
                if (!obj) return IFFERR_NOMEM;
                absorbBBOXProp (cb, ld, obj);
                absorbATTRProp (cb, ld, iff);
                if (ld->ld_HasAttr)
                    obj->do_Attrs = ld->ld_LastAttr;

                err = readVBMChunk (cb, ld, iff, cn, obj);
                if (err) return err;
                err = dr2dPushChild (cb, o, ld, obj);
                if (err) return err;
                break;

            case DR2D_ID_LAYR:
                dbgPut (cb, "read LAYR");
                err = readLAYRChunk (cb, o, ld, iff);
                if (err) return err;
                break;

            case DR2D_ID_CMAP:
                dbgPut (cb, "read CMAP");
                err = readCMAPChunk (cb, o, ld, iff, cn);
                if (err) return err;
                break;

            case DR2D_ID_FONS:
                dbgPut (cb, "read FONS");
                err = readFONSChunk (cb, o, ld, iff, cn);
                if (err) return err;
                break;

            default:
                /* Ignore.  iffparse will skip the chunk for us. */
                dbgPutID (cb, "ignore chunk ", cn->cn_ID);
                break;
        }
    }
    dbgPut (cb, "parse loop leaving OK");
    return 0;
}


/*****************************************************************************/
/* readPolyChunk                                                             */
/*                                                                           */
/* Parse a CPLY/OPLY chunk into an array of DrawPoint nodes.  The DR2D       */
/* on-disk format is:                                                        */
/*                                                                           */
/*    UWORD NumPoints                                                        */
/*    IEEE  Points[NumPoints * 2]    (xpair followed by ypair)               */
/*                                                                           */
/* The DR2D_IND magic value (0xFFFFFFFF) in the X slot signals that the      */
/* matching Y slot is a flag (DR2D_SPLINE = bezier control, DR2D_MOVETO =    */
/* start of a new sub-path), not a coordinate.                               */
/*****************************************************************************/

static LONG readPolyChunk (struct ClassBase *cb, struct localData *ld,
                           struct IFFHandle *iff,
                           struct ContextNode *cn,
                           struct DrawObj *obj, BOOL closed)
{
    ChunkScratch  hdr;
    ULONG  *raw;
    struct DrawPoint *dp;
    LONG   err;
    ULONG  rawBytes;
    ULONG  xRaw;
    ULONG  yRaw;
    UWORD  n;
    UWORD  i;
    UWORD  outCount = 0;
    UWORD  bezPos = 0;        /* 1..4 = Nth real point after an IND_SPLINE   */
    BOOL   pendingMove = FALSE;

    (void) cb;
    (void) cn;

    err = ReadChunkRecords (iff, (APTR) &hdr.poly,
                            (LONG) sizeof (hdr.poly), 1);
    if (err != 1) return IFFERR_MANGLED;
    n = hdr.poly.NumPoints;
    if (n == 0)
    {
        obj->do_Data.Poly.do_Points = NULL;
        obj->do_Data.Poly.do_NumPoints = 0;
        return 0;
    }

    rawBytes = ((ULONG) n) * 2 * sizeof (ULONG);
    raw = (ULONG *) AllocPooled (ld->ld_Pool, rawBytes);
    if (!raw) return IFFERR_NOMEM;

    err = ReadChunkRecords (iff, (APTR) raw, (LONG) rawBytes, 1);
    if (err != 1) return IFFERR_MANGLED;

    dp = (struct DrawPoint *) AllocPooled (ld->ld_Pool,
                                            ((ULONG) n) *
                                             sizeof (struct DrawPoint));
    if (!dp) return IFFERR_NOMEM;

    for (i = 0; i < n; i++)
    {
        xRaw = raw[(ULONG) i * 2 + 0];
        yRaw = raw[(ULONG) i * 2 + 1];

        if (xRaw == DR2D_IND)
        {
            /* Indicator: yRaw is a BIT FIELD (IND_SPLINE 0x01,           *
             * IND_MOVETO 0x02, ...), so test bits -- the two can be      *
             * combined (0x03) on the first point of a curved sub-path.   */
            if (yRaw & DR2D_MOVETO)
            {
                /* End the current sub-path; the next real point begins a *
                 * new one and is tagged DRPF_MOVETO below.               */
                if (outCount > 0)
                    dp[outCount - 1].dp_Flags |= DRPF_END;
                pendingMove = TRUE;
            }
            if (yRaw & DR2D_SPLINE)
            {
                /* A cubic follows as four real points P1,P2,P3,P4: the   *
                 * pen lines to P1, then curves through control points    *
                 * P2,P3 to the endpoint P4 (see the DR2D spec algorithm  *
                 * and the verified PolyPoints layout).  Flagging the two *
                 * middle points DRPF_BEZIER gives the renderer the       *
                 * "p0=cursor, p1,p2=controls, p3=end" pattern it expects *
                 * and subdivides for a smooth curve.                     */
                bezPos = 1;
            }
            continue;
        }

        dp[outCount].dp_X = (LONG) IEEE_TO_FIXED (xRaw);
        dp[outCount].dp_Y = (LONG) IEEE_TO_FIXED (yRaw);
        dp[outCount].dp_Flags = DRPF_NORMAL;
        if (outCount == 0 || pendingMove)
            dp[outCount].dp_Flags |= DRPF_MOVETO;
        pendingMove = FALSE;
        if (bezPos == 2 || bezPos == 3)
            dp[outCount].dp_Flags |= DRPF_BEZIER;
        outCount++;

        if (bezPos)
        {
            bezPos++;
            if (bezPos > 4) bezPos = 0;
        }
    }

    if (outCount > 0)
    {
        if (closed) dp[outCount - 1].dp_Flags |= DRPF_CLOSE;
        dp[outCount - 1].dp_Flags |= DRPF_END;
    }

    obj->do_Data.Poly.do_Points = dp;
    obj->do_Data.Poly.do_NumPoints = (ULONG) outCount;
    return 0;
}


/*****************************************************************************/
/* dr2dTrimTextNUL                                                           */
/*                                                                           */
/* ProVector counts the terminating NUL in NumChars for some text objects    */
/* (notably the OFNT/Type1 code-chart cells).  Preserve the rich text markup */
/* for vector.image, but trim the embedded terminator from the visible byte  */
/* count so graphics.library never sees it as a glyph.                       */
/*****************************************************************************/

static UWORD dr2dTrimTextNUL (STRPTR s, UWORD n)
{
    UWORD i;

    if (!s) return 0;

    for (i = 0; i < n; i++)
    {
        if (s[i] == '\0')
        {
            s[i] = '\0';
            return i;
        }
    }

    s[n] = '\0';
    return n;
}


/*****************************************************************************/
/* readSTXTChunk                                                              */
/*****************************************************************************/

static LONG readSTXTChunk (struct ClassBase *cb, struct localData *ld,
                           struct IFFHandle *iff,
                           struct ContextNode *cn,
                           struct DrawObj *obj)
{
    ChunkScratch  hdr;
    LONG  err;
    UWORD n;
    STRPTR s;

    (void) cb;
    (void) cn;

    err = ReadChunkRecords (iff, (APTR) &hdr.stxt,
                            (LONG) sizeof (hdr.stxt), 1);
    if (err != 1) return IFFERR_MANGLED;

    obj->do_Data.Text.do_WhichFont = (UWORD) hdr.stxt.WhichFont;
    obj->do_Data.Text.do_CharW     = IEEE_TO_FIXED (hdr.stxt.CharW);
    obj->do_Data.Text.do_CharH     = IEEE_TO_FIXED (hdr.stxt.CharH);
    obj->do_Data.Text.do_BaseX     = IEEE_TO_FIXED (hdr.stxt.BaseX);
    obj->do_Data.Text.do_BaseY     = IEEE_TO_FIXED (hdr.stxt.BaseY);
    obj->do_Data.Text.do_Rotation  = IEEE_ANGLE_FIXED (hdr.stxt.Rotation);
    obj->do_Data.Text.do_NumChars  = hdr.stxt.NumChars;

    n = hdr.stxt.NumChars;
    if (n == 0)
    {
        obj->do_Data.Text.do_String = NULL;
        return 0;
    }

    s = (STRPTR) AllocPooled (ld->ld_Pool, (ULONG) (n + 1));
    if (!s) return IFFERR_NOMEM;
    err = ReadChunkRecords (iff, s, (LONG) n, 1);
    if (err != 1) return IFFERR_MANGLED;
    s[n] = '\0';

    obj->do_Data.Text.do_NumChars = dr2dTrimTextNUL (s, n);
    obj->do_Data.Text.do_String = s;
    return 0;
}


/*****************************************************************************/
/* readTPTHChunk                                                             */
/*****************************************************************************/

static LONG readTPTHChunk (struct ClassBase *cb, struct localData *ld,
                           struct IFFHandle *iff,
                           struct ContextNode *cn,
                           struct DrawObj *obj)
{
    ChunkScratch  hdr;
    LONG   err;
    UWORD  numChars;
    UWORD  numPoints;
    UWORD  i;
    ULONG  xRaw;
    ULONG  yRaw;
    STRPTR s;
    UBYTE  pad;
    ULONG *raw;
    ULONG  rawBytes;
    struct DrawPoint *dp;

    (void) cb;
    (void) cn;

    err = ReadChunkRecords (iff, (APTR) &hdr.tpth,
                            (LONG) sizeof (hdr.tpth), 1);
    if (err != 1) return IFFERR_MANGLED;

    obj->do_Data.TextPath.do_WhichFont = hdr.tpth.WhichFont;
    obj->do_Data.TextPath.do_CharW     = IEEE_TO_FIXED (hdr.tpth.CharW);
    obj->do_Data.TextPath.do_CharH     = IEEE_TO_FIXED (hdr.tpth.CharH);
    numChars  = hdr.tpth.NumChars;
    numPoints = hdr.tpth.NumPoints;
    obj->do_Data.TextPath.do_NumChars  = numChars;
    obj->do_Data.TextPath.do_NumPath   = (ULONG) numPoints;

    /* Read text string and any pad byte. */
    if (numChars > 0)
    {
        s = (STRPTR) AllocPooled (ld->ld_Pool, (ULONG) (numChars + 1));
        if (!s) return IFFERR_NOMEM;
        err = ReadChunkRecords (iff, s, (LONG) numChars, 1);
        if (err != 1) return IFFERR_MANGLED;
        s[numChars] = '\0';
        obj->do_Data.TextPath.do_NumChars =
            dr2dTrimTextNUL (s, numChars);
        obj->do_Data.TextPath.do_String = s;

        if (numChars & 1)
        {
            err = ReadChunkRecords (iff, &pad, 1, 1);
            if (err != 1) return IFFERR_MANGLED;
        }
    }
    else
    {
        obj->do_Data.TextPath.do_String = NULL;
    }

    /* Read path points. */
    if (numPoints > 0)
    {
        rawBytes = ((ULONG) numPoints) * 2 * sizeof (ULONG);
        raw = (ULONG *) AllocPooled (ld->ld_Pool, rawBytes);
        if (!raw) return IFFERR_NOMEM;
        err = ReadChunkRecords (iff, (APTR) raw, (LONG) rawBytes, 1);
        if (err != 1) return IFFERR_MANGLED;

        dp = (struct DrawPoint *) AllocPooled (ld->ld_Pool,
                                                ((ULONG) numPoints) *
                                                 sizeof (struct DrawPoint));
        if (!dp) return IFFERR_NOMEM;

        for (i = 0; i < numPoints; i++)
        {
            xRaw = raw[(ULONG) i * 2];
            yRaw = raw[(ULONG) i * 2 + 1];

            if (xRaw == DR2D_IND)
            {
                dp[i].dp_X = 0;
                dp[i].dp_Y = 0;
                dp[i].dp_Flags = (yRaw == DR2D_SPLINE) ? DRPF_BEZIER
                              : (yRaw == DR2D_MOVETO) ? DRPF_MOVETO
                              :                          DRPF_END;
            }
            else
            {
                dp[i].dp_X = IEEE_TO_FIXED (xRaw);
                dp[i].dp_Y = IEEE_TO_FIXED (yRaw);
                dp[i].dp_Flags = (i == 0) ? DRPF_MOVETO : DRPF_NORMAL;
            }
        }
        obj->do_Data.TextPath.do_Path = dp;
    }
    else
    {
        obj->do_Data.TextPath.do_Path = NULL;
    }
    return 0;
}


/*****************************************************************************/
/* readVBMChunk                                                              */
/*****************************************************************************/

static LONG readVBMChunk (struct ClassBase *cb, struct localData *ld,
                          struct IFFHandle *iff,
                          struct ContextNode *cn,
                          struct DrawObj *obj)
{
    ChunkScratch  hdr;
    LONG  err;
    LONG  remaining;
    UWORD pathLen;
    UWORD bytesToRead;
    STRPTR s;

    (void) cb;

    err = ReadChunkRecords (iff, (APTR) &hdr.vbm,
                            (LONG) sizeof (hdr.vbm), 1);
    if (err != 1) return IFFERR_MANGLED;

    obj->do_Data.Bitmap.do_XPos     = IEEE_TO_FIXED (hdr.vbm.XPos);
    obj->do_Data.Bitmap.do_YPos     = IEEE_TO_FIXED (hdr.vbm.YPos);
    obj->do_Data.Bitmap.do_Width    = IEEE_TO_FIXED (hdr.vbm.XSize);
    obj->do_Data.Bitmap.do_Height   = IEEE_TO_FIXED (hdr.vbm.YSize);
    obj->do_Data.Bitmap.do_Rotation = IEEE_ANGLE_FIXED (hdr.vbm.Rotation);

    pathLen = hdr.vbm.PathLen;
    if (pathLen == 0)
    {
        obj->do_Data.Bitmap.do_Path = NULL;
        return 0;
    }

    /* ProVector 2.0 sometimes writes a PathLen larger than the actual  *
     * chunk; clamp to remaining bytes.                                 */
    remaining = (LONG) (cn->cn_Size - cn->cn_Scan);
    bytesToRead = (pathLen <= remaining) ? pathLen : (UWORD) remaining;

    s = (STRPTR) AllocPooled (ld->ld_Pool, (ULONG) (pathLen + 1));
    if (!s) return IFFERR_NOMEM;
    err = ReadChunkRecords (iff, s, (LONG) bytesToRead, 1);
    if (err != 1) return IFFERR_MANGLED;
    s[pathLen] = '\0';
    obj->do_Data.Bitmap.do_Path = s;
    return 0;
}


/*****************************************************************************/
/* readLAYRChunk                                                             */
/*                                                                           */
/* The DRAW.datatype root class owns the layer list.  We call the public     */
/* method DDTM_ADDLAYER on it so it can pool the layer record and chain      */
/* it onto its own list.                                                     */
/*****************************************************************************/

static LONG readLAYRChunk (struct ClassBase *cb, Object *o,
                           struct localData *ld,
                           struct IFFHandle *iff)
{
    ChunkScratch  data;
    LONG  err;
    struct DrawLayer *layer;
    struct drwAddLayer addMsg;
    LONG  i;

    (void) cb;

    err = ReadChunkRecords (iff, (APTR) &data.layr,
                            (LONG) sizeof (data.layr), 1);
    if (err != 1) return IFFERR_MANGLED;

    layer = (struct DrawLayer *) AllocPooled (ld->ld_Pool,
                                               sizeof (struct DrawLayer));
    if (!layer) return IFFERR_NOMEM;

    layer->dl_ID = data.layr.LayerID;
    layer->dl_Flags = 0;
    if (data.layr.Flags & DR2D_LAYR_ACTIVE)
        layer->dl_Flags |= DRLF_ACTIVE;
    /* Honour LF_DISPLAYED literally.  ProVector clears it on construction,   *
     * backup and "storage" layers (e.g. RainReport's hidden black canopy     *
     * copy on "TextStorage"); a faithful viewer must not paint those.  The   *
     * visible artwork in every ProVector sample carries LF_DISPLAYED, so no  *
     * "render unspecified layers" fallback is needed -- and adding one would *
     * resurrect exactly the hidden geometry we must skip.                    */
    if (data.layr.Flags & DR2D_LAYR_DISPLAYED)
        layer->dl_Flags |= DRLF_DISPLAYED;
    for (i = 0; i < 16 && i < (LONG) sizeof (layer->dl_Name) - 1; i++)
        layer->dl_Name[i] = data.layr.LayerName[i];
    layer->dl_Name[i] = '\0';

    addMsg.MethodID    = DDTM_ADDLAYER;
    addMsg.drwal_Layer = layer;
    DoMethodA (o, (Msg) &addMsg);
    ld->ld_NumLayersSeen++;
    return 0;
}


/*****************************************************************************/
/* readCMAPChunk                                                              */
/*****************************************************************************/

static LONG readCMAPChunk (struct ClassBase *cb, Object *o,
                           struct localData *ld,
                           struct IFFHandle *iff,
                           struct ContextNode *cn)
{
    UBYTE rgb[3];
    LONG  err;
    LONG  i;
    LONG  numColors;
    UWORD slot;
    struct drwAddColor addMsg;

    (void) cb;

    numColors = (LONG) (cn->cn_Size / 3);
    for (i = 0; i < numColors; i++)
    {
        err = ReadChunkRecords (iff, rgb, 3, 1);
        if (err != 1) return IFFERR_MANGLED;

        /* Stash a local copy for ATTR-index resolution.  Anything past *
         * DR2D_MAX_COLORS is forwarded to the root class but not       *
         * cached - those entries simply won't be resolvable via ATTR.  */
        if (ld->ld_NumColors < DR2D_MAX_COLORS)
        {
            slot = ld->ld_NumColors;
            ld->ld_CMap[slot][0] = rgb[0];
            ld->ld_CMap[slot][1] = rgb[1];
            ld->ld_CMap[slot][2] = rgb[2];
            ld->ld_NumColors++;
        }

        addMsg.MethodID  = DDTM_ADDCOLOR;
        addMsg.drwac_R   = rgb[0];
        addMsg.drwac_G   = rgb[1];
        addMsg.drwac_B   = rgb[2];
        addMsg.drwac_Pad = 0;
        DoMethodA (o, (Msg) &addMsg);
    }
    return 0;
}


/*****************************************************************************/
/* paletteToARGB                                                             */
/*                                                                           */
/* Resolve a DR2D palette index into a packed ARGB ULONG using the local     */
/* CMAP cache.  Returns DDTA_TRANSPARENT if the index is out of range or     */
/* the palette is empty - callers may use that as a "skip drawing" hint.     */
/*****************************************************************************/

static ULONG paletteToARGB (struct localData *ld, UWORD index)
{
    if (index >= ld->ld_NumColors) return DDTA_TRANSPARENT;
    return 0xFF000000UL
         | ((ULONG) ld->ld_CMap[index][0] << 16)
         | ((ULONG) ld->ld_CMap[index][1] <<  8)
         |  (ULONG) ld->ld_CMap[index][2];
}


/*****************************************************************************/
/* readFONSChunk                                                             */
/*****************************************************************************/

static UBYTE dr2dLower (UBYTE c)
{
    if (c >= 'A' && c <= 'Z') return (UBYTE) (c + ('a' - 'A'));
    return c;
}


static BOOL dr2dNameStarts (STRPTR name, STRPTR prefix)
{
    UWORD i;

    if (!name || !prefix) return FALSE;
    i = 0;
    while (prefix[i])
    {
        if (dr2dLower ((UBYTE) name[i]) != dr2dLower ((UBYTE) prefix[i]))
            return FALSE;
        i++;
    }
    return TRUE;
}


static BOOL dr2dNameContains (STRPTR name, STRPTR needle)
{
    UWORD i;
    UWORD j;

    if (!name || !needle) return FALSE;
    for (i = 0; name[i]; i++)
    {
        j = 0;
        while (needle[j] &&
               dr2dLower ((UBYTE) name[i + j]) ==
               dr2dLower ((UBYTE) needle[j]))
            j++;
        if (needle[j] == '\0') return TRUE;
    }
    return FALSE;
}


static STRPTR dr2dMapFontName (STRPTR name, UBYTE serif, UBYTE *styleFlags)
{
    UBYTE style;
    STRPTR mapped;

    style = 0;
    mapped = NULL;

    if (name && name[0])
    {
        if (dr2dNameContains (name, "italic") ||
            dr2dNameContains (name, "oblique") ||
            dr2dNameContains (name, "script"))
            style |= DRFS_ITALIC;
        if (dr2dNameContains (name, "bold") ||
            dr2dNameContains (name, "demi"))
            style |= DRFS_BOLD;

        if (dr2dNameStarts (name, "pv_roman") ||
            dr2dNameContains (name, "serif") ||
            dr2dNameStarts (name, "palatino") ||
            dr2dNameStarts (name, "times"))
            mapped = (STRPTR) "CGTimes.font";
        else if (dr2dNameStarts (name, "pv_sans") ||
                 dr2dNameContains (name, "simple_stroke") ||
                 dr2dNameStarts (name, "avantgarde") ||
                 dr2dNameStarts (name, "helvetica"))
            mapped = (STRPTR) "CGTriumvirate.font";
    }

    if (!mapped)
        mapped = (serif == 2) ? (STRPTR) "CGTimes.font"
                              : (STRPTR) "CGTriumvirate.font";

    if (styleFlags) *styleFlags = style;
    return mapped;
}

static LONG readFONSChunk (struct ClassBase *cb, Object *o,
                           struct localData *ld,
                           struct IFFHandle *iff,
                           struct ContextNode *cn)
{
    ChunkScratch data;
    LONG err;
    LONG nameLen;
    STRPTR name;
    STRPTR mappedName;
    struct DrawFont *font;
    struct drwAddFont addMsg;
    UBYTE styleFlags;

    (void) cb;

    err = ReadChunkRecords (iff, (APTR) &data.fons,
                            (LONG) sizeof (data.fons), 1);
    if (err != 1) return IFFERR_MANGLED;

    nameLen = (LONG) (cn->cn_Size - cn->cn_Scan);
    if (nameLen < 0) nameLen = 0;
    if (nameLen > 256) nameLen = 256;

    font = (struct DrawFont *) AllocPooled (ld->ld_Pool,
                                             sizeof (struct DrawFont));
    if (!font) return IFFERR_NOMEM;
    font->df_FontID       = data.fons.FontID;
    font->df_Proportional = data.fons.Proportional;
    font->df_Serif        = data.fons.Serif;
    font->df_StyleFlags   = 0;
    font->df_Name         = NULL;

    if (nameLen > 0)
    {
        name = (STRPTR) AllocPooled (ld->ld_Pool, (ULONG) (nameLen + 1));
        if (!name) return IFFERR_NOMEM;
        err = ReadChunkRecords (iff, name, nameLen, 1);
        if (err != 1) return IFFERR_MANGLED;
        name[nameLen] = '\0';
        font->df_Name = name;
    }

    mappedName = dr2dMapFontName (font->df_Name, font->df_Serif,
                                  &styleFlags);
    font->df_Name = mappedName;
    font->df_StyleFlags = styleFlags;

    addMsg.MethodID    = DDTM_ADDFONT;
    addMsg.drwaf_Font  = font;
    DoMethodA (o, (Msg) &addMsg);
    return 0;
}


/*****************************************************************************/
/* absorbATTRProp                                                            */
/*                                                                           */
/* The current PROP value for ATTR (the most recent ATTR chunk seen) is      */
/* the attribute set every following CPLY/OPLY/STXT/etc. inherits.           */
/*****************************************************************************/

static UWORD attrDbgCount = 0;

static LONG readATTRChunk (struct ClassBase *cb, struct localData *ld,
                           struct IFFHandle *iff, struct ContextNode *cn)
{
    UBYTE  buf[14];
    UBYTE *p;
    struct DrawAttr *a;
    struct DR2DGradient *g;
    UWORD  fillValue;
    UWORD  edgeValue;
    UBYTE  fillType;
    UBYTE  edgePattern;
    ULONG  ieee;
    LONG   got;

    /* DR2D's ATTR is 14 bytes of fixed fields followed by an optional      *
     * variable-length dash array.  We only need the leading 14 bytes;      *
     * iffparse skips any remainder of the chunk for us.                    */
    if (cn->cn_Size < 14)
    {
        ld->ld_HasAttr = FALSE;
        return 0;
    }

    got = ReadChunkRecords (iff, (APTR) buf, 14L, 1L);
    if (got != 1L) return IFFERR_MANGLED;

    p = buf;
    a = &ld->ld_LastAttr;

    /* ATTR chunk on-disk layout (big-endian, no padding):                  *
     *   p[0]  UBYTE  FillType    (FT_NONE / FT_COLOR / FT_OBJECTS)         *
     *   p[1]  UBYTE  JoinType                                              *
     *   p[2]  UBYTE  EdgePattern (DR2D's "DashPattern"; 0 means no edge)   *
     *   p[3]  UBYTE  ArrowHeads                                            *
     *   p[4]  USHORT FillValue   (CMAP index)                              *
     *   p[6]  USHORT EdgeValue   (CMAP index)                              *
     *   p[8]  USHORT WhichLayer                                            *
     *   p[10] IEEE   EdgeThick                                             */
    fillType    = p[0];
    edgePattern = p[2];

    fillValue = ((UWORD) p[4] << 8) | (UWORD) p[5];
    edgeValue = ((UWORD) p[6] << 8) | (UWORD) p[7];

    a->da_FillType    = fillType;
    a->da_JoinType    = p[1];
    a->da_EdgePattern = edgePattern;
    a->da_ArrowHeads  = p[3];
    a->da_LayerID     = ((UWORD) p[8] << 8) | (UWORD) p[9];

    /* Default the gradient / object-fill extension fields.  They are only  *
     * consulted by the renderer when da_FillType selects them, but keep    *
     * them well-defined for every ATTR so a stale value never leaks.       */
    a->da_FillColor2    = DDTA_TRANSPARENT;
    a->da_GradType      = DRGT_LINEAR;
    a->da_GradFlags     = 0;
    a->da_GradAngle     = 0;
    a->da_GradCX        = DRW_FIXED_HALF;
    a->da_GradCY        = DRW_FIXED_HALF;
    a->da_GradInterp    = DRGI_LINEAR;
    a->da_NumGradStops  = 0;
    a->da_GradStops     = NULL;
    a->da_FillID        = 0;
    a->da_FillPad       = 0;

    /* Resolve the fill according to its type.  DR2D's FT_* values share    *
     * the drawing.datatype DRFT_* numbering, so da_FillType is set above   *
     * directly; here we fill in the colour / gradient / pattern payload.   */
    if (fillType == DR2D_FT_COLOR)
    {
        a->da_FillColor = paletteToARGB (ld, fillValue);
    }
    else if (fillType == DR2D_FT_GRADIENT)
    {
        g = (fillValue < DR2D_MAX_GRADIENTS)
              ? &ld->ld_Gradients[fillValue] : NULL;
        if (g && g->gr_Valid)
        {
            a->da_FillType   = DRFT_GRADIENT;
            a->da_FillColor  = paletteToARGB (ld, g->gr_Start);
            a->da_FillColor2 = paletteToARGB (ld, g->gr_End);

            /* GradType / GradInterp share the drawing.datatype DRGT_/DRGI_   *
             * numbering (deliberately matched to the ProVector bytes), so a  *
             * straight copy works; clamp so a malformed byte can never index *
             * past the renderer's switch tables.                             */
            a->da_GradType   = (g->gr_RawType <= DRGT_SHAPE)
                                   ? g->gr_RawType : DRGT_LINEAR;
            a->da_GradInterp = (g->gr_Interp <= DRGI_ARCSINE)
                                   ? g->gr_Interp : DRGI_LINEAR;
            a->da_GradAngle  = g->gr_Angle;
            a->da_GradCX     = g->gr_CX;
            a->da_GradCY     = g->gr_CY;

            /* Hand the renderer the resolved multi-stop ramp when we have    *
             * one; otherwise da_NumGradStops stays 0 and it uses the         *
             * FillColor -> FillColor2 pair set above.                        */
            a->da_NumGradStops = g->gr_NumStops;
            a->da_GradStops    = g->gr_Stops;
        }
        else
        {
            /* No matching GRAD entry: render with no fill rather than       *
             * mistaking the gradient index for a palette colour.            */
            a->da_FillType  = DRFT_NONE;
            a->da_FillColor = DDTA_TRANSPARENT;
        }
    }
    else if (fillType == DR2D_FT_OBJECTS)
    {
        a->da_FillType  = DRFT_OBJECTS;
        a->da_FillID    = fillValue;
        a->da_FillColor = DDTA_TRANSPARENT;
    }
    else
    {
        a->da_FillColor = DDTA_TRANSPARENT;
    }

    /* DR2D uses EdgePattern==0 to mean "no edge".  Otherwise resolve the *
     * EdgeValue through the CMAP cache.                                  */
    if (edgePattern == 0)
        a->da_EdgeColor = DDTA_TRANSPARENT;
    else
        a->da_EdgeColor = paletteToARGB (ld, edgeValue);

    /* EdgeThick is an IEEE single at offset 10..13. */
    ieee = ((ULONG) p[10] << 24) |
            ((ULONG) p[11] << 16) |
            ((ULONG) p[12] <<  8) |
             (ULONG) p[13];
    a->da_EdgeThick = dr2dCoordToFixed (ieee);

    a->da_CapType     = DRCT_BUTT;
    a->da_FillOpacity = DRW_FIXED_ONE;

    ld->ld_HasAttr = TRUE;

    if (attrDbgCount < 4)
    {
        attrDbgCount++;
        dbgPutLong (cb, "ATTR fillType=", (LONG) fillType);
        dbgPutLong (cb, "ATTR edgePat=",  (LONG) edgePattern);
        dbgPutLong (cb, "ATTR fillARGB=", (LONG) a->da_FillColor);
        dbgPutLong (cb, "ATTR edgeARGB=", (LONG) a->da_EdgeColor);
    }
    return 0;
}


/*****************************************************************************/
/* readGRADChunk                                                             */
/*                                                                           */
/* Capture one GRAD (gradient) table entry.  GRAD is a ProVector 3           */
/* extension: it is not part of the published 1991 DR2D spec, but every      */
/* PV3 drawing defines a small GRAD table that ATTR chunks select via        */
/* FillType == FT_GRADIENT.  See dr2dclass.h for the GRAD chunk layout.      */
/* We stash the geometry (16.16 fixed) and the start/end                     */
/* CMAP indices; readATTRChunk later resolves the colours through the CMAP   */
/* cache (CMAP always precedes GRAD in the file).                            */
/*****************************************************************************/

static UWORD gradDbgCount = 0;

#define DR2D_GRAD_HDR   18        /* fixed-size GRAD header before the stops */
#define DR2D_GRAD_MAXST 256       /* stop array is indexed by a UBYTE count  */

static LONG readGRADChunk (struct ClassBase *cb, struct localData *ld,
                           struct IFFHandle *iff, struct ContextNode *cn)
{
    UBYTE  hdr[DR2D_GRAD_HDR];
    UBYTE  sbuf[DR2D_GRAD_MAXST * 2];
    LONG   got;
    LONG   stopBytes;
    LONG   avail;
    UWORD  gradID;
    UWORD  numStops;
    UWORD  i;
    UWORD  idx;
    struct DR2DGradient *g;
    ULONG  ieeeX;
    ULONG  ieeeY;
    ULONG  ieeeAngle;

    /* ProVector 3 GRAD layout (see examples/grad.h, struct GRADstruct):     *
     *   hdr[0..1]   UWORD  FillID          (the GradID an ATTR selects)      *
     *   hdr[2]      UBYTE  GradType        G_CONIC/LINEAR/RADIAL/SHAPE 0..3  *
     *   hdr[3]      UBYTE  GradInterp      I_LINEAR .. I_ARCSINE 0..4        *
     *   hdr[4]      UBYTE  NumColors       number of stop entries that follow*
     *   hdr[5]      UBYTE  Pad0            zero                              *
     *   hdr[6..9]   IEEE   GradX           centre X, 0..1 fraction of bbox   *
     *   hdr[10..13] IEEE   GradY           centre Y, 0..1 fraction of bbox   *
     *   hdr[14..17] IEEE   GradAngle       degrees (conic / linear direction)*
     *   hdr[18..]   UWORD  GradColors[NumColors]  CMAP indices              *
     * CMAP always precedes GRAD in the file, so the indices can be resolved *
     * to ARGB here (the local CMAP cache is already populated).             */
    if (cn->cn_Size < DR2D_GRAD_HDR)
        return 0;

    got = ReadChunkRecords (iff, (APTR) hdr, (LONG) DR2D_GRAD_HDR, 1L);
    if (got != 1L) return IFFERR_MANGLED;

    gradID = ((UWORD) hdr[0] << 8) | (UWORD) hdr[1];
    if (gradID >= DR2D_MAX_GRADIENTS)
        return 0;

    ieeeX     = ((ULONG) hdr[6]  << 24) | ((ULONG) hdr[7]  << 16) |
                ((ULONG) hdr[8]  <<  8) |  (ULONG) hdr[9];
    ieeeY     = ((ULONG) hdr[10] << 24) | ((ULONG) hdr[11] << 16) |
                ((ULONG) hdr[12] <<  8) |  (ULONG) hdr[13];
    ieeeAngle = ((ULONG) hdr[14] << 24) | ((ULONG) hdr[15] << 16) |
                ((ULONG) hdr[16] <<  8) |  (ULONG) hdr[17];

    /* Clamp the declared stop count to what the chunk actually carries so a *
     * truncated / mangled GRAD can never make us read past the chunk.       */
    numStops = (UWORD) hdr[4];
    avail    = (LONG) cn->cn_Size - DR2D_GRAD_HDR;
    if (avail < 0) avail = 0;
    if ((LONG) numStops > (avail / 2)) numStops = (UWORD) (avail / 2);
    if (numStops > DR2D_GRAD_MAXST) numStops = DR2D_GRAD_MAXST;

    g = &ld->ld_Gradients[gradID];
    g->gr_RawType  = (UWORD) hdr[2];
    g->gr_Interp   = (UWORD) hdr[3];
    g->gr_CX       = dr2dIEEEToFixed (ieeeX);
    g->gr_CY       = dr2dIEEEToFixed (ieeeY);
    g->gr_Angle    = dr2dIEEEToFixed (ieeeAngle);
    g->gr_NumStops = 0;
    g->gr_Stops    = NULL;
    g->gr_Start    = 0;
    g->gr_End      = 0;
    g->gr_Valid    = TRUE;

    if (numStops > 0)
    {
        stopBytes = (LONG) numStops * 2L;
        got = ReadChunkRecords (iff, (APTR) sbuf, stopBytes, 1L);
        if (got != 1L) return IFFERR_MANGLED;

        /* The first and last stops drive the two-colour fallback used by    *
         * producers / paths that do not honour the multi-stop array.        */
        g->gr_Start = ((UWORD) sbuf[0] << 8) | (UWORD) sbuf[1];
        g->gr_End   = ((UWORD) sbuf[(numStops - 1) * 2] << 8) |
                       (UWORD) sbuf[(numStops - 1) * 2 + 1];

        /* Resolve every stop index to packed ARGB once, into a pool array   *
         * shared by every object that references this GRAD entry.  Without  *
         * a pool we simply keep the fallback pair.                          */
        if (numStops >= 2 && ld->ld_Pool)
        {
            g->gr_Stops = (ULONG *) AllocPooled (ld->ld_Pool,
                              (ULONG) numStops * sizeof (ULONG));
            if (g->gr_Stops)
            {
                for (i = 0; i < numStops; i++)
                {
                    idx = ((UWORD) sbuf[i * 2] << 8) |
                           (UWORD) sbuf[i * 2 + 1];
                    g->gr_Stops[i] = paletteToARGB (ld, idx);
                }
                g->gr_NumStops = numStops;
            }
        }
    }

    if (gradDbgCount < 6)
    {
        gradDbgCount++;
        dbgPutLong (cb, "GRAD id=",     (LONG) gradID);
        dbgPutLong (cb, "GRAD type=",   (LONG) g->gr_RawType);
        dbgPutLong (cb, "GRAD interp=", (LONG) g->gr_Interp);
        dbgPutLong (cb, "GRAD nstops=", (LONG) g->gr_NumStops);
        dbgPutLong (cb, "GRAD start=",  (LONG) g->gr_Start);
        dbgPutLong (cb, "GRAD end=",    (LONG) g->gr_End);
    }
    return 0;
}


/*****************************************************************************/
/* absorbATTRProp                                                            */
/*                                                                           */
/* The running ATTR state is maintained inline by readATTRChunk (DR2D's      */
/* ATTR is not a real IFF PROP - see declareInterestingChunks).  Object      */
/* handlers still call this for symmetry; the current ld_LastAttr already    */
/* holds the attributes the object should inherit, so there is nothing to    */
/* do here.                                                                  */
/*****************************************************************************/

static void absorbATTRProp (struct ClassBase *cb, struct localData *ld,
                            struct IFFHandle *iff)
{
    (void) cb;
    (void) ld;
    (void) iff;
}


/*****************************************************************************/
/* absorbBBOXProp                                                            */
/*                                                                           */
/* Apply the BBOX captured inline (ld_PendingBBox) to the object being       */
/* built, then clear it so it is not reused by a later object that has no    */
/* BBOX of its own.  Objects without a BBOX keep the invalid sentinels set   */
/* by dr2dMakeDrawObj, which makes computeObjectBounds derive the box from   */
/* the geometry instead.                                                     */
/*****************************************************************************/

static UWORD bboxDbgCount = 0;

static void absorbBBOXProp (struct ClassBase *cb, struct localData *ld,
                            struct DrawObj *obj)
{
    ULONG *src;

    if (!ld->ld_HasPendingBBox) return;

    src = ld->ld_PendingBBox;
    obj->do_XMin = dr2dCoordToFixed (src[0]);
    obj->do_YMin = dr2dCoordToFixed (src[1]);
    obj->do_XMax = dr2dCoordToFixed (src[2]);
    obj->do_YMax = dr2dCoordToFixed (src[3]);
    ld->ld_HasPendingBBox = FALSE;

    if (bboxDbgCount < 4)
    {
        bboxDbgCount++;
        dbgPutLong (cb, "BBOX raw xmin=", (LONG) src[0]);
        dbgPutLong (cb, "BBOX raw xmax=", (LONG) src[2]);
        dbgPutLong (cb, "BBOX do_XMin fixed=", obj->do_XMin);
        dbgPutLong (cb, "BBOX do_XMax fixed=", obj->do_XMax);
    }
}


/*****************************************************************************/
/* Display-list construction helpers                                         */
/*****************************************************************************/

struct DrawObj *dr2dMakeDrawObj (struct ClassBase *cb, Object *o,
                                 struct localData *ld, UWORD type)
{
    struct DrawObj *obj;

    (void) cb;
    (void) o;

    obj = (struct DrawObj *) AllocPooled (ld->ld_Pool,
                                          sizeof (struct DrawObj));
    if (!obj) return NULL;
    obj->do_Type  = type;
    obj->do_Flags = 0;
    obj->do_XMin  = (LONG) 0x7FFFFFFFL;
    obj->do_YMin  = (LONG) 0x7FFFFFFFL;
    obj->do_XMax  = (LONG) 0x80000000L;
    obj->do_YMax  = (LONG) 0x80000000L;
    obj->do_Attrs.da_FillType    = DRFT_NONE;
    obj->do_Attrs.da_JoinType    = DRJT_MITER;
    obj->do_Attrs.da_CapType     = DRCT_BUTT;
    obj->do_Attrs.da_FillColor   = DDTA_TRANSPARENT;
    obj->do_Attrs.da_EdgeColor   = 0xFF000000UL;
    obj->do_Attrs.da_EdgeThick   = DRW_FIXED_ONE;
    obj->do_Attrs.da_FillOpacity = DRW_FIXED_ONE;
    obj->do_Attrs.da_FillColor2  = DDTA_TRANSPARENT;
    obj->do_Attrs.da_GradType    = DRGT_LINEAR;
    obj->do_Attrs.da_GradFlags   = 0;
    obj->do_Attrs.da_GradAngle   = 0;
    obj->do_Attrs.da_GradCX      = DRW_FIXED_HALF;
    obj->do_Attrs.da_GradCY      = DRW_FIXED_HALF;
    obj->do_Attrs.da_GradInterp   = DRGI_LINEAR;
    obj->do_Attrs.da_NumGradStops = 0;
    obj->do_Attrs.da_GradStops    = NULL;
    obj->do_Attrs.da_FillID      = 0;
    obj->do_Attrs.da_FillPad     = 0;
    return obj;
}


LONG dr2dPushChild (struct ClassBase *cb, Object *o,
                    struct localData *ld, struct DrawObj *obj)
{
    struct drwAddObject addMsg;
    struct DR2DGroup *g;

    (void) cb;

    if (!obj) return IFFERR_NOMEM;

    /* Increment our group bookkeeping. */
    if (ld->ld_StackDepth > 0)
    {
        g = &ld->ld_Stack[ld->ld_StackDepth - 1];
        g->dg_Received++;
    }

    addMsg.MethodID  = DDTM_ADDOBJECT;
    addMsg.drwa_Obj  = obj;
    DoMethodA (o, (Msg) &addMsg);
    return 0;
}


LONG dr2dBeginGroup (struct ClassBase *cb, Object *o,
                     struct localData *ld,
                     UWORD type, ULONG expect, struct DrawObj *owner)
{
    struct drwBeginGroup beg;

    (void) cb;

    if (ld->ld_StackDepth >= DR2D_MAX_STACK) return IFFERR_MANGLED;

    ld->ld_Stack[ld->ld_StackDepth].dg_Type     = type;
    ld->ld_Stack[ld->ld_StackDepth].dg_Pad      = 0;
    ld->ld_Stack[ld->ld_StackDepth].dg_Expect   = expect;
    ld->ld_Stack[ld->ld_StackDepth].dg_Received = 0;
    ld->ld_Stack[ld->ld_StackDepth].dg_Owner    = owner;
    ld->ld_StackDepth++;

    beg.MethodID        = DDTM_BEGINGROUP;
    beg.drwbg_Type      = type;
    beg.drwbg_Pad       = 0;
    beg.drwbg_ExpectCount = expect;
    beg.drwbg_FillID    = (owner && type == DROT_FILL)
                            ? (ULONG) owner->do_Data.Fill.do_FillID : 0;
    DoMethodA (o, (Msg) &beg);
    return 0;
}


LONG dr2dEndGroup (struct ClassBase *cb, Object *o,
                   struct localData *ld)
{
    struct drwEndGroup end;

    (void) cb;

    if (ld->ld_StackDepth == 0) return 0;

    ld->ld_StackDepth--;

    end.MethodID = DDTM_ENDGROUP;
    DoMethodA (o, (Msg) &end);
    return 0;
}
