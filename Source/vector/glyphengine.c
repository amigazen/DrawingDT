/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* glyphengine.c -- bullet.library outline glyph rasteriser for vector.image */
/*                                                                           */
/* See glyphengine.h for the rationale.  The procedure mirrors the           */
/* canonical bullet.library example from the RKRM/AutoDocs:                  */
/*                                                                           */
/*   1. Load the typeface's .otag file into RAM, verify its OT_FileIdent     */
/*      tag, and relocate every OT_Indirect tag's data from a file-relative  */
/*      offset to an absolute address inside the buffer.                     */
/*   2. OpenEngine(), then SetInfo(OT_OTagList, OT_OTagPath) to bind the     */
/*      typeface, plus OT_DeviceDPI/OT_PointHeight for the requested size.   */
/*   3. Per glyph: SetInfo(OT_GlyphCode, OT_RotateSin, OT_RotateCos), then   */
/*      ObtainInfo(OT_GlyphMap) to get a 1-bit bitmap rotated to the desired */
/*      baseline angle, blit it, ReleaseInfo().                              */
/*                                                                           */
/* The engine's glyph bitmap is read-only and may live outside Chip RAM, so  */
/* (like the Autodoc example) every glyph is copied into a Chip-RAM scratch  */
/* buffer before BltTemplate() hands it to the blitter.                      */
/*****************************************************************************/

#include "classbase.h"


/*****************************************************************************/
/* gleLoadOtag                                                               */
/*                                                                           */
/* Open the named .otag file, slurp it whole into an AllocVec'd buffer,      */
/* validate it (first tag must be OT_FileIdent carrying the file size), and  */
/* relocate the OT_Indirect tags so their ti_Data fields point at the real   */
/* in-buffer addresses instead of file offsets.  Returns the tag list (free  */
/* with FreeVec) or NULL.                                                    */
/*****************************************************************************/

static struct TagItem *gleLoadOtag (struct ClassBase *cb, STRPTR otagPath)
{
    struct FileInfoBlock *fib;
    struct TagItem       *list;
    struct TagItem       *walk;
    struct TagItem       *ti;
    BPTR                  fh;
    LONG                  size;

    if (!DOSBase || !otagPath) return NULL;

    list = NULL;

    fib = (struct FileInfoBlock *) AllocDosObject (DOS_FIB, NULL);
    if (!fib)
        return NULL;

    fh = Open (otagPath, MODE_OLDFILE);
    if (fh)
    {
        if (ExamineFH (fh, fib))
        {
            size = fib->fib_Size;
            if (size > (LONG) sizeof (struct TagItem))
            {
                list = (struct TagItem *) AllocVec ((ULONG) size, MEMF_CLEAR);
                if (list)
                {
                    if (Read (fh, (APTR) list, size) == size &&
                        list->ti_Tag  == OT_FileIdent &&
                        list->ti_Data == (ULONG) size)
                    {
                        walk = list;
                        while ((ti = NextTagItem (&walk)) != NULL)
                        {
                            /* Indirect tags store a byte offset from the     *
                             * start of the file image; turn it into a live   *
                             * pointer into our buffer.                       */
                            if (ti->ti_Tag & OT_Indirect)
                                ti->ti_Data = (ULONG) list + ti->ti_Data;
                        }
                    }
                    else
                    {
                        FreeVec (list);
                        list = NULL;
                    }
                }
            }
        }
        Close (fh);
    }

    FreeDosObject (DOS_FIB, fib);
    return list;
}


/*****************************************************************************/
/* gleOpenFont                                                               */
/*****************************************************************************/

BOOL gleOpenFont (struct ClassBase *cb, struct GlyphRenderCtx *ctx,
                  STRPTR otagPath, LONG heightPx,
                  UWORD xdpi, UWORD ydpi)
{
    if (!ctx) return FALSE;

    ctx->grc_Engine   = NULL;
    ctx->grc_OTag     = NULL;
    ctx->grc_ChipBuf  = NULL;
    ctx->grc_ChipSize = 0;
    ctx->grc_EmWidth  = heightPx;

    if (!BulletBase || !otagPath) return FALSE;
    if (heightPx < 1)   heightPx = 1;
    if (heightPx > 1024) heightPx = 1024;
    if (xdpi == 0) xdpi = 72;
    if (ydpi == 0) ydpi = 72;

    ctx->grc_OTag = gleLoadOtag (cb, otagPath);
    if (!ctx->grc_OTag)
        return FALSE;

    ctx->grc_Engine = OpenEngine ();
    if (!ctx->grc_Engine)
    {
        FreeVec (ctx->grc_OTag);
        ctx->grc_OTag = NULL;
        return FALSE;
    }

    /* Bind the typeface first, then size it.  Both OT_OTagList and          *
     * OT_OTagPath are required to select a face.                            */
    if (SetInfo (ctx->grc_Engine,
                 OT_OTagList, (ULONG) ctx->grc_OTag,
                 OT_OTagPath, (ULONG) otagPath,
                 TAG_END) != OTERR_Success)
    {
        gleCloseFont (cb, ctx);
        return FALSE;
    }

    if (SetInfo (ctx->grc_Engine,
                 OT_DeviceDPI,   (((ULONG) xdpi) << 16) | (ULONG) ydpi,
                 OT_PointHeight, ((ULONG) heightPx) << 16,
                 TAG_END) != OTERR_Success)
    {
        gleCloseFont (cb, ctx);
        return FALSE;
    }

    /* Em-square width in device pixels, used to turn the engine's           *
     * em-fraction advance (glm_Width) into a pixel advance.                 */
    ctx->grc_EmWidth = (heightPx * (LONG) xdpi) / 72;
    if (ctx->grc_EmWidth < 1)
        ctx->grc_EmWidth = 1;

    return TRUE;
}


