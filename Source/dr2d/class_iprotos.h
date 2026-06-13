/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* class_iprotos.h -- internal prototypes for dr2d.datatype                  */
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

/* load.c */
ULONG loadHeadersMethod (struct ClassBase *cb, Class *cl, Object *o,
                         struct drwLoadHeaders *msg);
ULONG loadContentMethod (struct ClassBase *cb, Class *cl, Object *o,
                         struct drwLoadContent *msg);

/* helpers in load.c */
LONG  dr2dParseContent  (struct ClassBase *cb, Object *o,
                         struct localData *ld,
                         struct IFFHandle *iff,
                         struct List *displayList,
                         ULONG *abortPtr, ULONG *progressPtr);

/* ieee.c -- IEEE 754 -> 16.16 Fixed conversion (no FPU needed) */
LONG  dr2dIEEEToFixed   (ULONG ieee);
LONG  dr2dCoordToFixed  (ULONG ieee);

/* support */
struct DrawObj *dr2dMakeDrawObj (struct ClassBase *cb, Object *o,
                                 struct localData *ld, UWORD type);
LONG  dr2dPushChild    (struct ClassBase *cb, Object *o,
                        struct localData *ld, struct DrawObj *obj);
LONG  dr2dBeginGroup   (struct ClassBase *cb, Object *o,
                        struct localData *ld,
                        UWORD type, ULONG expect, struct DrawObj *owner);
LONG  dr2dEndGroup     (struct ClassBase *cb, Object *o,
                        struct localData *ld);

#endif /* CLASS_IPROTOS_H */
