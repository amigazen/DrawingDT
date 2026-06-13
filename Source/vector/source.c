/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* source.c -- direct external file loader for vector.image                  */
/*                                                                           */
/* The native stream is deliberately a simple DR2D-like dump of already-     */
/* resolved display-list data: big-endian 16.16 coordinates, ARGB colours,   */
/* and path point arrays.  It avoids dragging datatypes.library into         */
/* vector.image while still allowing scalable UI assets to live in external  */
/* files. Format parsers such as dr2d.datatype remain above drawing.datatype */
/*****************************************************************************/

#include "classbase.h"

#define VEC_ID_NATIVE MAKE_ID('V','E','C','1')
#define VEC_VERSION   1

struct NativeHeader
{
    ULONG nh_Magic;
    UWORD nh_Version;
    UWORD nh_Flags;
    LONG  nh_Left;
    LONG  nh_Top;
    LONG  nh_Right;
    LONG  nh_Bottom;
    ULONG nh_Units;
    ULONG nh_NumObjects;
};

struct NativeObject
{
    UWORD no_Type;
    UWORD no_Pad;
    ULONG no_Flags;
    UBYTE no_FillType;
    UBYTE no_JoinType;
    UBYTE no_CapType;
    UBYTE no_EdgePattern;
    ULONG no_FillColor;
    ULONG no_EdgeColor;
    UWORD no_LayerID;
    UWORD no_ArrowHeads;
    LONG  no_EdgeThick;
    LONG  no_FillOpacity;
    LONG  no_XMin;
    LONG  no_YMin;
    LONG  no_XMax;
    LONG  no_YMax;
    ULONG no_NumPoints;
};

struct NativePoint
{
    LONG  np_X;
    LONG  np_Y;
    ULONG np_Flags;
};


static LONG readExact (struct ClassBase *cb, BPTR fh, APTR buffer, LONG size)
{
    if (size <= 0) return TRUE;
    return (FRead (fh, buffer, 1, size) == size);
}


static ULONG loadNativeFile (struct ClassBase *cb, Class *cl, Object *o,
                             CONST_STRPTR fileName)
{
    struct localData *lod;
    struct Image *im;
    BPTR fh;
    struct NativeHeader header;
    struct NativeObject rec;
    struct NativePoint point;
    struct DrawObj *obj;
    struct DrawPoint *points;
    struct vecSetBounds boundsMsg;
    struct vecClear clearMsg;
    ULONG i;
    ULONG j;
    ULONG ok;

    lod = INST_DATA (cl, o);
    im = (struct Image *) o;
    fh = 0;
    obj = NULL;
    points = NULL;
    ok = 0;

    if (!fileName) return 0;

    fh = Open ((STRPTR) fileName, MODE_OLDFILE);
    if (!fh) return 0;

    if (!readExact (cb, fh, &header, sizeof (header))) goto done;
    if (header.nh_Magic != VEC_ID_NATIVE) goto done;
    if (header.nh_Version != VEC_VERSION) goto done;

    clearMsg.MethodID = VIMM_CLEAR;
    clearDrawingMethod (cb, cl, o, &clearMsg);

    boundsMsg.MethodID = VIMM_SETBOUNDS;
    boundsMsg.vecsb_Left = header.nh_Left;
    boundsMsg.vecsb_Top = header.nh_Top;
    boundsMsg.vecsb_Right = header.nh_Right;
    boundsMsg.vecsb_Bottom = header.nh_Bottom;
    boundsMsg.vecsb_Units = header.nh_Units;
    setBoundsMethod (cb, cl, o, &boundsMsg);

    for (i = 0; i < header.nh_NumObjects; i++)
    {
        if (!readExact (cb, fh, &rec, sizeof (rec))) goto done;

        obj = poolDrawObj (cb, lod, rec.no_Type);
        if (!obj) goto done;

        obj->do_Flags = rec.no_Flags;
        obj->do_Attrs.da_FillType = rec.no_FillType;
        obj->do_Attrs.da_JoinType = rec.no_JoinType;
        obj->do_Attrs.da_CapType = rec.no_CapType;
        obj->do_Attrs.da_EdgePattern = rec.no_EdgePattern;
        obj->do_Attrs.da_FillColor = rec.no_FillColor;
        obj->do_Attrs.da_EdgeColor = rec.no_EdgeColor;
        obj->do_Attrs.da_LayerID = rec.no_LayerID;
        obj->do_Attrs.da_ArrowHeads = rec.no_ArrowHeads;
        obj->do_Attrs.da_EdgeThick = rec.no_EdgeThick;
        obj->do_Attrs.da_FillOpacity = rec.no_FillOpacity;
        obj->do_XMin = rec.no_XMin;
        obj->do_YMin = rec.no_YMin;
        obj->do_XMax = rec.no_XMax;
        obj->do_YMax = rec.no_YMax;

        if (rec.no_NumPoints > 0)
        {
            points = poolDrawPoints (cb, lod, rec.no_NumPoints);
            if (!points) goto done;

            for (j = 0; j < rec.no_NumPoints; j++)
            {
                if (!readExact (cb, fh, &point, sizeof (point))) goto done;
                points[j].dp_X = point.np_X;
                points[j].dp_Y = point.np_Y;
                points[j].dp_Flags = point.np_Flags;
            }

            obj->do_Data.Poly.do_Points = points;
            obj->do_Data.Poly.do_NumPoints = rec.no_NumPoints;
        }

        if (!appendObject (cb, lod, obj)) goto done;
    }

    recomputeDrawingBounds (lod);
    im->Width = (UWORD) DRW_FIXED_TO_INT (lod->lvi_CanvasRight -
                                          lod->lvi_CanvasLeft);
    im->Height = (UWORD) DRW_FIXED_TO_INT (lod->lvi_CanvasBottom -
                                           lod->lvi_CanvasTop);
    if (im->Width < 1) im->Width = 1;
    if (im->Height < 1) im->Height = 1;
    lod->lvi_Flags |= LVIF_CACHE_DIRTY;
    ok = 1;

done:
    if (fh) Close (fh);
    return ok;
}


ULONG loadSourceMethod (struct ClassBase *cb, Class *cl, Object *o,
                        struct vecLoadSource *msg)
{
    ULONG format;

    if (!msg || !msg->vecls_FileName) return 0;

    format = msg->vecls_Format;
    if (format == VIMSF_NATIVE)
        return loadNativeFile (cb, cl, o, msg->vecls_FileName);

    return 0;
}
