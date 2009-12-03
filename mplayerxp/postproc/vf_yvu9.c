#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../config.h"
#include "pp_msg.h"

#include "../libvo/img_format.h"
#include "mp_image.h"
#include "vf.h"

#include "../libvo/fastmemcpy.h"
#include "../postproc/rgb2rgb.h"

//===========================================================================//

static int __FASTCALL__ config(struct vf_instance_s* vf,
        int width, int height, int d_width, int d_height,
	unsigned int flags, unsigned int outfmt,void *tune){
    
    if(vf_next_query_format(vf,IMGFMT_YV12,d_width,d_height)<=0){
	MSG_ERR("yv12 isn't supported by next filter/vo :(\n");
	return 0;
    }
    
    return vf_next_config(vf,width,height,d_width,d_height,flags,IMGFMT_YV12,tune);
}

static int __FASTCALL__ put_slice(struct vf_instance_s* vf, mp_image_t *mpi){
    mp_image_t *dmpi;
    int y,w,h;

    // hope we'll get DR buffer:
    dmpi=vf_get_image(vf->next,IMGFMT_YV12,
	MP_IMGTYPE_TEMP, 0/*MP_IMGFLAG_ACCEPT_STRIDE*/,
	mpi->w, mpi->h);

#ifdef _OPENMP
#pragma omp parallel sections
{
#pragma omp section
#endif
    for(y=0;y<mpi->h;y++)
	memcpy(dmpi->planes[0]+dmpi->stride[0]*y,
	       mpi->planes[0]+mpi->stride[0]*y,
	       mpi->w);

#ifdef _OPENMP
#pragma omp section
#endif
    (*vu9_to_vu12)( mpi->planes[1], mpi->planes[2],
		    dmpi->planes[1], dmpi->planes[2],
		    w,h,mpi->stride[1],mpi->stride[2],
		    dmpi->stride[1],dmpi->stride[2]);
#ifdef _OPENMP
}
#endif
    vf_clone_mpi_attributes(dmpi, mpi);

    return vf_next_put_slice(vf,dmpi);
}

//===========================================================================//

static int __FASTCALL__ query_format(struct vf_instance_s* vf, unsigned int fmt,unsigned w,unsigned h){
    if (fmt == IMGFMT_YVU9 || fmt == IMGFMT_IF09)
	return vf_next_query_format(vf,IMGFMT_YV12,w,h) & (~VFCAP_CSP_SUPPORTED_BY_HW);
    return 0;
}

static int __FASTCALL__ vf_open(vf_instance_t *vf,const char* args){
    vf->config=config;
    vf->put_slice=put_slice;
    vf->query_format=query_format;
    return 1;
}

const vf_info_t vf_info_yvu9 = {
    "fast YVU9->YV12 conversion",
    "yvu9",
    "alex",
    "",
    VF_FLAGS_THREADS,
    vf_open
};

//===========================================================================//
