; SPDX-License-Identifier: BSD-2-Clause
;
; Copyright 2026 amigazen project
;

VERSION		EQU	42
REVISION	EQU	1
DATE	MACRO
		dc.b	'25.5.26'
	ENDM
VERS	MACRO
		dc.b	'drawing 42.1'
	ENDM
VSTRING	MACRO
		dc.b	'drawing 42.1 (25.5.26)',13,10,0
	ENDM
VERSTAG	MACRO
		dc.b	0,'$VER: drawing 42.1 (25.5.26)',0
	ENDM
