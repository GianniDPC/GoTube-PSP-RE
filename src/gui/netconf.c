/*
 * GoTube — Network Configuration dialog (PSP system SSID selector)
 * PSP utility dialog wrappers correspond to VAs 0x279fc and 0x27ad0.
 */
#include "gotube.h"
#include <psputility.h>
#include <psputility_netconf.h>

static pspUtilityNetconfData g_netconf;
static int g_netconf_active = 0;
static int g_netconf_shutting_down = 0;
int g_netconf_done = 0;
int g_netconf_connected = 0;

static int netconf_open_action(int action)
{
    if (g_netconf_active)
        return -1;

    memset(&g_netconf, 0, sizeof(g_netconf));
    g_netconf.base.size        = sizeof(g_netconf);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE,
                                &g_netconf.base.language);
    g_netconf.base.buttonSwap  = go_utility_button_swap();
    g_netconf.base.graphicsThread = 0x11;
    g_netconf.base.accessThread   = 0x13;
    g_netconf.base.fontThread     = 0x12;
    g_netconf.base.soundThread    = 0x10;
    g_netconf.action          = action;
    g_netconf.hotspot         = 1;
    g_netconf.wifisp          = 1;
    g_netconf.adhocparam      = NULL;

    {
        int ret = sceUtilityNetconfInitStart(&g_netconf);
        if (ret < 0)
            return -1;
    }

    g_netconf_active = 1;
    g_netconf_shutting_down = 0;
    g_netconf_done = 0;
    g_netconf_connected = 0;
    return 0;
}

int go_netconf_open(void)
{
    return netconf_open_action(PSP_NETCONF_ACTION_CONNECTAP);
}

int go_netconf_open_status(void)
{
    /* Native menu action 0x6b reaches VA 0x27ad0, whose parameter block
     * stores action 1: the PSP connection-status dialog. */
    return netconf_open_action(PSP_NETCONF_ACTION_DISPLAYSTATUS);
}

int go_netconf_update(void)
{
    int status;

    if (!g_netconf_active)
        return 0;

    status = sceUtilityNetconfGetStatus();

    /* Native VA 0x9a78 updates only status 2 (VISIBLE) and starts shutdown
     * only at status 3 (QUIT).  Calling Update unconditionally can leave the
     * hardware utility in its terminal state, which kept g_netconf_active set
     * and made go_input_poll reject every controller event. */
    if (status == PSP_UTILITY_DIALOG_VISIBLE) {
        sceUtilityNetconfUpdate(2);
    } else if (status == PSP_UTILITY_DIALOG_QUIT &&
               !g_netconf_shutting_down) {
        sceUtilityNetconfShutdownStart();
        g_netconf_shutting_down = 1;
    }

    if (status == PSP_UTILITY_DIALOG_FINISHED ||
        (status == PSP_UTILITY_DIALOG_NONE && g_netconf_shutting_down)) {
        g_netconf_active = 0;
        g_netconf_shutting_down = 0;
        g_netconf_done = 1;
        g_netconf_connected = g_netconf.hotspot_connected != 0;
        if (g_netconf_connected)
            return 2;  /* DONE — connected */
        else
            return 3;  /* CANCELLED — no connection */
    }

    return 1; /* RUNNING */
}

int go_netconf_is_active(void) { return g_netconf_active; }
