/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2026 amigazen project
 */

 #ifndef IMAGES_VECTOR_H
 #define IMAGES_VECTOR_H
 /*
 **  vectorclass.h
 **
 **  Public interface for vector.image -- reusable BOOPSI vector renderer.
 **
 **  vector.image is an imageclass subclass that owns a resolution-independent
 **  display list (struct DrawObj) and rasterises it on demand.  It is the
 **  foundation layer used by drawing.datatype and may be used directly by
 **  gadgets (IA_Image) for scalable UI artwork.
 **
 **  drawing.datatype embeds a vector.image instance and forwards parser
 **  construction methods (DDTM_*) to the equivalent VIMM_* methods here.
 */
 
 #ifndef UTILITY_TAGITEM_H
 #include <utility/tagitem.h>
 #endif
 
 #ifndef EXEC_LISTS_H
 #include <exec/lists.h>
 #endif
 
 #ifndef EXEC_NODES_H
 #include <exec/nodes.h>
 #endif
 
 #ifndef INTUITION_INTUITION_H
 #include <intuition/intuition.h>
 #endif
 
 #ifndef INTUITION_IMAGECLASS_H
 #include <intuition/imageclass.h>
 #endif
 
 #ifndef IMAGES_BITMAP_H
 #include <images/bitmap.h>
 #endif
 
 #ifndef GRAPHICS_GFX_H
 #include <graphics/gfx.h>
 #endif
 
 #ifndef GRAPHICS_RASTPORT_H
 #include <graphics/rastport.h>
 #endif
 
 /*****************************************************************************/
 
 #define VECTORIMAGECLASS      "vector.image"
 
 /* Method IDs for vector.image (user method range). */
 #define VIMM_Dummy              0x82001000UL
 
 #define VIMM_RENDER             (VIMM_Dummy + 0x00)
 #define VIMM_RENDERPIXELS       (VIMM_Dummy + 0x01)
 #define VIMM_HITTESTPOINT       (VIMM_Dummy + 0x02)
 #define VIMM_GETBOUNDINGBOX     (VIMM_Dummy + 0x03)
 #define VIMM_FITTOBOX           (VIMM_Dummy + 0x04)
 
 #define VIMM_ADDOBJECT          (VIMM_Dummy + 0x10)
 #define VIMM_BEGINGROUP         (VIMM_Dummy + 0x11)
 #define VIMM_ENDGROUP           (VIMM_Dummy + 0x12)
 #define VIMM_ADDLAYER           (VIMM_Dummy + 0x13)
 #define VIMM_ADDCOLOR           (VIMM_Dummy + 0x14)
 #define VIMM_ADDFONT            (VIMM_Dummy + 0x15)
 #define VIMM_CLEAR              (VIMM_Dummy + 0x16)
 #define VIMM_SETBOUNDS          (VIMM_Dummy + 0x17)
 #define VIMM_INVALIDATECACHE    (VIMM_Dummy + 0x18)
 #define VIMM_LOADSOURCE         (VIMM_Dummy + 0x19)
 
 /* Tag IDs (VIMA_*). */
 #define VIMA_Dummy              (TAG_USER + 0x5600)
 
 #define VIMA_CanvasLeft         (VIMA_Dummy +   0)
 #define VIMA_CanvasTop          (VIMA_Dummy +   1)
 #define VIMA_CanvasRight        (VIMA_Dummy +   2)
 #define VIMA_CanvasBottom       (VIMA_Dummy +   3)
 #define VIMA_CanvasUnits        (VIMA_Dummy +   4)
 #define VIMA_NativeWidth        (VIMA_Dummy +   5)
 #define VIMA_NativeHeight       (VIMA_Dummy +   6)
 
 #define VIMA_NativeDPI          (VIMA_Dummy +  20)
 #define VIMA_RenderDPI          (VIMA_Dummy +  21)
 #define VIMA_Scale              (VIMA_Dummy +  22)
 #define VIMA_FixedScale         (VIMA_Dummy +  23)
 #define VIMA_Rotation           (VIMA_Dummy +  24)
 #define VIMA_RenderQuality      (VIMA_Dummy +  25)
 #define VIMA_RenderFlags        (VIMA_Dummy +  26)
 #define VIMA_AntiAlias          (VIMA_Dummy +  27)
 #define VIMA_ColorMode          (VIMA_Dummy +  28)
 #define VIMA_BackgroundR        (VIMA_Dummy +  29)
 #define VIMA_BackgroundG        (VIMA_Dummy +  30)
 #define VIMA_BackgroundB        (VIMA_Dummy +  31)
 #define VIMA_FitToBox           (VIMA_Dummy +  32)
 #define VIMA_PreserveAspect     (VIMA_Dummy +  33)
 #define VIMA_ColorMap           (VIMA_Dummy +  34)
 #define VIMA_UnitsPerPoint      (VIMA_Dummy +  35)
 
 #define VIMA_DisplayList        (VIMA_Dummy +  70)
 #define VIMA_ObjectCount        (VIMA_Dummy +  71)
 #define VIMA_LayerCount         (VIMA_Dummy +  72)
 #define VIMA_LayerList          (VIMA_Dummy +  73)
 #define VIMA_ActiveLayer        (VIMA_Dummy +  74)
 #define VIMA_ColorTable         (VIMA_Dummy +  75)
 #define VIMA_NumColors          (VIMA_Dummy +  76)
 #define VIMA_Pool               (VIMA_Dummy +  77)
 #define VIMA_CachedBitMap       (VIMA_Dummy +  78)
 #define VIMA_DrawingFlags       (VIMA_Dummy +  79)
 
 /* Direct image usage follows bitmap.image source-file tags. */
 #define VIMA_SourceFile         BITMAP_SourceFile
 #define VIMA_SelectedSourceFile BITMAP_SelectSourceFile
 #define VIMA_DisabledSourceFile (VIMA_Dummy + 100)
 #define VIMA_SourceFormat       (VIMA_Dummy + 101)
 
 #define VIMSF_NATIVE            0
 
 /* DRWRF_* / DRWQ_* / DRWCM_* / DRWU_* shared with drawing.datatype */
 #define DRWRF_HINTING           (1L<<0)
 #define DRWRF_NO_BACKGROUND     (1L<<1)
 #define DRWRF_GRAYSCALE         (1L<<2)
 #define DRWRF_INVERT            (1L<<3)
 #define DRWRF_NO_TEXT           (1L<<4)
 #define DRWRF_NO_BITMAPS        (1L<<5)
 #define DRWRF_WIREFRAME         (1L<<6)
 #define DRWRF_NO_FILL           (1L<<7)
 #define DRWRF_HIDDEN_LAYERS     (1L<<8)
 #define DRWRF_BOUNDING_BOX      (1L<<9)
 
 #define DRWQ_DRAFT              0
 #define DRWQ_NORMAL             1
 #define DRWQ_HIGH               2
 #define DRWQ_PRESS              3
 
 #define DRWCM_PALETTE           0
 #define DRWCM_GREYSCALE         1
 #define DRWCM_TRUECOLOR         2
 #define DRWCM_MONOCHROME        3
 
 #define DRWU_INCHES             0
 #define DRWU_MILLIMETRES        1
 #define DRWU_POINTS             2
 #define DRWU_PIXELS             3
 #define DRWU_USER               4
 
 #define DRWF_HAS_LAYERS         (1L<< 0)
 #define DRWF_HAS_TEXT           (1L<< 1)
 #define DRWF_HAS_BITMAPS        (1L<< 2)
 #define DRWF_HAS_GROUPS         (1L<< 3)
 #define DRWF_HAS_BEZIER         (1L<< 4)
 #define DRWF_HAS_GRADIENT       (1L<< 5)
 #define DRWF_HAS_PATTERN        (1L<< 6)
 #define DRWF_HAS_FONTS          (1L<< 7)
 #define DRWF_HAS_DASH           (1L<< 8)
 #define DRWF_HAS_ARROW          (1L<< 9)
 #define DRWF_ANIMATED           (1L<<10)
 #define DRWF_INTERACTIVE        (1L<<11)
 
 #define DROT_POLYGON            0
 #define DROT_POLYLINE           1
 #define DROT_BEZIER             2
 #define DROT_RECT               3
 #define DROT_ELLIPSE            4
 #define DROT_TEXT               5
 #define DROT_TEXTPATH           6
 #define DROT_BITMAP             7
 #define DROT_GROUP              8
 #define DROT_FILL               9
 #define DROT_EXTERNAL          10
 #define DROT_USER             100
 
 #define DRFT_NONE               0
 #define DRFT_COLOR              1
 #define DRFT_OBJECTS            2
 #define DRFT_GRADIENT           3
 #define DRFT_PATTERN            4
 
 /* Gradient sub-type for da_GradType when da_FillType == DRFT_GRADIENT.       *
  * The numbering matches the DR2D GRAD chunk's "GradType" byte exactly        *
  * (G_CONIC=0, G_LINEAR=1, G_RADIAL=2, G_SHAPE=3) so the DR2D loader can copy *
  * it through unchanged.  DRGT_SHAPE (concentric copies of the object outline)*
  * is approximated by the renderer as a radial ramp about the gradient centre.*/
 #define DRGT_CONIC              0
 #define DRGT_LINEAR             1
 #define DRGT_RADIAL             2
 #define DRGT_SHAPE              3
 
 /* Colour-progression interpolation for da_GradInterp.  These mirror the       *
  * DR2D GRAD "GradInterp" byte (I_LINEAR=0 .. I_ARCSINE=4).  The renderer      *
  * applies the mode as a spatial warp of where each colour band lands, so a    *
  * uniform set of stops can ease in/out across the object.                     */
 #define DRGI_LINEAR             0
 #define DRGI_ACCEL              1
 #define DRGI_DECEL              2
 #define DRGI_SINE               3
 #define DRGI_ARCSINE            4
 
 #define DRJT_NONE               0
 #define DRJT_MITER              1
 #define DRJT_BEVEL              2
 #define DRJT_ROUND              3
 
 #define DRCT_BUTT               0
 #define DRCT_ROUND              1
 #define DRCT_SQUARE             2
 
 #define DRPF_NORMAL             0
 #define DRPF_BEZIER             (1L<<0)
 #define DRPF_MOVETO             (1L<<1)
 #define DRPF_CLOSE              (1L<<2)
 #define DRPF_END                (1L<<3)
 
 #define DRFS_BOLD               (1L<<0)
 #define DRFS_ITALIC             (1L<<1)
 
 #define DRLF_ACTIVE             (1L<<0)
 #define DRLF_DISPLAYED          (1L<<1)
 #define DRLF_LOCKED             (1L<<2)
 
 #ifndef DDTA_TRANSPARENT
 #define DDTA_TRANSPARENT        0x01000000UL
 #endif
 
 #define DRW_FIXED_ONE           0x00010000L
 #define DRW_FIXED_HALF          0x00008000L
 #define DRW_FIXED_ZERO          0x00000000L
 
 #define DRW_INT_TO_FIXED(x)     ((LONG)(x) << 16)
 #define DRW_FIXED_TO_INT(x)     ((LONG)(x) >> 16)
 #define DRW_FIXED_TO_FLOAT(x)   ((float)(x) / 65536.0f)
 
 
 /*****************************************************************************/
 /* Display-list structures                                                   */
 /*****************************************************************************/
 
 struct DrawPoint
 {
     LONG    dp_X;
     LONG    dp_Y;
     ULONG   dp_Flags;
 };
 
 struct DrawTextRun
 {
     ULONG   tr_Offset;      /* offset into the cleaned text string             */
     ULONG   tr_Length;      /* number of display bytes in this run             */
     UWORD   tr_FontID;      /* FONS register; 0 means use object default       */
     UWORD   tr_StyleFlags;  /* DRFS_*                                          */
     ULONG   tr_FillColor;   /* ARGB, DDTA_TRANSPARENT means object colour      */
     LONG    tr_PointSize;   /* 16.16 points; 0 means use object CharH          */
     LONG    tr_DX;          /* 16.16 point movement applied before this run    */
     LONG    tr_DY;          /* 16.16 point movement applied before this run    */
     LONG    tr_Rotation;    /* 16.16 degrees; stored for future transform use  */
     LONG    tr_Slant;       /* 16.16 degrees; stored for future transform use  */
     LONG    tr_Aspect;      /* 16.16 ratio; 1.0 or 0 means normal aspect       */
     STRPTR  tr_FontName;    /* optional \fn() face name, already pool-owned    */
 };
 
 struct DrawAttr
 {
     UBYTE   da_FillType;
     UBYTE   da_JoinType;
     UBYTE   da_CapType;
     UBYTE   da_EdgePattern;
     ULONG   da_FillColor;
     ULONG   da_EdgeColor;
     UWORD   da_LayerID;
     UWORD   da_ArrowHeads;
     LONG    da_EdgeThick;
     LONG    da_FillOpacity;
 
     /* Gradient extension (valid when da_FillType == DRFT_GRADIENT).          *
      * da_FillColor holds the START colour, da_FillColor2 the END colour.     *
      * da_GradType is DRGT_LINEAR / DRGT_RADIAL / DRGT_CONIC / DRGT_SHAPE.     *
      * da_GradAngle is the linear / conic direction in 16.16 fixed degrees;   *
      * da_GradCX / da_GradCY are the centre expressed as a 16.16 fraction     *
      * (0..1) of the object bounding box.                                     *
      *                                                                        *
      * Multi-stop ramps: when da_NumGradStops >= 2 and da_GradStops is        *
      * non-NULL, the renderer samples that array of evenly-spaced ARGB stops  *
      * (DR2D GRAD tables routinely carry 3..16 colours).  When it is < 2      *
      * or NULL the renderer falls back to the two-colour da_FillColor ->      *
      * da_FillColor2 ramp, so older producers keep working unchanged.         *
      * da_GradStops points into the load-time memory pool and is therefore    *
      * owned by the display list, not by the DrawAttr copy: several objects   *
      * that share one GRAD entry share the same pointer (read-only at draw).  */
     ULONG   da_FillColor2;
     UWORD   da_GradType;
     UWORD   da_GradFlags;
     LONG    da_GradAngle;
     LONG    da_GradCX;
     LONG    da_GradCY;
     UWORD   da_GradInterp;     /* DRGI_* spatial colour-progression warp      */
     UWORD   da_NumGradStops;   /* count in da_GradStops; < 2 => 2-colour ramp */
     ULONG  *da_GradStops;      /* pool-owned ARGB stop array, or NULL         */
 
     /* Tiled object-fill (valid when da_FillType == DRFT_OBJECTS).  Holds the *
      * fill-pattern ID that selects a DROT_FILL prototype in the display list.*/
     UWORD   da_FillID;
     UWORD   da_FillPad;
 };
 
 struct DrawObj
 {
     struct MinNode do_Node;
     UWORD   do_Type;
     UWORD   do_Pad0;
     ULONG   do_Flags;
     struct DrawAttr do_Attrs;
     LONG    do_XMin, do_YMin;
     LONG    do_XMax, do_YMax;
     union {
         struct {
             struct DrawPoint *do_Points;
             ULONG   do_NumPoints;
         } Poly;
         struct {
             LONG    do_Pad[2];
         } Shape;
         struct {
             LONG    do_BaseX;
             LONG    do_BaseY;
             LONG    do_CharW;
             LONG    do_CharH;
             LONG    do_Rotation;
             STRPTR  do_String;
             UWORD   do_NumChars;
             UWORD   do_WhichFont;
             struct DrawTextRun *do_Runs;
             UWORD   do_NumRuns;
             UWORD   do_TextPad;
         } Text;
         struct {
             STRPTR  do_String;
             UWORD   do_NumChars;
             UWORD   do_WhichFont;
             LONG    do_CharW;
             LONG    do_CharH;
             struct DrawPoint *do_Path;
             ULONG   do_NumPath;
             struct DrawTextRun *do_Runs;
             UWORD   do_NumRuns;
             UWORD   do_TextPad;
         } TextPath;
         struct {
             LONG    do_XPos;
             LONG    do_YPos;
             LONG    do_Width;
             LONG    do_Height;
             LONG    do_Rotation;
             STRPTR  do_Path;
         } Bitmap;
         struct {
             struct List do_Children;
             ULONG       do_NumChildren;
         } Group;
         struct {
             UWORD       do_FillID;
             UWORD       do_Pad;
             struct List do_Children;
         } Fill;
         struct {
             UWORD       do_CallBacks;
             UWORD       do_NameLen;
             STRPTR      do_Name;
             struct DrawObj *do_Child;
         } External;
         struct {
             APTR        do_Data;
             ULONG       do_Length;
         } User;
     } do_Data;
 };
 
 struct DrawLayer
 {
     struct MinNode dl_Node;
     UWORD   dl_ID;
     UWORD   dl_Pad;
     ULONG   dl_Flags;
     UBYTE   dl_Name[24];
 };
 
 struct DrawFont
 {
     struct MinNode df_Node;
     UBYTE   df_FontID;
     UBYTE   df_Proportional;
     UBYTE   df_Serif;
     UBYTE   df_StyleFlags;     /* DRFS_* derived from mapped face name         */
     STRPTR  df_Name;
 };
 
 
 /*****************************************************************************/
 /* Method messages (shared layout with drawing.datatype DDTM_* messages)     */
 /*****************************************************************************/
 
 struct vecRender
 {
     ULONG   MethodID;
     struct RastPort   *vecr_RPort;
     LONG    vecr_Left;
     LONG    vecr_Top;
     LONG    vecr_Width;
     LONG    vecr_Height;
     LONG    vecr_FixedScaleX;
     LONG    vecr_FixedScaleY;
     UWORD   vecr_Rotation;
     UWORD   vecr_RenderQuality;
     ULONG   vecr_RenderFlags;
     ULONG  *vecr_AbortPtr;
     ULONG  *vecr_ProgressPtr;
     struct ColorMap *vecr_ColorMap;
 };
 
 struct vecRenderPixels
 {
     ULONG   MethodID;
     APTR    vecrp_PixelData;
     ULONG   vecrp_PixelFormat;
     LONG    vecrp_PixelArrayMod;
     LONG    vecrp_Width;
     LONG    vecrp_Height;
     LONG    vecrp_FixedScaleX;
     LONG    vecrp_FixedScaleY;
     UWORD   vecrp_Rotation;
     UWORD   vecrp_RenderQuality;
     ULONG   vecrp_RenderFlags;
     ULONG  *vecrp_AbortPtr;
     ULONG  *vecrp_ProgressPtr;
     struct ColorMap *vecrp_ColorMap;
 };
 
 struct vecHitTestPoint
 {
     ULONG   MethodID;
     LONG    vech_X;
     LONG    vech_Y;
     struct DrawObj **vech_Result;
 };
 
 struct vecGetBoundingBox
 {
     ULONG   MethodID;
     LONG    *vecbb_XMin;
     LONG    *vecbb_YMin;
     LONG    *vecbb_XMax;
     LONG    *vecbb_YMax;
 };
 
 struct vecFitToBox
 {
     ULONG   MethodID;
     LONG    vecf_BoxWidth;
     LONG    vecf_BoxHeight;
     BOOL    vecf_PreserveAspect;
     LONG   *vecf_FixedScaleX;
     LONG   *vecf_FixedScaleY;
 };
 
 struct vecAddObject
 {
     ULONG   MethodID;
     struct DrawObj *veca_Obj;
 };
 
 struct vecBeginGroup
 {
     ULONG   MethodID;
     UWORD   vecbg_Type;
     UWORD   vecbg_Pad;
     ULONG   vecbg_ExpectCount;
     ULONG   vecbg_FillID;          /* DROT_FILL pattern id (else 0)         */
 };
 
 struct vecEndGroup
 {
     ULONG   MethodID;
 };
 
 struct vecAddLayer
 {
     ULONG   MethodID;
     struct DrawLayer *vecal_Layer;
 };
 
 struct vecAddColor
 {
     ULONG   MethodID;
     UBYTE   vecac_R;
     UBYTE   vecac_G;
     UBYTE   vecac_B;
     UBYTE   vecac_Pad;
 };
 
 struct vecAddFont
 {
     ULONG   MethodID;
     struct DrawFont *vecaf_Font;
 };
 
 struct vecClear
 {
     ULONG   MethodID;
 };
 
 struct vecSetBounds
 {
     ULONG   MethodID;
     LONG    vecsb_Left;
     LONG    vecsb_Top;
     LONG    vecsb_Right;
     LONG    vecsb_Bottom;
     ULONG   vecsb_Units;
 };
 
 struct vecInvalidateCache
 {
     ULONG   MethodID;
 };
 
 struct vecLoadSource
 {
     ULONG   MethodID;
     CONST_STRPTR vecls_FileName;
     ULONG   vecls_Format;
 };
 
 
 /*****************************************************************************/
 /* ObtainVectorImageClass -- exported by vector.image.library                */
 /*****************************************************************************/
 
 Class *ObtainVectorImageClass (void);
 
 
 #endif /* IMAGES_VECTORCLASS_H */
 
 