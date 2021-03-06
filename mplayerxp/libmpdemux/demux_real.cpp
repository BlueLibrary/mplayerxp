#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
/*     Real parser & demuxer

    (C) Alex Beregszaszi <alex@naxine.org>

    Based on FFmpeg's libav/rm.c.

Audio codecs: (supported by RealPlayer8 for Linux)
    DNET - RealAudio 3.0, really it's AC3 in swapped-byteorder
    SIPR - SiproLab's audio codec, ACELP decoder working with MPlayer,
	   needs fine-tuning too :)
    ATRC - RealAudio 8 (ATRAC3) - www.minidisc.org/atrac3_article.pdf,
	   ACM decoder uploaded, needs some fine-tuning to work
	   -> RealAudio 8
    COOK/COKR - Real Cooker -> RealAudio G2

Video codecs: (supported by RealPlayer8 for Linux)
    RV10 - H.263 based, working with libavcodec's decoder
    RV20-RV40 - using RealPlayer's codec plugins

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mpxp_help.h"

#include "libmpstream2/stream.h"
#include "demuxer.h"
#include "demuxer_internal.h"
#include "stheader.h"
#include "osdep/bswap.h"
#include "aviprint.h"
#include "libmpcodecs/dec_audio.h"
#include "libao3/afmt.h"
#include "demux_msg.h"

#define MKTAG(a, b, c, d) (a | (b << 8) | (c << 16) | (d << 24))

static const int MAX_STREAMS=32;

typedef struct {
    int		timestamp;
    int		offset;
    int		packetno;
} real_index_table_t;

struct real_priv_t : public Opaque {
    public:
	real_priv_t() {}
	virtual ~real_priv_t();

	/* for seeking */
	int		index_chunk_offset;
	real_index_table_t *index_table[MAX_STREAMS];
	int		index_table_size[MAX_STREAMS];
	int		index_malloc_size[MAX_STREAMS];
	int		data_chunk_offset;
	int		num_of_packets;
	int		current_packet;

	int		audio_need_keyframe;
	int		video_after_seek;

	int		current_apacket;
	int		current_vpacket;

	// timestamp correction:
	int		kf_base;// timestamp of the prev. video keyframe
	int		kf_pts;	// timestamp of next video keyframe
	float	v_pts;  // previous video timestamp
	float	a_pts;
	unsigned long	duration;

	/**
	* Used to demux multirate files
	*/
	int is_multirate; ///< != 0 for multirate files
	int str_data_offset[MAX_STREAMS]; ///< Data chunk offset for every audio/video stream
	int audio_curpos; ///< Current file position for audio demuxing
	int video_curpos; ///< Current file position for video demuxing
	int a_num_of_packets; ///< Number of audio packets
	int v_num_of_packets; ///< Number of video packets
	int a_idx_ptr; ///< Audio index position pointer
	int v_idx_ptr; ///< Video index position pointer
	int a_bitrate; ///< Audio bitrate
	int v_bitrate; ///< Video bitrate
	int stream_switch; ///< Flag used to switch audio/video demuxing
};

real_priv_t::~real_priv_t() {
    for(unsigned i=0; i<MAX_STREAMS; i++)
	if(index_table[i])
	    delete index_table[i];
}

/* originally from FFmpeg */
static void get_str(int isbyte, Demuxer *demuxer, char *buf, int buf_size)
{
    int len;

    if (isbyte)
	len = demuxer->stream->read(type_byte);
    else
	len = demuxer->stream->read(type_word);

    binary_packet bp=demuxer->stream->read((len > buf_size) ? buf_size : len); memcpy(buf,bp.data(),bp.size());
    if (len > buf_size)
	demuxer->stream->skip( len-buf_size);

    MSG_V("read_str: %d bytes read\n", len);
}

static void skip_str(int isbyte, Demuxer *demuxer)
{
    int len;

    if (isbyte)
	len = demuxer->stream->read(type_byte);
    else
	len = demuxer->stream->read(type_word);

    demuxer->stream->skip( len);

    MSG_V("skip_str: %d bytes skipped\n", len);
}

static void dump_index(Demuxer *demuxer, int stream_id)
{
    real_priv_t *priv = static_cast<real_priv_t*>(demuxer->priv);
    real_index_table_t *index;
    int i, entries;

    if (mp_conf.verbose<=1)
	return;

    if (stream_id > MAX_STREAMS)
	return;

    index = priv->index_table[stream_id];
    entries = priv->index_table_size[stream_id];

    MSG_V("Index table for stream %d\n", stream_id);
    for (i = 0; i < entries; i++)
	MSG_V("i: %d, pos: %d, timestamp: %d\n", i, index[i].offset, index[i].timestamp);
}

static int parse_index_chunk(Demuxer *demuxer)
{
    real_priv_t *priv = static_cast<real_priv_t*>(demuxer->priv);
    int origpos = demuxer->stream->tell();
    int next_header_pos = priv->index_chunk_offset;
    int i, entries, stream_id;

read_index:
    demuxer->stream->seek( next_header_pos);

    i = demuxer->stream->read_le(type_dword);
    if ((i == -256) || (i != MKTAG('I', 'N', 'D', 'X')))
    {
	MSG_V("Something went wrong, no index chunk found on given address (%d)\n",
	    next_header_pos);
	index_mode = -1;
	if (i == -256)
	    demuxer->stream->reset();
	demuxer->stream->seek( origpos);
	return 0;
    }

    MSG_V("Reading index table from index chunk (%d)\n",
	next_header_pos);

    i = demuxer->stream->read(type_dword);
    MSG_V("size: %d bytes\n", i);

    i = demuxer->stream->read(type_word);
    if (i != 0)
	MSG_V("Hmm, index table with unknown version (%d), please report it to MPlayer developers!\n", i);

    entries = demuxer->stream->read(type_dword);
    MSG_V("entries: %d\n", entries);

    stream_id = demuxer->stream->read(type_word);
    MSG_V("stream_id: %d\n", stream_id);

    next_header_pos = demuxer->stream->read(type_dword);
    MSG_V("next_header_pos: %d\n", next_header_pos);
    if (entries <= 0)
    {
	if (next_header_pos)
	    goto read_index;
	i = entries;
	goto end;
    }

    priv->index_table_size[stream_id] = entries;
    priv->index_table[stream_id] = new real_index_table_t[priv->index_table_size[stream_id]];

    for (i = 0; i < entries; i++)
    {
	demuxer->stream->skip( 2); /* version */
	priv->index_table[stream_id][i].timestamp = demuxer->stream->read(type_dword);
	priv->index_table[stream_id][i].offset = demuxer->stream->read(type_dword);
	priv->index_table[stream_id][i].packetno = demuxer->stream->read(type_dword);
    }

    dump_index(demuxer, stream_id);

    if (next_header_pos > 0)
	goto read_index;

end:
    demuxer->flags |= Demuxer::Seekable; /* got index, we're able to seek */
    if (i == -256)
	demuxer->stream->reset();
    demuxer->stream->seek( origpos);
    if (i == -256)
	return 0;
    else
	return 1;
}

static void add_index_item(Demuxer *demuxer, int stream_id, int timestamp, int offset)
{
  if ((unsigned)stream_id < MAX_STREAMS)
  {
    real_priv_t *priv = static_cast<real_priv_t*>(demuxer->priv);
    real_index_table_t *index;
    if (priv->index_table_size[stream_id] >= priv->index_malloc_size[stream_id])
    {
      if (priv->index_malloc_size[stream_id] == 0)
	priv->index_malloc_size[stream_id] = 2048;
      else
	priv->index_malloc_size[stream_id] += priv->index_malloc_size[stream_id] / 2;
      priv->index_table[stream_id] = (real_index_table_t*)mp_realloc(priv->index_table[stream_id], priv->index_malloc_size[stream_id]*sizeof(priv->index_table[0][0]));
    }
    if (priv->index_table_size[stream_id] > 0)
    {
      index = &priv->index_table[stream_id][priv->index_table_size[stream_id] - 1];
      if (index->timestamp >= timestamp || index->offset >= offset)
	return;
    }
    index = &priv->index_table[stream_id][priv->index_table_size[stream_id]++];
    index->timestamp = timestamp;
    index->offset = offset;
  }
}

