/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* class_iprotos.h -- internal prototypes for vector.image                     */
/*****************************************************************************/

#ifndef VECTOR_CLASS_IPROTOS_H
#define VECTOR_CLASS_IPROTOS_H

struct Library *ASM LibInit    (REG (d0) struct ClassBase *cb,
                                REG (a0) BPTR seglist,
                                REG (a6) struct Library *sysbase);
LONG ASM            LibOpen    (REG (a6) struct ClassBase *cb);
LONG ASM            LibClose   (REG (a6) struct ClassBase *cb);
LONG ASM            LibExpunge (REG (a6) struct ClassBase *cb);

Class *initClass (struct ClassBase *cb);

ULONG getVectorAttr   (struct ClassBase *cb, Class *cl, Object *o,
                       struct opGet *msg);
ULONG setVectorAttrs  (struct ClassBase *cb, Class *cl, Object *o,
                       struct opSet *msg);

ULONG vectorIMDraw    (struct ClassBase *cb, Class *cl, Object *o,
                       struct impDraw *msg);
ULONG vectorIMDrawFrame (struct ClassBase *cb, Class *cl, Object *o,
                         struct impDraw *msg);
ULONG vectorIMErase   (struct ClassBase *cb, Class *cl, Object *o,
                       struct impErase *msg);
ULONG vectorIMHitTest (struct ClassBase *cb, Class *cl, Object *o,
                       struct impHitTest *msg);
ULONG vectorIMDomainFrame (struct ClassBase *cb, Class *cl, Object *o,
                           struct impDomainFrame *msg);
ULONG rebuildCache    (struct ClassBase *cb, Class *cl, Object *o);
void  freeVectorCache (struct ClassBase *cb, struct localData *lod);
ULONG loadSourceMethod (struct ClassBase *cb, Class *cl, Object *o,
                        struct vecLoadSource *msg);

void  initDisplayList  (struct ClassBase *cb, struct localData *lod);
void  clearDisplayList (struct ClassBase *cb, struct localData *lod);
ULONG appendObject     (struct ClassBase *cb, struct localData *lod,
                        struct DrawObj *obj);
ULONG beginGroupObject (struct ClassBase *cb, struct localData *lod,
                        UWORD groupType, ULONG expect, ULONG fillID);
ULONG endGroupObject   (struct ClassBase *cb, struct localData *lod);
struct DrawObj *poolDrawObj (struct ClassBase *cb, struct localData *lod,
                             UWORD type);
struct DrawPoint *poolDrawPoints (struct ClassBase *cb,
                                  struct localData *lod, ULONG nPoints);
void  computeObjectBounds (struct DrawObj *obj);
void  recomputeDrawingBounds (struct localData *lod);
void  syncCanvasExtents (struct localData *lod);

ULONG renderVectorMethod       (struct ClassBase *cb, Class *cl, Object *o,
                                struct vecRender *msg);
ULONG renderVectorPixelsMethod (struct ClassBase *cb, Class *cl, Object *o,
                                struct vecRenderPixels *msg);
void  renderObjectList         (struct ClassBase *cb, struct localData *lod,
                                struct RastPort *rp, struct ColorMap *cmap,
                                struct List *objects,
                                LONG destX, LONG destY,
                                LONG destW, LONG destH,
                                LONG fixedScaleX, LONG fixedScaleY,
                                LONG canvasOriginX, LONG canvasOriginY,
                                UWORD rotation, UWORD quality, ULONG flags,
                                ULONG *abortPtr, ULONG *progressPtr);
LONG  drwMulFixed              (LONG a, LONG b);
BOOL  parseProVectorText       (struct ClassBase *cb, struct localData *lod,
                                struct DrawObj *obj);

ULONG addObjectMethod    (struct ClassBase *cb, Class *cl, Object *o,
                          struct vecAddObject *msg);
ULONG beginGroupMethod   (struct ClassBase *cb, Class *cl, Object *o,
                          struct vecBeginGroup *msg);
ULONG endGroupMethod     (struct ClassBase *cb, Class *cl, Object *o,
                          struct vecEndGroup *msg);
ULONG addLayerMethod     (struct ClassBase *cb, Class *cl, Object *o,
                          struct vecAddLayer *msg);
ULONG addColorMethod     (struct ClassBase *cb, Class *cl, Object *o,
                          struct vecAddColor *msg);
ULONG addFontMethod      (struct ClassBase *cb, Class *cl, Object *o,
                          struct vecAddFont *msg);
ULONG clearDrawingMethod (struct ClassBase *cb, Class *cl, Object *o,
                          struct vecClear *msg);
ULONG setBoundsMethod    (struct ClassBase *cb, Class *cl, Object *o,
                          struct vecSetBounds *msg);

ULONG hitTestPointMethod (struct ClassBase *cb, Class *cl, Object *o,
                          struct vecHitTestPoint *msg);
ULONG getBBoxMethod      (struct ClassBase *cb, Class *cl, Object *o,
                          struct vecGetBoundingBox *msg);
ULONG fitToBoxMethod     (struct ClassBase *cb, Class *cl, Object *o,
                          struct vecFitToBox *msg);

LONG   drwFloatToFixed    (float f);
float  drwFixedToFloat    (LONG fixed);
void   drwIEEEToFixed     (ULONG ieee, LONG *outFixed);

APTR   poolAlloc          (struct ClassBase *cb, struct localData *lod,
                           ULONG size);
APTR   poolAllocClear     (struct ClassBase *cb, struct localData *lod,
                           ULONG size);
STRPTR poolStrDup         (struct ClassBase *cb, struct localData *lod,
                           CONST_STRPTR src);

#endif /* VECTOR_CLASS_IPROTOS_H */
