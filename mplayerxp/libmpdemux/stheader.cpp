#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
// read vmpideo frame

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mpxp_help.h"
#include "sub_cc.h"

#include "libmpstream2/stream.h"
#include "demuxer.h"
#include "stheader.h"
#include "parse_es.h"
#include "mpeg_hdr.h"
#include "mplayerxp.h"
#include "demux_msg.h"

extern void ty_processuserdata(const unsigned char* buf, int len );
namespace	usr {

sh_audio_t::~sh_audio_t(){
    MSG_V("DEMUXER: freeing sh_audio at %p  \n",this);
    if(wf) delete wf;
}

sh_video_t::~sh_video_t(){
    MSG_V("DEMUXER: freeing sh_video at %p  \n",this);
    if(bih) delete bih;
}

/* biCompression constant */
static const int BI_RGB=0L;

static mp_mpeg_header_t picture;
static int telecine=0;
static float telecine_cnt=-2.5;

#if 0
/* aspect 0 means don't prescale */
static float mpeg1_aspects[]=
{
  0.0000, 0.0000, 1.4848, 1.4222, 1.3131, 1.2415, 1.1853, 1.1912,
  1.0666, 1.0188, 1.0255, 1.0695, 1.1250, 1.1575, 1.2015, 0.0000
};

static float mpeg2_aspects[]=
{
  0.0000, 0.0000, 1.3333, 1.7777, 2.2100, 0.8055, 0.8437, 0.8935,
  0.9375, 0.9815, 1.0255, 1.0695, 1.1250, 1.1575, 1.2015, 0.0000
};

static float mpeg_framerates[]=
{
   0.000, 23.976, 24.000, 25.000, 29.970, 30.000, 50.000, 59.940, 60.000,
  15.000, /* Xing's 15fps: (9)*/
  /* libmpeg3's "Unofficial economy rates": (10-13) */
   5.00,10.00,12.00,15.00,
  /* some invalid ones: (14-15) */
   0.0, 0.0
};
#endif
int sh_video_t::read_properties(){
Demuxer_Stream& d_video=*ds;

enum {
	VIDEO_MPEG12,
	VIDEO_MPEG4,
	VIDEO_H264,
	VIDEO_OTHER
} video_codec;

if((d_video.demuxer->file_format == Demuxer::Type_PVA) ||
   (d_video.demuxer->file_format == Demuxer::Type_MPEG_ES) ||
   (d_video.demuxer->file_format == Demuxer::Type_MPEG_PS && ((! fourcc) || (fourcc==0x10000001) || (fourcc==0x10000002))) ||
   (d_video.demuxer->file_format == Demuxer::Type_MPEG_TS && ((fourcc==0x10000001) || (fourcc==0x10000002)))
#ifdef STREAMING_LIVE_DOT_COM
  || ((d_video.demuxer->file_format == Demuxer::Type_RTP) && demux_is_mpeg_rtp_stream(d_video.demuxer))
#endif
  )
    video_codec = VIDEO_MPEG12;
  else if((d_video.demuxer->file_format == Demuxer::Type_MPEG4_ES) ||
    ((d_video.demuxer->file_format == Demuxer::Type_MPEG_TS) && (fourcc==0x10000004)) ||
    ((d_video.demuxer->file_format == Demuxer::Type_MPEG_PS) && (fourcc==0x10000004))
  )
    video_codec = VIDEO_MPEG4;
  else if((d_video.demuxer->file_format == Demuxer::Type_H264_ES) ||
    ((d_video.demuxer->file_format == Demuxer::Type_MPEG_TS) && (fourcc==0x10000005)) ||
    ((d_video.demuxer->file_format == Demuxer::Type_MPEG_PS) && (fourcc==0x10000005))
  )
    video_codec = VIDEO_H264;
  else
    video_codec = VIDEO_OTHER;

// Determine image properties:
switch(video_codec){
 case VIDEO_OTHER: {
 if((d_video.demuxer->file_format == Demuxer::Type_ASF) || (d_video.demuxer->file_format == Demuxer::Type_AVI)) {
  // display info:
#if 0
    if(bih->biCompression == BI_RGB &&
       (video.fccHandler == mmioFOURCC('D', 'I', 'B', ' ') ||
	video.fccHandler == mmioFOURCC('R', 'G', 'B', ' ') ||
	video.fccHandler == mmioFOURCC('R', 'A', 'W', ' ') ||
	video.fccHandler == 0)) {
		fourcc = mmioFOURCC(0, 'R', 'G', 'B') | bih->biBitCount;
    }
    else
#endif
	fourcc=bih->biCompression;

	src_w=bih->biWidth;
	src_h=abs(bih->biHeight);

#if 1
    /* hack to support decoding of mpeg1 chunks in AVI's with libmpeg2 -- 2002 alex */
    if ((fourcc == 0x10000001) ||
	(fourcc == 0x10000002) ||
	(fourcc == mmioFOURCC('m','p','g','1')) ||
	(fourcc == mmioFOURCC('M','P','G','1')) ||
	(fourcc == mmioFOURCC('m','p','g','2')) ||
	(fourcc == mmioFOURCC('M','P','G','2')) ||
	(fourcc == mmioFOURCC('m','p','e','g')) ||
	(fourcc == mmioFOURCC('M','P','E','G')))
    {
//	int saved_pos;
	Demuxer::demuxer_type_e saved_type;

	/* demuxer pos saving is required for libavcodec mpeg decoder as it's
	   reading the mpeg header self! */

//	saved_pos = d_video.buffer_pos;
	saved_type = d_video.demuxer->file_format;

	d_video.demuxer->file_format = Demuxer::Type_MPEG_ES;
	read_properties();
	d_video.demuxer->file_format = saved_type;
//	d_video.buffer_pos = saved_pos;
//	goto mpeg_header_parser;
    }
#endif
  }
  break;
 }
 case VIDEO_MPEG4: {
   int pos = 0, vop_cnt=0, units[3];
   videobuf_len=0; videobuf_code_len=0;
   MSG_V("Searching for Video Object Start code... ");fflush(stdout);
   while(1){
      int i=sync_video_packet(d_video);
      if(i<=0x11F) break; // found it!
      if(!i || !skip_video_packet(d_video)){
	MSG_V("NONE :(\n");
	return 0;
      }
   }
   MSG_V("OK!\n");
   if(!videobuffer) videobuffer=new(alignmem,8) unsigned char[VIDEOBUFFER_SIZE];
   if(!videobuffer){
     MSG_ERR(MSGTR_ShMemAllocFail);
     return 0;
   }
   MSG_V("Searching for Video Object Layer Start code... ");fflush(stdout);
   while(1){
      int i=sync_video_packet(d_video);
      MSG_V("M4V: 0x%X\n",i);
      if(i>=0x120 && i<=0x12F) break; // found it!
      if(!i || !read_video_packet(d_video)){
	MSG_V("NONE :(\n");
	return 0;
      }
   }
   pos = videobuf_len+4;
   if(!read_video_packet(d_video)){
     MSG_ERR("Can't read Video Object Layer Header\n");
     return 0;
   }
   mp4_header_process_vol(&picture, &(videobuffer[pos]));
   MSG_V("OK! FPS SEEMS TO BE %.3f\nSearching for Video Object Plane Start code... ", fps);fflush(stdout);
 mp4_init:
   while(1){
      int i=sync_video_packet(d_video);
      if(i==0x1B6) break; // found it!
      if(!i || !read_video_packet(d_video)){
	MSG_V("NONE :(\n");
	return 0;
      }
   }
   pos = videobuf_len+4;
   if(!read_video_packet(d_video)){
     MSG_ERR("Can't read Video Object Plane Header\n");
     return 0;
   }
   mp4_header_process_vop(&picture, &(videobuffer[pos]));
   units[vop_cnt] = picture.timeinc_unit;
   vop_cnt++;
   //mp_msg(MSGT_DECVIDEO,MSGL_V, "TYPE: %d, unit: %d\n", picture.picture_type, picture.timeinc_unit);
   if(!picture.fps) {
     int i, mn, md, mx, diff;
     if(vop_cnt < 3)
	  goto mp4_init;

     i=0;
     mn = mx = units[0];
     for(i=0; i<3; i++) {
       if(units[i] < mn)
	 mn = units[i];
       if(units[i] > mx)
	 mx = units[i];
     }
     md = mn;
     for(i=0; i<3; i++) {
       if((units[i] > mn) && (units[i] < mx))
	 md = units[i];
     }
     MSG_V("MIN: %d, mid: %d, max: %d\n", mn, md, mx);
     if(mx - md > md - mn)
       diff = md - mn;
     else
       diff = mx - md;
     if(diff > 0){
       picture.fps = (picture.timeinc_resolution * 10000) / diff;
       MSG_V("FPS seems to be: %d/10000, resolution: %d, delta_units: %d\n", picture.fps, picture.timeinc_resolution, diff);
     }
   }
   if(picture.fps) {
    fps=picture.fps*0.0001f;
    MSG_INFO("FPS seems to be: %d/10000\n", picture.fps);
   }
   MSG_V("OK!\n");
   fourcc=0x10000004;
   break;
 }
 case VIDEO_H264: {
   int pos = 0;
   videobuf_len=0; videobuf_code_len=0;
   MSG_V("Searching for sequence parameter set... ");fflush(stdout);
   while(1){
      int i=sync_video_packet(d_video);
      if((i&~0x60) == 0x107 && i != 0x107) break; // found it!
      if(!i || !skip_video_packet(d_video)){
	MSG_V("NONE :(\n");
	return 0;
      }
   }
   MSG_V("OK!\n");
   if(!videobuffer) videobuffer=new(alignmem,8) unsigned char[VIDEOBUFFER_SIZE];
   if(!videobuffer){
     MSG_ERR(MSGTR_ShMemAllocFail);
     return 0;
   }
   pos = videobuf_len+4;
   if(!read_video_packet(d_video)){
     MSG_ERR("Can't read sequence parameter set\n");
     return 0;
   }
   h264_parse_sps(&picture, &(videobuffer[pos]), videobuf_len - pos);
   MSG_V("Searching for picture parameter set... ");fflush(stdout);
   while(1){
      int i=sync_video_packet(d_video);
      MSG_V("H264: 0x%X\n",i);
      if((i&~0x60) == 0x108 && i != 0x108) break; // found it!
      if(!i || !read_video_packet(d_video)){
	MSG_V("NONE :(\n");
	return 0;
      }
   }
   MSG_V("OK!\nSearching for Slice... ");fflush(stdout);
   while(1){
      int i=sync_video_packet(d_video);
      if((i&~0x60) == 0x101 || (i&~0x60) == 0x102 || (i&~0x60) == 0x105) break; // found it!
      if(!i || !read_video_packet(d_video)){
	MSG_V("NONE :(\n");
	return 0;
      }
   }
   MSG_V("OK!\n");
   fourcc=0x10000005;
   if(picture.fps) {
     fps=picture.fps*0.0001f;
     MSG_INFO("FPS seems to be: %d/10000\n", picture.fps);
   }
   break;
 }
 case VIDEO_MPEG12: {
//mpeg_header_parser:
   // Find sequence_header first:
   videobuf_len=0; videobuf_code_len=0;
   telecine=0; telecine_cnt=-2.5;
   MSG_V("Searching for sequence header... ");fflush(stdout);
   while(1){
      int i=sync_video_packet(d_video);
      if(i==0x1B3) break; // found it!
      if(!i || !skip_video_packet(d_video)){
	MSG_V("NONE :(\n");
	MSG_ERR(MSGTR_MpegNoSequHdr);
	return 0;
      }
   }
   MSG_V("OK!\n");
//   sh_video=d_video.sh;ds=d_video;
//   mpeg2_init();
   // ========= Read & process sequence header & extension ============
   if(!videobuffer) videobuffer=new(alignmem,8) unsigned char[VIDEOBUFFER_SIZE];
   if(!videobuffer){
     MSG_ERR(MSGTR_ShMemAllocFail);
     return 0;
   }

   if(!read_video_packet(d_video)){
     MSG_ERR(MSGTR_CannotReadMpegSequHdr);
     return 0;
   }
   if(mp_header_process_sequence_header (&picture, &videobuffer[4])) {
     MSG_ERR(MSGTR_BadMpegSequHdr);
     return 0;
   }
   if(sync_video_packet(d_video)==0x1B5){ // next packet is seq. ext.
//    videobuf_len=0;
    int pos=videobuf_len;
    if(!read_video_packet(d_video)){
      MSG_ERR(MSGTR_CannotReadMpegSequHdrEx);
      return 0;
    }
    if(mp_header_process_extension (&picture, &videobuffer[pos+4])) {
      MSG_ERR(MSGTR_BadMpegSequHdrEx);
      return 0;
    }
   }
   // fill aspect info:
   switch(picture.aspect_ratio_information){
     case 2:  // PAL/NTSC SVCD/DVD 4:3
     case 8:  // PAL VCD 4:3
     case 12: // NTSC VCD 4:3
       aspect=4.0/3.0;
     break;
     case 3:  // PAL/NTSC Widescreen SVCD/DVD 16:9
     case 6:  // (PAL?)/NTSC Widescreen SVCD 16:9
       aspect=16.0/9.0;
     break;
     case 4:  // according to ISO-138182-2 Table 6.3
       aspect=2.21;
       break;
     case 9: // Movie Type ??? / 640x480
       aspect=0.0;
     break;
     default:
       MSG_ERR("Detected unknown aspect_ratio_information in mpeg sequence header.\n"
	       "Please report the aspect value (%i) along with the movie type (VGA,PAL,NTSC,"
	       "SECAM) and the movie resolution (720x576,352x240,480x480,...) to the MPlayer"
	       " developers, so that we can add support for it!\nAssuming 1:1 aspect for now.\n",
	       picture.aspect_ratio_information);
     case 1:  // VGA 1:1 - do not prescale
       aspect=0.0;
     break;
   }
   // fill aspect info:
   fourcc=picture.mpeg1?0x10000001:0x10000002; // mpeg video
#if 0
   if(picture.mpeg1)
	aspect=mpeg1_aspects[picture.aspect_ratio_information & 0x0F];
   else
	aspect=mpeg2_aspects[picture.aspect_ratio_information & 0x0F];
#endif
   // display info:
   fps=picture.fps*0.0001f;
#if 0
   fps=mpeg_framerates[picture.frame_rate_code & 0x0F];
#endif
   src_w=picture.display_picture_width;
   src_h=picture.display_picture_height;
   // info:
   MSG_DBG2("mpeg bitrate: %d (%X)\n",picture.bitrate,picture.bitrate);
   MSG_V("VIDEO:  %s  %dx%d  (aspect %d)  %4.2f fps  %5.1f kbps (%4.1f kbyte/s)\n",
    picture.mpeg1?"MPEG1":"MPEG2",
    src_w,src_h,
    picture.aspect_ratio_information,
    fps,
    picture.bitrate*0.5f,
    picture.bitrate/16.0f );
  break;
 }
} // switch(file_format)

return 1;
}

static void process_userdata(const unsigned char* buf,int len){
    int i;
    /* if the user data starts with "CC", assume it is a Closed Captions info packet */
    if(len>2 && buf[0]=='C' && buf[1]=='C'){
      subcc_process_data(buf+2,len-2);
    }
    if( len > 2 && buf[ 0 ] == 'T' && buf[ 1 ] == 'Y' )
    {
       ty_processuserdata( buf + 2, len - 2 );
       return;
    }
    MSG_V( "user_data: len=%3d  %02X %02X %02X %02X '",
	    len, buf[0], buf[1], buf[2], buf[3]);
    for(i=0;i<len;i++)
	if(buf[i]>=32 && buf[i]<127) MSG_V("%c",buf[i]);
    MSG_V("'\n");
}

int sh_video_t::read_frame(float* frame_time_ptr,float *v_pts,unsigned char** start,int force_fps){
    Demuxer_Stream& d_video=*ds;
    Demuxer *demuxer=d_video.demuxer;
    float frame_time=1;
    float pts1=d_video.pts;
    int picture_coding_type=0;
//    unsigned char* start=NULL;
    int in_size=0;

    *start=NULL;

  if(demuxer->file_format==Demuxer::Type_MPEG_ES ||
	(demuxer->file_format==Demuxer::Type_MPEG_PS && ((! fourcc) || (fourcc==0x10000001) || (fourcc==0x10000002)))
		  || demuxer->file_format==Demuxer::Type_PVA ||
		  ((demuxer->file_format==Demuxer::Type_MPEG_TS) && ((fourcc==0x10000001) || (fourcc==0x10000002))))
  {
	int in_frame=0;
	//float newfps;
	//videobuf_len=0;
	while(videobuf_len<VIDEOBUFFER_SIZE-MAX_VIDEO_PACKET_SIZE){
	  int i=sync_video_packet(d_video);
	  //any_t* buffer=&videobuffer[videobuf_len+4];
	  int __start=videobuf_len+4;
	  if(in_frame){
	    if(i<0x101 || i>=0x1B0){  // not slice code -> end of frame
#if 1
	      // send END OF FRAME code:
	      videobuffer[videobuf_len+0]=0;
	      videobuffer[videobuf_len+1]=0;
	      videobuffer[videobuf_len+2]=1;
	      videobuffer[videobuf_len+3]=0xFF;
	      videobuf_len+=4;
#endif
	      if(!i) return -1; // EOF
	      break;
	    }
	  } else {
	    //if(i==0x100) in_frame=1; // picture startcode
	    if(i>=0x101 && i<0x1B0) in_frame=1; // picture startcode
	    else if(!i) return -1; // EOF
	  }
	  if(!read_video_packet(d_video)) return -1; // EOF
	  // process headers:
	  switch(i){
	      case 0x1B3: mp_header_process_sequence_header (&picture, &videobuffer[__start]);break;
	      case 0x1B5: mp_header_process_extension (&picture, &videobuffer[__start]);break;
	      case 0x1B2: process_userdata (&videobuffer[__start], videobuf_len-__start);break;
	      case 0x100: picture_coding_type=(videobuffer[__start+1] >> 3) & 7;break;
	  }
	}
	// if(videobuf_len>max_framesize) max_framesize=videobuf_len; // debug
	*start=videobuffer; in_size=videobuf_len;

#if 1
    // get mpeg fps:
    //newfps=frameratecode2framerate[picture->frame_rate_code]*0.0001f;
    if((int)(fps*10000+0.5)!=picture.fps) if(!force_fps && !telecine){
	    MSG_WARN("Warning! FPS changed %5.3f -> %5.3f  (%f) [%d]  \n",fps,picture.fps*0.0001,fps-picture.fps*0.0001,picture.frame_rate_code);
	    fps=picture.fps*0.0001;
    }
#endif

    // fix mpeg2 frametime:
    frame_time=(picture.display_time)*0.01f;
    picture.display_time=100;
    videobuf_len=0;

    telecine_cnt*=0.9; // drift out error
    telecine_cnt+=frame_time-5.0/4.0;
    MSG_DBG2("\r telecine = %3.1f  %5.3f     \n",frame_time,telecine_cnt);

    if(telecine){
	frame_time=1;
	if(telecine_cnt<-1.5 || telecine_cnt>1.5){
	    MSG_INFO("Leave telecine mode\n");
	    telecine=0;
	}
    } else
	if(telecine_cnt>-0.5 && telecine_cnt<0.5 && !force_fps){
	    fps=fps*4/5;
	    MSG_INFO("Enter telecine mode\n");
	    telecine=1;
	}

  } else if((demuxer->file_format==Demuxer::Type_MPEG4_ES) || ((demuxer->file_format==Demuxer::Type_MPEG_TS) && (fourcc==0x10000004)) ||
	    ((demuxer->file_format==Demuxer::Type_MPEG_PS) && (fourcc==0x10000004))
	    ){
      //
	while(videobuf_len<VIDEOBUFFER_SIZE-MAX_VIDEO_PACKET_SIZE){
	  int i=sync_video_packet(d_video);
	  if(!i) return -1;
	  if(!read_video_packet(d_video)) return -1; // EOF
	  if(i==0x1B6) break;
	}
	*start=videobuffer; in_size=videobuf_len;
	videobuf_len=0;

  } else if(demuxer->file_format==Demuxer::Type_H264_ES || ((demuxer->file_format==Demuxer::Type_MPEG_TS) && (fourcc==0x10000005)) ||
	    ((demuxer->file_format==Demuxer::Type_MPEG_PS) && (fourcc==0x10000005))
	    ){
      //
	while(videobuf_len<VIDEOBUFFER_SIZE-MAX_VIDEO_PACKET_SIZE){
	  int i=sync_video_packet(d_video);
	  int pos = videobuf_len+4;
	  if(!i) return -1;
	  if(!read_video_packet(d_video)) return -1; // EOF
	  if((i&~0x60) == 0x107 && i != 0x107) {
	    h264_parse_sps(&picture, &(videobuffer[pos]), videobuf_len - pos);
	    if(picture.fps > 0) {
	      fps=picture.fps*0.0001f;
	    }
	    i=sync_video_packet(d_video);
	    if(!i) return -1;
	    if(!read_video_packet(d_video)) return -1; // EOF
	  }
	  if((i&~0x60) == 0x101 || (i&~0x60) == 0x102 || (i&~0x60) == 0x105) break;
	}
	*start=videobuffer; in_size=videobuf_len;
	videobuf_len=0;
  } else {
    /* frame-based file formats: (AVI,ASF,MOV) */
    in_size=d_video.get_packet(start);
    if(in_size<0) return -1; // EOF
  }

    // Increase video timers:
    frame_time*=1.0f/fps;

    /* override frame_time for variable/unknown FPS formats: */
    if(!force_fps)
      switch(demuxer->file_format)
      {
	case Demuxer::Type_REAL:
	case Demuxer::Type_MATROSKA:
	if(d_video.pts>0 && pts1>0 && d_video.pts>pts1)
	  frame_time=d_video.pts-pts1;
	break;
      default:
#ifdef USE_TV
      case Demuxer::Type_TV:
#endif
      case Demuxer::Type_MOV:
      case Demuxer::Type_FILM:
      case Demuxer::Type_VIVO:
      case Demuxer::Type_ASF: {
	/* .ASF files has no fixed FPS - just frame durations! */
	float next_pts = d_video.get_next_pts();
	float d= next_pts > 0 ? next_pts - d_video.pts : d_video.pts-pts1;
	if(d>=0){
	  if(d>0)
	    if((int)fps==1000)
	      MSG_STATUS("\rASF framerate: %d fps             \n",(int)(1.0f/d));
	  fps=1.0f/d;
	  frame_time = d;
	} else {
	  MSG_WARN("\nInvalid frame duration value (%2.5f/%2.5f => %5.3f). Defaulting to 1/25 sec.\n",d_video.pts,next_pts,frame_time);
	  frame_time = 1/25.0;
	}
      }
    }
    if(demuxer->file_format==Demuxer::Type_MPEG_PS ||
       demuxer->file_format==Demuxer::Type_MPEG_ES ||
       demuxer->file_format==Demuxer::Type_MPEG_TS) d_video.pts+=frame_time;
    /* FIXUP VIDEO PTS*/
    if((demuxer->file_format == Demuxer::Type_MPEG_ES ||
	demuxer->file_format == Demuxer::Type_MPEG4_ES ||
	demuxer->file_format == Demuxer::Type_H264_ES ||
	demuxer->file_format == Demuxer::Type_MPEG_PS ||
	((demuxer->file_format==Demuxer::Type_MPEG_TS) && ((fourcc==0x10000001) || (fourcc==0x10000002))) ||
	mp_conf.av_force_pts_fix) && mp_conf.av_sync_pts && mp_conf.av_force_pts_fix2!=1)
    {
	if(d_video.pts_flags && d_video.pts < 1.0 && d_video.prev_pts > 2.0)
	{
	    float spts;
	    spts=d_video.demuxer->stream->stream_pts();
	    d_video.pts_corr=spts>0?spts:d_video.prev_pts;
	    d_video.pts_flags=0;
	    MSG_V("***PTS discontinuity happens*** correcting video %f pts as %f\n",d_video.pts,d_video.pts_corr);
	}
	if(d_video.pts>1.0) d_video.pts_flags=1;
	if(!d_video.eof) d_video.prev_pts=d_video.pts+d_video.pts_corr;
	*v_pts=d_video.prev_pts;
    }
    else *v_pts=d_video.pts;

    if(frame_time_ptr) *frame_time_ptr=frame_time;
    return in_size;
}
} // namespace	usr
