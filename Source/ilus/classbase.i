; SPDX-License-Identifier: BSD-2-Clause
;
; Copyright 2026 amigazen project
;

	IFND CLASSBASE_I
CLASSBASE_I SET	1

;------------------------------------------------------------------------------
; Internal assembly header for ilus.datatype.
;------------------------------------------------------------------------------

	INCLUDE	"exec/types.i"
	INCLUDE	"exec/libraries.i"
	INCLUDE	"exec/lists.i"
	INCLUDE	"exec/semaphores.i"
	INCLUDE	"utility/tagitem.i"

;------------------------------------------------------------------------------

   STRUCTURE ClassBase,LIB_SIZE
	UWORD	cb_UsageCnt
	ULONG	cb_SegList
	ULONG	cb_SysBase
	ULONG	cb_DOSBase
	ULONG	cb_IntuitionBase
	ULONG	cb_UtilityBase
	ULONG	cb_GfxBase
	ULONG	cb_DataTypesBase
	ULONG	cb_IFFParseBase
	ULONG	cb_SuperClassBase
	STRUCT	cb_Lock,SS_SIZE
	ULONG	cb_Class
   LABEL ClassBase_SIZEOF

;------------------------------------------------------------------------------

	LIBINIT

	LIBDEF	_LVODispatch

;------------------------------------------------------------------------------

CALL MACRO <Function_Name>
	xref _LVO\1
	jsr _LVO\1(A6)
     ENDM

;------------------------------------------------------------------------------

GO   MACRO <Function_Name>
	xref _LVO\1
	jmp _LVO\1(A6)
     ENDM

;------------------------------------------------------------------------------

	ENDC	; CLASSBASE_I
