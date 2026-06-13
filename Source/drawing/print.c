/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* print.c -- DTM_PRINT for drawing.datatype                                 */
/*                                                                           */
/* Two routes are available:                                                 */
/*                                                                           */
/*   1. If the host's printer.device accepts PRD_DUMPRPORT (i.e. all         */
/*      Workbench 3.x systems do), we render the drawing at a higher         */
/*      DPI than the screen into a scratch BitMap and ship it via            */
/*      PRD_DUMPRPORT.  Rendering at 144 DPI typically gives a clean         */
/*      vector-quality print suitable for letter-sized output.               */
/*                                                                           */
/*   2. If route 1 fails (no printer device, no DUMPRPORT support), we       */
/*      fall back to DoSuperMethodA which prints the cached low-             */
/*      resolution bitmap.                                                   */
/*****************************************************************************/

#include "classbase.h"


/*****************************************************************************/
/* DTM_PRINT                                                                 */
/*****************************************************************************/

ULONG printMethod (struct ClassBase *cb, Class *cl, Object *o,
                   struct dtPrint *msg)
{
    struct localData *lod = INST_DATA (cl, o);
    union  printerIO *pio;
    struct IODRPReq  *iodrp;
    LONG   printerDPI = 144;
    LONG   pixW = 0;
    LONG   pixH = 0;
    LONG   ratioX, ratioY;
    LONG   err;
    struct BitMap *scratch;
    struct RastPort scratchRP;
    ULONG  savedScale;
    LONG   savedFixed;
    ULONG  savedDPI;

    if (!msg || !msg->dtp_PIO)
        return DoSuperMethodA (cl, o, (Msg) msg);

    pio = (union printerIO *) msg->dtp_PIO;
    iodrp = &pio->iodrp;

    /* Recompute pixel size at the chosen printer DPI.  We swap the       *
     * resolution fields in/out around the call so the existing cached    *
     * screen rendering is not disturbed.                                 */
    savedScale  = lod->ldd_Scale;
    savedFixed  = lod->ldd_FixedScale;
    savedDPI    = lod->ldd_RenderDPI;

    lod->ldd_RenderDPI  = (ULONG) printerDPI;
    lod->ldd_Scale      = 100;
    lod->ldd_FixedScale = DRW_FIXED_ONE;
    computePixelSize (lod, &pixW, &pixH);

    if (pixW <= 0 || pixH <= 0)
    {
        lod->ldd_Scale      = savedScale;
        lod->ldd_FixedScale = savedFixed;
        lod->ldd_RenderDPI  = savedDPI;
        return DoSuperMethodA (cl, o, (Msg) msg);
    }

    scratch = AllocBitMap ((ULONG) pixW, (ULONG) pixH, 8, BMF_CLEAR, NULL);
    if (!scratch)
    {
        lod->ldd_Scale      = savedScale;
        lod->ldd_FixedScale = savedFixed;
        lod->ldd_RenderDPI  = savedDPI;
        return DoSuperMethodA (cl, o, (Msg) msg);
    }

    InitRastPort (&scratchRP);
    scratchRP.BitMap = scratch;

    /* Compute the printer-DPI/native-DPI scaling factor in 16.16 fixed.  */
    if (lod->ldd_NativeDPI == 0) lod->ldd_NativeDPI = 72;
    ratioX = ((LONG) printerDPI << 16) / (LONG) lod->ldd_NativeDPI;
    ratioY = ratioX;

    drawingRenderVectors (cb, lod, &scratchRP,
                          0, 0, pixW, pixH,
                          ratioX, ratioY,
                          lod->ldd_CanvasLeft, lod->ldd_CanvasTop,
                          (UWORD) lod->ldd_Rotation,
                          (UWORD) lod->ldd_RenderQuality,
                          lod->ldd_RenderFlags,
                          NULL, NULL);

    /* PRD_DUMPRPORT -- send the bitmap to the printer. */
    iodrp->io_Command   = PRD_DUMPRPORT;
    iodrp->io_RastPort  = &scratchRP;
    iodrp->io_ColorMap  = lod->ldd_ColorMap;
    iodrp->io_Modes     = 0;
    iodrp->io_SrcX      = 0;
    iodrp->io_SrcY      = 0;
    iodrp->io_SrcWidth  = pixW;
    iodrp->io_SrcHeight = pixH;
    iodrp->io_DestCols  = 0;     /* 0 = "use page width"                  */
    iodrp->io_DestRows  = 0;
    iodrp->io_Special   = 0;

    err = DoIO ((struct IORequest *) pio);

    WaitBlit ();
    FreeBitMap (scratch);

    lod->ldd_Scale      = savedScale;
    lod->ldd_FixedScale = savedFixed;
    lod->ldd_RenderDPI  = savedDPI;

    if (err)
        return DoSuperMethodA (cl, o, (Msg) msg);

    return 1;
}
