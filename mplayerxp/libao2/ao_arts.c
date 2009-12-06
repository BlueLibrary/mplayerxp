/*
 * aRts (KDE analogue Real-Time synthesizer) audio output driver for MPlayerXP
 *
 * copyright (c) 2002 Michele Balistreri <brain87@gmx.net>
 *
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <artsc.h>
#include <stdio.h>

#include "mp_config.h"
#include "audio_out.h"
#include "audio_out_internal.h"
#include "postproc/af_format.h"
#include "afmt.h"
#include "ao_msg.h"

#define OBTAIN_BITRATE(a) (((a != AFMT_U8) && (a != AFMT_S8)) ? 16 : 8)

/* Feel free to experiment with the following values: */
#define ARTS_PACKETS 10 /* Number of audio packets */
#define ARTS_PACKET_SIZE_LOG2 11 /* Log2 of audio packet size */

static arts_stream_t stream;

static const ao_info_t info =
{
    "aRts audio output",
    "arts",
    "Michele Balistreri <brain87@gmx.net>",
    ""
};

LIBAO_EXTERN(arts)

static int control(int cmd, long arg)
{
	return CONTROL_UNKNOWN;
}

static int init(int flags)
{
	int err;
	int frag_spec;

	if( (err=arts_init()) ) {
		MSG_ERR("aRts: can't init: %s\n", arts_error_text(err));
		return 0;
	}
	MSG_INFO("aRts: connected to server\n");
}

static int __FASTCALL__ configure(int rate,int channels,int format)
{
	int frag_spec;
	/*
	 * arts supports 8bit unsigned and 16bit signed sample formats
	 * (16bit apparently in little endian format, even in the case
	 * when artsd runs on a big endian cpu).
	 *
	 * Unsupported formats are translated to one of these two formats
	 * using mplayer's audio filters.
	 */
	switch (format) {
	case AFMT_U8:
	case AFMT_S8:
	    format = AFMT_U8;
	    break;
	default:
	    format = AFMT_S16_LE;    /* artsd always expects little endian?*/
	    break;
	}

	ao_data.format = format;
	ao_data.channels = channels;
	ao_data.samplerate = rate;
	ao_data.bps = (rate*channels);

	if(format != AFMT_U8 && format != AFMT_S8)
		ao_data.bps*=2;

	stream=arts_play_stream(rate, OBTAIN_BITRATE(format), channels, "MPlayerXP");

	if(stream == NULL) {
		MSG_ERR("ARts: Can't open stream\n");
		arts_free();
		return 0;
	}

	/* Set the stream to blocking: it will not block anyway, but it seems */
	/* to be working better */
	arts_stream_set(stream, ARTS_P_BLOCKING, 1);
	frag_spec = ARTS_PACKET_SIZE_LOG2 | ARTS_PACKETS << 16;
	arts_stream_set(stream, ARTS_P_PACKET_SETTINGS, frag_spec);
	ao_data.buffersize = arts_stream_get(stream, ARTS_P_BUFFER_SIZE);
	MSG_INFO("aRts: Stream opened\n");

	MSG_V("aRts: buffersize=%u\n",ao_data.buffersize);
	MSG_V("aRts: buffersize=%u\n", arts_stream_get(stream, ARTS_P_PACKET_SIZE));

	return 1;
}

static void uninit(void)
{
	arts_close_stream(stream);
	arts_free();
}

static int play(void* data,int len,int flags)
{
	return arts_write(stream, data, len);
}

static void audio_pause(void)
{
}

static void audio_resume(void)
{
}

static void reset(void)
{
}

static int get_space(void)
{
	return arts_stream_get(stream, ARTS_P_BUFFER_SPACE);
}

static float get_delay(void)
{
	return ((float) (ao_data.buffersize - arts_stream_get(stream,
		ARTS_P_BUFFER_SPACE))) / ((float) ao_data.bps);
}

