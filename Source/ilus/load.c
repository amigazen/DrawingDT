/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* load.c -- IFF ILUS parser for ilus.datatype                               */
/*                                                                           */
/* drawing.datatype sends DDTM_LOADHEADERS during OM_NEW so the subclass     */
/* can populate the DrawingInfo (canvas bounds, colour table) before         */
/* scroller geometry is computed; it then sends DDTM_LOADCONTENT for the     */
/* display list.  IFF ILUS files are small and walked in a single pass, so   */
/* the whole drawing is built during DDTM_LOADHEADERS and DDTM_LOADCONTENT   */
/* is a no-op (the same shape the sibling dr2d.datatype uses).               */
/*                                                                           */
/* The IFF ILUS structure (FORM ILUS), is:                                   */
/*                                                                           */
/*   FORM ILUS                                                               */
/*     NAME / ANNO / PGDM / GRID / MRGN / HGUI / VGUI   leading metadata     */
/*     CTAG                            named colour (and other) tag table    */
/*     <objects>                       a mix of:                             */
/*        IOBJ                         a simple object (OB + AT + geometry)  */
/*        FORM IOBJ                     a group  (IOBJ[OB+GR] + FORM WRAP)   */
/*                                      or text  (IOBJ[OB+AT+TO] + CTXT)     */
/*                                                                           */
/* Every object is described by 2-character "punks" inside its IOBJ chunk:   */
/* an OB header (flags + 2-char type code + bounding box), an optional AT    */
/* attribute-data stream (fill / stroke colour, edge weight), and one        */
/* geometry punk named by the OB type code (PA+PD path, BX/RB/CO box, LN     */
/* line, CI circle, TO text, GR/DR group, PI/PS placed image).               */
/*                                                                           */
/* Colours are NOT an indexed palette: the CTAG chunk defines named colour   */
/* tags whose CMYK values the AT stream references by name, so the loader    */
/* resolves each reference to packed 0xAARRGGBB and stores ARGB (not an      */
/* index) in DrawAttr - the renderer never sees an ILUS colour name.         */
/*****************************************************************************/

#include "classbase.h"


/* Attribute-data parsing context: which paint a nested colour belongs to. */
#define ILUS_CTX_NONE     0
#define ILUS_CTX_FILL     1
#define ILUS_CTX_STROKE   2

/* Circle-as-bezier control handle length: 0.5522847498 of the radius,
 * expressed in 16.16 fixed (0.5522847498 * 65536 ~= 36195). */
#define ILUS_BEZ_KAPPA    36195L


/*****************************************************************************/
/* Result of decoding an AT attribute-data stream for one object.            */
/*****************************************************************************/

struct ILUSAttr
{
    ULONG  fillARGB;
    LONG   edgeThick;       /* 16.16 Fixed points                          */
    BOOL   fillSet;
    BOOL   edgeSet;
    ULONG  edgeARGB;
    BOOL   edgeThickSet;
};


/*****************************************************************************/
/* Debug helpers (PutStr to the serial / console when DOSBase is present).   */
/*****************************************************************************/

#ifdef ILUS_DEBUG

static void dbgPut (struct ClassBase *cb, STRPTR text)
{
    if (DOSBase && text)
    {
        PutStr ("[ilus] ");
        PutStr (text);
        PutStr ("\n");
    }
}

static void dbgPutLong (struct ClassBase *cb, STRPTR label, LONG value)
{
    if (DOSBase && label)
        Printf ("[ilus] %s%ld\n", (LONG) label, value);
}

#else   /* !ILUS_DEBUG: helpers compile away to nothing */

#define dbgPut(cb,text)            ((void) 0)
#define dbgPutLong(cb,label,value) ((void) 0)

#endif  /* ILUS_DEBUG */


/*****************************************************************************/
/* Big-endian field readers from an unaligned byte buffer.                   */
/*****************************************************************************/

static ULONG be32 (const UBYTE *p)
{
    return ((ULONG) p[0] << 24) | ((ULONG) p[1] << 16) |
           ((ULONG) p[2] <<  8) |  (ULONG) p[3];
}

static UWORD be16 (const UBYTE *p)
{
    return (UWORD) (((UWORD) p[0] << 8) | (UWORD) p[1]);
}


/******************************************************************************/
/* ilusVNum -- read one variable-length unsigned number                       */
/*                                                                            */
/* High bit set means "more bytes follow"; each continuation byte contributes */
/* its low 7 bits.  Reads stop at the buffer end so a truncated stream can    */
/* never run away.                                                            */
/******************************************************************************/

static ULONG ilusVNum (const UBYTE *b, LONG len, LONG *pos)
{
    ULONG v = 0;
    LONG  p = *pos;

    while (p < len && b[p] > 127)
    {
        v = (v << 7) + (ULONG) (b[p] - 128);
        p++;
    }
    if (p < len)
    {
        v = (v << 7) + (ULONG) b[p];
        p++;
    }
    *pos = p;
    return v;
}


/*****************************************************************************/
/* Colour helpers                                                            */
/*****************************************************************************/

static UBYTE ilusLower (UBYTE c)
{
    if (c >= 'A' && c <= 'Z') return (UBYTE) (c + ('a' - 'A'));
    return c;
}

/*
 * Convert an ILUS COLOR_STYLE CMYK triple-plus-key (each component is an
 * EMS value where 10000 == 100%) to packed 0xFFRRGGBB.  The standard
 * naive conversion R = 255 (1-C)(1-K) reproduces the primary and process
 * colours found in the sample files exactly (Red = M100 Y100, Black =
 * K100, White = all zero, Purple = M50 K50, ...).
 */
static ULONG ilusCMYKToARGB (ULONG c, ULONG m, ULONG y, ULONG k)
{
    LONG r, g, b;

    if (c > 10000) c = 10000;
    if (m > 10000) m = 10000;
    if (y > 10000) y = 10000;
    if (k > 10000) k = 10000;

    r = (255L * (LONG) (10000UL - c)) / 10000L;
    g = (255L * (LONG) (10000UL - m)) / 10000L;
    b = (255L * (LONG) (10000UL - y)) / 10000L;

    r = (r * (LONG) (10000UL - k)) / 10000L;
    g = (g * (LONG) (10000UL - k)) / 10000L;
    b = (b * (LONG) (10000UL - k)) / 10000L;

    return 0xFF000000UL | ((ULONG) r << 16) | ((ULONG) g << 8) | (ULONG) b;
}

/*
 * Decode a COLOR_STYLE command payload.  Layout (all VNums):
 *   flags, cyan, magenta, yellow, black, tint, type, <type-data...>
 * The leading C,M,Y,K fields hold the CMYK equivalent for every colour
 * type (RGB / grey / spot tags fill them in too), so reading those four
 * is enough to recover an RGB value regardless of the declared type.
 */
