/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

/*****************************************************************************/
/* glyphengine.h -- bullet.library outline glyph rasteriser for vector.image */
/*                                                                           */
/* graphics.library Text() can only draw upright glyph rows, so text that    */
/* must follow a curved/slanted path (DR2D TPTH "text on a path") cannot be  */
/* rendered faithfully with it.  bullet.library exposes the IntelliFont      */
/* outline engine, which CAN rotate a glyph's baseline about its origin via  */
/* the OT_RotateSin / OT_RotateCos tags.  This module wraps the engine so    */
/* the renderer can: select a Compugraphic outline typeface from its .otag   */
/* file, then for each character ask the engine for a 1-bit glyph bitmap     */
/* rotated to the path tangent and blit it onto the destination RastPort.    */
/*                                                                           */
/* All engine access is routed through the ClassBase (cb->cb_BulletBase via  */
/* the BulletBase macro), so this module is A4/small-data clean exactly like */
/* the rest of vector.image.                                                 */
/*****************************************************************************/

#ifndef VECTOR_GLYPHENGINE_H
#define VECTOR_GLYPHENGINE_H

struct ClassBase;

/* Holds one open typeface ready to emit glyph bitmaps.  Treat as opaque:   *
 * always zero it, gleOpenFont() fills it and gleCloseFont() tears it down. */
struct GlyphRenderCtx
{
    struct GlyphEngine *grc_Engine;   /* live bullet engine handle           */
    struct TagItem     *grc_OTag;     /* loaded+relocated otag tag list      */
    APTR                grc_ChipBuf;  /* MEMF_CHIP scratch for BltTemplate   */
    ULONG               grc_ChipSize; /* bytes currently allocated for above */
    LONG                grc_EmWidth;  /* em-square width in device px        */
};

/* Select an outline typeface from its .otag file (e.g. "FONTS:CGTimes.otag")
 * scaled to heightPx device pixels at the given device resolution.  Returns
 * TRUE on success (ctx is then ready for gleDrawGlyph), FALSE otherwise, in
 * which case ctx is left torn down and the caller should fall back to an
 * upright graphics.library text run. */
BOOL gleOpenFont (struct ClassBase *cb, struct GlyphRenderCtx *ctx,
                  STRPTR otagPath, LONG heightPx,
                  UWORD xdpi, UWORD ydpi);

/* Release everything gleOpenFont() acquired.  Safe to call on a zeroed or
 * partially-initialised context. */
void gleCloseFont (struct ClassBase *cb, struct GlyphRenderCtx *ctx);

/* Render one glyph whose baseline origin sits at (penX,penY) in device
 * pixels, rotated by the angle whose sine/cosine are sinFx/cosFx (16.16
 * fixed point, font space: +x along the baseline, +y upward).  The glyph is
 * stamped in the RastPort's current FgPen using JAM1, so the caller must set
 * the pen and draw mode beforehand.  On success *advanceOut receives the
 * glyph advance width in device pixels (along the unrotated baseline) so the
 * caller can step to the next glyph along the path. */
BOOL gleDrawGlyph (struct ClassBase *cb, struct GlyphRenderCtx *ctx,
                   struct RastPort *rp, ULONG code,
                   LONG penX, LONG penY, LONG sinFx, LONG cosFx,
                   LONG *advanceOut);

#endif /* VECTOR_GLYPHENGINE_H */
