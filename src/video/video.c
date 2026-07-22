#include "gotube.h"
#include <kubridge.h>

extern int pspDveManagerCheckVideoOut(void);
extern int pspDveManagerSetVideoOut(int, int, int, int, int, int, int);

static int cable_state = -1;
static unsigned poll_frame;

int go_video_output_poll(void)
{
    int state;
    if (kuKernelGetModel() != 1) return 0;
    if ((poll_frame++ & 31) != 0) return cable_state;
    state = pspDveManagerCheckVideoOut();
    if (state < 0) return cable_state;
    if (state != cable_state) {
        cable_state = state;
    }
    return cable_state;
}

int go_video_output_apply(int state)
{
    if (kuKernelGetModel() != 1) return -1;
    if (state == 1)
        return pspDveManagerSetVideoOut(2, 0x1d1, 0x2d0, 0x1f7, 1, 0xf, 0);
    if (state == 2 && g_video_out_mode)
        return pspDveManagerSetVideoOut(0, 0x1d1, 0x2d0, 0x1f7, 1, 0xf, 0);
    if (state == 2)
        return pspDveManagerSetVideoOut(0, 0x1d2, 0x2d0, 0x1e0, 1, 0xf, 0);
    return pspDveManagerSetVideoOut(0, 0, 0x1e0, 0x110, 1, 0xf, 0);
}
