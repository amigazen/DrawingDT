/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

#ifndef DATATYPES_ILUSCLASS_H
#define DATATYPES_ILUSCLASS_H
/*
**  ilusclass.h
**
**  Public interface for ilus.datatype.  This subclass adds nothing
**  beyond what drawing.datatype already exposes - applications use
**  the standard DDTA_* attributes and DDTM_* methods on the object.
**  The header exists so the subclass library has an "external" name
**  for tools that look for one, and it gathers the IFF ILUS format
**  constants the loader needs in one place.
**
**  IFF ILUS is the "structured illustration" FORM shipped by Soft-Logik
**  PageStream and the ProVector / PageLiner family.  Unlike IFF DR2D it
**  stores no CMAP and no IEEE floats: spatial values are 32-bit signed
**  COORDs in units of 1/1828800 inch (= 1/25400 point), colours are
**  named tags collected in a CTAG chunk and referenced by name from the
**  per-object attribute-data byte stream, and object geometry is carried
**  in nested 2-character "punks" inside IOBJ chunks.  See the annotated
**  specification in github/DrawingType/examples/ilus.txt.
*/

#ifndef DATATYPES_DRAWINGCLASS_H
#include <datatypes/drawingclass.h>
#endif

#define ILUSDTCLASS              "ilus.datatype"

/*****************************************************************************/
/* IFF FORM and chunk identifiers (4-character)                              */
/*****************************************************************************/

#define ILUS_ID_ILUS             MAKE_ID ('I','L','U','S')
#define ILUS_ID_DOC              MAKE_ID ('D','O','C',' ')
#define ILUS_ID_IOBJ             MAKE_ID ('I','O','B','J')
#define ILUS_ID_WRAP             MAKE_ID ('W','R','A','P')
#define ILUS_ID_CTXT             MAKE_ID ('C','T','X','T')
#define ILUS_ID_PGDM             MAKE_ID ('P','G','D','M')
#define ILUS_ID_GRID             MAKE_ID ('G','R','I','D')
#define ILUS_ID_MRGN             MAKE_ID ('M','R','G','N')
#define ILUS_ID_HGUI             MAKE_ID ('H','G','U','I')
#define ILUS_ID_VGUI             MAKE_ID ('V','G','U','I')
#define ILUS_ID_CTAG             MAKE_ID ('C','T','A','G')
#define ILUS_ID_NAME             MAKE_ID ('N','A','M','E')
#define ILUS_ID_ANNO             MAKE_ID ('A','N','N','O')
#define ILUS_ID_VERS             MAKE_ID ('V','E','R','S')
#define ILUS_ID_ILBM             MAKE_ID ('I','L','B','M')

/*****************************************************************************/
/* Punk identifiers (2-character, stored as a big-endian UWORD)              */
/*                                                                           */
/* A punk is a sub-record inside an IOBJ or CTAG chunk: a 2-char ID, a       */
/* UWORD payload length, then the payload (IFF-padded to even length).       */
/* The same 2-char codes are stored in the OB punk's Type field to name      */
/* which geometry punk describes the object.                                 */
/*****************************************************************************/

#define ILUS_MKPUNK(a,b)         ((UWORD)(((UBYTE)(a) << 8) | (UBYTE)(b)))

#define ILUS_PUNK_OB             ILUS_MKPUNK('O','B')   /* object header     */
#define ILUS_PUNK_AT             ILUS_MKPUNK('A','T')   /* attribute data    */
#define ILUS_PUNK_RT             ILUS_MKPUNK('R','T')   /* rotation          */
#define ILUS_PUNK_FR             ILUS_MKPUNK('F','R')   /* frame transform   */
#define ILUS_PUNK_TC             ILUS_MKPUNK('T','C')   /* text container    */
#define ILUS_PUNK_TW             ILUS_MKPUNK('T','W')   /* text wrap         */
#define ILUS_PUNK_TG             ILUS_MKPUNK('T','G')   /* CTAG tag entry    */

