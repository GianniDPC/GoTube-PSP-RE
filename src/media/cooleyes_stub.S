	.set noreorder

#include "pspstub.s"

	STUB_START "cooleyesBridge",0x40090000,0x00010005
	STUB_FUNC 0x90CC7DD7,cooleyesMeBootStart
	STUB_END
