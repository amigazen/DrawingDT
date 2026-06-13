; SPDX-License-Identifier: BSD-2-Clause
;
; Copyright 2026 amigazen project
;

; classinit.asm -- ROMTAG and library init for vector.image

        NOLIST

        INCLUDE "exec/types.i"
        INCLUDE "exec/libraries.i"
        INCLUDE "exec/lists.i"
        INCLUDE "exec/alerts.i"
        INCLUDE "exec/initializers.i"
        INCLUDE "exec/resident.i"
        INCLUDE "libraries/dos.i"

        INCLUDE "vector_rev.i"
        INCLUDE "classbase.i"

        LIST

        XREF    _LibInit
        XREF    _LibOpen
        XREF    _LibClose
        XREF    _LibExpunge

        XREF    ENDCODE

        SECTION TEXT,CODE

Start:
        moveq   #-1,d0
        rts

ROMTAG:
        DC.W    RTC_MATCHWORD
        DC.L    ROMTAG
        DC.L    ENDCODE
        DC.B    RTF_AUTOINIT
        DC.B    VERSION
        DC.B    NT_LIBRARY
        DC.B    0
        DC.L    LibName
        DC.L    LibId
        DC.L    LibInitTable

LibName DC.B 'vector.image',0
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
        V_DEF   ObtainVectorImageClass
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

LibReserved:
        moveq   #0,d0
        rts

ObtainVectorImageClass:
        move.l  cb_Class(a6),d0
        rts

        END
