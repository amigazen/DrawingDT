/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* class_iprotos.h -- internal prototypes for drawing.datatype               */
/*****************************************************************************/

#ifndef CLASS_IPROTOS_H
#define CLASS_IPROTOS_H

/* classbase.c */
struct Library *ASM LibInit    (REG (d0) struct ClassBase *cb,
                                REG (a0) BPTR seglist,
                                REG (a6) struct Library *sysbase);
LONG ASM            LibOpen    (REG (a6) struct ClassBase *cb);
LONG ASM            LibClose   (REG (a6) struct ClassBase *cb);
LONG ASM            LibExpunge (REG (a6) struct ClassBase *cb);

/* dispatch.c */
Class *initClass (struct ClassBase *cb);

/* attrs.c */
ULONG getDrawingAttr   (struct ClassBase *cb, Class *cl, Object *o,
                        struct opGet *msg);
ULONG setDrawingAttrs  (struct ClassBase *cb, Class *cl, Object *o,
                        struct opSet *msg);
void  doNotifyChanges  (struct ClassBase *cb, Object *o,
                        struct GadgetInfo *ginfo);
void  doNotifyBusy     (struct ClassBase *cb, Object *o,
                        struct GadgetInfo *ginfo, BOOL busy);

/* layout.c */
ULONG drawingLayout    (struct ClassBase *cb, Class *cl, Object *o,
                        struct gpLayout *gpl);
ULONG drawingRender    (struct ClassBase *cb, Class *cl, Object *o,
                        struct gpRender *msg);
ULONG drawingFrameBox  (struct ClassBase *cb, Class *cl, Object *o,
                        struct dtFrameBox *dtf);
void  invalidateView   (struct ClassBase *cb, Object *o);
void  freeCachedBitMap (struct ClassBase *cb, struct localData *lod);
void  computePixelSize (struct localData *lod, LONG *outW, LONG *outH);

/* displaylist.c */
void  initDisplayList  (struct ClassBase *cb, struct localData *lod);
void  clearDisplayList (struct ClassBase *cb, struct localData *lod);
ULONG appendObject     (struct ClassBase *cb, struct localData *lod,
                        struct DrawObj *obj);
ULONG beginGroupObject (struct ClassBase *cb, struct localData *lod,
                        UWORD groupType, ULONG expect);
ULONG endGroupObject   (struct ClassBase *cb, struct localData *lod);
struct DrawObj *poolDrawObj (struct ClassBase *cb, struct localData *lod,
                             UWORD type);
struct DrawPoint *poolDrawPoints (struct ClassBase *cb,
                                  struct localData *lod, ULONG nPoints);
void  computeObjectBounds (struct DrawObj *obj);
void  recomputeDrawingBounds (struct localData *lod);
void  syncHeaderFromInfo (struct localData *lod);

/* primitives.c -- OS4 version DDTM_ADDPOLY / ADDATTR / SETSIZE handlers */
ULONG addPolyMethod    (struct ClassBase *cb, Class *cl, Object *o,
                        struct ddtPolygon *msg);
ULONG addAttrMethod    (struct ClassBase *cb, Class *cl, Object *o,
                        struct ddtAttr *msg);
ULONG setSizeMethod    (struct ClassBase *cb, Class *cl, Object *o,
                        struct ddtSize *msg);
ULONG addObjectMethod  (struct ClassBase *cb, Class *cl, Object *o,
                        struct drwAddObject *msg);
ULONG beginGroupMethod (struct ClassBase *cb, Class *cl, Object *o,
                        struct drwBeginGroup *msg);
ULONG endGroupMethod   (struct ClassBase *cb, Class *cl, Object *o,
                        struct drwEndGroup *msg);
ULONG addLayerMethod   (struct ClassBase *cb, Class *cl, Object *o,
                        struct drwAddLayer *msg);
ULONG addColorMethod   (struct ClassBase *cb, Class *cl, Object *o,
                        struct drwAddColor *msg);
ULONG addFontMethod    (struct ClassBase *cb, Class *cl, Object *o,
                        struct drwAddFont *msg);
ULONG clearDrawingMethod (struct ClassBase *cb, Class *cl, Object *o,
                          struct drwClearDrawing *msg);
ULONG setBoundsMethod  (struct ClassBase *cb, Class *cl, Object *o,
                        struct drwSetBounds *msg);

/* hittest.c */
ULONG hitTestPointMethod  (struct ClassBase *cb, Class *cl, Object *o,
                           struct drwHitTestPoint *msg);
ULONG getBBoxMethod       (struct ClassBase *cb, Class *cl, Object *o,
                           struct drwGetBoundingBox *msg);
ULONG fitToBoxMethod      (struct ClassBase *cb, Class *cl, Object *o,
                           struct drwFitToBox *msg);

/* navigate.c */
ULONG hitTestMethod       (struct ClassBase *cb, Class *cl, Object *o,
                           struct gpHitTest *msg);
ULONG goActiveMethod      (struct ClassBase *cb, Class *cl, Object *o,
                           struct gpInput *msg);
ULONG handleInputMethod   (struct ClassBase *cb, Class *cl, Object *o,
                           struct gpInput *msg);
ULONG gotoMethod          (struct ClassBase *cb, Class *cl, Object *o,
                           struct dtGoto *msg);
ULONG triggerMethod       (struct ClassBase *cb, Class *cl, Object *o,
                           struct dtTrigger *msg);

/* writeobj.c */
ULONG copyMethod          (struct ClassBase *cb, Class *cl, Object *o,
                           struct dtGeneral *msg);
ULONG writeMethod         (struct ClassBase *cb, Class *cl, Object *o,
                           struct dtWrite *msg);
ULONG selectMethod        (struct ClassBase *cb, Class *cl, Object *o,
                           struct dtSelect *msg);
ULONG clearSelectedMethod (struct ClassBase *cb, Class *cl, Object *o,
                           struct dtGeneral *msg);

/* print.c */
ULONG printMethod         (struct ClassBase *cb, Class *cl, Object *o,
                           struct dtPrint *msg);

/* stream.c */
BOOL  openStream          (struct ClassBase *cb, struct localData *lod,
                           struct TagItem *attrList);
void  closeStream         (struct ClassBase *cb, struct localData *lod);
LONG  fileStreamRead      (struct DrawStream *self, APTR buffer, LONG n);
LONG  fileStreamSeek      (struct DrawStream *self, LONG offset, LONG mode);
LONG  ramStreamRead       (struct DrawStream *self, APTR buffer, LONG n);
LONG  ramStreamSeek       (struct DrawStream *self, LONG offset, LONG mode);
struct IFFHandle *getIFFHandle (struct ClassBase *cb, struct localData *lod);

/* dispatch.c helpers */
APTR   poolAlloc          (struct ClassBase *cb, struct localData *lod,
                           ULONG size);
APTR   poolAllocClear     (struct ClassBase *cb, struct localData *lod,
                           ULONG size);
STRPTR poolStrDup         (struct ClassBase *cb, struct localData *lod,
                           CONST_STRPTR src);
ULONG  cancelMethod       (struct ClassBase *cb, Class *cl, Object *o,
                           struct drwCancel *msg);

/* fixed.c */
LONG   drwFloatToFixed    (float f);
float  drwFixedToFloat    (LONG fixed);
void   drwIEEEToFixed     (ULONG ieee, LONG *outFixed);
LONG   drwMulFixed        (LONG a, LONG b);

#endif /* CLASS_IPROTOS_H */
