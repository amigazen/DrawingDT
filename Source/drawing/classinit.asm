; SPDX-License-Identifier: BSD-2-Clause
;
; Copyright 2026 amigazen project
;

; classinit.asm
;
;  Library initialisation table and ROMTAG for drawing.datatype.
;  Mirrors example datatype source code classinit.asm.  The only
;  exported public function beyond the four lifecycle entry points is
;  ObtainDrawingEngine() which returns the root IClass pointer.  The
;  bundled subclasses resolve us by public class name (DRAWINGDTCLASS)
;  in MakeClass() and no longer need it; it is retained as a standard
;  datatype convenience/ABI entry (cf. picture.datatype) so callers can
;  NewObjectA(parent, ...) on us directly without going through
;  datatypes.library/NewDTObject.
;
;---------------------------------------------------------------------------

        NOLIST

        INCLUDE "exec/types.i"
        INCLUDE "exec/libraries.i"
        INCLUDE "exec/lists.i"
        INCLUDE "exec/alerts.i"
        INCLUDE "exec/initializers.i"
        INCLUDE "exec/resident.i"
        INCLUDE "libraries/dos.i"

        INCLUDE "drawing_rev.i"
        INCLUDE "classbase.i"

        LIST

;---------------------------------------------------------------------------

        XREF    _LibInit
        XREF    _LibOpen
        XREF    _LibClose
        XREF    _LibExpunge

        XREF    ENDCODE

;---------------------------------------------------------------------------

        SECTION TEXT,CODE

; First executable location, must return an error to the caller
Start:
        moveq   #-1,d0
        rts

;---------------------------------------------------------------------------

ROMTAG:
        DC.W    RTC_MATCHWORD           ; UWORD RT_MATCHWORD
        DC.L    ROMTAG                  ; APTR  RT_MATCHTAG
        DC.L    ENDCODE                 ; APTR  RT_ENDSKIP
        DC.B    RTF_AUTOINIT            ; UBYTE RT_FLAGS
        DC.B    VERSION                 ; UBYTE RT_VERSION
        DC.B    NT_LIBRARY              ; UBYTE RT_TYPE
        DC.B    0                       ; BYTE  RT_PRI
        DC.L    LibName                 ; APTR  RT_NAME
        DC.L    LibId                   ; APTR  RT_IDSTRING
        DC.L    LibInitTable            ; APTR  RT_INIT

LibName DC.B 'drawing.datatype',0
LibId   VSTRING

        CNOP    0,4

LibInitTable:
        DC.L    ClassBase_SIZEOF
        DC.L    LibFuncTable
        DC.L    LibDataTable
        DC.L    _LibInit

V_DEF   MACRO
        DC.W    \1+(*-LibFuncTable)
        ENDM

LibFuncTable:
        DC.W    -1
        V_DEF   _LibOpen
        V_DEF   _LibClose
        V_DEF   _LibExpunge
        V_DEF   LibReserved

        V_DEF   ObtainDrawingEngine

        DC.W    -1

LibDataTable:
        INITBYTE   LN_TYPE,NT_LIBRARY
        INITLONG   LN_NAME,LibName
        INITBYTE   LIB_FLAGS,(LIBF_SUMUSED!LIBF_CHANGED)
        INITWORD   LIB_VERSION,VERSION
        INITWORD   LIB_REVISION,REVISION
        INITLONG   LIB_IDSTRING,LibId
        DC.W       0

        CNOP    0,4

;---------------------------------------------------------------------------

LibReserved:
        moveq   #0,d0
        rts

;---------------------------------------------------------------------------
; ObtainDrawingEngine
;
; Returns a pointer to the root IClass for drawing.datatype, suitable
; for subclasses that call NewObjectA(parent, ...) on us directly
; without going through datatypes.library/MakeDataType machinery.
; Same pattern as picture.datatype/ObtainDTEngine
;---------------------------------------------------------------------------

ObtainDrawingEngine:
        move.l  cb_Class(a6),d0
        rts

;---------------------------------------------------------------------------
        END