static void add_index_segment(Demuxer *demuxer, int seek_stream_id, int seek_timestamp)
{
  int tag, len, stream_id, timestamp, flags;
  if (seek_timestamp != -1 && (unsigned)seek_stream_id >= MAX_STREAMS)
    return;
  while (1)
  {
    demuxer->filepos = demuxer->stream->tell();

    tag = demuxer->stream->read(type_dword);
    if (tag == MKTAG('A', 'T', 'A', 'D'))
    {
      demuxer->stream->skip( 14);
      continue; /* skip to next loop */
    }
    len = tag & 0xffff;
    if (tag == -256 || len < 12)
      break;

    stream_id = demuxer->stream->read(type_word);
    timestamp = demuxer->stream->read(type_dword);

    demuxer->stream->skip( 1); /* reserved */
    flags = demuxer->stream->read(type_byte);

    if (flags == -256)
      break;

    if (flags & 2)
    {
      add_index_item(demuxer, stream_id, timestamp, demuxer->filepos);
      if (stream_id == seek_stream_id && timestamp >= seek_timestamp)
      {
	demuxer->stream->seek( demuxer->filepos);
	return;
      }
    }
    // printf("Index: stream=%d packet=%d timestamp=%d len=%d flags=0x%x datapos=0x%x\n", stream_id, entries, timestamp, len, flags, index->offset);
    /* skip data */
    demuxer->stream->skip( len-12);
  }
}

static int generate_index(Demuxer *demuxer)
{
  real_priv_t *priv = static_cast<real_priv_t*>(demuxer->priv);
  int origpos = demuxer->stream->tell();
  int data_pos = priv->data_chunk_offset-10;
  int i;
  int tag;

  demuxer->stream->seek( data_pos);
  tag = demuxer->stream->read(type_dword);
  if (tag != MKTAG('A', 'T', 'A', 'D'))
  {
    MSG_WARN("Something went wrong, no data chunk found on given address (%d)\n", data_pos);
  }
  else
  {
    demuxer->stream->skip( 14);
    add_index_segment(demuxer, -1, -1);
  }
  for (i = 0; i < MAX_STREAMS; i++)
  {
    if (priv->index_table_size[i] > 0)
    {
      dump_index(demuxer, i);
    }
  }
  demuxer->stream->reset();
  demuxer->stream->seek( origpos);
  return 0;
}

static MPXP_Rc real_probe(Demuxer* demuxer)
{
    real_priv_t *priv;
    int c;

    MSG_V("Checking for REAL\n");

    c = demuxer->stream->read_le(type_dword);
    if (c == -256)
	return MPXP_False; /* EOF */
    if (c != MKTAG('.', 'R', 'M', 'F'))
	return MPXP_False; /* bad magic */

    priv = new(zeromem) real_priv_t;
    demuxer->priv = priv;
    demuxer->file_format=Demuxer::Type_REAL;
    return MPXP_Ok;
}

void hexdump(char *, unsigned long);

#define SKIP_BITS(n) buffer<<=n
#define SHOW_BITS(n) ((buffer)>>(32-(n)))
static float real_fix_timestamp(real_priv_t* priv, unsigned char* s, int timestamp, unsigned int format)
{
  float v_pts;
  uint32_t buffer= (s[0]<<24) + (s[1]<<16) + (s[2]<<8) + s[3];
  int kf=timestamp;
  int pict_type;
  int orig_kf;

#if 1
  if(format==mmioFOURCC('R','V','3','0') || format==mmioFOURCC('R','V','4','0')){
    if(format==mmioFOURCC('R','V','3','0')){
      SKIP_BITS(3);
      pict_type= SHOW_BITS(2);
      SKIP_BITS(2 + 7);
    }else{
      SKIP_BITS(1);
      pict_type= SHOW_BITS(2);
      SKIP_BITS(2 + 7 + 3);
    }
    orig_kf=
    kf= SHOW_BITS(13);  //    kf= 2*SHOW_BITS(12);
//    if(pict_type==0)
    if(pict_type<=1){
      // I frame, sync timestamps:
      priv->kf_base=timestamp-kf;
      if(mp_conf.verbose>1) MSG_DBG2("\nTS: base=%08X\n",priv->kf_base);
      kf=timestamp;
    } else {
      // P/B frame, merge timestamps:
      int tmp=timestamp-priv->kf_base;
      kf|=tmp&(~0x1fff);	// combine with packet timestamp
      if(kf<tmp-4096) kf+=8192; else // workaround wrap-around problems
      if(kf>tmp+4096) kf-=8192;
      kf+=priv->kf_base;
    }
    if(pict_type != 3){ // P || I  frame -> swap timestamps
	int tmp=kf;
	kf=priv->kf_pts;
	priv->kf_pts=tmp;
//	if(kf<=tmp) kf=0;
    }
    MSG_DBG2("\nTS: %08X -> %08X (%04X) %d %02X %02X %02X %02X %5d\n",timestamp,kf,orig_kf,pict_type,s[0],s[1],s[2],s[3],kf-(int)(1000.0*priv->v_pts));
  }
#endif
    v_pts=kf*0.001f;
    priv->v_pts=v_pts;
    return v_pts;
}

typedef struct dp_hdr_s {
    uint32_t chunks;	// number of chunks
    uint32_t timestamp; // timestamp from packet header
    uint32_t len;	// length of actual data
    uint32_t chunktab;	// offset to chunk offset array
} dp_hdr_t;