static ULONG ilusDecodeColorStyle (const UBYTE *d, LONG len)
{
    LONG  pos = 0;
    ULONG c, m, y, k;

    (void) ilusVNum (d, len, &pos);   /* flags */
    c = ilusVNum (d, len, &pos);
    m = ilusVNum (d, len, &pos);
    y = ilusVNum (d, len, &pos);
    k = ilusVNum (d, len, &pos);

    return ilusCMYKToARGB (c, m, y, k);
}

/*
 * Look up a colour tag name (not NUL-terminated, dlen bytes) in the table
 * built from the CTAG chunk.  Returns opaque black when the name is not
 * found so a missing reference still paints something visible.
 */
static ULONG ilusLookupColor (struct localData *ld, const UBYTE *name,
                              LONG dlen)
{
    UWORD i;
    LONG  j;
    BOOL  match;

    if (dlen <= 0) return 0xFF000000UL;

    for (i = 0; i < ld->ld_NumColors; i++)
    {
        match = TRUE;
        for (j = 0; j < dlen && j < ILUS_NAME_LEN - 1; j++)
        {
            if (ilusLower ((UBYTE) ld->ld_Colors[i].ic_Name[j]) !=
                ilusLower (name[j]))
            {
                match = FALSE;
                break;
            }
        }
        if (match && ld->ld_Colors[i].ic_Name[dlen] == '\0')
            return ld->ld_Colors[i].ic_ARGB;
    }
    return 0xFF000000UL;
}


/*******************************************************************************/
/* ilusDecodeAttr -- walk an attribute-data stream extracting paint            */
/*                                                                             */
/* Commands are { 0 marker, VNum cmd, VNum flags, VNum length, data[length] }. */
/* FILL nests attribute-data directly; STROKE wraps it in a (VNum count,       */
/* { VNum length, data }) list; COLOR wraps colour entries the same way but    */
/* keeps the enclosing fill / stroke context so the colour it carries is       */
/* assigned to the right paint.                                                */
/*******************************************************************************/

static void ilusDecodeAttr (struct localData *ld, const UBYTE *b, LONG len,
                            UWORD ctx, struct ILUSAttr *res)
{
    LONG  pos = 0;
    ULONG cmd, dlen;
    const UBYTE *pd;
    LONG  q, n, sub;
    ULONG argb;
    ULONG wtype, wval;

    while (pos < len)
    {
        if (b[pos] != 0)
        {
            pos++;
            continue;
        }
        pos++;                                  /* skip the zero marker      */
        if (pos >= len) break;

        cmd  = ilusVNum (b, len, &pos);
        (void) ilusVNum (b, len, &pos);         /* command flags (unused)    */
        dlen = ilusVNum (b, len, &pos);
        if ((LONG) dlen > len - pos) dlen = (ULONG) (len - pos);
        pd = b + pos;

        switch ((int) cmd)
        {
            case ILUS_CMD_FILL:
                ilusDecodeAttr (ld, pd, (LONG) dlen, ILUS_CTX_FILL, res);
                break;

            case ILUS_CMD_STROKE:
                q = 0;
                n = (LONG) ilusVNum (pd, (LONG) dlen, &q);
                while (n-- > 0 && q < (LONG) dlen)
                {
                    sub = (LONG) ilusVNum (pd, (LONG) dlen, &q);
                    if (sub > (LONG) dlen - q) sub = (LONG) dlen - q;
                    ilusDecodeAttr (ld, pd + q, sub, ILUS_CTX_STROKE, res);
                    q += sub;
                }
                break;

            case ILUS_CMD_COLOR:
                q = 0;
                n = (LONG) ilusVNum (pd, (LONG) dlen, &q);
                while (n-- > 0 && q < (LONG) dlen)
                {
                    sub = (LONG) ilusVNum (pd, (LONG) dlen, &q);
                    if (sub > (LONG) dlen - q) sub = (LONG) dlen - q;
                    ilusDecodeAttr (ld, pd + q, sub, ctx, res);
                    q += sub;
                }
                break;

            case ILUS_CMD_STYLETAG_COLOR:
                argb = ilusLookupColor (ld, pd, (LONG) dlen);
                if (ctx == ILUS_CTX_FILL)
                {
                    res->fillARGB = argb;
                    res->fillSet  = TRUE;
                }
                else if (ctx == ILUS_CTX_STROKE)
                {
                    res->edgeARGB = argb;
                    res->edgeSet  = TRUE;
                }
                break;

            case ILUS_CMD_COLOR_STYLE:
                argb = ilusDecodeColorStyle (pd, (LONG) dlen);
                if (ctx == ILUS_CTX_FILL)
                {
                    res->fillARGB = argb;
                    res->fillSet  = TRUE;
                }
                else if (ctx == ILUS_CTX_STROKE)
                {
                    res->edgeARGB = argb;
                    res->edgeSet  = TRUE;
                }
                break;

            case ILUS_CMD_STROKE_WEIGHT:
                if (ctx == ILUS_CTX_STROKE)
                {
                    q = 0;
                    wtype = ilusVNum (pd, (LONG) dlen, &q);
                    (void) wtype;
                    wval  = ilusVNum (pd, (LONG) dlen, &q);
                    res->edgeThick    = ilusCoordToFixed ((LONG) wval);
                    res->edgeThickSet = TRUE;
                }
                break;

            default:
                break;
        }
        pos += (LONG) dlen;
    }
}


/*****************************************************************************/
/* Display-list construction helpers                                         */
/*****************************************************************************/

struct DrawObj *ilusMakeDrawObj (struct ClassBase *cb, Object *o,
                                 struct localData *ld, UWORD type)
{
    struct DrawObj *obj;

    (void) cb;
    (void) o;

    obj = (struct DrawObj *) AllocPooled (ld->ld_Pool,
                                          sizeof (struct DrawObj));
    if (!obj) return NULL;

    obj->do_Type  = type;
    obj->do_Pad0  = 0;
    obj->do_Flags = 0;
    obj->do_XMin  = (LONG) 0x7FFFFFFFL;
    obj->do_YMin  = (LONG) 0x7FFFFFFFL;
    obj->do_XMax  = (LONG) 0x80000000L;
    obj->do_YMax  = (LONG) 0x80000000L;
    obj->do_Attrs.da_FillType    = DRFT_NONE;
    obj->do_Attrs.da_JoinType    = DRJT_MITER;
    obj->do_Attrs.da_CapType     = DRCT_BUTT;
    obj->do_Attrs.da_EdgePattern = 0;
    obj->do_Attrs.da_FillColor   = DDTA_TRANSPARENT;
    obj->do_Attrs.da_EdgeColor   = DDTA_TRANSPARENT;
    obj->do_Attrs.da_LayerID     = 0;
    obj->do_Attrs.da_ArrowHeads  = 0;
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


LONG ilusPushChild (struct ClassBase *cb, Object *o,
                    struct localData *ld, struct DrawObj *obj)
{
    struct drwAddObject addMsg;

