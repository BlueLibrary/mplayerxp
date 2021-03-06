#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
#include <stdio.h>
#include <stdlib.h>

#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "afmt.h"
#include "audio_out.h"
#include "audio_out_internal.h"
#include "ao_msg.h"

namespace	usr {
const char *oss_mixer_device = PATH_DEV_MIXER;
/* Support for >2 output channels added 2001-11-25 - Steve Davies <steve@daviesfam.org> */
class Oss_AO_Interface : public AO_Interface {
    public:
	Oss_AO_Interface(const std::string& subdevice);
	virtual ~Oss_AO_Interface();

	virtual MPXP_Rc		open(unsigned flags);
	virtual MPXP_Rc		configure(unsigned rate,unsigned channels,unsigned format);
	virtual unsigned	samplerate() const;
	virtual unsigned	channels() const;
	virtual unsigned	format() const;
	virtual unsigned	buffersize() const;
	virtual unsigned	outburst() const;
	virtual MPXP_Rc		test_rate(unsigned r) const;
	virtual MPXP_Rc		test_channels(unsigned c) const;
	virtual MPXP_Rc		test_format(unsigned f) const;
	virtual void		reset();
	virtual unsigned	get_space();
	virtual float		get_delay();
	virtual unsigned	play(const any_t* data,unsigned len,unsigned flags);
	virtual void		pause();
	virtual void		resume();
	virtual MPXP_Rc		ctrl(int cmd,long arg) const;
    private:
	unsigned	_channels,_samplerate,_format;
	unsigned	_buffersize,_outburst;
	unsigned	bps() const { return _channels*_samplerate*afmt2bps(_format); }
	void		show_fmts() const;
	void		show_caps();