/* Geometry punks (also used as OB.Type codes). */
#define ILUS_PUNK_GR             ILUS_MKPUNK('G','R')   /* group             */
#define ILUS_PUNK_DR             ILUS_MKPUNK('D','R')   /* drawing/sub-group */
#define ILUS_PUNK_PA             ILUS_MKPUNK('P','A')   /* path placement    */
#define ILUS_PUNK_PD             ILUS_MKPUNK('P','D')   /* path data         */
#define ILUS_PUNK_BX             ILUS_MKPUNK('B','X')   /* box               */
#define ILUS_PUNK_RB             ILUS_MKPUNK('R','B')   /* rounded box       */
#define ILUS_PUNK_LN             ILUS_MKPUNK('L','N')   /* line              */
#define ILUS_PUNK_CI             ILUS_MKPUNK('C','I')   /* circle / ellipse  */
#define ILUS_PUNK_CO             ILUS_MKPUNK('C','O')   /* text column       */
#define ILUS_PUNK_TO             ILUS_MKPUNK('T','O')   /* text object       */
#define ILUS_PUNK_PI             ILUS_MKPUNK('P','I')   /* picture (ILBM)    */
#define ILUS_PUNK_PS             ILUS_MKPUNK('P','S')   /* EPS               */

/*****************************************************************************/
/* OB punk flags                                                             */
/*****************************************************************************/

#define ILUS_OBF_FILLED          0x00000100UL   /* object has a fill   */
#define ILUS_OBF_STROKED         0x00000200UL   /* object has an edge  */

/*****************************************************************************/
/* Attribute-data command numbers (the AT punk and CTAG tag-data streams)    */
/*                                                                           */
/* The attribute-data stream is a sequence of commands.  Each command is     */
/* a zero marker byte followed by three VNums - command number, flags,       */
/* and payload length - and then <length> bytes of command-specific data.    */
/* Some commands nest further attribute-data inside their payload.           */
/*****************************************************************************/

#define ILUS_CMD_STYLETAG_COLOR  4     /* payload = colour tag name        */
#define ILUS_CMD_FILL            100   /* payload = nested attribute-data  */
#define ILUS_CMD_STROKE          101   /* payload = VNum count + sub-lists */
#define ILUS_CMD_COLOR           102   /* payload = VNum count + sub-lists */
#define ILUS_CMD_FILL_STYLE      103
#define ILUS_CMD_FILL_PATTERN    104
#define ILUS_CMD_STROKE_STYLE    105
#define ILUS_CMD_STROKE_WEIGHT   106   /* payload = VNum type + VNum COORD */
#define ILUS_CMD_STROKE_JOIN     107
#define ILUS_CMD_STROKE_CAPEND   110
#define ILUS_CMD_COLOR_STYLE     111   /* payload = VNum flags,c,m,y,k,... */
#define ILUS_CMD_COLOR_TINT      112
#define ILUS_CMD_SCREEN          114

/* CTAG tag types (TG punk Type field). */
#define ILUS_CTAG_COLOR          4

/*****************************************************************************/
/* PD path-data segment commands (low nibble of each segment flags word)     */
/*****************************************************************************/

#define ILUS_PD_TYPEMASK         0x000F
#define ILUS_PD_MOVETO           0x0001   /* COORD X, COORD Y                */
#define ILUS_PD_LINETO           0x0002   /* COORD X, COORD Y                */
#define ILUS_PD_CURVETO          0x0003   /* 3 control points (6 COORDs)     */
#define ILUS_PD_CLOSEPATH        0x0006   /* no coordinates                  */

/*****************************************************************************/
/* Coordinate model                                                          */
/*                                                                           */
/* COORD is a signed 32-bit value in units of 1/1828800 inch.  One PostScript*/
/* point (1/72 inch) is therefore 1828800/72 = 25400 COORDs.  The loader     */
/* converts COORDs to drawing.datatype's 16.16 Fixed "points" so that all    */
/* geometry shares one unit and DDTM_FITTOBOX scales the whole drawing into  */
/* the output rectangle.                                                     */
/*****************************************************************************/

#define ILUS_COORDS_PER_POINT    25400L

#endif /* DATATYPES_ILUSCLASS_H */