    (void) cb;
    (void) ld;

    if (!obj) return IFFERR_NOMEM;

    addMsg.MethodID = DDTM_ADDOBJECT;
    addMsg.drwa_Obj = obj;
    DoMethodA (o, (Msg) &addMsg);
    return 0;
}


LONG ilusBeginGroup (struct ClassBase *cb, Object *o,
                     struct localData *ld,
                     UWORD type, ULONG expect, struct DrawObj *owner)
{
    struct drwBeginGroup beg;

    (void) cb;

    if (ld->ld_StackDepth >= ILUS_MAX_STACK) return IFFERR_MANGLED;

    ld->ld_Stack[ld->ld_StackDepth].ig_Type  = type;
    ld->ld_Stack[ld->ld_StackDepth].ig_Pad   = 0;
    ld->ld_Stack[ld->ld_StackDepth].ig_Owner = owner;
    ld->ld_StackDepth++;

    beg.MethodID          = DDTM_BEGINGROUP;
    beg.drwbg_Type        = type;
    beg.drwbg_Pad         = 0;
    beg.drwbg_ExpectCount = expect;
    beg.drwbg_FillID      = 0;
    DoMethodA (o, (Msg) &beg);
    return 0;
}


LONG ilusEndGroup (struct ClassBase *cb, Object *o,
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


/*****************************************************************************/
/* applyAttr -- combine the OB filled/stroked flags with the decoded AT      */
/*              colours and write them into a DrawObj's attribute record.    */
/*****************************************************************************/

static void applyAttr (struct DrawObj *obj, ULONG obFlags,
                       struct ILUSAttr *at)
{
    if (obFlags & ILUS_OBF_FILLED)
    {
        obj->do_Attrs.da_FillType  = DRFT_COLOR;
        obj->do_Attrs.da_FillColor = at->fillSet ? at->fillARGB
                                                  : 0xFF000000UL;
    }
    else
    {
        obj->do_Attrs.da_FillType  = DRFT_NONE;
        obj->do_Attrs.da_FillColor = DDTA_TRANSPARENT;
    }

    if (obFlags & ILUS_OBF_STROKED)
    {
        obj->do_Attrs.da_EdgePattern = 1;
        obj->do_Attrs.da_EdgeColor   = at->edgeSet ? at->edgeARGB
                                                    : 0xFF000000UL;
        if (at->edgeThickSet && at->edgeThick > 0)
            obj->do_Attrs.da_EdgeThick = at->edgeThick;
    }
    else
    {
        obj->do_Attrs.da_EdgePattern = 0;
        obj->do_Attrs.da_EdgeColor   = DDTA_TRANSPARENT;
    }
}


/*****************************************************************************/
/* setObjBBox -- record an object's bounding box (in COORDs) into 16.16      */
/*               Fixed, and fold it into the running canvas union.           */
/*****************************************************************************/

static void setObjBBox (struct localData *ld, struct DrawObj *obj,
                        LONG x1, LONG y1, LONG x2, LONG y2)
{
    LONG t;

    if (x2 < x1) { t = x1; x1 = x2; x2 = t; }
    if (y2 < y1) { t = y1; y1 = y2; y2 = t; }

    obj->do_XMin = ilusCoordToFixed (x1);
    obj->do_YMin = ilusCoordToFixed (y1);
    obj->do_XMax = ilusCoordToFixed (x2);
    obj->do_YMax = ilusCoordToFixed (y2);

    if (!ld->ld_HaveBBox)
    {
        ld->ld_BBoxX1 = x1; ld->ld_BBoxY1 = y1;
        ld->ld_BBoxX2 = x2; ld->ld_BBoxY2 = y2;
        ld->ld_HaveBBox = TRUE;
    }
    else
    {
        if (x1 < ld->ld_BBoxX1) ld->ld_BBoxX1 = x1;
        if (y1 < ld->ld_BBoxY1) ld->ld_BBoxY1 = y1;
        if (x2 > ld->ld_BBoxX2) ld->ld_BBoxX2 = x2;
        if (y2 > ld->ld_BBoxY2) ld->ld_BBoxY2 = y2;
    }
}


/*****************************************************************************/
/* buildRect / buildLine / buildCircle -- geometry for the primitive punks   */
/*                                                                           */
/* BX (box), RB (rounded box) and CO (text column) all reduce to an absolute */
/* four-corner closed polygon; the corner radius of RB is approximated as a  */
/* plain rectangle.  LN is a two-point polyline.  CI is a circle / ellipse   */
/* emitted as four cubic beziers (the renderer flattens beziers smoothly).   */
/* All coordinates here are absolute COORDs straight from the punk.          */
/*****************************************************************************/

static LONG buildRect (struct ClassBase *cb, struct localData *ld,
                       struct DrawObj *obj,
                       LONG x1, LONG y1, LONG x2, LONG y2)
{
    struct DrawPoint *dp;

    (void) cb;

    dp = (struct DrawPoint *) AllocPooled (ld->ld_Pool,
                                           4 * sizeof (struct DrawPoint));
    if (!dp) return IFFERR_NOMEM;

    dp[0].dp_X = ilusCoordToFixed (x1); dp[0].dp_Y = ilusCoordToFixed (y1);
    dp[0].dp_Flags = DRPF_MOVETO;
    dp[1].dp_X = ilusCoordToFixed (x2); dp[1].dp_Y = ilusCoordToFixed (y1);
    dp[1].dp_Flags = DRPF_NORMAL;
    dp[2].dp_X = ilusCoordToFixed (x2); dp[2].dp_Y = ilusCoordToFixed (y2);
    dp[2].dp_Flags = DRPF_NORMAL;
    dp[3].dp_X = ilusCoordToFixed (x1); dp[3].dp_Y = ilusCoordToFixed (y2);
    dp[3].dp_Flags = DRPF_CLOSE | DRPF_END;

    obj->do_Data.Poly.do_Points    = dp;
    obj->do_Data.Poly.do_NumPoints = 4;
    return 0;
}


static LONG buildLine (struct ClassBase *cb, struct localData *ld,
                       struct DrawObj *obj,
                       LONG x1, LONG y1, LONG x2, LONG y2)
{
    struct DrawPoint *dp;

    (void) cb;

    dp = (struct DrawPoint *) AllocPooled (ld->ld_Pool,
                                           2 * sizeof (struct DrawPoint));
    if (!dp) return IFFERR_NOMEM;

    dp[0].dp_X = ilusCoordToFixed (x1); dp[0].dp_Y = ilusCoordToFixed (y1);
    dp[0].dp_Flags = DRPF_MOVETO;
    dp[1].dp_X = ilusCoordToFixed (x2); dp[1].dp_Y = ilusCoordToFixed (y2);
    dp[1].dp_Flags = DRPF_END;

    obj->do_Data.Poly.do_Points    = dp;
    obj->do_Data.Poly.do_NumPoints = 2;
    return 0;
}


static LONG buildCircle (struct ClassBase *cb, struct localData *ld,
                         struct DrawObj *obj,
                         LONG cx, LONG cy, LONG rx, LONG ry)
{
    struct DrawPoint *dp;
    LONG kx, ky;

    (void) cb;

    dp = (struct DrawPoint *) AllocPooled (ld->ld_Pool,
                                           13 * sizeof (struct DrawPoint));
    if (!dp) return IFFERR_NOMEM;

    if (rx < 0) rx = -rx;
    if (ry < 0) ry = -ry;
    kx = ilusMulDiv (rx, ILUS_BEZ_KAPPA, 65536L);
    ky = ilusMulDiv (ry, ILUS_BEZ_KAPPA, 65536L);

    /* Start at the right-hand quadrant point and sweep clockwise through    *
     * four cubic beziers; each bezier is two control points (DRPF_BEZIER)   *
     * followed by its endpoint, the convention the renderer flattens.       */
    dp[0].dp_X = ilusCoordToFixed (cx + rx); dp[0].dp_Y = ilusCoordToFixed (cy);
    dp[0].dp_Flags = DRPF_MOVETO;

    dp[1].dp_X = ilusCoordToFixed (cx + rx); dp[1].dp_Y = ilusCoordToFixed (cy + ky);
    dp[1].dp_Flags = DRPF_BEZIER;
    dp[2].dp_X = ilusCoordToFixed (cx + kx); dp[2].dp_Y = ilusCoordToFixed (cy + ry);
    dp[2].dp_Flags = DRPF_BEZIER;
    dp[3].dp_X = ilusCoordToFixed (cx);      dp[3].dp_Y = ilusCoordToFixed (cy + ry);
    dp[3].dp_Flags = DRPF_NORMAL;

    dp[4].dp_X = ilusCoordToFixed (cx - kx); dp[4].dp_Y = ilusCoordToFixed (cy + ry);
    dp[4].dp_Flags = DRPF_BEZIER;
    dp[5].dp_X = ilusCoordToFixed (cx - rx); dp[5].dp_Y = ilusCoordToFixed (cy + ky);
    dp[5].dp_Flags = DRPF_BEZIER;
    dp[6].dp_X = ilusCoordToFixed (cx - rx); dp[6].dp_Y = ilusCoordToFixed (cy);
    dp[6].dp_Flags = DRPF_NORMAL;

    dp[7].dp_X = ilusCoordToFixed (cx - rx); dp[7].dp_Y = ilusCoordToFixed (cy - ky);
    dp[7].dp_Flags = DRPF_BEZIER;
    dp[8].dp_X = ilusCoordToFixed (cx - kx); dp[8].dp_Y = ilusCoordToFixed (cy - ry);
    dp[8].dp_Flags = DRPF_BEZIER;
    dp[9].dp_X = ilusCoordToFixed (cx);      dp[9].dp_Y = ilusCoordToFixed (cy - ry);
    dp[9].dp_Flags = DRPF_NORMAL;

    dp[10].dp_X = ilusCoordToFixed (cx + kx); dp[10].dp_Y = ilusCoordToFixed (cy - ry);
    dp[10].dp_Flags = DRPF_BEZIER;
    dp[11].dp_X = ilusCoordToFixed (cx + rx); dp[11].dp_Y = ilusCoordToFixed (cy - ky);
    dp[11].dp_Flags = DRPF_BEZIER;
    dp[12].dp_X = ilusCoordToFixed (cx + rx); dp[12].dp_Y = ilusCoordToFixed (cy);
    dp[12].dp_Flags = DRPF_CLOSE | DRPF_END;

    obj->do_Data.Poly.do_Points    = dp;
    obj->do_Data.Poly.do_NumPoints = 13;
    return 0;
}


/*****************************************************************************/
/* countPDPoints -- size the DrawPoint array a PD path needs                 */
/*****************************************************************************/

static ULONG countPDPoints (const UBYTE *pd, LONG len)
{
    LONG  pos = 0;
    ULONG n = 0;
    UWORD cmd;

    while (pos + 2 <= len)
    {
        cmd = (UWORD) (be16 (pd + pos) & ILUS_PD_TYPEMASK);
        pos += 2;
        switch (cmd)
        {
            case ILUS_PD_MOVETO:
            case ILUS_PD_LINETO:
                pos += 8;  n += 1;  break;
            case ILUS_PD_CURVETO:
                pos += 24; n += 3;  break;
            case ILUS_PD_CLOSEPATH:
                break;
            default:
                /* Unknown segment: stop, the remaining bytes are unsafe. */
                return n;
        }
    }
    return n;
}


/******************************************************************************/
/* buildPath -- transform a PD local path through its PA placement box        */
/*                                                                            */
/* PD coordinates live in the object's "original" space (0..OrigWidth,        */
/* 0..OrigHeight).  PA carries the current placement box (X1,Y1)-(X2,Y2) and  */
/* the original width / height, so each PD point maps as                      */
/*     X = X1 + px * (X2 - X1) / OrigWidth                                    */
/*     Y = Y1 + py * (Y2 - Y1) / OrigHeight                                   */
/* (a translate + independent X/Y scale).  Several objects can share an       */
/* identical PD while differing only in their PA box.                         */
/******************************************************************************/

static LONG buildPath (struct ClassBase *cb, struct localData *ld,
                       struct DrawObj *obj, BOOL filled,
                       const UBYTE *pa, LONG paLen,
                       const UBYTE *pd, LONG pdLen)
{
    struct DrawPoint *dp;
    ULONG  count;
    LONG   x1, y1, x2, y2, ow, oh;
    LONG   dx, dy;
    LONG   pos, out;
    UWORD  cmd;
    LONG   px, py;
    int    k;
    BOOL   subClosed;

    (void) cb;

    if (paLen >= 26)
    {
        x1 = (LONG) be32 (pa +  2);
        y1 = (LONG) be32 (pa +  6);
        x2 = (LONG) be32 (pa + 10);
        y2 = (LONG) be32 (pa + 14);
        ow = (LONG) be32 (pa + 18);
        oh = (LONG) be32 (pa + 22);
    }
    else
    {
        /* No PA placement: fall back to an identity transform so the PD
         * local coordinates are used as-is. */
        x1 = 0; y1 = 0; x2 = 0; y2 = 0; ow = 0; oh = 0;
    }
    dx = x2 - x1;
    dy = y2 - y1;
    if (ow == 0) { ow = 1; dx = (dx == 0) ? 1 : dx; }
    if (oh == 0) { oh = 1; dy = (dy == 0) ? 1 : dy; }
    if (paLen < 26) { x1 = 0; y1 = 0; dx = ow; dy = oh; }

    count = countPDPoints (pd, pdLen);
    if (count == 0)
    {
        obj->do_Data.Poly.do_Points    = NULL;
        obj->do_Data.Poly.do_NumPoints = 0;
        return 0;
    }

    dp = (struct DrawPoint *) AllocPooled (ld->ld_Pool,
                                  (ULONG) count * sizeof (struct DrawPoint));
    if (!dp) return IFFERR_NOMEM;

    pos = 0;
    out = 0;
    subClosed = FALSE;

    while (pos + 2 <= pdLen && (ULONG) out < count)
    {
        cmd = (UWORD) (be16 (pd + pos) & ILUS_PD_TYPEMASK);
        pos += 2;

        if (cmd == ILUS_PD_MOVETO || cmd == ILUS_PD_LINETO)
        {
            if (pos + 8 > pdLen) break;
            px = (LONG) be32 (pd + pos);
            py = (LONG) be32 (pd + pos + 4);
            pos += 8;

            if (cmd == ILUS_PD_MOVETO && out > 0 && !subClosed)
                dp[out - 1].dp_Flags |= DRPF_END;

            dp[out].dp_X = ilusCoordToFixed (x1 + ilusMulDiv (px, dx, ow));
            dp[out].dp_Y = ilusCoordToFixed (y1 + ilusMulDiv (py, dy, oh));
            dp[out].dp_Flags = (cmd == ILUS_PD_MOVETO || out == 0)
                                 ? DRPF_MOVETO : DRPF_NORMAL;
            out++;
            subClosed = FALSE;
        }
        else if (cmd == ILUS_PD_CURVETO)
        {
            if (pos + 24 > pdLen || (ULONG) out + 3 > count) break;
            for (k = 0; k < 3; k++)
            {
                px = (LONG) be32 (pd + pos + k * 8);
                py = (LONG) be32 (pd + pos + k * 8 + 4);
                dp[out].dp_X = ilusCoordToFixed (x1 + ilusMulDiv (px, dx, ow));
                dp[out].dp_Y = ilusCoordToFixed (y1 + ilusMulDiv (py, dy, oh));
                dp[out].dp_Flags = (k < 2) ? DRPF_BEZIER : DRPF_NORMAL;
                out++;
            }
            pos += 24;
            subClosed = FALSE;
        }
        else if (cmd == ILUS_PD_CLOSEPATH)
        {
            if (out > 0)
            {
                dp[out - 1].dp_Flags |= (DRPF_CLOSE | DRPF_END);
                subClosed = TRUE;
            }
        }
        else
        {
            break;      /* unknown segment type; stop safely */
        }
    }

    if (out > 0 && !(dp[out - 1].dp_Flags & (DRPF_END | DRPF_CLOSE)))
    {
        if (filled)
            dp[out - 1].dp_Flags |= (DRPF_CLOSE | DRPF_END);
        else
            dp[out - 1].dp_Flags |= DRPF_END;
    }

    obj->do_Data.Poly.do_Points    = dp;
    obj->do_Data.Poly.do_NumPoints = (ULONG) out;
    return 0;
}


/*****************************************************************************/
/* parseIOBJ -- turn one IOBJ chunk's punk stream into a DrawObj             */
/*                                                                           */
/* The chunk has already been read into `buf`.  We locate the OB header, the */
/* optional AT attribute-data, and the geometry punk named by OB.Type, then  */
/* build and push the matching DrawObj.  Group objects (GR/DR) are pushed    */
/* and remembered in ld_GroupForWrap so the following FORM WRAP can open     */
/* them; text objects (TO) are remembered in ld_PendingText so the following */
/* CTXT chunk can supply the displayed string.                               */
/*****************************************************************************/

static LONG parseIOBJ (struct ClassBase *cb, Object *o, struct localData *ld,
                       const UBYTE *buf, LONG len)
{
    LONG   pos;
    UWORD  pid;
    LONG   plen;
    const UBYTE *pd;

    ULONG  obFlags = 0;
    UWORD  obType  = 0;
    LONG   bx1 = 0, by1 = 0, bx2 = 0, by2 = 0;
    BOOL   haveOB = FALSE;

    const UBYTE *atPtr = NULL;   LONG atLen = 0;
    const UBYTE *paPtr = NULL;   LONG paLen = 0;
    const UBYTE *pdPtr = NULL;   LONG pdLen = 0;
    const UBYTE *shPtr = NULL;   LONG shLen = 0;

    struct ILUSAttr at;
    struct DrawObj *obj;
    BOOL   filled;
    LONG   err;
    LONG   sx1, sy1, sx2, sy2;
    LONG   charH;

    /* ----- scan the punks --------------------------------------------- */
    pos = 0;
    while (pos + 4 <= len)
    {
        pid  = be16 (buf + pos);
        plen = (LONG) be16 (buf + pos + 2);
        pd   = buf + pos + 4;
        if (plen > len - (pos + 4)) plen = len - (pos + 4);

        switch (pid)
        {
            case ILUS_PUNK_OB:
                if (plen >= 22)
                {
                    obFlags = be32 (pd);
                    obType  = be16 (pd + 4);
                    bx1 = (LONG) be32 (pd +  6);
                    by1 = (LONG) be32 (pd + 10);
                    bx2 = (LONG) be32 (pd + 14);
                    by2 = (LONG) be32 (pd + 18);
                    haveOB = TRUE;
                }
                break;

            case ILUS_PUNK_AT:
                atPtr = pd;  atLen = plen;
                break;

            case ILUS_PUNK_PA:
                paPtr = pd;  paLen = plen;
                break;

            case ILUS_PUNK_PD:
                pdPtr = pd;  pdLen = plen;
                break;

            case ILUS_PUNK_BX:
            case ILUS_PUNK_RB:
            case ILUS_PUNK_CO:
            case ILUS_PUNK_LN:
            case ILUS_PUNK_CI:
            case ILUS_PUNK_TO:
            case ILUS_PUNK_GR:
            case ILUS_PUNK_DR:
            case ILUS_PUNK_PI:
            case ILUS_PUNK_PS:
                shPtr = pd;  shLen = plen;
                break;

            default:
                /* RT, FR, TC, TW and any unknown punk: ignored for now. */
                break;
        }
        pos += 4 + plen + (plen & 1);
    }

    if (!haveOB)
        return 0;        /* nothing usable in this chunk */

    /* ----- decode attributes ------------------------------------------ */
    at.fillARGB = 0xFF000000UL;
    at.edgeARGB = 0xFF000000UL;
    at.edgeThick = DRW_FIXED_ONE;
    at.fillSet = FALSE;
    at.edgeSet = FALSE;
    at.edgeThickSet = FALSE;
    if (atPtr)
        ilusDecodeAttr (ld, atPtr, atLen, ILUS_CTX_NONE, &at);

    filled = (obFlags & ILUS_OBF_FILLED) ? TRUE : FALSE;

    /* Any non-group object clears a group pointer left waiting for a WRAP. *
     * The GR/DR case below sets it again for the group it creates.         */
    ld->ld_GroupForWrap = NULL;

    /* ----- build geometry per object type ----------------------------- */
    switch (obType)
    {
        case ILUS_PUNK_GR:
        case ILUS_PUNK_DR:
            /* A group: create the container, push it at the current level *
             * and remember it for the FORM WRAP that carries its children. */
            obj = ilusMakeDrawObj (cb, o, ld, DROT_GROUP);
            if (!obj) return IFFERR_NOMEM;
            obj->do_Data.Group.do_NumChildren = 0;
            NewList (&obj->do_Data.Group.do_Children);
            setObjBBox (ld, obj, bx1, by1, bx2, by2);
            err = ilusPushChild (cb, o, ld, obj);
            if (err) return err;
            ld->ld_GroupForWrap = obj;
            ld->ld_PendingText  = NULL;
            return 0;

        case ILUS_PUNK_TO:
            /* A text object: the displayed string arrives in a sibling
             * CTXT chunk, so create the DROT_TEXT now and remember it. */
            obj = ilusMakeDrawObj (cb, o, ld, DROT_TEXT);
            if (!obj) return IFFERR_NOMEM;
            setObjBBox (ld, obj, bx1, by1, bx2, by2);
            applyAttr (obj, obFlags | ILUS_OBF_FILLED, &at);
            obj->do_Attrs.da_FillType  = DRFT_COLOR;
            obj->do_Attrs.da_FillColor = at.fillSet ? at.fillARGB
                                                    : 0xFF000000UL;

            charH = obj->do_YMax - obj->do_YMin;
            if (charH < (4L << 16)) charH = (12L << 16);
            obj->do_Data.Text.do_BaseX     = obj->do_XMin;
            obj->do_Data.Text.do_BaseY     = obj->do_YMax;
            obj->do_Data.Text.do_CharW     = 0;
            obj->do_Data.Text.do_CharH     = charH;
            obj->do_Data.Text.do_Rotation  = 0;
            obj->do_Data.Text.do_String    = NULL;
            obj->do_Data.Text.do_NumChars  = 0;
            obj->do_Data.Text.do_WhichFont = 0;
            obj->do_Data.Text.do_Runs      = NULL;
            obj->do_Data.Text.do_NumRuns   = 0;
            obj->do_Data.Text.do_TextPad   = 0;

            err = ilusPushChild (cb, o, ld, obj);
            if (err) return err;
            ld->ld_PendingText = obj;
            return 0;

        case ILUS_PUNK_PA:
            obj = ilusMakeDrawObj (cb, o, ld,
                                   filled ? DROT_POLYGON : DROT_POLYLINE);
            if (!obj) return IFFERR_NOMEM;
            setObjBBox (ld, obj, bx1, by1, bx2, by2);
            applyAttr (obj, obFlags, &at);
            if (pdPtr)
            {
                err = buildPath (cb, ld, obj, filled, paPtr, paLen,
                                 pdPtr, pdLen);
                if (err) return err;
            }
            else
            {
                obj->do_Data.Poly.do_Points    = NULL;
                obj->do_Data.Poly.do_NumPoints = 0;
            }
            ld->ld_PendingText = NULL;
            return ilusPushChild (cb, o, ld, obj);

        case ILUS_PUNK_BX:
        case ILUS_PUNK_RB:
        case ILUS_PUNK_CO:
            obj = ilusMakeDrawObj (cb, o, ld, DROT_POLYGON);
            if (!obj) return IFFERR_NOMEM;
            setObjBBox (ld, obj, bx1, by1, bx2, by2);
            applyAttr (obj, obFlags, &at);
            if (shPtr && shLen >= 18)
            {
                sx1 = (LONG) be32 (shPtr +  2);
                sy1 = (LONG) be32 (shPtr +  6);
                sx2 = (LONG) be32 (shPtr + 10);
                sy2 = (LONG) be32 (shPtr + 14);
            }
            else
            {
                sx1 = bx1; sy1 = by1; sx2 = bx2; sy2 = by2;
            }
            err = buildRect (cb, ld, obj, sx1, sy1, sx2, sy2);
            if (err) return err;
            ld->ld_PendingText = NULL;
            return ilusPushChild (cb, o, ld, obj);

        case ILUS_PUNK_LN:
            obj = ilusMakeDrawObj (cb, o, ld, DROT_POLYLINE);
            if (!obj) return IFFERR_NOMEM;
            setObjBBox (ld, obj, bx1, by1, bx2, by2);
            applyAttr (obj, obFlags | ILUS_OBF_STROKED, &at);
            if (shPtr && shLen >= 18)
            {
                sx1 = (LONG) be32 (shPtr +  2);
                sy1 = (LONG) be32 (shPtr +  6);
                sx2 = (LONG) be32 (shPtr + 10);
                sy2 = (LONG) be32 (shPtr + 14);
            }
            else
            {
                sx1 = bx1; sy1 = by1; sx2 = bx2; sy2 = by2;
            }
            err = buildLine (cb, ld, obj, sx1, sy1, sx2, sy2);
            if (err) return err;
            ld->ld_PendingText = NULL;
            return ilusPushChild (cb, o, ld, obj);

        case ILUS_PUNK_CI:
            obj = ilusMakeDrawObj (cb, o, ld,
                                   filled ? DROT_POLYGON : DROT_POLYLINE);
            if (!obj) return IFFERR_NOMEM;
            setObjBBox (ld, obj, bx1, by1, bx2, by2);
            applyAttr (obj, obFlags, &at);
            if (shPtr && shLen >= 18)
            {
                /* CI = UWORD flags, COORD Centre.x, Centre.y, XRadius, YRadius */
                sx1 = (LONG) be32 (shPtr +  2);   /* centre x */
                sy1 = (LONG) be32 (shPtr +  6);   /* centre y */
                sx2 = (LONG) be32 (shPtr + 10);   /* x radius */
                sy2 = (LONG) be32 (shPtr + 14);   /* y radius */
                err = buildCircle (cb, ld, obj, sx1, sy1, sx2, sy2);
            }
            else
            {
                err = buildRect (cb, ld, obj, bx1, by1, bx2, by2);
            }
            if (err) return err;
            ld->ld_PendingText = NULL;
            return ilusPushChild (cb, o, ld, obj);

        case ILUS_PUNK_PI:
        case ILUS_PUNK_PS:
            /* Placed raster (ILBM) / EPS.  Decoding the embedded image is   *
             * out of scope for this loader, so draw the placement rectangle *
             * as an unfilled frame to mark where the artwork sits.          */
            obj = ilusMakeDrawObj (cb, o, ld, DROT_POLYGON);
            if (!obj) return IFFERR_NOMEM;
            setObjBBox (ld, obj, bx1, by1, bx2, by2);
            obj->do_Attrs.da_FillType    = DRFT_NONE;
            obj->do_Attrs.da_FillColor   = DDTA_TRANSPARENT;
            obj->do_Attrs.da_EdgePattern = 1;
            obj->do_Attrs.da_EdgeColor   = 0xFF000000UL;
            err = buildRect (cb, ld, obj, bx1, by1, bx2, by2);
            if (err) return err;
            ld->ld_PendingText = NULL;
            return ilusPushChild (cb, o, ld, obj);

        default:
            /* Unknown object type: keep its box folded into the canvas so   *
             * the scroller geometry still covers it, but draw nothing.      */
            {
                struct DrawObj tmp;
                setObjBBox (ld, &tmp, bx1, by1, bx2, by2);
            }
            ld->ld_PendingText = NULL;
            return 0;
    }
}


/******************************************************************************/
/* fillPendingText -- copy the displayable glyphs out of a CTXT chunk         */
/*                                                                            */
/* The CTXT byte stream interleaves displayable characters (any non-zero      */
/* byte) with formatting commands (a zero marker, then VNum command / flags / */
/* length and that many bytes of command data - e.g. the TEXT_TYPEFAMILY      */
/* font-name string).  We keep the glyphs and skip the commands, which gives  */
/* the visible label text for the pending DROT_TEXT object.                   */
/******************************************************************************/

static LONG fillPendingText (struct ClassBase *cb, struct localData *ld,
                             const UBYTE *buf, LONG len)
{
    STRPTR out;
    LONG   pos, n;
    ULONG  dlen;

    (void) cb;

    if (!ld->ld_PendingText) return 0;
    if (len <= 0) return 0;

    out = (STRPTR) AllocPooled (ld->ld_Pool, (ULONG) (len + 1));
    if (!out) return IFFERR_NOMEM;

    pos = 0;
    n   = 0;
    while (pos < len)
    {
        if (buf[pos] == 0)
        {
            pos++;
            if (pos >= len) break;
            (void) ilusVNum (buf, len, &pos);    /* command number */
            (void) ilusVNum (buf, len, &pos);    /* command flags  */
            dlen = ilusVNum (buf, len, &pos);    /* data length    */
            pos += (LONG) dlen;
        }
        else
        {
            out[n++] = (char) buf[pos++];
        }
    }
    out[n] = '\0';

    ld->ld_PendingText->do_Data.Text.do_String   = out;
    ld->ld_PendingText->do_Data.Text.do_NumChars = (UWORD) n;
    ld->ld_PendingText = NULL;
    return 0;
}


/******************************************************************************/
/* parseCTAG -- build the named colour table from the CTAG chunk              */
/*                                                                            */
/* CTAG holds a series of TG punks: UWORD Type, UWORD Flags, then three       */
/* VSTRs (UWORD length + bytes) - the tag name, a "next" name and the tag     */
/* data.  For a colour tag (Type == CTAG_COLOR) the tag data is a COLOR_STYLE */
/* attribute-data command whose CMYK fields give the colour.  We cache the    */
/* name -> ARGB mapping and forward the RGB to the root class so DDTA_Color*  */
/* queries still work.                                                        */
/******************************************************************************/

static LONG parseCTAG (struct ClassBase *cb, Object *o, struct localData *ld,
                       const UBYTE *buf, LONG len)
{
    LONG   pos, q;
    UWORD  pid;
    LONG   plen;
    const UBYTE *pd;
    UWORD  type;
    LONG   nameLen, nextLen, dataLen;
    const UBYTE *namePtr;
    const UBYTE *dataPtr;
    struct ILUSAttr res;
    struct drwAddColor addMsg;
    ULONG  argb;
    LONG   i;

    pos = 0;
    while (pos + 4 <= len)
    {
        pid  = be16 (buf + pos);
        plen = (LONG) be16 (buf + pos + 2);
        pd   = buf + pos + 4;
        if (plen > len - (pos + 4)) plen = len - (pos + 4);

        if (pid == ILUS_PUNK_TG && plen >= 4)
        {
            type = be16 (pd);
            q = 4;                                   /* skip Type + Flags    */

            /* VSTR TagName */
            if (q + 2 > plen) goto next;
            nameLen = (LONG) be16 (pd + q); q += 2;
            namePtr = pd + q;
            if (nameLen > plen - q) nameLen = plen - q;
            q += nameLen;

            /* VSTR NextTagName (skipped) */
            if (q + 2 > plen) goto next;
            nextLen = (LONG) be16 (pd + q); q += 2;
            if (nextLen > plen - q) nextLen = plen - q;
            q += nextLen;

            /* VSTR TagData */
            if (q + 2 > plen) goto next;
            dataLen = (LONG) be16 (pd + q); q += 2;
            dataPtr = pd + q;
            if (dataLen > plen - q) dataLen = plen - q;

            if (type == ILUS_CTAG_COLOR &&
                ld->ld_NumColors < ILUS_MAX_COLORS)
            {
                res.fillARGB = 0xFF000000UL;
                res.edgeARGB = 0xFF000000UL;
                res.edgeThick = DRW_FIXED_ONE;
                res.fillSet = FALSE;
                res.edgeSet = FALSE;
                res.edgeThickSet = FALSE;
                ilusDecodeAttr (ld, dataPtr, dataLen, ILUS_CTX_FILL, &res);
                argb = res.fillSet ? res.fillARGB : 0xFF000000UL;

                for (i = 0; i < nameLen && i < ILUS_NAME_LEN - 1; i++)
                    ld->ld_Colors[ld->ld_NumColors].ic_Name[i] =
                        namePtr[i];
                ld->ld_Colors[ld->ld_NumColors].ic_Name[i] = '\0';
                ld->ld_Colors[ld->ld_NumColors].ic_ARGB = argb;
                ld->ld_NumColors++;

                addMsg.MethodID = DDTM_ADDCOLOR;
                addMsg.drwac_R  = (UBYTE) ((argb >> 16) & 0xFF);
                addMsg.drwac_G  = (UBYTE) ((argb >>  8) & 0xFF);
                addMsg.drwac_B  = (UBYTE) ( argb        & 0xFF);
                addMsg.drwac_Pad = 0;
                DoMethodA (o, (Msg) &addMsg);
            }
        }
next:
        pos += 4 + plen + (plen & 1);
    }

    dbgPutLong (cb, "CTAG colours cached=", (LONG) ld->ld_NumColors);
    return 0;
}


/******************************************************************************/
/* ilusParseContent -- walk the FORM ILUS tree, building the display list     */
/*                                                                            */
/* IFFPARSE_STEP visits every chunk and every context boundary, which is the  */
/* natural fit for ILUS's deep nesting (FORM IOBJ holding an IOBJ plus a FORM */
/* WRAP of children, FORM CTXT holding the text of a text object, ...).  A    */
/* small context stack records the type of every container we are inside so   */
/* that entering a WRAP opens the group its preceding IOBJ created and the    */
/* matching end-of-context closes it.                                         */
/******************************************************************************/

LONG ilusParseContent (struct ClassBase *cb, Object *o,
                       struct localData *ld,
                       struct IFFHandle *iff,
                       struct List *displayList,
                       ULONG *abortPtr, ULONG *progressPtr)
{
    struct ContextNode *cn;
    UBYTE  *buf;
    LONG    err;
    LONG    size;
    LONG    got;
    LONG    iterations = 0;
    BOOL    done = FALSE;

    (void) displayList;
    (void) progressPtr;

    while (!done)
    {
        if (abortPtr && *abortPtr) return -1;

        iterations++;
        if (iterations > 2000000L)
        {
            dbgPut (cb, "parse iteration guard tripped");
            return IFFERR_MANGLED;
        }

        err = ParseIFF (iff, IFFPARSE_STEP);

        if (err == IFFERR_EOF)
        {
            done = TRUE;
            break;
        }
        if (err == IFFERR_EOC)
        {
            /* Leaving a container: pop our context stack and, if that
             * container was a WRAP we had opened as a group, close it. */
            if (ld->ld_CtxDepth > 0)
            {
                ld->ld_CtxDepth--;
                if (ld->ld_Ctx[ld->ld_CtxDepth].ix_OpenedGroup)
                    ilusEndGroup (cb, o, ld);
            }
            continue;
        }
        if (err != 0)
        {
            dbgPutLong (cb, "ParseIFF fatal err=", err);
            return err;
        }

        cn = CurrentChunk (iff);
        if (!cn) continue;

        if (cn->cn_ID == ID_FORM || cn->cn_ID == ID_LIST ||
            cn->cn_ID == ID_CAT  || cn->cn_ID == ID_PROP)
        {
            /* Entered a container.  Record its type; if it is a WRAP and a
             * group is waiting, open the group so the children attach. */
            BOOL opened = FALSE;

            if (cn->cn_Type == ILUS_ID_WRAP && ld->ld_GroupForWrap)
            {
                ilusBeginGroup (cb, o, ld, DROT_GROUP, 0,
                                ld->ld_GroupForWrap);
                ld->ld_GroupForWrap = NULL;
                opened = TRUE;
            }

            if (ld->ld_CtxDepth < ILUS_MAX_STACK)
            {
                ld->ld_Ctx[ld->ld_CtxDepth].ix_Type        = cn->cn_Type;
                ld->ld_Ctx[ld->ld_CtxDepth].ix_OpenedGroup = opened;
                ld->ld_CtxDepth++;
            }
            continue;
        }

        /* A leaf data chunk. */
        size = (LONG) cn->cn_Size;

        switch (cn->cn_ID)
        {
            case ILUS_ID_CTAG:
            case ILUS_ID_IOBJ:
            case ILUS_ID_CTXT:
                if (size <= 0 || size > 4L * 1024L * 1024L)
                    break;

                buf = (UBYTE *) AllocVec ((ULONG) size, MEMF_PUBLIC);
                if (!buf) return IFFERR_NOMEM;
                got = ReadChunkBytes (iff, buf, size);
                if (got != size)
                {
                    FreeVec (buf);
                    break;          /* tolerate a short / odd chunk */
                }

                if (cn->cn_ID == ILUS_ID_CTAG)
                    err = parseCTAG (cb, o, ld, buf, size);
                else if (cn->cn_ID == ILUS_ID_IOBJ)
                    err = parseIOBJ (cb, o, ld, buf, size);
                else
                    err = fillPendingText (cb, ld, buf, size);

                FreeVec (buf);
                if (err) return err;
                break;

            default:
                /* PGDM, GRID, MRGN, HGUI, VGUI, NAME, ANNO, VERS, embedded
                 * ILBM/EPS data and anything else: skipped (iffparse steps
                 * past the chunk for us on the next ParseIFF). */
                break;
        }
    }

    return 0;
}


/*****************************************************************************/
/* DDTM_LOADHEADERS                                                          */
/*                                                                           */
/* Single-pass load: walk the whole FORM ILUS, build the display list and    */
/* the colour table, then report the canvas extents derived from the union   */
/* of every object's bounding box (ILUS PGDM is routinely all-zero, so the   */
/* geometry union is the reliable canvas).                                   */
/*****************************************************************************/

ULONG loadHeadersMethod (struct ClassBase *cb, Class *cl, Object *o,
                         struct drwLoadHeaders *msg)
{
    struct localData *ld;
    struct IFFHandle *iff;
    LONG  err;

    dbgPut (cb, "DDTM_LOADHEADERS entered");
    if (!msg) return 0;
    ld = (struct localData *) INST_DATA (cl, o);

    iff = NULL;
    GetAttr (DDTA_IFFHandle, o, (ULONG *) &iff);
    if (!iff)
    {
        dbgPut (cb, "no IFF handle from drawing.datatype");
        return 0;
    }
    ld->ld_IFF  = iff;
    ld->ld_Pool = msg->drwlh_Pool;

    err = ilusParseContent (cb, o, ld, iff, NULL, NULL, NULL);
    dbgPutLong (cb, "ilusParseContent err=", err);
    if (err != 0 && err != IFFERR_EOF) return 0;

    if (msg->drwlh_Info && ld->ld_HaveBBox)
    {
        msg->drwlh_Info->di_Left   = ilusCoordToFixed (ld->ld_BBoxX1);
        msg->drwlh_Info->di_Top    = ilusCoordToFixed (ld->ld_BBoxY1);
        msg->drwlh_Info->di_Right  = ilusCoordToFixed (ld->ld_BBoxX2);
        msg->drwlh_Info->di_Bottom = ilusCoordToFixed (ld->ld_BBoxY2);
        msg->drwlh_Info->di_Units  = DRWU_POINTS;
        dbgPutLong (cb, "canvas right fixed=", msg->drwlh_Info->di_Right);
        dbgPutLong (cb, "canvas bottom fixed=", msg->drwlh_Info->di_Bottom);
    }
    else
    {
        dbgPut (cb, "no object bounds captured");
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