// return value:
//     0 = EOF or no stream found
//     1 = successfully read a packet
static int real_demux(Demuxer *demuxer,Demuxer_Stream *__ds)
{
    real_priv_t *priv = static_cast<real_priv_t*>(demuxer->priv);
    Demuxer_Stream *ds = NULL;
    float pts;
    int len;
    int timestamp;
    int stream_id;
    int flags;
    int version;
    int reserved;
    Demuxer_Packet *dp;
    binary_packet bp(1);

  while(1){

    /* Handle audio/video demxing switch for multirate files (non-interleaved) */
    if (priv->is_multirate && priv->stream_switch) {
	if (priv->a_idx_ptr >= priv->index_table_size[demuxer->audio->id])
	    demuxer->audio->eof = 1;
	if (priv->v_idx_ptr >= priv->index_table_size[demuxer->video->id])
	    demuxer->video->eof = 1;
	if (demuxer->audio->eof && demuxer->video->eof)
	    return 0;
	else if (!demuxer->audio->eof && demuxer->video->eof)
	    demuxer->stream->seek( priv->audio_curpos); // Get audio
	else if (demuxer->audio->eof && !demuxer->video->eof)
	    demuxer->stream->seek( priv->video_curpos); // Get video
	else if (priv->index_table[demuxer->audio->id][priv->a_idx_ptr].timestamp <
	    priv->index_table[demuxer->video->id][priv->v_idx_ptr].timestamp)
	    demuxer->stream->seek( priv->audio_curpos); // Get audio
	else
	    demuxer->stream->seek( priv->video_curpos); // Get video
	priv->stream_switch = 0;
    }

    demuxer->filepos = demuxer->stream->tell();
    version = demuxer->stream->read(type_word); /* version */
    len = demuxer->stream->read(type_word);
    if ((version==0x4441) && (len==0x5441)) { /* new data chunk */
	MSG_V("demux_real: New data chunk is comming!!!\n");
	if (priv->is_multirate) return 0; // EOF
	demuxer->stream->skip(14);
	demuxer->filepos = demuxer->stream->tell();
	version = demuxer->stream->read(type_word); /* version */
	len = demuxer->stream->read(type_word);
    } else if ((version == 0x494e) && (len == 0x4458)) {
	MSG_V("demux_real: Found INDX chunk. EOF.\n");
	demuxer->stream->eof(1);
	return 0;
    }

    if (len == -256){ /* EOF */
	return 0;
    }
    if (len < 12){
	MSG_V("%08X: packet v%d len=%d  \n",(int)demuxer->filepos,(int)version,(int)len);
	MSG_V("bad packet len (%d)\n", len);
	demuxer->stream->skip( len);
	continue; //goto loop;
    }

    stream_id = demuxer->stream->read(type_word);
    timestamp = demuxer->stream->read(type_dword);
    pts=timestamp*0.001f;
    reserved = demuxer->stream->read(type_byte);
    flags = demuxer->stream->read(type_byte);
    /* flags:		*/
    /*  0x1 - reliable  */
    /* 	0x2 - keyframe	*/

    if (version == 1) {
	int tmp;
	tmp = demuxer->stream->read(type_byte);
	MSG_DBG2("Version: %d, skipped byte is %d\n", version, tmp);
	len--;
    }

    if (flags & 2)
	add_index_item(demuxer, stream_id, timestamp, demuxer->filepos);

    MSG_DBG2("\npacket#%d: pos: 0x%0x, len: %d, id: %d, pts: %d, flags: %x rvd:%d\n",
	priv->current_packet, (int)demuxer->filepos, len, stream_id, pts, flags, reserved);

    priv->current_packet++;
    len -= 12;

    /* check stream_id: */

    if(demuxer->audio->id==stream_id){
	if (priv->audio_need_keyframe == 1&& flags != 0x2)
		goto discard;
got_audio:
	ds=demuxer->audio;
	MSG_DBG2( "packet is audio (id: %d)\n", stream_id);

	// parse audio chunk:
	{
	    unsigned plen;
	    plen=0;
	    if (((sh_audio_t *)ds->sh)->wtag == mmioFOURCC('M', 'P', '4', 'A')) {
		uint16_t *sub_packet_lengths, sub_packets, i;
		/* AAC in Real: several AAC frames in one Real packet. */
		/* Second byte, upper four bits: number of AAC frames */
		/* next n * 2 bytes: length of the AAC frames in bytes, BE */
		sub_packets = (demuxer->stream->read(type_word) & 0xf0) >> 4;
		sub_packet_lengths = new(zeromem) uint16_t[sub_packets];
		for (i = 0; i < sub_packets; i++)
		    sub_packet_lengths[i] = demuxer->stream->read(type_word);
		for (i = 0; i < sub_packets; i++) {
		    int l;
		    dp = new(zeromem) Demuxer_Packet(sub_packet_lengths[i]);
		    bp=demuxer->stream->read(sub_packet_lengths[i]); memcpy(dp->buffer(),bp.data(),bp.size());
		    l=bp.size();
		    dp->resize(l);
		    dp->pts = pts;
		    priv->a_pts = pts;
		    dp->pos = demuxer->filepos;
		    dp->flags=DP_NONKEYFRAME;
		    ds->add_packet(dp);
		}
		delete sub_packet_lengths;
		return 1;
	    }
	    dp = new(zeromem) Demuxer_Packet(len);
	    bp=demuxer->stream->read(len); memcpy(dp->buffer(),bp.data(),bp.size());
	    len=bp.size();
	    dp->resize(len);
	    if (priv->audio_need_keyframe == 1) {
		dp->pts = 0;
		priv->audio_need_keyframe = 0;
	    }else
		dp->pts = pts;
	    priv->a_pts=pts;
	    dp->pos = demuxer->filepos;
	    dp->flags = (flags & 0x2) ? DP_KEYFRAME : DP_NONKEYFRAME;
	    ds->add_packet(dp);
	}
// we will not use audio index if we use -idx and have a video
	if(!demuxer->video->sh && index_mode == 2 && (unsigned)demuxer->audio->id < MAX_STREAMS)
		while (priv->current_apacket + 1 < priv->index_table_size[demuxer->audio->id] &&
		       timestamp > priv->index_table[demuxer->audio->id][priv->current_apacket].timestamp)
			priv->current_apacket += 1;

	if(priv->is_multirate)
		while (priv->a_idx_ptr + 1 < priv->index_table_size[demuxer->audio->id] &&
		       timestamp > priv->index_table[demuxer->audio->id][priv->a_idx_ptr + 1].timestamp) {
			priv->a_idx_ptr++;
			priv->audio_curpos = demuxer->stream->tell();
			priv->stream_switch = 1;
		}

	return 1;
    }

    if(demuxer->video->id==stream_id){
got_video:
	ds=demuxer->video;
	MSG_DBG2("packet is video (id: %d)\n", stream_id);

	// parse video chunk:
	{
	    // we need a more complicated, 2nd level demuxing, as the video
	    // frames are stored fragmented in the video chunks :(
	    sh_video_t *sh_video = reinterpret_cast<sh_video_t*>(ds->sh);
	    int vpkg_header, vpkg_length, vpkg_offset;
	    int vpkg_seqnum=-1;
	    int vpkg_subseq=0;

	    while(len>2){
		dp_hdr_t* dp_hdr;
		unsigned char* dp_data;
		uint32_t* extra;

//		printf("xxx len=%d  \n",len);

		// read packet header
		// bit 7: 1=last block in block chain
		// bit 6: 1=short header (only one block?)
		vpkg_header=demuxer->stream->read(type_byte); --len;
		MSG_DBG2("hdr: %02X (len=%d) ",vpkg_header,len);

		if (0x40==(vpkg_header&0xc0)) {
		    // seems to be a very short header
		    // 2 bytes, purpose of the second byte yet unknown
		    int bummer;
		    bummer=demuxer->stream->read(type_byte); --len;
		    MSG_DBG2("%02X",bummer);
		    vpkg_offset=0;
		    vpkg_length=len;
		} else {

		    if (0==(vpkg_header&0x40)) {
			// sub-seqnum (bits 0-6: number of fragment. bit 7: ???)
			vpkg_subseq=demuxer->stream->read(type_byte);
			--len;
			MSG_DBG2("subseq: %02X ",vpkg_subseq);
			vpkg_subseq&=0x7f;
		    }

		    // size of the complete packet
		    // bit 14 is always one (same applies to the offset)
		    vpkg_length=demuxer->stream->read(type_word);
		    len-=2;
		    MSG_DBG2("l: %02X %02X ",vpkg_length>>8,vpkg_length&0xff);
		    if (!(vpkg_length&0xC000)) {
			vpkg_length<<=16;
			vpkg_length|=demuxer->stream->read(type_word);
			MSG_DBG2("l+: %02X %02X ",(vpkg_length>>8)&0xff,vpkg_length&0xff);
			len-=2;
		    } else
		    vpkg_length&=0x3fff;

		    // offset of the following data inside the complete packet
		    // Note: if (hdr&0xC0)==0x80 then offset is relative to the
		    // _end_ of the packet, so it's equal to fragment size!!!
		    vpkg_offset=demuxer->stream->read(type_word);
		    len-=2;
		    MSG_DBG2("o: %02X %02X ",vpkg_offset>>8,vpkg_offset&0xff);
		    if (!(vpkg_offset&0xC000)) {
			vpkg_offset<<=16;
			vpkg_offset|=demuxer->stream->read(type_word);
			MSG_DBG2("o+: %02X %02X ",(vpkg_offset>>8)&0xff,vpkg_offset&0xff);
			len-=2;
		    } else
		    vpkg_offset&=0x3fff;

		    vpkg_seqnum=demuxer->stream->read(type_byte); --len;
		    MSG_DBG2("seq: %02X ",vpkg_seqnum);
		}
		MSG_DBG2("\n");
		MSG_DBG2("blklen=%d\n", len);
		MSG_DBG2("block: hdr=0x%0x, len=%d, offset=%d, seqnum=%d\n",
		    vpkg_header, vpkg_length, vpkg_offset, vpkg_seqnum);

		if(ds->asf_packet){
		    dp=ds->asf_packet;
		    dp_hdr=(dp_hdr_t*)dp->buffer();
		    dp_data=dp->buffer()+sizeof(dp_hdr_t);
		    extra=(uint32_t*)(dp->buffer()+dp_hdr->chunktab);
		    MSG_DBG2("we have an incomplete packet (oldseq=%d new=%d)\n",ds->asf_seq,vpkg_seqnum);
		    // we have an incomplete packet:
		    if(ds->asf_seq!=vpkg_seqnum){
			// this fragment is for new packet, close the old one
			MSG_DBG2("closing probably incomplete packet, len: %d  \n",dp->length());
			if(priv->video_after_seek){
			    dp->pts=timestamp;
				priv->kf_base = 0;
				priv->kf_pts = dp_hdr->timestamp;
				priv->video_after_seek = 0;
			} else
			dp->pts=(dp_hdr->len<3)?0:
			    real_fix_timestamp(priv,dp_data,dp_hdr->timestamp,sh_video->fourcc);
			ds->add_packet(dp);
			ds->asf_packet=NULL;
		    } else {
			// append data to it!
			++dp_hdr->chunks;
			MSG_DBG2("[chunks=%d  subseq=%d]\n",dp_hdr->chunks,vpkg_subseq);
			if(dp_hdr->chunktab+8*(1+dp_hdr->chunks)>dp->length()){
			    // increase buffer size, this should not happen!
			    MSG_WARN("chunktab buffer too small!!!!!\n");
			    dp->resize(dp_hdr->chunktab+8*(4+dp_hdr->chunks));
			    // re-calc pointers:
			    dp_hdr=(dp_hdr_t*)dp->buffer();
			    dp_data=dp->buffer()+sizeof(dp_hdr_t);
			    extra=(uint32_t*)(dp->buffer()+dp_hdr->chunktab);
			}
			extra[2*dp_hdr->chunks+0]=1;
			extra[2*dp_hdr->chunks+1]=dp_hdr->len;
			if(0x80==(vpkg_header&0xc0)){
			    // last fragment!
			    if(dp_hdr->len!=vpkg_length-vpkg_offset)
				MSG_V("warning! assembled.len=%d  frag.len=%d  total.len=%d  \n",dp->length(),vpkg_offset,vpkg_length-vpkg_offset);
			    bp=demuxer->stream->read(vpkg_offset); memcpy(dp_data+dp_hdr->len,bp.data(),bp.size());
			    if((dp_data[dp_hdr->len]&0x20) && (sh_video->fourcc==0x30335652)) --dp_hdr->chunks; else
			    dp_hdr->len+=vpkg_offset;
			    len-=vpkg_offset;
			    MSG_DBG2("fragment (%d bytes) appended, %d bytes left\n",vpkg_offset,len);
			    // we know that this is the last fragment -> we can close the packet!
			    if(priv->video_after_seek){
				dp->pts=pts;
				    priv->kf_base = 0;
				    priv->kf_pts = dp_hdr->timestamp;
				    priv->video_after_seek = 0;
			    } else
			    dp->pts=(dp_hdr->len<3)?0:
				real_fix_timestamp(priv,dp_data,dp_hdr->timestamp,sh_video->fourcc);
			    ds->add_packet(dp);
			    ds->asf_packet=NULL;
			    // continue parsing
			    continue;
			}
			// non-last fragment:
			if(dp_hdr->len!=vpkg_offset)
			    MSG_V("warning! assembled.len=%d  offset=%d  frag.len=%d  total.len=%d  \n",dp->length(),vpkg_offset,len,vpkg_length);
			bp=demuxer->stream->read(len); memcpy(dp_data+dp_hdr->len,bp.data(),bp.size());
			if((dp_data[dp_hdr->len]&0x20) && (sh_video->fourcc==0x30335652)) --dp_hdr->chunks; else
			dp_hdr->len+=len;
			len=0;
			break; // no more fragments in this chunk!
		    }
		}
		// create new packet!
		dp = new(zeromem) Demuxer_Packet(sizeof(dp_hdr_t)+vpkg_length+8*(1+2*(vpkg_header&0x3F)));
		// the timestamp seems to be in milliseconds
		dp->pts = 0; // timestamp/1000.0f; //timestamp=0;
		dp->pos = demuxer->filepos;
		dp->flags = (flags & 0x2) ? DP_KEYFRAME : DP_NONKEYFRAME;
		ds->asf_seq = vpkg_seqnum;
		dp_hdr=(dp_hdr_t*)dp->buffer();
		dp_hdr->chunks=0;
		dp_hdr->timestamp=timestamp;
		dp_hdr->chunktab=sizeof(dp_hdr_t)+vpkg_length;
		dp_data=dp->buffer()+sizeof(dp_hdr_t);
		extra=(uint32_t*)(dp->buffer()+dp_hdr->chunktab);
		extra[0]=1; extra[1]=0; // offset of the first chunk
		if(0x00==(vpkg_header&0xc0)){
		    // first fragment:
		    dp_hdr->len=len;
		    bp=demuxer->stream->read(len); memcpy(dp_data,bp.data(),bp.size());
		    ds->asf_packet=dp;
		    len=0;
		    break;
		}
		// whole packet (not fragmented):
		if (vpkg_length > len) {
		    MSG_WARN("\n******** WARNING: vpkg_length=%i > len=%i ********\n", vpkg_length, len);
		    /*
		     * To keep the video stream rolling, we need to break
		     * here. We shouldn't touch len to make sure rest of the
		     * broken packet is skipped.
		     */
		    break;
		}
		dp_hdr->len=vpkg_length; len-=vpkg_length;
		bp=demuxer->stream->read(vpkg_length); memcpy(dp_data,bp.data(),bp.size());
		if(priv->video_after_seek){
		    dp->pts=pts;
			priv->kf_base = 0;
			priv->kf_pts = dp_hdr->timestamp;
			priv->video_after_seek = 0;
		} else
		dp->pts=(dp_hdr->len<3)?0:
		    real_fix_timestamp(priv,dp_data,dp_hdr->timestamp,sh_video->fourcc);
		ds->add_packet(dp);

	    } // while(len>0)

	    if(len){
		MSG_WARN("\n******** !!!!!!!! BUG!! len=%d !!!!!!!!!!! ********\n",len);
		if(len>0) demuxer->stream->skip( len);
	    }
	}
	if ((unsigned)demuxer->video->id < MAX_STREAMS)
		while (priv->current_vpacket + 1 < priv->index_table_size[demuxer->video->id] &&
		       timestamp > priv->index_table[demuxer->video->id][priv->current_vpacket + 1].timestamp)
			priv->current_vpacket += 1;

	if(priv->is_multirate)
		while (priv->v_idx_ptr + 1 < priv->index_table_size[demuxer->video->id] &&
		       timestamp > priv->index_table[demuxer->video->id][priv->v_idx_ptr + 1].timestamp) {
			priv->v_idx_ptr++;
			priv->video_curpos = demuxer->stream->tell();
			priv->stream_switch = 1;
		}

	return 1;
    }

if(stream_id<256){

    if(demuxer->audio->id==-1 && demuxer->get_sh_audio(stream_id)){
	sh_audio_t *sh = demuxer->get_sh_audio(stream_id);
	demuxer->audio->id=stream_id;
	sh->ds=demuxer->audio;
	demuxer->audio->sh=sh;
	MSG_V("Auto-selected RM audio ID = %d\n",stream_id);
	goto got_audio;
    }

    if(demuxer->video->id==-1 && demuxer->get_sh_video(stream_id)){
	sh_video_t *sh = demuxer->get_sh_video(stream_id);
	demuxer->video->id=stream_id;
	sh->ds=demuxer->video;
	demuxer->video->sh=sh;
	MSG_V("Auto-selected RM video ID = %d\n",stream_id);
	goto got_video;
    }

}

    MSG_DBG2("unknown stream id (%d)\n", stream_id);
discard:
    demuxer->stream->skip( len);
  }//    goto loop;
}

