; SPDX-License-Identifier: BSD-2-Clause
;
; Copyright 2026 amigazen project
;

VERSION		EQU	42
REVISION	EQU	1
DATE	MACRO
		dc.b	'13.6.26'
	ENDM
VERS	MACRO
		dc.b	'ilus 42.1'
	ENDM
VSTRING	MACRO
		dc.b	'ilus 42.1 (13.6.26)',13,10,0
	ENDM
VERSTAG	MACRO
		dc.b	0,'$VER: ilus 42.1 (13.6.26)',0
	ENDM
