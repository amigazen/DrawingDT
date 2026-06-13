; SPDX-License-Identifier: BSD-2-Clause
;
; Copyright 2026 amigazen project
;

VERSION		EQU	42
REVISION	EQU	1
DATE	MACRO
		dc.b	'26.5.25'
	ENDM
VERS	MACRO
		dc.b	'vector 42.1'
	ENDM
VSTRING	MACRO
		dc.b	'vector 42.1 (26.5.25)',13,10,0
	ENDM
VERSTAG	MACRO
		dc.b	0,'$VER: vector 42.1 (26.5.25)',0
	ENDM