static Opaque* real_open(Demuxer* demuxer)
{
    real_priv_t* priv = static_cast<real_priv_t*>(demuxer->priv);
    int num_of_headers;
    int a_streams=0;
    int v_streams=0;
    int i;
    binary_packet bp(1);

    demuxer->stream->skip( 4); /* header size */
    demuxer->stream->skip( 2); /* version */
//    demuxer->stream->skip( 4);
    i = demuxer->stream->read(type_dword);
    MSG_V( "real: File version: %d\n", i);
    num_of_headers = demuxer->stream->read(type_dword);
//    demuxer->stream->skip( 4); /* number of headers */

    /* parse chunks */
    for (i = 1; i <= num_of_headers; i++)
    {
	int chunk_id, chunk_pos, chunk_size;

	chunk_pos = demuxer->stream->tell();
	chunk_id = demuxer->stream->read_le(type_dword);
	chunk_size = demuxer->stream->read(type_dword);

	demuxer->stream->skip( 2); /* version */

	MSG_V( "Chunk: %.4s (%x) (size: 0x%x, offset: 0x%x)\n",
	    (char *)&chunk_id, chunk_id, chunk_size, chunk_pos);

	if (chunk_size < 10){
	    MSG_ERR("demux_real: invalid chunksize! (%d)\n",chunk_size);
	    break; //return;
	}

	switch(chunk_id)
	{
	    case MKTAG('P', 'R', 'O', 'P'):
		/* Properties header */

		demuxer->stream->skip( 4); /* max bitrate */
		demuxer->stream->skip( 4); /* avg bitrate */
		demuxer->stream->skip( 4); /* max packet size */
		demuxer->stream->skip( 4); /* avg packet size */
		demuxer->stream->skip( 4); /* nb packets */
		demuxer->movi_length=demuxer->stream->read(type_dword)/1000; /* duration */
		demuxer->stream->skip( 4); /* preroll */
		priv->index_chunk_offset = demuxer->stream->read(type_dword);
		MSG_V("First index chunk offset: 0x%x\n", priv->index_chunk_offset);
		priv->data_chunk_offset = demuxer->stream->read(type_dword)+10;
		MSG_V("First data chunk offset: 0x%x\n", priv->data_chunk_offset);
		demuxer->stream->skip( 2); /* nb streams */
#if 0
		demuxer->stream->skip( 2); /* flags */
#else
		{
		    int flags = demuxer->stream->read(type_word);

		    if (flags)
		    {
		    MSG_V("Flags (%x): ", flags);
		    if (flags & 0x1)
			MSG_V("[save allowed] ");
		    if (flags & 0x2)
			MSG_V("[perfect play (more buffers)] ");
		    if (flags & 0x4)
			MSG_V("[live broadcast] ");
		    MSG_V("\n");
		    }
		}
#endif
		break;
	    case MKTAG('C', 'O', 'N', 'T'):
	    {
		/* Content description header */
		char *buf;
		int len;

		len = demuxer->stream->read(type_word);
		if (len > 0)
		{
		    buf = new char [len+1];
		    bp=demuxer->stream->read(len); memcpy(buf,bp.data(),bp.size());
		    buf[len] = 0;
		    demuxer->info().add( INFOT_NAME, buf);
		    delete buf;
		}

		len = demuxer->stream->read(type_word);
		if (len > 0)
		{
		    buf = new char [len+1];
		    bp=demuxer->stream->read(len); memcpy(buf,bp.data(),bp.size());
		    buf[len] = 0;
		    demuxer->info().add( INFOT_AUTHOR, buf);
		    delete buf;
		}

		len = demuxer->stream->read(type_word);
		if (len > 0)
		{
		    buf = new char [len+1];
		    bp=demuxer->stream->read(len); memcpy(buf,bp.data(),bp.size());
		    buf[len] = 0;
		    demuxer->info().add( INFOT_COPYRIGHT, buf);
		    delete buf;
		}

		len = demuxer->stream->read(type_word);
		if (len > 0)
		{
		    buf = new char [len+1];
		    bp=demuxer->stream->read(len); memcpy(buf,bp.data(),bp.size());
		    buf[len] = 0;
		    demuxer->info().add( INFOT_COMMENTS, buf);
		    delete buf;
		}
		break;
	    }
	    case MKTAG('M', 'D', 'P', 'R'):
	    {
		/* Media properties header */
		int stream_id;
		int bitrate;
		int codec_data_size;
		int codec_pos;
		int tmp,len;
		char tmps[256];

		stream_id = demuxer->stream->read(type_word);
		MSG_V("Found new stream (id: %d)\n", stream_id);

		demuxer->stream->skip( 4); /* max bitrate */
		bitrate = demuxer->stream->read(type_dword); /* avg bitrate */
		demuxer->stream->skip( 4); /* max packet size */
		demuxer->stream->skip( 4); /* avg packet size */
		demuxer->stream->skip( 4); /* start time */
		demuxer->stream->skip( 4); /* preroll */
		demuxer->stream->skip( 4); /* duration */

		tmp=demuxer->stream->read(type_byte);
		bp=demuxer->stream->read(tmp); memcpy(tmps,bp.data(),bp.size());
		tmps[tmp]=0;
		if(!demuxer->info().get(INFOT_DESCRIPTION))
		    demuxer->info().add( INFOT_DESCRIPTION, tmps);
		/* Type specific header */
		codec_data_size = demuxer->stream->read(type_dword);
		codec_pos = demuxer->stream->tell();

#ifdef MP_DEBUG
#define stream_skip(st,siz) { int i; for(i=0;i<siz;i++) MSG_V(" %02X",stream_read_char(st)); MSG_V("\n");}
#endif

	if (!strncmp(tmps,"audio/",6)) {
	  if (strstr(tmps,"x-pn-realaudio") || strstr(tmps,"x-pn-multirate-realaudio")) {
		// skip unknown shit - FIXME: find a better/cleaner way!
		len=codec_data_size;
		tmp = demuxer->stream->read(type_dword);
//		mp_msg(MSGT_DEMUX,MSGL_DBG2,"demux_real: type_spec: len=%d  fpos=0x%X  first_dword=0x%X (%.4s)  \n",
//		    (int)codec_data_size,(int)codec_pos,tmp,&tmp);
		while(--len>=8){
			if(tmp==MKTAG(0xfd, 'a', 'r', '.')) break; // audio
			tmp=(tmp<<8)|demuxer->stream->read(type_byte);
		}
		if (tmp != MKTAG(0xfd, 'a', 'r', '.'))
		{
		    MSG_V("Audio: can't find .ra in codec data\n");
		} else {
		    /* audio header */
		    sh_audio_t *sh = demuxer->new_sh_audio(stream_id);
		    char buf[128]; /* for codec name */
		    int frame_size;
		    int sub_packet_size=0;
		    int sub_packet_h=0;
		    int version;
		    int flavor=0;
		    int coded_frame_size=0;
		    int codecdata_length;
		    int i;
		    char *buft;
		    int hdr_size;

		    MSG_V("Found audio stream!\n");
		    version = demuxer->stream->read(type_word);
		    MSG_V("version: %d\n", version);
//		    demuxer->stream->skip( 2); /* version (4 or 5) */
		   if (version == 3) {
		    demuxer->stream->skip( 2);
		    demuxer->stream->skip( 10);
		    demuxer->stream->skip( 4);
		    // Name, author, (c) are also in CONT tag
		    if ((i = demuxer->stream->read(type_byte)) != 0) {
		      buft = new char [i+1];
		      bp=demuxer->stream->read(i); memcpy(buft,bp.data(),bp.size());
		      buft[i] = 0;
		      demuxer->info().add( INFOT_NAME, buft);
		      delete buft;
		    }
		    if ((i = demuxer->stream->read(type_byte)) != 0) {
		      buft = new char [i+1];
		      bp=demuxer->stream->read(i); memcpy(buft,bp.data(),bp.size());
		      buft[i] = 0;
		      demuxer->info().add( INFOT_AUTHOR, buft);
		      delete buft;
		    }
		    if ((i = demuxer->stream->read(type_byte)) != 0) {
		      buft = new char [i+1];
		      bp=demuxer->stream->read(i); memcpy(buft,bp.data(),bp.size());
		      buft[i] = 0;
		      demuxer->info().add( INFOT_COPYRIGHT, buft);
		      delete buft;
		    }
		    if ((i = demuxer->stream->read(type_byte)) != 0)
		      MSG_WARN("Last header byte is not zero!\n");
		    demuxer->stream->skip(1);
		    i = demuxer->stream->read(type_byte);
		    sh->wtag = demuxer->stream->read_le(type_dword);
		    if (i != 4) {
		      MSG_WARN("Audio FourCC size is not 4 (%d), please report to "
			     "MPlayer developers\n", i);
		      demuxer->stream->skip(i - 4);
		    }
		    if (sh->wtag != mmioFOURCC('l','p','c','J')) {
		      MSG_WARN("Version 3 audio with FourCC %8x, please report to "
			     "MPlayer developers\n", sh->wtag);
		    }
		    sh->nch = 1;
		    sh->afmt = bps2afmt(2);
		    sh->rate = 8000;
		    frame_size = 240;
		    strcpy(buf, "14_4");
		   } else {
		    demuxer->stream->skip( 2); // 00 00
		    demuxer->stream->skip( 4); /* .ra4 or .ra5 */
		    demuxer->stream->skip( 4); // ???
		    demuxer->stream->skip( 2); /* version (4 or 5) */
//		    demuxer->stream->skip( 4); // header size == 0x4E
		    hdr_size = demuxer->stream->read(type_dword); // header size
		    MSG_V("header size: %d\n", hdr_size);
		    flavor = demuxer->stream->read(type_word);/* codec flavor id */
		    coded_frame_size = demuxer->stream->read(type_dword);/* needed by codec */
		    //demuxer->stream->skip( 4); /* coded frame size */
		    demuxer->stream->skip( 4); // big number
		    demuxer->stream->skip( 4); // bigger number
		    demuxer->stream->skip( 4); // 2 || -''-
//		    demuxer->stream->skip( 2); // 0x10
		    sub_packet_h = demuxer->stream->read(type_word);
//		    demuxer->stream->skip( 2); /* coded frame size */
		    frame_size = demuxer->stream->read(type_word);
		    MSG_V("frame_size: %d\n", frame_size);
		    sub_packet_size = demuxer->stream->read(type_word);
		    MSG_V("sub_packet_size: %d\n", sub_packet_size);
		    demuxer->stream->skip( 2); // 0

		    if (version == 5)
			demuxer->stream->skip( 6); //0,srate,0

		    sh->rate = demuxer->stream->read(type_word);
		    demuxer->stream->skip( 2);  // 0
		    sh->afmt = bps2afmt(demuxer->stream->read(type_word)/8);
		    sh->nch = demuxer->stream->read(type_word);
		    MSG_V("samplerate: %d, channels: %d\n",
			sh->rate, sh->nch);

		    if (version == 5)
		    {
			demuxer->stream->skip( 4);  // "genr"
			bp=demuxer->stream->read(4); memcpy(buf,bp.data(),bp.size()); // fourcc
			buf[4] = 0;
		    }
		    else
		    {
			/* Desc #1 */
			skip_str(1, demuxer);
			/* Desc #2 */
			get_str(1, demuxer, buf, sizeof(buf));
		    }
		   }

		    /* Emulate WAVEFORMATEX struct: */
		    sh->wf = new(zeromem) WAVEFORMATEX;
		    sh->wf->nChannels = sh->nch;
		    sh->wf->wBitsPerSample = afmt2bps(sh->afmt)*8;
		    sh->wf->nSamplesPerSec = sh->rate;
		    sh->wf->nAvgBytesPerSec = bitrate;
		    sh->wf->nBlockAlign = frame_size;
		    sh->wf->cbSize = 0;
		    sh->wtag = MKTAG(buf[0], buf[1], buf[2], buf[3]);

		    switch (sh->wtag)
		    {
			case MKTAG('d', 'n', 'e', 't'): /* direct support */
			    break;
			case MKTAG('1', '4', '_', '4'):
			    sh->wf->cbSize = 10;
			    sh->wf = (WAVEFORMATEX*)mp_realloc(sh->wf, sizeof(WAVEFORMATEX)+sh->wf->cbSize);
			    ((short*)(sh->wf+1))[0]=0;
			    ((short*)(sh->wf+1))[1]=240;
			    ((short*)(sh->wf+1))[2]=0;
			    ((short*)(sh->wf+1))[3]=0x14;
			    ((short*)(sh->wf+1))[4]=0;
			    break;

			case MKTAG('2', '8', '_', '8'):
			    sh->wf->cbSize = 10;
			    sh->wf = (WAVEFORMATEX*)mp_realloc(sh->wf, sizeof(WAVEFORMATEX)+sh->wf->cbSize);
			    ((short*)(sh->wf+1))[0]=sub_packet_size;
			    ((short*)(sh->wf+1))[1]=sub_packet_h;
			    ((short*)(sh->wf+1))[2]=flavor;
			    ((short*)(sh->wf+1))[3]=coded_frame_size;
			    ((short*)(sh->wf+1))[4]=0;
			    break;

			case MKTAG('s', 'i', 'p', 'r'):
			case MKTAG('a', 't', 'r', 'c'):
			case MKTAG('c', 'o', 'o', 'k'):
			    // realaudio codec plugins - common:
//			    sh->wf->cbSize = 4+2+24;
			    demuxer->stream->skip(3);  // Skip 3 unknown bytes
			    if (version==5)
			      demuxer->stream->skip(1);  // Skip 1 additional unknown byte
			    codecdata_length=demuxer->stream->read(type_dword);
			    sh->wf->cbSize = 10+codecdata_length;
			    sh->wf = (WAVEFORMATEX*)mp_realloc(sh->wf, sizeof(WAVEFORMATEX)+sh->wf->cbSize);
			    ((short*)(sh->wf+1))[0]=sub_packet_size;
			    ((short*)(sh->wf+1))[1]=sub_packet_h;
			    ((short*)(sh->wf+1))[2]=flavor;
			    ((short*)(sh->wf+1))[3]=coded_frame_size;
			    ((short*)(sh->wf+1))[4]=codecdata_length;
			    bp=demuxer->stream->read(codecdata_length); memcpy(((char*)(sh->wf+1))+10,bp.data(),bp.size()); // extras
			    break;
			case MKTAG('r', 'a', 'a', 'c'):
			case MKTAG('r', 'a', 'c', 'p'):
			    /* This is just AAC. The two or five bytes of */
			    /* config data needed for libfaad are stored */
			    /* after the audio headers. */
			    demuxer->stream->skip(3);  // Skip 3 unknown bytes
			    if (version==5)
				demuxer->stream->skip(1);  // Skip 1 additional unknown byte
			    codecdata_length=demuxer->stream->read(type_dword);
			    if (codecdata_length>=1) {
				sh->codecdata_len = codecdata_length - 1;
				sh->codecdata = new unsigned char [sh->codecdata_len];
				demuxer->stream->skip( 1);
				bp=demuxer->stream->read(sh->codecdata_len); memcpy(sh->codecdata,bp.data(),bp.size());
			    }
			    sh->wtag = mmioFOURCC('M', 'P', '4', 'A');
			    break;
			default:
			    MSG_V("Audio: Unknown (%s)\n", buf);
		    }

		    sh->wf->wFormatTag = sh->wtag;

		    if (mp_conf.verbose > 0)
		    print_wave_header(sh->wf,sizeof(WAVEFORMATEX));

		    /* Select audio stream with highest bitrate if multirate file*/
		    if (priv->is_multirate && ((demuxer->audio->id == -1) ||
					       ((demuxer->audio->id >= 0) && priv->a_bitrate && (bitrate > priv->a_bitrate)))) {
			    demuxer->audio->id = stream_id;
			    priv->a_bitrate = bitrate;
			    MSG_DBG2("Multirate autoselected audio id %d with bitrate %d\n", stream_id, bitrate);
		    }

		    if(demuxer->audio->id==stream_id){
			demuxer->audio->id=stream_id;
			sh->ds=demuxer->audio;
			demuxer->audio->sh=sh;
		    }

		    ++a_streams;

#ifdef stream_skip
#undef stream_skip
#endif
		}
	  } else if (strstr(tmps,"X-MP3-draft-00")) {
		    sh_audio_t *sh = demuxer->new_sh_audio(stream_id);

		    /* Emulate WAVEFORMATEX struct: */
		    sh->wf = new(zeromem) WAVEFORMATEX;
		    sh->wf->nChannels = 0;//sh->channels;
		    sh->wf->wBitsPerSample = 16;
		    sh->wf->nSamplesPerSec = 0;//sh->samplerate;
		    sh->wf->nAvgBytesPerSec = 0;//bitrate;
		    sh->wf->nBlockAlign = 0;//frame_size;
		    sh->wf->cbSize = 0;
		    sh->wf->wFormatTag = sh->wtag = mmioFOURCC('a','d','u',0x55);

		    if(demuxer->audio->id==stream_id){
			    sh->ds=demuxer->audio;
			    demuxer->audio->sh=sh;
		    }

		    ++a_streams;
	  } else if (strstr(tmps,"x-ralf-mpeg4")) {
		 MSG_ERR("Real lossless audio not supported yet\n");
	  } else {
		 MSG_V("Unknown audio stream format\n");
		}
	} else if (!strncmp(tmps,"video/",6)) {
	  if (strstr(tmps,"x-pn-realvideo") || strstr(tmps,"x-pn-multirate-realvideo")) {
		tmp = demuxer->stream->read(type_dword);
		len=codec_data_size;
		while(--len>=8){
			if(tmp==MKTAG('O', 'D', 'I', 'V')) break;  // video
			tmp=(tmp<<8)|demuxer->stream->read(type_byte);
		}
		if(tmp != MKTAG('O', 'D', 'I', 'V'))
		{
		    MSG_V("Video: can't find VIDO in codec data\n");
		} else {
		    /* video header */
		    sh_video_t *sh = demuxer->new_sh_video(stream_id);

		    sh->fourcc = demuxer->stream->read_le(type_dword); /* fourcc */
		    MSG_V("video fourcc: %.4s (%x)\n", (char *)&sh->fourcc, sh->fourcc);

		    /* emulate BITMAPINFOHEADER */
		    sh->bih = (BITMAPINFOHEADER*)mp_mallocz(sizeof(BITMAPINFOHEADER)+16);
		    sh->bih->biSize = 48;
		    sh->src_w = sh->bih->biWidth = demuxer->stream->read(type_word);
		    sh->src_h = sh->bih->biHeight = demuxer->stream->read(type_word);
		    sh->bih->biPlanes = 1;
		    sh->bih->biBitCount = 24;
		    sh->bih->biCompression = sh->fourcc;
		    sh->bih->biSizeImage= sh->bih->biWidth*sh->bih->biHeight*3;

		    sh->fps = (float) demuxer->stream->read(type_word);
		    if (sh->fps<=0) sh->fps=24; // we probably won't even care about fps

		    demuxer->stream->skip( 4);
//		    if(sh->wtag==0x30335652 || sh->wtag==0x30325652 )
		    if(1)
		    {
			int tmp=demuxer->stream->read(type_word);
			if(tmp>0){
			    sh->fps=tmp;
			}
		    } else {
			int fps=demuxer->stream->read(type_word);
			MSG_WARN("realvid: ignoring FPS = %d\n",fps);
		    }
		    demuxer->stream->skip( 2);

		    // read codec sub-wtag (to make difference between low and high rate codec)
		    ((unsigned int*)(sh->bih+1))[0]=demuxer->stream->read(type_dword);

		    /* h263 hack */
		    tmp = demuxer->stream->read(type_dword);
		    ((unsigned int*)(sh->bih+1))[1]=tmp;
		    MSG_V("H.263 ID: %x\n", tmp);
		    switch (tmp)
		    {
			case 0x10000000:
			    /* sub id: 0 */
			    /* codec id: rv10 */
			    break;
			case 0x10003000:
			case 0x10003001:
			    /* sub id: 3 */
			    /* codec id: rv10 */
			    sh->bih->biCompression = sh->fourcc = mmioFOURCC('R', 'V', '1', '3');
			    break;
			case 0x20001000:
			case 0x20100001:
			case 0x20200002:
			    /* codec id: rv20 */
			    break;
			case 0x30202002:
			    /* codec id: rv30 */
			    break;
			case 0x40000000:
			    /* codec id: rv40 */
			    break;
			default:
			    /* codec id: none */
			    MSG_V("unknown id: %x\n", tmp);
		    }

		    if((sh->fourcc<=0x30335652) && (tmp>=0x20200002)){
			    // read data for the cmsg24[] (see vd_realvid.c)
			    unsigned int cnt = codec_data_size - (demuxer->stream->tell() - codec_pos);
			    if (cnt < 2) {
				MSG_ERR("realvid: cmsg24 data too short (size %u)\n", cnt);
			    } else  {
				unsigned ii;
				if (cnt > 6) {
				    MSG_WARN("realvid: cmsg24 data too big, please report (size %u)\n", cnt);
				    cnt = 6;
				}
				for (ii = 0; ii < cnt; ii++)
				    ((unsigned char*)(sh->bih+1))[8+ii]=(unsigned short)demuxer->stream->read(type_byte);
			    }
		    }

		    /* Select video stream with highest bitrate if multirate file*/
		    if (priv->is_multirate && ((demuxer->video->id == -1) ||
					       ((demuxer->video->id >= 0) && priv->v_bitrate && (bitrate > priv->v_bitrate)))) {
			    demuxer->video->id = stream_id;
			    priv->v_bitrate = bitrate;
			    MSG_DBG2("Multirate autoselected video id %d with bitrate %d\n", stream_id, bitrate);
		    }

		    if(demuxer->video->id==stream_id){
			demuxer->video->id=stream_id;
			sh->ds=demuxer->video;
			demuxer->video->sh=sh;
		    }

		    ++v_streams;

		}
	  } else {
		 MSG_V("Unknown video stream format\n");
	  }
	} else if (strstr(tmps,"logical-")) {
		 if (strstr(tmps,"fileinfo")) {
		     MSG_V("Got a logical-fileinfo chunk\n");
		 } else if (strstr(tmps,"-audio") || strstr(tmps,"-video")) {
		    int i, stream_cnt;
		    int stream_list[MAX_STREAMS];

		    priv->is_multirate = 1;
		    demuxer->stream->skip( 4); // Length of codec data (repeated)
		    stream_cnt = demuxer->stream->read(type_dword); // Get number of audio or video streams
		    if (stream_cnt >= MAX_STREAMS) {
			MSG_ERR("Too many streams in %s. Big troubles ahead.\n",tmps);
			goto skip_this_chunk;
		    }
		    for (i = 0; i < stream_cnt; i++)
			stream_list[i] = demuxer->stream->read(type_word);
		    for (i = 0; i < stream_cnt; i++)
			if (stream_list[i] >= MAX_STREAMS) {
			    MSG_ERR("Stream id out of range: %d. Ignored.\n", stream_list[i]);
			    demuxer->stream->skip( 4); // Skip DATA offset for broken stream
			} else {
			    priv->str_data_offset[stream_list[i]] = demuxer->stream->read(type_dword);
			    MSG_V("Stream %d with DATA offset 0x%08x\n", stream_list[i], priv->str_data_offset[stream_list[i]]);
			}
		    // Skip the rest of this chunk
		 } else
		     MSG_V("Unknown logical stream\n");
		}
		else {
		    MSG_ERR("Not audio/video stream or unsupported!\n");
		}
//		break;
//	    default:
skip_this_chunk:
		/* skip codec info */
		tmp = demuxer->stream->tell() - codec_pos;
		MSG_V("### skipping %d bytes of codec info\n", codec_data_size - tmp);
		demuxer->stream->skip( codec_data_size - tmp);
		break;
//	    }
	    }
	    case MKTAG('D', 'A', 'T', 'A'):
		goto header_end;
	    case MKTAG('I', 'N', 'D', 'X'):
	    default:
		MSG_V("Unknown chunk: %x\n", chunk_id);
		demuxer->stream->skip( chunk_size - 10);
		break;
	}
    }

header_end:
    if(priv->is_multirate) {
	MSG_V("Selected video id %d audio id %d\n", demuxer->video->id, demuxer->audio->id);
	/* Perform some sanity checks to avoid checking streams id all over the code*/
	if (demuxer->audio->id >= MAX_STREAMS) {
	    MSG_ERR("Invalid audio stream %d. No sound will be played.\n", demuxer->audio->id);
	    demuxer->audio->id = -2;
	} else if ((demuxer->audio->id >= 0) && (priv->str_data_offset[demuxer->audio->id] == 0)) {
	    MSG_ERR("Audio stream %d not found. No sound will be played.\n", demuxer->audio->id);
	    demuxer->audio->id = -2;
	}
	if (demuxer->video->id >= MAX_STREAMS) {
	    MSG_ERR("Invalid video stream %d. No video will be played.\n", demuxer->video->id);
	    demuxer->video->id = -2;
	} else if ((demuxer->video->id >= 0) && (priv->str_data_offset[demuxer->video->id] == 0)) {
	    MSG_ERR("Video stream %d not found. No video will be played.\n", demuxer->video->id);
	    demuxer->video->id = -2;
	}
    }

    if(priv->is_multirate && ((demuxer->video->id >= 0) || (demuxer->audio->id  >=0))) {
	/* If audio or video only, seek to right place and behave like standard file */
	if (demuxer->video->id < 0) {
	    // Stream is audio only, or -novideo
	    demuxer->stream->seek( priv->data_chunk_offset = priv->str_data_offset[demuxer->audio->id]+10);
	    priv->is_multirate = 0;
	}
	if (demuxer->audio->id < 0) {
	    // Stream is video only, or -nosound
	    demuxer->stream->seek( priv->data_chunk_offset = priv->str_data_offset[demuxer->video->id]+10);
	    priv->is_multirate = 0;
	}
    }

  if(!priv->is_multirate) {
//    printf("i=%d num_of_headers=%d   \n",i,num_of_headers);
    priv->num_of_packets = demuxer->stream->read(type_dword);
//    demuxer->stream->skip( 4); /* number of packets */
    demuxer->stream->skip( 4); /* next data header */

    MSG_V("Packets in file: %d\n", priv->num_of_packets);

    if (priv->num_of_packets == 0)
	priv->num_of_packets = -10;
  } else {
	priv->audio_curpos = priv->str_data_offset[demuxer->audio->id] + 18;
	demuxer->stream->seek( priv->str_data_offset[demuxer->audio->id]+10);
	priv->a_num_of_packets = demuxer->stream->read(type_dword);
	priv->video_curpos = priv->str_data_offset[demuxer->video->id] + 18;
	demuxer->stream->seek( priv->str_data_offset[demuxer->video->id]+10);
	priv->v_num_of_packets = demuxer->stream->read(type_dword);
	priv->stream_switch = 1;
	/* Index required for multirate playback, force building if it's not there */
	/* but respect user request to force index regeneration */
	if (index_mode == -1)
	    index_mode = 1;
    }


    priv->audio_need_keyframe = 0;
    priv->video_after_seek = 0;

    switch (index_mode){
	case -1: // untouched
	    if (priv->index_chunk_offset && (priv->index_chunk_offset < demuxer->movi_end))
	    {
		parse_index_chunk(demuxer);
		demuxer->flags|=Demuxer::Seekable;
	    }
	    break;
	case 1: // use (generate index)
	    if (priv->index_chunk_offset && (priv->index_chunk_offset < demuxer->movi_end))
	    {
		parse_index_chunk(demuxer);
		demuxer->flags|=Demuxer::Seekable;
	    } else {
		generate_index(demuxer);
		demuxer->flags|=Demuxer::Seekable;
	    }
	    break;
	case 2: // force generating index
	    generate_index(demuxer);
	    demuxer->flags|=Demuxer::Seekable;
	    break;
	default: // do nothing
	    break;
    }

    if(priv->is_multirate)
	demuxer->flags&=~Demuxer::Seekable; // No seeking yet for multirate streams

    // detect streams:
    if(demuxer->video->id==-1 && v_streams>0){
	// find the valid video stream:
	if(!demuxer->video->fill_buffer()){
	  MSG_INFO("RM: " MSGTR_MissingVideoStream);
	}
    }
    if(demuxer->audio->id==-1 && a_streams>0){
	// find the valid audio stream:
	if(!demuxer->audio->fill_buffer()){
	  MSG_INFO("RM: " MSGTR_MissingAudioStream);
	}
    }

    if(demuxer->video->sh){
	sh_video_t *sh=reinterpret_cast<sh_video_t*>(demuxer->video->sh);
	MSG_V("VIDEO:  %.4s [%08X,%08X]  %dx%d  (aspect %4.2f)  %4.2f fps\n",
	    &sh->fourcc,((unsigned int*)(sh->bih+1))[1],((unsigned int*)(sh->bih+1))[0],
	    sh->src_w,sh->src_h,sh->aspect,sh->fps);
    }
    check_pin("demuxer",demuxer->pin,DEMUX_PIN);
    return priv;
}

