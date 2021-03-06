
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mp_config.h"

#include "libmpstream/stream.h"
#include "demuxer.h"
#include "stheader.h"
#include "demux_msg.h"

//--------------------------

// audio stream skip/resync functions requires only for seeking.
// (they should be implemented in the audio codec layer)
void mpca_skip_frame(sh_audio_t *sh_audio){
}
void mpca_resync_stream(sh_audio_t *sh_audio){
}

int verbose=5; // must be global!

//---------------

extern stream_t* open_stream(amy_t*libinput,char* filename,int* file_format);

int main(int argc,char* argv[]){

stream_t* stream=NULL;
demuxer_t* demuxer=NULL;
int file_format=DEMUXER_TYPE_UNKNOWN;

  mp_msg_init(verbose+MSGL_STATUS);

  if(argc>1)
    stream=open_stream(NULL,argv[1],0,&file_format);
  else
//  stream=open_stream("/3d/divx/405divx_sm_v2[1].avi",0,&file_format);
    stream=open_stream(NULL,"/dev/cdrom",2,&file_format); // VCD track 2

  if(!stream){
	printf("Cannot open file/device\n");
	exit(1);
  }

  printf("success: format: %d  data: 0x%X - 0x%X\n",file_format, (int)(stream->start_pos),(int)(stream->end_pos));

  stream_enable_cache(stream,NULL,2048*1024,0,0);

  demuxer=demux_open(stream,file_format,-1,-1,-1);
  if(!demuxer){
	printf("Cannot open demuxer\n");
	exit(1);
  }


}
