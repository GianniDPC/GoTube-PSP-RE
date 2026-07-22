/* Firmware-NID bridge derived from PMPlayer Advance/cooleyesBridge.
 * Copyright (C) 2007-2011 cooleyes <eyes.cooleyes@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <pspsdk.h>
#include <pspkernel.h>

PSP_MODULE_INFO("cooleyesBridge", 0x1006, 1, 1);
PSP_MAIN_THREAD_ATTR(0);

int sceMeBootStart(int type);
int sceMeBootStart371(int type);
int sceMeBootStart380(int type);
int sceMeBootStart395(int type);
int sceMeBootStart500(int type);
int sceMeBootStart620(int type);
int sceMeBootStart635(int type);
int sceMeBootStart660(int type);

int cooleyesMeBootStart(int version, int type)
{
    unsigned int k1 = pspSdkSetK1(0);
    int result;
    if (version < 0x03070000) result = sceMeBootStart(type);
    else if (version < 0x03080000) result = sceMeBootStart371(type);
    else if (version < 0x03090500) result = sceMeBootStart380(type);
    else if (version < 0x05000000) result = sceMeBootStart395(type);
    else if (version < 0x06020000) result = sceMeBootStart500(type);
    else if (version < 0x06030500) result = sceMeBootStart620(type);
    else if (version < 0x06060000) result = sceMeBootStart635(type);
    else result = sceMeBootStart660(type);
    pspSdkSetK1(k1);
    return result;
}

int module_start(SceSize args, void *argp) { return 0; }
int module_stop(SceSize args, void *argp) { return 0; }