static void real_close(Demuxer *demuxer)
{
    real_priv_t* priv = static_cast<real_priv_t*>(demuxer->priv);

    if (priv) delete priv;
    return;
}

/* please upload RV10 samples WITH INDEX CHUNK */
static void real_seek(Demuxer *demuxer,const seek_args_t* seeka)
{
    real_priv_t *priv = static_cast<real_priv_t*>(demuxer->priv);
    Demuxer_Stream *d_audio = demuxer->audio;
    Demuxer_Stream *d_video = demuxer->video;
    sh_audio_t *sh_audio = reinterpret_cast<sh_audio_t*>(d_audio->sh);
    sh_video_t *sh_video = reinterpret_cast<sh_video_t*>(d_video->sh);
    int vid = d_video->id, aid = d_audio->id;
    int next_offset = 0;
    int cur_timestamp = 0;
    int streams = 0;
    int retried = 0;


    if (sh_video && (unsigned)vid < MAX_STREAMS && priv->index_table_size[vid])
	streams |= 1;
    if (sh_audio && (unsigned)aid < MAX_STREAMS && priv->index_table_size[aid])
	streams |= 2;

//    printf("streams: %d\n", streams);

    if (!streams)
	return;

    if (seeka->flags & DEMUX_SEEK_SET)
	/* seek SOF */
	priv->current_apacket = priv->current_vpacket = 0;

    if ((streams & 1) && priv->current_vpacket >= priv->index_table_size[vid])
	priv->current_vpacket = priv->index_table_size[vid] - 1;
    if ((streams & 2) && priv->current_apacket >= priv->index_table_size[aid])
	priv->current_apacket = priv->index_table_size[aid] - 1;

//    if (index_mode == 1 || index_mode == 2) {
	if (streams & 1) {// use the video index if we have one
	    cur_timestamp = priv->index_table[vid][priv->current_vpacket].timestamp;
	    if (seeka->secs > 0)
		while ((priv->index_table[vid][priv->current_vpacket].timestamp - cur_timestamp) < seeka->secs * 1000){
			priv->current_vpacket += 1;
			if (priv->current_vpacket >= priv->index_table_size[vid]) {
				priv->current_vpacket = priv->index_table_size[vid] - 1;
				if (!retried) {
					demuxer->stream->seek( priv->index_table[vid][priv->current_vpacket].offset);
					add_index_segment(demuxer, vid, cur_timestamp + seeka->secs * 1000);
					retried = 1;
				}
				else
					break;
			}
		}
	    else if (seeka->secs < 0)
		while ((cur_timestamp - priv->index_table[vid][priv->current_vpacket].timestamp) < - seeka->secs * 1000){
			priv->current_vpacket -= 1;
			if (priv->current_vpacket < 0) {
				priv->current_vpacket = 0;
				break;
			}
		}
	    next_offset = priv->index_table[vid][priv->current_vpacket].offset;
	    priv->audio_need_keyframe = 1;
	    priv->video_after_seek = 1;
	}
	else if (streams & 2) {
	    cur_timestamp = priv->index_table[aid][priv->current_apacket].timestamp;
	    if (seeka->secs > 0)
		while ((priv->index_table[aid][priv->current_apacket].timestamp - cur_timestamp) < seeka->secs * 1000){
			priv->current_apacket += 1;
			if (priv->current_apacket >= priv->index_table_size[aid]) {
				priv->current_apacket = priv->index_table_size[aid] - 1;
				break;
			}
		}
	    else if (seeka->secs < 0)
		while ((cur_timestamp - priv->index_table[aid][priv->current_apacket].timestamp) < - seeka->secs * 1000){
			priv->current_apacket -= 1;
			if (priv->current_apacket < 0) {
				priv->current_apacket = 0;
				break;
			}
		}
	    next_offset = priv->index_table[aid][priv->current_apacket].offset;
	}
//    }
//    printf("seek: pos: %d, current packets: a: %d, v: %d\n",
//	next_offset, priv->current_apacket, priv->current_vpacket);
    if (next_offset)
	demuxer->stream->seek( next_offset);

    real_demux(demuxer,NULL);
    return;
}

static MPXP_Rc real_control(const Demuxer *demuxer,int cmd,any_t*args)
{
    UNUSED(demuxer);
    UNUSED(cmd);
    UNUSED(args);
    return MPXP_Unknown;
}

extern const demuxer_driver_t demux_real =
{
    "real",
    "Real media parser",
    ".rm",
    NULL,
    real_probe,
    real_open,
    real_demux,
    real_seek,
    real_close,
    real_control
};
