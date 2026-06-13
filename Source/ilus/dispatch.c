/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* dispatch.c -- BOOPSI dispatcher for ilus.datatype                         */
/*                                                                           */
/* ilus.datatype is intentionally thin.  It overrides only the two methods   */
/* the root class sends DOWN to subclasses (DDTM_LOADHEADERS and             */
/* DDTM_LOADCONTENT).  Everything else - rendering, layout, hit-testing,     */
/* attribute storage, OM_NEW/OM_DISPOSE, GM_RENDER, DTM_PRINT - is           */
/* inherited unchanged from drawing.datatype.                                */
/*                                                                           */
/* OM_NEW / OM_DISPOSE need only allocate / free our localData instance      */
/* (which holds the IFF group-nest stack) - the super class allocates        */
/* its own per-instance data alongside ours via INST_DATA.                   */
/*****************************************************************************/

#include "classbase.h"


static ULONG ASM Dispatch (REG (a0) Class *cl,
                           REG (a2) Object *o,
                           REG (a1) Msg msg);
static Object *newObject (struct ClassBase *cb, Class *cl, Object *o,
                          struct opSet *ops);
static ULONG   disposeObject (struct ClassBase *cb, Class *cl, Object *o);


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

#else   /* !ILUS_DEBUG: helper compiles away to nothing */

#define dbgPut(cb,text)   ((void) 0)

#endif  /* ILUS_DEBUG */


/*****************************************************************************/
/* The class entry point.                                                     */
/*****************************************************************************/

static ULONG ASM Dispatch (REG (a0) Class *cl,
                           REG (a2) Object *o,
                           REG (a1) Msg msg)
{
    struct ClassBase *cb = (struct ClassBase *) cl->cl_UserData;
    ULONG retval = 0;

    switch (msg->MethodID)
    {
        case OM_NEW:
            dbgPut (cb, "dispatch OM_NEW");
            retval = (ULONG) newObject (cb, cl, o, (struct opSet *) msg);
            dbgPut (cb, "dispatch OM_NEW returned");
            break;

        case OM_DISPOSE:
            dbgPut (cb, "dispatch OM_DISPOSE");
            retval = disposeObject (cb, cl, o);
            dbgPut (cb, "dispatch OM_DISPOSE returned");
            break;

        case DDTM_LOADHEADERS:
            dbgPut (cb, "dispatch DDTM_LOADHEADERS");
            retval = loadHeadersMethod (cb, cl, o,
                                        (struct drwLoadHeaders *) msg);
            dbgPut (cb, "dispatch DDTM_LOADHEADERS returned");
            break;

        case DDTM_LOADCONTENT:
            dbgPut (cb, "dispatch DDTM_LOADCONTENT");
            retval = loadContentMethod (cb, cl, o,
                                        (struct drwLoadContent *) msg);
            dbgPut (cb, "dispatch DDTM_LOADCONTENT returned");
            break;

        default:
            retval = DoSuperMethodA (cl, o, msg);
            break;
    }
    return retval;
}


/*****************************************************************************/
/* initClass                                                                 */
/*                                                                           */
/* Build the BOOPSI class.  drawing.datatype was opened in LibOpen and is    */
/* held resident in cb->cb_SuperClassBase; opening it registered it as a     */
/* public BOOPSI class, so we name it as the super-class and let MakeClass() */
/* resolve the pointer -- the same convention the example datatype           */
/* subclasses use.  This avoids relying on the root's private                */
/* ObtainDrawingEngine() jump-table entry.                                   */
/*****************************************************************************/

Class *initClass (struct ClassBase *cb)
{
    Class *cl;

    cl = MakeClass (ILUSDTCLASS,
                    DRAWINGDTCLASS,
                    NULL,
                    sizeof (struct localData),
                    0);
    if (!cl) return NULL;

    cl->cl_UserData = (ULONG) cb;
    cl->cl_Dispatcher.h_Entry    = (ULONG (*)()) Dispatch;
    cl->cl_Dispatcher.h_SubEntry = NULL;

    AddClass (cl);
    return cl;
}


/*****************************************************************************/
/* newObject -- OM_NEW                                                       */
/*                                                                           */
/* Pass the message to the super class first.  drawing.datatype will         */
/* allocate its localData, open the streaming source, then call back to      */
/* us with DDTM_LOADHEADERS and (eventually) DDTM_LOADCONTENT, both of       */
/* which arrive via Dispatch above.  At each callback our localData is       */
/* available as INST_DATA(cl, o) and is zero-filled by MakeClass().          */
/*****************************************************************************/

static Object *newObject (struct ClassBase *cb, Class *cl, Object *o,
                          struct opSet *ops)
{
    Object *newObj;
    struct localData *ld;

    (void) cb;

    newObj = (Object *) DoSuperMethodA (cl, o, (Msg) ops);
    if (!newObj) return NULL;

    ld = (struct localData *) INST_DATA (cl, newObj);
    ld->ld_Object = newObj;

    return newObj;
}


/*****************************************************************************/
/* disposeObject -- OM_DISPOSE                                               */
/*                                                                           */
/* The IFFHandle is owned by the root class, not by us - the super class     */
/* closes / frees it when DTM_DISPOSE flows up.  We only need to release     */
/* any references we kept in our own stack.                                  */
/*****************************************************************************/

static ULONG disposeObject (struct ClassBase *cb, Class *cl, Object *o)
{
    struct localData *ld;
    struct opSet disposeMsg;

    (void) cb;
    ld = (struct localData *) INST_DATA (cl, o);
    ld->ld_StackDepth = 0;
    ld->ld_CtxDepth   = 0;

    disposeMsg.MethodID     = OM_DISPOSE;
    disposeMsg.ops_AttrList = NULL;
    disposeMsg.ops_GInfo    = NULL;
    return DoSuperMethodA (cl, o, (Msg) &disposeMsg);
}
