/* The reconstructed player owns its blocking PSP SRC output on the decoder
 * worker.  The historical main loop still has an audio tick slot, so retain
 * the call boundary without duplicating channel ownership here. */
#include "gotube.h"

void go_audio_tick(void)
{
    /* Audio is advanced by src/media/player.c. */
}
