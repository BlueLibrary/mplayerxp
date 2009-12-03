#ifndef TV_H
#define TV_H

extern int tv_param_on;

#ifdef USE_TV
typedef struct tvi_info_s
{
    const char *name;
    const char *short_name;
    const char *author;
    const char *comment;
} tvi_info_t;

typedef struct tvi_functions_s
{
    int (*init)();
    int (*uninit)();
    int (*control)();
    int (*start)();
    double (*grab_video_frame)();
#ifdef HAVE_TV_BSDBT848
    double (*grabimmediate_video_frame)();
#endif
    int (*get_video_framesize)();
    double (*grab_audio_frame)();
    int (*get_audio_framesize)();
} tvi_functions_t;

typedef struct tvi_handle_s {
    tvi_info_t		*info;
    tvi_functions_t	*functions;
    void		*priv;
    int 		seq;

    /* specific */
    int			norm;
    int			chanlist;
    struct CHANLIST	*chanlist_s;
    int			channel;
} tvi_handle_t;


#define TVI_CONTROL_FALSE		0
#define TVI_CONTROL_TRUE		1
#define TVI_CONTROL_NA			-1
#define TVI_CONTROL_UNKNOWN		-2

/* ======================== CONTROLS =========================== */

/* GENERIC controls */
#define TVI_CONTROL_IS_AUDIO		0x1
#define TVI_CONTROL_IS_VIDEO		0x2
#define TVI_CONTROL_IS_TUNER		0x3
#ifdef HAVE_TV_BSDBT848
#define TVI_CONTROL_IMMEDIATE       0x4
#endif

/* VIDEO controls */
#define TVI_CONTROL_VID_GET_FPS		0x101
#define TVI_CONTROL_VID_GET_PLANES	0x102
#define TVI_CONTROL_VID_GET_BITS	0x103
#define TVI_CONTROL_VID_CHK_BITS	0x104
#define TVI_CONTROL_VID_SET_BITS	0x105
#define TVI_CONTROL_VID_GET_FORMAT	0x106
#define TVI_CONTROL_VID_CHK_FORMAT	0x107
#define TVI_CONTROL_VID_SET_FORMAT	0x108
#define TVI_CONTROL_VID_GET_WIDTH	0x109
#define TVI_CONTROL_VID_CHK_WIDTH	0x110
#define TVI_CONTROL_VID_SET_WIDTH	0x111
#define TVI_CONTROL_VID_GET_HEIGHT	0x112
#define TVI_CONTROL_VID_CHK_HEIGHT	0x113
#define TVI_CONTROL_VID_SET_HEIGHT	0x114
#define TVI_CONTROL_VID_GET_BRIGHTNESS	0x115
#define TVI_CONTROL_VID_SET_BRIGHTNESS	0x116
#define TVI_CONTROL_VID_GET_HUE		0x117
#define TVI_CONTROL_VID_SET_HUE		0x118
#define TVI_CONTROL_VID_GET_SATURATION	0x119
#define TVI_CONTROL_VID_SET_SATURATION	0x11a
#define TVI_CONTROL_VID_GET_CONTRAST	0x11b
#define TVI_CONTROL_VID_SET_CONTRAST	0x11c
#define TVI_CONTROL_VID_GET_PICTURE	0x11d
#define TVI_CONTROL_VID_SET_PICTURE	0x11e

/* TUNER controls */
#define TVI_CONTROL_TUN_GET_FREQ	0x201
#define TVI_CONTROL_TUN_SET_FREQ	0x202
#define TVI_CONTROL_TUN_GET_TUNER	0x203	/* update priv->tuner struct for used input */
#define TVI_CONTROL_TUN_SET_TUNER	0x204	/* update priv->tuner struct for used input */
#define TVI_CONTROL_TUN_GET_NORM	0x205
#define TVI_CONTROL_TUN_SET_NORM	0x206

/* AUDIO controls */
#define TVI_CONTROL_AUD_GET_FORMAT	0x301
#define TVI_CONTROL_AUD_GET_SAMPLERATE	0x302
#define TVI_CONTROL_AUD_GET_SAMPLESIZE	0x303
#define TVI_CONTROL_AUD_GET_CHANNELS	0x304
#define TVI_CONTROL_AUD_SET_SAMPLERATE	0x305

/* SPECIFIC controls */
#define TVI_CONTROL_SPC_GET_INPUT	0x401	/* set input channel (tv,s-video,composite..) */
#define TVI_CONTROL_SPC_SET_INPUT	0x402	/* set input channel (tv,s-video,composite..) */

extern __FASTCALL__ tvi_handle_t *tv_begin(void);
extern int __FASTCALL__ tv_init(tvi_handle_t *tvh);
extern int __FASTCALL__ tv_uninit(tvi_handle_t *tvh);

int __FASTCALL__ tv_set_color_options(tvi_handle_t *tvh, int opt, int val);
#define TV_COLOR_BRIGHTNESS	1
#define TV_COLOR_HUE		2
#define TV_COLOR_SATURATION	3
#define TV_COLOR_CONTRAST	4

int __FASTCALL__ tv_step_channel(tvi_handle_t *tvh, int direction);
#define TV_CHANNEL_LOWER	1
#define TV_CHANNEL_HIGHER	2

int __FASTCALL__ tv_step_norm(tvi_handle_t *tvh);
int __FASTCALL__ tv_step_chanlist(tvi_handle_t *tvh);

#define TV_NORM_PAL		1
#define TV_NORM_NTSC		2
#define TV_NORM_SECAM		3

#endif /* USE_TV */

#endif /* TV_H */
