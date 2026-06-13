/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* class_iprotos.h -- internal prototypes for ilus.datatype                  */
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

LONG  ilusParseContent  (struct ClassBase *cb, Object *o,
                         struct localData *ld,
                         struct IFFHandle *iff,
                         struct List *displayList,
                         ULONG *abortPtr, ULONG *progressPtr);

/* coord.c -- COORD (1/1828800 inch) -> 16.16 Fixed points, plus the
 * 64-bit-intermediate multiply/divide the path transform needs. */
LONG  ilusCoordToFixed  (LONG coord);
LONG  ilusMulDiv        (LONG a, LONG b, LONG c);

/* support (load.c) */
struct DrawObj *ilusMakeDrawObj (struct ClassBase *cb, Object *o,
                                 struct localData *ld, UWORD type);
LONG  ilusPushChild    (struct ClassBase *cb, Object *o,
                        struct localData *ld, struct DrawObj *obj);
LONG  ilusBeginGroup   (struct ClassBase *cb, Object *o,
                        struct localData *ld,
                        UWORD type, ULONG expect, struct DrawObj *owner);
LONG  ilusEndGroup     (struct ClassBase *cb, Object *o,
                        struct localData *ld);

#endif /* CLASS_IPROTOS_H */
