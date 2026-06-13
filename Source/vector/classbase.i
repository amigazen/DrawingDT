; SPDX-License-Identifier: BSD-2-Clause
;
; Copyright 2026 amigazen project
;

	IFND VECTOR_CLASSBASE_I
VECTOR_CLASSBASE_I SET	1

	INCLUDE	"exec/types.i"
	INCLUDE	"exec/libraries.i"
	INCLUDE	"exec/lists.i"
	INCLUDE	"exec/semaphores.i"

   STRUCTURE ClassBase,LIB_SIZE
	UWORD	cb_UsageCnt
	ULONG	cb_SegList
	ULONG	cb_SysBase
	ULONG	cb_DOSBase
	ULONG	cb_IntuitionBase
	ULONG	cb_GfxBase
	ULONG	cb_UtilityBase
	ULONG	cb_DiskfontBase
	ULONG	cb_BulletBase
	STRUCT	cb_Lock,SS_SIZE
	ULONG	cb_Class
   LABEL ClassBase_SIZEOF

	LIBINIT

	ENDC
