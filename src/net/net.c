/*
 * GoTube — connection-state poll.
 * FUN_00009804 defines online as a successful apctl query returning state 4.
 */
#include "gotube.h"
#include <pspnet_apctl.h>

int g_net_online = 0;

void go_network_tick(void)
{
    int state = 0;
    g_net_online = sceNetApctlGetState(&state) >= 0 && state == 4;
}
