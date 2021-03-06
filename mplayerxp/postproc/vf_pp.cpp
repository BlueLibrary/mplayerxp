#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>

#include "osdep/cpudetect.h"

#include "libvo2/img_format.h"
#include "xmpcore/xmp_image.h"
#include "vf.h"
#include "vf_internal.h"

#include "postprocess.h"
#include "pp_msg.h"

struct vf_priv_t {
    int pp;
    pp_mode *ppMode[PP_QUALITY_MAX+1];
    any_t*context;
    unsigned int outfmt;
};

//===========================================================================//

static int __FASTCALL__ vf_config(vf_instance_t* vf,
	int width, int height, int d_width, int d_height,
	vo_flags_e voflags, unsigned int outfmt){
    int flags=
	  (gCpuCaps.hasMMX   ? PP_CPU_CAPS_MMX   : 0)
	| (gCpuCaps.hasMMX2  ? PP_CPU_CAPS_MMX2  : 0)
	| (gCpuCaps.has3DNow ? PP_CPU_CAPS_3DNOW : 0);

    switch(outfmt){
    case IMGFMT_444P: flags|= PP_FORMAT_444; break;
    case IMGFMT_422P: flags|= PP_FORMAT_422; break;
    case IMGFMT_411P: flags|= PP_FORMAT_411; break;
    default:          flags|= PP_FORMAT_420; break;
    }

    if(vf->priv->context) pp_free_context(vf->priv->context);
    vf->priv->context= pp2_get_context(width, height, flags);

    return vf_next_config(vf,width,height,d_width,d_height,voflags,outfmt);
}

static void __FASTCALL__ uninit(vf_instance_t* vf){
    int i;
    for(i=0; i<=PP_QUALITY_MAX; i++){
	if(vf->priv->ppMode[i])
	    pp_free_mode(vf->priv->ppMode[i]);
    }
    if(vf->priv->context) pp_free_context(vf->priv->context);
}

static int __FASTCALL__ query_format(vf_instance_t* vf, unsigned int fmt,unsigned w,unsigned h){
    switch(fmt){
    case IMGFMT_YV12:
    case IMGFMT_I420:
    case IMGFMT_IYUV:
    case IMGFMT_444P:
    case IMGFMT_422P:
    case IMGFMT_411P:
	return vf_next_query_format(vf,fmt,w,h);
    }
    return 0;
}

static MPXP_Rc __FASTCALL__ control_vf(vf_instance_t* vf, int request, any_t* data){
    switch(request){
    case VFCTRL_QUERY_MAX_PP_LEVEL:
	*reinterpret_cast<unsigned int*>(data)=PP_QUALITY_MAX;
	return MPXP_Ok;
    case VFCTRL_SET_PP_LEVEL:
	vf->priv->pp= *((unsigned int*)data);
	return MPXP_True;
    }
    return vf_next_control(vf,request,data);
}

static void __FASTCALL__ get_image(vf_instance_t* vf, mp_image_t *smpi){
    if(vf->priv->pp&0xFFFF) return; // non-local filters enabled
    if((smpi->type==MP_IMGTYPE_IPB || vf->priv->pp) &&
	smpi->flags&MP_IMGFLAG_PRESERVE) return; // don't change
    if(!(smpi->flags&MP_IMGFLAG_ACCEPT_STRIDE) && smpi->imgfmt!=vf->priv->outfmt)
	return; // colorspace differ
    // ok, we can do pp in-place (or pp disabled):
    vf->dmpi=vf_get_new_genome(vf->next,*smpi);
    smpi->planes[0]=vf->dmpi->planes[0];
    smpi->stride[0]=vf->dmpi->stride[0];
    smpi->width=vf->dmpi->width;
    if(smpi->flags&MP_IMGFLAG_PLANAR){
	smpi->planes[1]=vf->dmpi->planes[1];
	smpi->planes[2]=vf->dmpi->planes[2];
	smpi->stride[1]=vf->dmpi->stride[1];
	smpi->stride[2]=vf->dmpi->stride[2];
    }
    smpi->flags|=MP_IMGFLAG_DIRECT;
}

static int __FASTCALL__ put_slice(vf_instance_t* vf,const mp_image_t& smpi){
    if(!(smpi.flags&MP_IMGFLAG_DIRECT)){
	// no DR, so get a new image! hope we'll get DR buffer:
	vf->dmpi=vf_get_new_image(vf->next,smpi.imgfmt,
	    MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE|MP_IMGFLAG_PREFER_ALIGNED_STRIDE,
	    (smpi.w+7)&(~7),(smpi.h+7)&(~7),smpi.xp_idx);
    }

    if(vf->priv->pp || !(smpi.flags&MP_IMGFLAG_DIRECT)){
	// do the postprocessing! (or copy if no DR)
	pp_postprocess(
		    const_cast<const uint8_t**>(smpi.planes),
		    reinterpret_cast<const int*>(smpi.stride),
		    vf->dmpi->planes,
		    reinterpret_cast<int*>(vf->dmpi->stride),
		    (smpi.w+7)&(~7),smpi.h,
		    reinterpret_cast<const int8_t*>(smpi.qscale), smpi.qstride,
		    vf->priv->ppMode[ vf->priv->pp ], vf->priv->context,
		    smpi.pict_type | (smpi.qscale_type ? PP_PICT_TYPE_QP2 : 0));
    }
    return vf_next_put_slice(vf,*vf->dmpi);
}

//===========================================================================//

static unsigned int fmt_list[]={
    IMGFMT_YV12,
    IMGFMT_I420,
    IMGFMT_IYUV,
    IMGFMT_444P,
    IMGFMT_422P,
    IMGFMT_411P,
    0
};

static MPXP_Rc __FASTCALL__ vf_open(vf_instance_t *vf,const char* args){
    char *endptr;
    const char* name;
    int i;
    int hex_mode=0;

    vf->query_format=query_format;
    vf->control_vf=control_vf;
    vf->config_vf=vf_config;
    vf->get_image=get_image;
    vf->put_slice=put_slice;
    vf->uninit=uninit;
    vf->default_caps=VFCAP_ACCEPT_STRIDE|VFCAP_POSTPROC;
    vf->priv=new(zeromem) vf_priv_t;
    vf->priv->context=NULL;

    // check csp:
    vf_conf_t conf = { 1, 1, IMGFMT_YV12 };
    vf->priv->outfmt=vf_match_csp(&vf->next,fmt_list,&conf);
    if(!vf->priv->outfmt) return MPXP_False; // no csp match :(

    if(args){
	hex_mode= strtol(args, &endptr, 0);
	if(*endptr){
	    name= args;
	}else
	    name= NULL;
    }else{
	name="de";
    }

    for(i=0; i<=PP_QUALITY_MAX; i++){
	vf->priv->ppMode[i]= pp_get_mode_by_name_and_quality(name, i);
	if(vf->priv->ppMode[i]==NULL) return MPXP_Error;
    }

    vf->priv->pp=PP_QUALITY_MAX;
    check_pin("vfilter",vf->pin,VF_PIN);
    return MPXP_Ok;
}

extern const vf_info_t vf_info_pp = {
    "postprocessing",
    "pp",
    "A'rpi",
    "",
    VF_FLAGS_THREADS,
    vf_open
};

//===========================================================================//
