/*
 * GoTube — PSP callback handling
 * Sets up the PSP exit callback thread.
 */
#include "gotube.h"

static int exit_callback(int arg1, int arg2, void *common)
{
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    int cbid = sceKernelCreateCallback("exit_cb", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

int setup_callbacks(void)
{
    int thid = sceKernelCreateThread("cb_thread", callback_thread,
                                      0x11, 0xFA0, 0, NULL);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, NULL);
    }
    return thid;
}

void go_callback_process(void)
{
    /* Exit delivery occurs on callback_thread via sceKernelSleepThreadCB. */
}

/* Preserve the original main-loop subsystem boundary while no asynchronous
 * callback-chain work is queued by the reconstructed subsystems. */
void go_callback_tick(void)
{
}
