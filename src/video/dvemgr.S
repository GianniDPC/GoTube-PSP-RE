	.set noreorder

#include "pspstub.s"

/* Original import descriptor at VA 0x4e2188 and NIDs at 0x4e2548.  The
 * 0x40000000 flag keeps this weak until dvemgr.prx is loaded on PSP-2000. */
	STUB_START "pspDveManager",0x40090000,0x00020005
	STUB_FUNC 0x2ACFCB6D,pspDveManagerCheckVideoOut
	STUB_FUNC 0xF9C86C73,pspDveManagerSetVideoOut
	STUB_END