/*****************************************************************************/
/* gleCloseFont                                                              */
/*****************************************************************************/

void gleCloseFont (struct ClassBase *cb, struct GlyphRenderCtx *ctx)
{
    if (!ctx) return;

    if (ctx->grc_Engine)
    {
        if (BulletBase)
            CloseEngine (ctx->grc_Engine);
        ctx->grc_Engine = NULL;
    }
    if (ctx->grc_OTag)
    {
        FreeVec (ctx->grc_OTag);
        ctx->grc_OTag = NULL;
    }
    if (ctx->grc_ChipBuf)
    {
        FreeMem (ctx->grc_ChipBuf, ctx->grc_ChipSize);
        ctx->grc_ChipBuf  = NULL;
        ctx->grc_ChipSize = 0;
    }
}


/*****************************************************************************/
/* gleDrawGlyph                                                              */
/*****************************************************************************/

BOOL gleDrawGlyph (struct ClassBase *cb, struct GlyphRenderCtx *ctx,
                   struct RastPort *rp, ULONG code,
                   LONG penX, LONG penY, LONG sinFx, LONG cosFx,
                   LONG *advanceOut)
{
    struct GlyphMap *gm;
    ULONG  bmSize;
    LONG   destX;
    LONG   destY;
    LONG   advance;

    if (advanceOut) *advanceOut = 0;
    if (!ctx || !ctx->grc_Engine || !rp || !BulletBase)
        return FALSE;

    /* The engine wants the sine set before the cosine; both must be given   *
     * together or it reports OTERR_NoRotate.                                */
    if (SetInfo (ctx->grc_Engine,
                 OT_GlyphCode, code,
                 OT_RotateSin, (ULONG) sinFx,
                 OT_RotateCos, (ULONG) cosFx,
                 TAG_END) != OTERR_Success)
        return FALSE;

    gm = NULL;
    if (ObtainInfo (ctx->grc_Engine, OT_GlyphMap, (ULONG) &gm, TAG_END)
            != OTERR_Success || !gm)
        return FALSE;

    advance = (LONG) (((LONG) gm->glm_Width >> 8) * ctx->grc_EmWidth >> 8);
    if (advance < 0) advance = 0;

    bmSize = (ULONG) gm->glm_BMModulo * (ULONG) gm->glm_BMRows;
    if (bmSize > 0 && gm->glm_BitMap)
    {
        /* Grow (never shrink) the Chip-RAM scratch buffer so BltTemplate    *
         * has a blitter-accessible source.  A real glyph never needs        *
         * megabytes, so a simple grow-on-demand policy is fine.             */
        if (bmSize > ctx->grc_ChipSize)
        {
            if (ctx->grc_ChipBuf)
                FreeMem (ctx->grc_ChipBuf, ctx->grc_ChipSize);
            ctx->grc_ChipBuf  = AllocMem (bmSize, MEMF_CHIP);
            ctx->grc_ChipSize = ctx->grc_ChipBuf ? bmSize : 0;
        }

        if (ctx->grc_ChipBuf)
        {
            UBYTE *src;
            LONG   srcX;
            LONG   srcMod;
            LONG   w;
            LONG   h;
            LONG   bmW;
            LONG   bmH;

            CopyMem (gm->glm_BitMap, ctx->grc_ChipBuf, bmSize);

            /* glm_X0/glm_Y0 are the whole-pixel offset from the bitmap's    *
             * top-left corner to the glyph origin, so subtract them to land *
             * the origin exactly on the requested baseline point.           */
            destX  = penX - (LONG) gm->glm_X0;
            destY  = penY - (LONG) gm->glm_Y0;
            src    = (UBYTE *) ctx->grc_ChipBuf;
            srcX   = 0;
            srcMod = (LONG) gm->glm_BMModulo;
            w      = (LONG) gm->glm_BMModulo * 8;
            h      = (LONG) gm->glm_BMRows;

            /* BltTemplate() clips against a Layer, but the datatype async    *
             * layout daemon renders into a bare off-screen RastPort that has *
             * no Layer.  Without a layer the blitter writes wherever it is   *
             * told, so a glyph stepped along a path that runs off a small    *
             * layout bitmap would scribble outside the allocation and crash  *
             * the daemon.  Clip the template to the destination BitMap by    *
             * hand whenever there is no Layer to do it for us.               */
            if (rp->Layer == NULL && rp->BitMap)
            {
                bmW = (LONG) rp->BitMap->BytesPerRow * 8;
                bmH = (LONG) rp->BitMap->Rows;

                if (destX < 0)
                {
                    srcX  += -destX;
                    w     -= -destX;
                    destX  = 0;
                }
                if (destY < 0)
                {
                    src   += (ULONG) (-destY) * (ULONG) srcMod;
                    h     -= -destY;
                    destY  = 0;
                }
                if (destX + w > bmW) w = bmW - destX;
                if (destY + h > bmH) h = bmH - destY;
            }

            if (w > 0 && h > 0)
            {
                WaitBlit ();
                BltTemplate ((PLANEPTR) src, srcX, srcMod, rp,
                             destX, destY, w, h);
            }
        }
    }

    ReleaseInfo (ctx->grc_Engine, OT_GlyphMap, (ULONG) gm, TAG_END);

    if (advanceOut) *advanceOut = advance;
    return TRUE;
}