	std::string	dsp;
	const char*	mixer_device;
	int		mixer_channel;
	int		fd;
	audio_buf_info	zz;
	int		delay_method;
};

Oss_AO_Interface::Oss_AO_Interface(const std::string& _subdevice)
		:AO_Interface(_subdevice),mixer_device(oss_mixer_device) {}
Oss_AO_Interface::~Oss_AO_Interface() {
    if(fd == -1) return;
#ifdef SNDCTL_DSP_RESET
    ::ioctl(fd, SNDCTL_DSP_RESET, NULL);
#endif
    ::close(fd);
    fd=-1;
}

// to set/get/query special features/parameters
MPXP_Rc Oss_AO_Interface::ctrl(int cmd,long arg) const {
    switch(cmd){
	case AOCONTROL_GET_VOLUME:
	case AOCONTROL_SET_VOLUME: {
	    ao_control_vol_t *vol = (ao_control_vol_t *)arg;
	    int _fd, v, devs;

	    if(_format == AFMT_AC3) return MPXP_True;

	    if ((_fd = ::open(mixer_device, O_RDONLY)) > 0) {
		::ioctl(_fd, SOUND_MIXER_READ_DEVMASK, &devs);
		if (devs & (1 << mixer_channel)) {
		    if (cmd == AOCONTROL_GET_VOLUME) {
			::ioctl(_fd, MIXER_READ(mixer_channel), &v);
			vol->right = (v & 0xFF00) >> 8;
			vol->left = v & 0x00FF;
		    } else {
			v = ((int)vol->right << 8) | (int)vol->left;
			::ioctl(_fd, MIXER_WRITE(mixer_channel), &v);
		    }
		} else {
		    ::close(_fd);
		    return MPXP_Error;
		}
		::close(_fd);
		return MPXP_Ok;
	    }
	}
	return MPXP_Error;
    }
    return MPXP_Unknown;
}

void Oss_AO_Interface::show_fmts() const
{
    int rval;
    rval=0;
    if (::ioctl (fd, SNDCTL_DSP_GETFMTS, &rval) != -1) {
	mpxp_info<<"AO-INFO: List of supported formats: ";
	if(rval & AFMT_MU_LAW) mpxp_info<<"AFMT_MU_LAW ";
	if(rval & AFMT_A_LAW) mpxp_info<<"AFMT_A_LAW ";
	if(rval & AFMT_IMA_ADPCM) mpxp_info<<"AFMT_IMA_ADPCM ";
	if(rval & AFMT_U8) mpxp_info<<"AFMT_U8 ";
	if(rval & AFMT_S16_LE) mpxp_info<<"AFMT_S16_LE ";
	if(rval & AFMT_S16_BE) mpxp_info<<"AFMT_S16_BE ";
	if(rval & AFMT_S8) mpxp_info<<"AFMT_S8 ";
	if(rval & AFMT_U16_LE) mpxp_info<<"AFMT_U16_LE ";

	if(rval & AFMT_U16_BE) mpxp_info<<"AFMT_U16_BE ";
	if(rval & AFMT_MPEG) mpxp_info<<"AFMT_MPEG ";
	if(rval & AFMT_AC3) mpxp_info<<"AFMT_AC3 ";
	if(rval & AFMT_S24_LE) mpxp_info<<"AFMT_S24_LE ";
	if(rval & AFMT_S24_BE) mpxp_info<<"AFMT_S24_BE ";
	if(rval & AFMT_U24_LE) mpxp_info<<"AFMT_U24_LE ";
	if(rval & AFMT_U24_BE) mpxp_info<<"AFMT_U24_LE ";
	if(rval & AFMT_S32_LE) mpxp_info<<"AFMT_S32_LE ";
	if(rval & AFMT_S32_BE) mpxp_info<<"AFMT_S32_BE ";
	if(rval & AFMT_U32_LE) mpxp_info<<"AFMT_U32_LE ";
	if(rval & AFMT_U32_BE) mpxp_info<<"AFMT_U32_LE ";
	mpxp_info<<std::endl;
    }
}

void Oss_AO_Interface::show_caps()
{
    int rval;
#ifdef __linux__
    fd=::open(dsp.c_str(), O_WRONLY | O_NONBLOCK);
#else
    fd=::open(dsp.c_str(), O_WRONLY);
#endif
    if(fd<0){
	mpxp_err<<"Can't open audio device "<<dsp<<": "<<strerror(errno)<<std::endl;
	return;
    }
    show_fmts();
    rval=0;
    if (::ioctl (fd, SNDCTL_DSP_GETCAPS, &rval) != -1) {
	mpxp_info<<"AO-INFO: Capabilities: ";
	mpxp_info<<"rev-"<<(rval & DSP_CAP_REVISION)<<" ";
	if(rval & DSP_CAP_DUPLEX) mpxp_info<<"duplex ";
	if(rval & DSP_CAP_REALTIME) mpxp_info<<"realtime ";
	if(rval & DSP_CAP_BATCH) mpxp_info<<"batch ";
	if(rval & DSP_CAP_COPROC) mpxp_info<<"coproc ";
	if(rval & DSP_CAP_TRIGGER) mpxp_info<<"trigger ";
	if(rval & DSP_CAP_MMAP) mpxp_info<<"mmap ";
	if(rval & DSP_CAP_MULTI) mpxp_info<<"multiopen ";
	if(rval & DSP_CAP_BIND) mpxp_info<<"bind ";
	mpxp_info<<std::endl;
    }
    ::close(fd);
}
// open & setup audio device
// return: 1=success 0=fail
MPXP_Rc Oss_AO_Interface::open(unsigned flags){
    const char* mixer_channels [SOUND_MIXER_NRDEVICES] = SOUND_DEVICE_NAMES;
    UNUSED(flags);
    dsp=PATH_DEV_DSP;
    mixer_channel=SOUND_MIXER_PCM;
    fd=-1;
    delay_method=2;
    if (!subdevice.empty()) {
	std::string p;
	size_t pos;
	pos=subdevice.find(':');
	dsp = subdevice.substr(0,pos);
	if(pos!=std::string::npos) {
	    p=subdevice.substr(pos+1);
	    if(p=="-1") { show_caps(); return MPXP_False; }
	}
    }
    mpxp_v<<"audio_setup: using '"<<dsp<<"' dsp device"<<std::endl;
    mpxp_v<<"audio_setup: using '"<<mixer_device<<"'("<<mixer_channels[mixer_channel]<<") mixer device"<<std::endl;

#ifdef __linux__
    fd=::open(dsp.c_str(), O_WRONLY | O_NONBLOCK);
#else
    fd=::open(dsp.c_str(), O_WRONLY);
#endif
    if(fd<0){
	mpxp_err<<"Can't open audio device "<<dsp<<": "<<strerror(errno)<<std::endl;
	return MPXP_False;
    }

#ifdef __linux__
  /* Remove the non-blocking flag */
    if(::fcntl(fd, F_SETFL, 0) < 0) {
	mpxp_err<<"Can't make filedescriptor non-blocking: "<<strerror(errno)<<std::endl;
	return MPXP_False;
    }
#endif
#if defined(FD_CLOEXEC) && defined(F_SETFD)
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
    return MPXP_Ok;
}

MPXP_Rc Oss_AO_Interface::configure(unsigned r,unsigned c,unsigned f)
{
    mpxp_v<<"ao3: "<<r<<" Hz  "<<c<<" chans "<<ao_format_name(f)<<std::endl;

    if(f == AFMT_AC3) {
	_samplerate=r;
	::ioctl (fd, SNDCTL_DSP_SPEED, &_samplerate);
    }

ac3_retry:
    _format=f;
    if( ::ioctl(fd, SNDCTL_DSP_SETFMT, &_format)<0 || _format != f) {
	if(_format == AFMT_AC3){
	    mpxp_warn<<"OSS-CONF: Can't set audio device "<<dsp<<" to AC3 output, trying S16..."<<std::endl;
#ifdef WORDS_BIGENDIAN
	    _format=AFMT_S16_BE;
#else
	    _format=AFMT_S16_LE;
#endif
	    goto ac3_retry;
	} else {
	    mpxp_err<<"OSS-CONF: Can't config_ao for: "<<ao_format_name(_format)<<std::endl;
	    show_fmts();
	    _format=f;
	    return MPXP_False;
	}
    }
    _channels = c;
    if(_format != AFMT_AC3) {
	// We only use SNDCTL_DSP_CHANNELS for >2 channels, in case some drivers don't have it
	if (_channels > 2) {
	    if (::ioctl(fd, SNDCTL_DSP_CHANNELS, &_channels) == -1 || _channels != c ) {
		mpxp_err<<"OSS-CONF: Failed to set audio device to "<<_channels<<" channels"<<std::endl;
		return MPXP_False;
	    }
	} else {
	    int _c = _channels-1;
	    if (::ioctl(fd, SNDCTL_DSP_STEREO, &_c) == -1) {
		mpxp_err<<"OSS-CONF: Failed to set audio device to "<<_channels<<" channels"<<std::endl;
		return MPXP_False;
	    }
	    _channels=_c+1;
	}
	mpxp_v<<"OSS-CONF: using "<<_channels<<" channels (requested: "<<c<<")"<<std::endl;
	// set rate
	_samplerate=r;
	::ioctl (fd, SNDCTL_DSP_SPEED, &_samplerate);
	mpxp_v<<"OSS-CONF: using "<<_samplerate<<" Hz samplerate (requested: "<<r<<")"<<std::endl;
    }

    if(::ioctl(fd, SNDCTL_DSP_GETOSPACE, &zz)==-1){
	int _r=0;
	mpxp_warn<<"OSS-CONF: driver doesn't support SNDCTL_DSP_GETOSPACE"<<std::endl;
	if(::ioctl(fd, SNDCTL_DSP_GETBLKSIZE, &_r)==-1){
	    mpxp_v<<"OSS-CONF: "<<_outburst<<" bytes/frag (mpxp_config.h)"<<std::endl;
	} else {
	    _outburst=r;
	    mpxp_v<<"OSS-CONF: "<<_outburst<<" bytes/frag (GETBLKSIZE)"<<std::endl;
	}
    } else {
	mpxp_v<<"OSS-CONF: frags: "<<zz.fragments<<"/"<<zz.fragstotal<<" ("<<zz.fragsize<<" bytes/frag)  mp_free: "<<zz.bytes<<std::endl;
	if(_buffersize==0) _buffersize=zz.bytes;
	_outburst=zz.fragsize;
    }

    if(_buffersize==0){
	// Measuring buffer size:
	char* data;
	_buffersize=0;
#ifdef HAVE_AUDIO_SELECT
	data=new char[_outburst];
	while(_buffersize<0x40000) {
	    fd_set rfds;
	    struct timeval tv;
	    FD_ZERO(&rfds); FD_SET(fd,&rfds);
	    tv.tv_sec=0; tv.tv_usec = 0;
	    if(!::select(fd+1, NULL, &rfds, NULL, &tv)) break;
	    ::write(fd,data,_outburst);
	    _buffersize+=_outburst;
	}
	delete data;
	if(_buffersize==0){
	    mpxp_err<<std::endl<<"*** OSS-CONF: Your audio driver DOES NOT support select()  ***"<<std::endl;
	    mpxp_err<<"Recompile mplayerxp with #undef HAVE_AUDIO_SELECT in mpxp_config.h!"<<std::endl;
	    return MPXP_False;
	}
#endif
    }

    _outburst-=_outburst % bps(); // round down
    return MPXP_Ok;
}

// stop playing and empty buffers (for seeking/pause)
void Oss_AO_Interface::reset(){
    unsigned long tmp;
    ::close(fd);
    fd=::open(dsp.c_str(), O_WRONLY);
    if(fd < 0){
	mpxp_fatal<<std::endl<<"Fatal error: *** CANNOT RE-OPEN / RESET AUDIO DEVICE *** "<<strerror(errno)<<std::endl;
	return;
    }

#if defined(FD_CLOEXEC) && defined(F_SETFD)
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
    ::ioctl (SNDCTL_DSP_SETFMT, reinterpret_cast<long>(&_format));
    if(_format != AFMT_AC3) {
	if (_channels > 2)
	    ::ioctl(fd, SNDCTL_DSP_CHANNELS, &_channels);
	else {
	    int c = _channels-1;
	    ::ioctl (fd, SNDCTL_DSP_STEREO, &c);
	}
	::ioctl (fd, SNDCTL_DSP_SPEED, &_samplerate);
    }
}

// stop playing, keep buffers (for pause)
void Oss_AO_Interface::pause() { reset(); }
// resume playing, after audio_pause()
void Oss_AO_Interface::resume() { reset(); }
// return: how many bytes can be played without blocking
unsigned Oss_AO_Interface::get_space() {
    unsigned playsize=_outburst;

#ifdef SNDCTL_DSP_GETOSPACE
    if(::ioctl(fd, SNDCTL_DSP_GETOSPACE, &zz)!=-1){
	// calculate exact buffer space:
	playsize = zz.fragments*zz.fragsize;
	if (playsize > MAX_OUTBURST)
	    playsize = (MAX_OUTBURST / zz.fragsize) * zz.fragsize;
	return playsize;
    }
#endif
    // check buffer
#ifdef HAVE_AUDIO_SELECT
    {  fd_set rfds;
       struct timeval tv;
       FD_ZERO(&rfds);
       FD_SET(fd, &rfds);
       tv.tv_sec = 0;
       tv.tv_usec = 0;
       if(!::select(fd+1, NULL, &rfds, NULL, &tv)) return 0; // not block!
    }
#endif
    return _outburst;
}

// plays 'len' bytes of 'data'
// it should round it down to outburst*n
// return: number of bytes played
unsigned Oss_AO_Interface::play(const any_t* data,unsigned len,unsigned flags){
    UNUSED(flags);
    len/=_outburst;
    len=::write(fd,data,len*_outburst);
    return len;
}

// return: delay in seconds between first and last sample in buffer
float Oss_AO_Interface::get_delay(){
    int ierr;
    /* Calculate how many bytes/second is sent out */
    if(delay_method==2){
#ifdef SNDCTL_DSP_GETODELAY
	int r=0;
	ierr=::ioctl(fd, SNDCTL_DSP_GETODELAY, &r);
	if(ierr!=-1) return ((float)r)/(float)bps();
#endif
	delay_method=1; // fallback if not supported
    }
    if(delay_method==1){
	// SNDCTL_DSP_GETOSPACE
	ierr=::ioctl(fd, SNDCTL_DSP_GETOSPACE, &zz);
	if(ierr!=-1) return ((float)(_buffersize-zz.bytes))/(float)bps();
	delay_method=0; // fallback if not supported
    }
    return ((float)_buffersize)/(float)bps();
}

unsigned Oss_AO_Interface::samplerate() const { return _samplerate; }
unsigned Oss_AO_Interface::channels() const { return _channels; }
unsigned Oss_AO_Interface::format() const { return _format; }
unsigned Oss_AO_Interface::buffersize() const { return _buffersize; }
unsigned Oss_AO_Interface::outburst() const { return _outburst; }
MPXP_Rc  Oss_AO_Interface::test_channels(unsigned ch) const {
    unsigned rval=ch;
    if (rval > 2) {
	if (::ioctl(fd, SNDCTL_DSP_CHANNELS, &rval) == -1 ||
	rval != ch ) return MPXP_False;
    } else {
	int c = rval-1;
	if (::ioctl(fd, SNDCTL_DSP_STEREO, &c) == -1) return MPXP_False;
    }
    return MPXP_Ok;
}
MPXP_Rc  Oss_AO_Interface::test_rate(unsigned r) const {
    unsigned rval=r;
    if (::ioctl(fd, SNDCTL_DSP_SPEED, &rval) != -1) {
	if(rval == r) return MPXP_Ok;
    }
    return MPXP_False;
}
MPXP_Rc  Oss_AO_Interface::test_format(unsigned arg) const {
    unsigned rval;
    if (::ioctl (fd, SNDCTL_DSP_GETFMTS, &rval) != -1) {
	if((rval & AFMT_MU_LAW) && arg==AFMT_MU_LAW) return MPXP_Ok;
	if((rval & AFMT_A_LAW) && arg==AFMT_A_LAW) return MPXP_Ok;
	if((rval & AFMT_IMA_ADPCM) && arg==AFMT_IMA_ADPCM) return MPXP_Ok;
	if((rval & AFMT_U8) && arg==AFMT_U8) return MPXP_Ok;
	if((rval & AFMT_S16_LE) && arg==AFMT_S16_LE) return MPXP_Ok;
	if((rval & AFMT_S16_BE) && arg==AFMT_S16_BE) return MPXP_Ok;
	if((rval & AFMT_S8) && arg==AFMT_S8) return MPXP_Ok;
	if((rval & AFMT_U16_LE) && arg==AFMT_U16_LE) return MPXP_Ok;
	if((rval & AFMT_U16_BE) && arg==AFMT_U16_BE) return MPXP_Ok;
	if((rval & AFMT_MPEG) && arg==AFMT_MPEG) return MPXP_Ok;
	if((rval & AFMT_AC3) && arg==AFMT_AC3) return MPXP_Ok;
	if((rval & AFMT_S24_LE) && arg==AFMT_S24_LE) return MPXP_Ok;
	if((rval & AFMT_S24_BE) && arg==AFMT_S24_BE) return MPXP_Ok;
	if((rval & AFMT_U24_LE) && arg==AFMT_U24_LE) return MPXP_Ok;
	if((rval & AFMT_U24_BE) && arg==AFMT_U24_BE) return MPXP_Ok;
	if((rval & AFMT_S32_LE) && arg==AFMT_S32_LE) return MPXP_Ok;
	if((rval & AFMT_S32_BE) && arg==AFMT_S32_BE) return MPXP_Ok;
	if((rval & AFMT_U32_LE) && arg==AFMT_U32_LE) return MPXP_Ok;
	if((rval & AFMT_U32_BE) && arg==AFMT_U32_BE) return MPXP_Ok;
    }
    return MPXP_False;
}

static AO_Interface* query_interface(const std::string& sd) { return new(zeromem) Oss_AO_Interface(sd); }

extern const ao_info_t audio_out_oss = {
    "OSS/ioctl audio output",
    "oss",
    "A'rpi",
    "",
    query_interface
};
} // namespace	usr
