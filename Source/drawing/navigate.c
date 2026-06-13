/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* navigate.c -- DTM_GOTO / DTM_TRIGGER / gadget input for drawing.datatype  */
/*                                                                           */
/* drawing.datatype is a single-page object so DTM_GOTO has nothing to do;   */
/* we accept the message for protocol compatibility and return success.      */
/* DTM_TRIGGER is used to wire up STM_PAUSE / STM_PLAY (the datatypes        */
/* control bar's transport buttons); for a vector drawing the only           */
/* trigger we honour is STM_PRINT and the catchall STM_ACTIVATE which        */
/* selects the whole drawing.                                                */
/*                                                                           */
/* GM_HITTEST, GM_GOACTIVE and GM_HANDLEINPUT are mostly delegated to        */
/* the base class - the application implements selection / drag-zoom         */
/* through DTM_SELECT calls.  We trap right-mouse-button + drag for          */
/* pan, and the +/- keyboard keys for zoom in/out.                           */
/*****************************************************************************/

#include "classbase.h"


/*****************************************************************************/
/* DTM_GOTO                                                                   */
/*****************************************************************************/

ULONG gotoMethod (struct ClassBase *cb, Class *cl, Object *o,
                  struct dtGoto *msg)
{
    (void) cb;
    (void) cl;
    (void) o;
    (void) msg;
    /* No pages.  Acknowledge so applications that use STM_RETRACE etc.  *
     * don't trip on an error return.                                    */
    return 1;
}


/*****************************************************************************/
/* DTM_TRIGGER                                                                */
/*****************************************************************************/

ULONG triggerMethod (struct ClassBase *cb, Class *cl, Object *o,
                     struct dtTrigger *msg)
{
    ULONG retval = 0;

    if (!msg) return 0;

    switch (msg->dtt_Function)
    {
        case STM_PAUSE:
        case STM_PLAY:
        case STM_REWIND:
        case STM_FASTFORWARD:
        case STM_PREV_FIELD:
        case STM_NEXT_FIELD:
            /* Not applicable to a static drawing - quietly succeed.    */
            retval = 1;
            break;

        case STM_ACTIVATE_FIELD:
            retval = 1;
            break;

        default:
            /* Anything else (printing is the DTM_PRINT method, not an   *
             * STM_ trigger) is handled by datatypesclass.               */
            retval = DoSuperMethodA (cl, o, (Msg) msg);
            break;
    }

    (void) cb;
    return retval;
}


/*****************************************************************************/
/* GM_HITTEST                                                                */
/*****************************************************************************/

ULONG hitTestMethod (struct ClassBase *cb, Class *cl, Object *o,
                     struct gpHitTest *msg)
{
    (void) cb;
    return DoSuperMethodA (cl, o, (Msg) msg);
}


/*****************************************************************************/
/* GM_GOACTIVE                                                               */
/*****************************************************************************/

ULONG goActiveMethod (struct ClassBase *cb, Class *cl, Object *o,
                      struct gpInput *msg)
{
    (void) cb;
    return DoSuperMethodA (cl, o, (Msg) msg);
}


/*****************************************************************************/
/* GM_HANDLEINPUT                                                            */
/*                                                                           */
/* We intercept a couple of keyboard shortcuts for zoom and rotation, then   */
/* fall through to the base class for everything else.                       */
/*****************************************************************************/

ULONG handleInputMethod (struct ClassBase *cb, Class *cl, Object *o,
                         struct gpInput *msg)
{
    struct localData *lod = INST_DATA (cl, o);
    ULONG retval;

    if (msg && msg->gpi_IEvent &&
        msg->gpi_IEvent->ie_Class == IECLASS_RAWKEY &&
        !(msg->gpi_IEvent->ie_Code & IECODE_UP_PREFIX))
    {
        BOOL handled = FALSE;
        ULONG newScale = lod->ldd_Scale;
        ULONG newRot   = lod->ldd_Rotation;

        switch (msg->gpi_IEvent->ie_Code)
        {
            case 0x0D:   /* '+'         */
                newScale = lod->ldd_Scale + 25;
                handled = TRUE;
                break;
            case 0x0C:   /* '-'         */
                if (lod->ldd_Scale > 25)
                    newScale = lod->ldd_Scale - 25;
                handled = TRUE;
                break;
            case 0x36:   /* 'r' or rotate */
                newRot = (lod->ldd_Rotation + 90) % 360;
                handled = TRUE;
                break;
        }

        if (handled)
        {
            SetGadgetAttrs ((struct Gadget *) o,
                            msg->gpi_GInfo ? msg->gpi_GInfo->gi_Window : NULL,
                            NULL,
                            DDTA_Scale,    newScale,
                            DDTA_Rotation, newRot,
                            TAG_DONE);
            return GMR_NOREUSE | GMR_VERIFY;
        }
    }

    retval = DoSuperMethodA (cl, o, (Msg) msg);
    (void) cb;
    return retval;
}
