/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* vectorbridge.h -- bridge from drawing.datatype to vector.image              */
/*****************************************************************************/

#ifndef VECTORBRIDGE_H
#define VECTORBRIDGE_H

#include <images/vectorclass.h>

APTR        drawingGetPool         (struct localData *lod);
struct List *drawingGetObjectList  (struct localData *lod);
void        drawingSyncFromVector  (struct localData *lod);
ULONG       drawingForwardDrwm     (struct localData *lod, Msg msg);
ULONG       drawingRenderDrawing   (struct localData *lod,
                                    struct drwRenderDrawing *msg);
ULONG       drawingRenderPixels    (struct localData *lod,
                                    struct drwRenderPixels *msg);
ULONG       drawingRenderVectors   (struct ClassBase *cb, struct localData *lod,
                                    struct RastPort *rp,
                                    LONG destX, LONG destY,
                                    LONG destW, LONG destH,
                                    LONG fixedScaleX, LONG fixedScaleY,
                                    LONG canvasOriginX, LONG canvasOriginY,
                                    UWORD rotation, UWORD quality,
                                    ULONG flags,
                                    ULONG *abortPtr, ULONG *progressPtr);

#endif /* VECTORBRIDGE_H */
