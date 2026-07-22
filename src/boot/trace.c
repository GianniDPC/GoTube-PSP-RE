/* GoTube — file-based trace logger for boot debugging */
#include "gotube.h"

void gt_trace(const char *msg)
{
#ifdef GT_ENABLE_TRACE
    SceUID fd = sceIoOpen("ms0:/gt_trace.txt",
                          PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, msg, strlen(msg));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
#else
    (void)msg;
#endif
}

void gt_trace_ptr(const char *msg, const void *p)
{
    char buf[160];
    int n = 0;
    const char *s = msg;
    while (*s && n < 120) buf[n++] = *s++;
    buf[n++] = ' ';
    /* hex of pointer */
    unsigned int v = (unsigned int)(unsigned long)p;
    buf[n++] = '0'; buf[n++] = 'x';
    int i;
    for (i = 7; i >= 0; i--) {
        int d = (v >> (i*4)) & 0xF;
        buf[n++] = d < 10 ? '0'+d : 'a'+d-10;
    }
    buf[n] = 0;
    gt_trace(buf);
}
