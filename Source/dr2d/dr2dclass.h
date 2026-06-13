/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

#ifndef DATATYPES_DR2DCLASS_H
#define DATATYPES_DR2DCLASS_H
/*
**  dr2dclass.h
**
**  Public interface for dr2d.datatype.  This subclass adds nothing
**  beyond what drawing.datatype already exposes - applications use
**  the standard DDTA_* attributes and DDTM_* methods on the object.
**  The header exists so the subclass library has an "external" name
**  for tools that look for one.
*/

#ifndef DATATYPES_DRAWINGCLASS_H
#include <datatypes/drawingclass.h>
#endif

#define DR2DDTCLASS              "dr2d.datatype"

/* IFF DR2D format identifiers.  Repeated here from <iffp/dr2d.h> so the
 * subclass does not depend on the external example header. */
#define DR2D_ID_DR2D             MAKE_ID ('D','R','2','D')
#define DR2D_ID_DRHD             MAKE_ID ('D','R','H','D')
#define DR2D_ID_PPRF             MAKE_ID ('P','P','R','F')
#define DR2D_ID_DASH             MAKE_ID ('D','A','S','H')
#define DR2D_ID_AROW             MAKE_ID ('A','R','O','W')
#define DR2D_ID_FILL             MAKE_ID ('F','I','L','L')
#define DR2D_ID_LAYR             MAKE_ID ('L','A','Y','R')
#define DR2D_ID_ATTR             MAKE_ID ('A','T','T','R')
#define DR2D_ID_BBOX             MAKE_ID ('B','B','O','X')
#define DR2D_ID_CPLY             MAKE_ID ('C','P','L','Y')
#define DR2D_ID_OPLY             MAKE_ID ('O','P','L','Y')
#define DR2D_ID_GRUP             MAKE_ID ('G','R','U','P')
#define DR2D_ID_XTRN             MAKE_ID ('X','T','R','N')
#define DR2D_ID_TPTH             MAKE_ID ('T','P','T','H')
#define DR2D_ID_STXT             MAKE_ID ('S','T','X','T')
#define DR2D_ID_VBM              MAKE_ID ('V','B','M',' ')
#define DR2D_ID_CMAP             MAKE_ID ('C','M','A','P')
#define DR2D_ID_CMYK             MAKE_ID ('C','M','Y','K')
#define DR2D_ID_CNAM             MAKE_ID ('C','N','A','M')
#define DR2D_ID_FONS             MAKE_ID ('F','O','N','S')

/* GRAD is a ProVector 3 extension to the original Stylus DR2D FORM (it is
 * not in the published 1991 spec, but every PV3 drawing defines a GRAD
 * table).  An ATTR with FillType == DR2D_FT_GRADIENT selects a GRAD entry
 * by GradID in its FillValue field.  The 22-byte (or longer) chunk layout
 * reverse-engineered from the official ProVector sample drawings is:
 *
 *   USHORT GradID;        index referenced by ATTR.FillValue
 *   UBYTE  Sub;           secondary index (unused by us)
 *   UBYTE  Sub2;          ditto
 *   UBYTE  Type;          2 = linear, 3/4/5 = radial / centred variants
 *   UBYTE  Pad;
 *   IEEE   F1;            centre / focus X as a 0..1 fraction of the bbox
 *   IEEE   F2;            centre / focus Y as a 0..1 fraction of the bbox
 *   IEEE   Angle;         linear direction, degrees
 *   USHORT StartColor;    CMAP index of the gradient start colour
 *   USHORT EndColor;      CMAP index of the gradient end colour
 *   ...                   type-specific trailing fields (ignored)
 */
#define DR2D_ID_GRAD             MAKE_ID ('G','R','A','D')

/* DR2D coordinate-array indeterminate-value tag.  When a CPLY/OPLY point
 * has 0xFFFFFFFF as its X coordinate, the value of the Y coordinate
 * is one of DR2D_SPLINE/MOVETO/IND interpreted as a flag, not a
 * coordinate. */
#define DR2D_IND                 0xFFFFFFFFUL
#define DR2D_SPLINE              0x00000001UL
#define DR2D_MOVETO              0x00000002UL

/* DR2D fill types.  Same numeric values used by drawing.datatype DRFT_*. */
#define DR2D_FT_NONE             0
#define DR2D_FT_COLOR            1
#define DR2D_FT_OBJECTS          2
#define DR2D_FT_GRADIENT         3   /* ProVector 3 extension; selects GRAD  */

/* DR2D join types.  Same numeric values used by drawing.datatype DRJT_*. */
#define DR2D_JT_NONE             0
#define DR2D_JT_MITER            1
#define DR2D_JT_BEVEL            2
#define DR2D_JT_ROUND            3

/* DR2D layer flags.  Same numeric values used by drawing.datatype DRLF_*. */
#define DR2D_LAYR_ACTIVE         0x01
#define DR2D_LAYR_DISPLAYED      0x02

#endif /* DATATYPES_DR2DCLASS_H */
