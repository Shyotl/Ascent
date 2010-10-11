/** 
 * @file llimagej2coj.cpp
 * @brief This is an implementation of JPEG2000 encode/decode using OpenJPEG.
 *
 * $LicenseInfo:firstyear=2006&license=viewergpl$
 * 
 * Copyright (c) 2006-2009, Linden Research, Inc.
 * 
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlifegrid.net/programs/open_source/licensing/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution, or
 * online at
 * http://secondlifegrid.net/programs/open_source/licensing/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llimagej2coj.h"

#ifdef USE_OPENJPEG2 //If not using lls openjpeg dll+lib, statically include our entire custom lib
# define OPJ_STATIC
# define USE_OPJ_DEPRECATED
# include "../../openjpeg_v2_alpha_0/libopenjpeg/openjpeg.h"
#else
// this is defined so that we get static linking.
# include "openjpeg.h"
#endif

#include "lltimer.h"
#include "llmemory.h"

const char* fallbackEngineInfoLLImageJ2CImpl()
{
	static std::string version_string =
		std::string("OpenJPEG: " OPENJPEG_VERSION ", Runtime: ")
		+ opj_version();
	return version_string.c_str();
}

LLImageJ2CImpl* fallbackCreateLLImageJ2CImpl()
{
	return new LLImageJ2COJ();
}

void fallbackDestroyLLImageJ2CImpl(LLImageJ2CImpl* impl)
{
	delete impl;
	impl = NULL;
}

// Return string from message, eliminating final \n if present
static std::string chomp(const char* msg)
{
	// stomp trailing \n
	std::string message = msg;
	if (!message.empty())
	{
		size_t last = message.size() - 1;
		if (message[last] == '\n')
		{
			message.resize( last );
		}
	}
	return message;
}

/**
sample error callback expecting a LLFILE* client object
*/
void error_callback(const char* msg, void*)
{
	llwarns << "LLImageJ2COJ: " << chomp(msg) << llendl;
}
/**
sample warning callback expecting a LLFILE* client object
*/
void warning_callback(const char* msg, void*)
{
	llwarns << "LLImageJ2COJ: " << chomp(msg) << llendl;
}
/**
sample debug callback expecting no client object
*/
void info_callback(const char* msg, void*)
{
	llinfos << "LLImageJ2COJ: " << chomp(msg) << llendl;
}


LLImageJ2COJ::LLImageJ2COJ() : LLImageJ2CImpl()
{
}


LLImageJ2COJ::~LLImageJ2COJ()
{
}


#ifdef USE_OPENJPEG2
struct CJ2CStream
{
private:
	opj_codec_t *mCodec;	/* handle to a compressor/decompressor */
	opj_stream_t *mIO;		/* Input/output buffer stream */
	LLImageJ2C &mImage;
	U8 *mBuf;				/* mImage.getData() isn't inline. Sadface. */
	U32 mBufPos;			/* position in the stream */
	union param_t
	{
		opj_cparameters_t *comp;
		opj_dparameters_t *dec;
	}mParams;
	bool mbDecode;

	void init()
	{
		if(	(mCodec = mbDecode ? opj_create_decompress(CODEC_J2K) : opj_create_compress(CODEC_J2K))==NULL ||
			(mIO = opj_stream_create(J2K_STREAM_CHUNK_SIZE,mbDecode))==NULL )
			return;
		/* catch events using our callbacks and give a local context */
		opj_set_info_handler(getCodec(),info_callback,NULL);
		opj_set_warning_handler(getCodec(),warning_callback,NULL);
		opj_set_error_handler(getCodec(),error_callback,NULL);

		//Stream handlers
		opj_stream_set_user_data(getStream(), this);
		opj_stream_set_read_function(getStream(), &Readfn);
		opj_stream_set_write_function(getStream(), &Writefn);
		opj_stream_set_skip_function(getStream(), &Skipfn);
		opj_stream_set_seek_function(getStream(), &Seekfn);
	}
public:
	CJ2CStream(LLImageJ2C &image, bool bDecode) :
		mImage(image), mbDecode(bDecode), mBuf(image.getData()),
		mBufPos(0),	mCodec(NULL), mIO(NULL)
	{
		mParams.comp=NULL;
		init();
	}
	//params are passed byref to make sure NULL isn't ambiguously given to the following two ctors
	CJ2CStream(LLImageJ2C &image, opj_cparameters_t &params) :
		mImage(image),mbDecode(false), mBuf(image.getData()),
		mBufPos(0),	mCodec(NULL), mIO(NULL)
	{
		mParams.comp=&params;
		init();
	}
	CJ2CStream(LLImageJ2C &image, opj_dparameters_t &params) :
		mImage(image), mbDecode(true),	mBuf(image.getData()),
		mBufPos(0),	mCodec(NULL), mIO(NULL)
	{
		mParams.dec=&params;
		init();
	}
	~CJ2CStream()
	{
		if(getCodec())
			opj_destroy_codec(getCodec());
		if(getStream())
			opj_stream_destroy(getStream());
		 if(!mbDecode)
		 {
			 //if(mParams.comp && mParams.comp->cp_comment)
			//	free(mParams.comp->cp_comment);
			 if(mParams.comp && mParams.comp->cp_matrice)
				free(mParams.comp->cp_matrice);
		 }
	}
	opj_codec_t *getCodec() const
	{
		return (opj_codec_t*)mCodec;
	}
	opj_stream_t *getStream() const
	{
		return (opj_stream_t*)mIO;
	}
	bool isValid() const
	{
		return getCodec() && getStream();
	}
	opj_image_t*  Decode(bool bHeaderOnly=false)
	{
		if(!mbDecode || !isValid())
		{
			LL_INFOS("Openjpeg") << "Invalid or not decode codec" << llendl;
			return NULL;
		}
		/* setup the decoder/encoder parameters using user parameters */
		else if(mParams.dec && !opj_setup_decoder(getCodec(), mParams.dec))
		{
			LL_INFOS("Openjpeg") << "opj_setup_decoder failed" << llendl;
			return NULL;
		}

		opj_image *image = NULL;
		OPJ_INT32 tile_x0;
		OPJ_INT32 tile_y0;
		OPJ_UINT32 tile_width;
		OPJ_UINT32 tile_height;
		OPJ_UINT32 nb_tiles_x;
		OPJ_UINT32 nb_tiles_y;

		if(!opj_read_header( getCodec(),&image,&tile_x0,&tile_y0,&tile_width,&tile_height,&nb_tiles_x,&nb_tiles_y, getStream()))
		{
			LL_INFOS("Openjpeg") << "ERROR -> decodeImpl: failed to decode image header!" << LL_ENDL;
			if (image)
				opj_image_destroy(image);
			return NULL;
		}
		LL_INFOS("Openjpeg") << "Decoded header : x0="<<tile_x0<<" y0="<<tile_y0<<" width="<<tile_width<<" height="<<tile_height<<" tilesx="<<nb_tiles_x<<" tilesy"<<nb_tiles_y<<llendl;
		if(bHeaderOnly)
			return image;
		image = opj_decode(getCodec(), getStream());
		opj_end_decompress(getCodec(), getStream());
		mImage.mDecoding = FALSE;
		if(!image)
			LL_INFOS("Openjpeg") << "ERROR -> decodeImpl: failed to decode image!" << LL_ENDL;
		return image;
	}
	bool Encode(opj_image_t *image)
	{
		if(mbDecode || !isValid())
			return false;
		/* setup the decoder/encoder parameters using user parameters */
		else if(mParams.comp && !opj_setup_encoder(getCodec(), mParams.comp,image))
			return false;
		return	opj_start_compress(getCodec(), image, getStream()) && 
						opj_encode(getCodec(),getStream()) &&
						opj_end_compress(getCodec(), getCodec());
	}
	//Handlers:
	static OPJ_UINT32 Readfn (void * p_buffer, OPJ_UINT32 p_nb_bytes, void * p_user_data)
	{
		return ((CJ2CStream*)p_user_data)->Read(p_buffer,p_nb_bytes);
	}
	OPJ_UINT32 Read(void * p_buffer, OPJ_UINT32 p_nb_bytes)
	{
		
		if(!mImage.getDataSize() || mBufPos >= (U32)mImage.getDataSize())return -1;
		const U32 bytes_remaining = mImage.getDataSize() - mBufPos;
		const U32 bytes_to_read = bytes_remaining > p_nb_bytes ? p_nb_bytes : bytes_remaining;
		memcpy(p_buffer,mBuf + mBufPos, bytes_to_read);
		llinfos<<"Buffpos at "<<mBufPos<<llendl;
		mBufPos += bytes_to_read;
		llinfos<<"Buffer size = "<<mImage.getDataSize()<<llendl;
		llinfos<<"Attempting read of "<<p_nb_bytes<<" bytes of remaining "<<bytes_remaining<<" bytes"<<llendl;
		llinfos<<"Read "<<bytes_to_read<<" bytes"<<llendl;
		llinfos<<"Remaining bytes "<<(mImage.getDataSize() - mBufPos)<<llendl;
		return bytes_to_read;
	}
	static OPJ_UINT32 Writefn (void * p_buffer, OPJ_UINT32 p_nb_bytes, void * p_user_data)
	{
		return ((CJ2CStream*)p_user_data)->Write(p_buffer,p_nb_bytes);
	}
	OPJ_UINT32 Write(void * p_buffer, OPJ_UINT32 p_nb_bytes)
	{
		llinfos<<"Writing "<<p_nb_bytes<<" bytes"<<llendl;
		const U32 buff_size = mBufPos + p_nb_bytes;
		if(!mBuf)
			mBuf = mImage.allocateData(buff_size);
		else if((U32)mImage.getDataSize() < buff_size)
			mBuf = mImage.reallocateData(buff_size);
		memcpy(mBuf,p_buffer,buff_size-mBufPos);
		mBufPos = buff_size;
		return p_nb_bytes;
	}
	static OPJ_SIZE_T Skipfn(OPJ_SIZE_T p_nb_bytes, void * p_user_data)
	{
		return ((CJ2CStream*)p_user_data)->Skip(p_nb_bytes);
	}
	OPJ_SIZE_T Skip(OPJ_SIZE_T p_nb_bytes)
	{
		llinfos<<"Skipping "<<p_nb_bytes<<" bytes"<<llendl;
		mBufPos+=p_nb_bytes;
		if(mBufPos > (U32)mImage.getDataSize())
			mBufPos = mImage.getDataSize(); //one beyond end of buffer.
		else if(mBufPos < 0)
			mBufPos = 0;					//clamp to start of buffer
		return mBufPos;
	}
	static bool Seekfn(OPJ_SIZE_T p_nb_bytes, void * p_user_data)
	{
		return ((CJ2CStream*)p_user_data)->Seek(p_nb_bytes);
	}
	bool Seek(OPJ_SIZE_T p_nb_bytes)
	{
		llinfos<<"Seeking "<<p_nb_bytes<<" bytes"<<llendl;
		if(!mImage.getDataSize() || (p_nb_bytes >= (U32)mImage.getDataSize()))return false;
		mBufPos = p_nb_bytes;
		return true;
	}
};
#endif
BOOL LLImageJ2COJ::decodeImpl(LLImageJ2C &base, LLImageRaw &raw_image, F32 decode_time, S32 first_channel, S32 max_channel_count)
{
	//
	// FIXME: Get the comment field out of the texture
	//
	if (!base.getData()) return FALSE;
	if (!base.getDataSize()) return FALSE;
	if (!raw_image.getData()) return FALSE;
	if (!raw_image.getDataSize()) return FALSE;

	LLTimer decode_timer;

	opj_dparameters_t parameters;	/* decompression parameters */
	opj_image_t *image = NULL;		/* decoded image */

#ifndef USE_OPENJPEG2
	opj_event_mgr_t event_mgr;		/* event manager */
	opj_dinfo_t* dinfo = NULL;		/* handle to a decompressor */
	opj_cio_t *cio = NULL;			/* Input/output buffer stream */

	/* configure the event callbacks (not required) */
	memset(&event_mgr, 0, sizeof(opj_event_mgr_t));
	event_mgr.error_handler = error_callback;
	event_mgr.warning_handler = warning_callback;
	event_mgr.info_handler = info_callback;
#endif

	/* set decoding parameters to default values */
	opj_set_default_decoder_parameters(&parameters);

	parameters.cp_reduce = base.getRawDiscardLevel();

	/* decode the code-stream */
	/* ---------------------- */

	/* JPEG-2000 codestream */

	
#ifdef USE_OPENJPEG2
	/* create stream */
	CJ2CStream j2cstream(base,parameters);
	if (!j2cstream.isValid())
	{
		return FALSE;
	}

	/* decode the stream and fill the image structure */
	image = j2cstream.Decode();
#else
	/* get a decoder handle */
	if((dinfo = opj_create_decompress(CODEC_J2K)) == NULL) 
		return FALSE;

	/* catch events using our callbacks and give a local context */
	opj_set_event_mgr((opj_common_ptr)dinfo, &event_mgr, stderr);

	/* setup the decoder decoding parameters using user parameters */
	opj_setup_decoder(dinfo, &parameters);

	/* open a byte stream */
	cio = opj_cio_open((opj_common_ptr)dinfo, base.getData(), base.getDataSize());

	if (!cio || cio->bp == NULL)
	{
		opj_destroy_decompress(dinfo);
		return FALSE;
	}

	/* decode the stream and fill the image structure */
	image = opj_decode(dinfo, cio);

	/* close the byte stream */
	opj_cio_close(cio);

	/* free remaining structures */
	opj_destroy_decompress(dinfo);
#endif

	// The image decode failed if the return was NULL or the component
	// count was zero.  The latter is just a sanity check before we
	// dereference the array.
	if(!image)
	{
	    LL_DEBUGS("Openjpeg")  << "ERROR -> decodeImpl: failed to decode image - no image" << LL_ENDL;
	    return TRUE; // done
  	}

  	const S32 img_components = image->numcomps;

  	if( !img_components ) // < 1 ||img_components > 4 )
  	{
    	LL_DEBUGS("Openjpeg") << "ERROR -> decodeImpl: failed to decode image - wrong number of components: " << img_components << LL_ENDL;
		opj_image_destroy(image);
		return TRUE; // done
	}
	else if( !image->x1 || !image->y1 )
  	{
    	LL_DEBUGS("Openjpeg") << "ERROR -> decodeImpl: failed to decode image - invalid dimensions: " << image->x1 <<"x"<< image->y1 << LL_ENDL;
		opj_image_destroy(image);
		return TRUE; // done
	}

	// sometimes we get bad data out of the cache - check to see if the decode succeeded
	for (S32 i = 0; i < img_components; i++)
	{
		LL_DEBUGS("Openjpeg") <<"Component "<<i<<" characteristics: "<<image->comps[i].w <<"x"<<image->comps[i].h<<"x"<<image->comps[i].prec<< (image->comps[i].sgnd==1 ? " signed": " unsigned")<<llendl;
		if (image->comps[i].factor != base.getRawDiscardLevel())
		{
			// if we didn't get the discard level we're expecting, fail
			//anyway somthing odd with the image, better check than crash
			LL_DEBUGS("Openjpeg") << "image->comps["<<i<<"].factor != "<<base.getRawDiscardLevel()<<llendl;
			opj_image_destroy(image);
			return TRUE;
		}
	}
	
	if(img_components <= first_channel)
	{
		LL_DEBUGS("Openjpeg") << "trying to decode more channels than are present in image: numcomps: " << img_components << " first_channel: " << first_channel << LL_ENDL;
		opj_image_destroy(image);
		return TRUE;
	}

	// Copy image data into our raw image format (instead of the separate channel format

	S32 channels = img_components - first_channel;
	if( channels > max_channel_count )
		channels = max_channel_count;

	// Component buffers are allocated in an image width by height buffer.
	// The image placed in that buffer is ceil(width/2^factor) by
	// ceil(height/2^factor) and if the factor isn't zero it will be at the
	// top left of the buffer with black filled in the rest of the pixels.
	// It is integer math so the formula is written in ceildivpo2.
	// (Assuming all the components have the same width, height and
	// factor.)
	S32 comp_width = image->comps[0].w;
	S32 f=image->comps[0].factor;
	S32 width = ceildivpow2(image->x1 - image->x0, f);
	S32 height = ceildivpow2(image->y1 - image->y0, f);
	raw_image.resize(width, height, channels);
	U8 *rawp = raw_image.getData();

	// first_channel is what channel to start copying from
	// dest is what channel to copy to.  first_channel comes from the
	// argument, dest always starts writing at channel zero.
	for (S32 comp = first_channel, dest=0; comp < first_channel + channels;
		comp++, dest++)
	{
		if (image->comps[comp].data)
		{
			S32 offset = dest;
			for (S32 y = (height - 1); y >= 0; y--)
			{
				for (S32 x = 0; x < width; x++)
				{
					rawp[offset] = image->comps[comp].data[y*comp_width + x];
					offset += channels;
				}
			}
		}
		else // Some rare OpenJPEG versions have this bug.
		{
			llwarns << "ERROR -> decodeImpl: failed to decode image! (NULL comp data - OpenJPEG bug)" << llendl;
			opj_image_destroy(image);
			return TRUE; // done
		}
	}

	/* free image data structure */
	opj_image_destroy(image);

	return TRUE; // done
}


BOOL LLImageJ2COJ::encodeImpl(LLImageJ2C &base, const LLImageRaw &raw_image, const char* comment_text, F32 encode_time, BOOL reversible)
{
	const S32 MAX_COMPS = 5;
	opj_cparameters_t parameters;	/* compression parameters */
#ifndef USE_OPENJPEG2
	opj_event_mgr_t event_mgr;		/* event manager */

	/* 
	configure the event callbacks (not required)
	setting of each callback is optional 
	*/
	memset(&event_mgr, 0, sizeof(opj_event_mgr_t));
	event_mgr.error_handler = error_callback;
	event_mgr.warning_handler = warning_callback;
	event_mgr.info_handler = info_callback;
#endif

	/* set encoding parameters to default values */
	opj_set_default_encoder_parameters(&parameters);
	parameters.cod_format = 0;
	parameters.cp_disto_alloc = 1;

	if (reversible)
	{
		parameters.tcp_numlayers = 1;
		parameters.tcp_rates[0] = 0.0f;
	}
	else
	{
		//F32 compress_ratio = gSavedSettings.getF32("J2CEncodeQuality");
		F32 compress_ratio = 1.5f;
		parameters.tcp_numlayers = 5;
		parameters.tcp_rates[0] = 1920.0f*compress_ratio;
		parameters.tcp_rates[1] = 480.0f*compress_ratio;
		parameters.tcp_rates[2] = 120.0f*compress_ratio;
		parameters.tcp_rates[3] = 30.0f*compress_ratio;
		parameters.tcp_rates[4] = 10.0f*compress_ratio;
		parameters.irreversible = 1;
		if (raw_image.getComponents() >= 3)
		{
			parameters.tcp_mct = 1;
		}
	}

	if (!comment_text)
	{
		parameters.cp_comment = (char *) "";
	}
	else
	{
		// Awful hacky cast, too lazy to copy right now.
		parameters.cp_comment = (char *) comment_text;
	}

	//
	// Fill in the source image from our raw image
	//
	OPJ_COLOR_SPACE color_space = CLRSPC_SRGB;
	opj_image_cmptparm_t cmptparm[MAX_COMPS];
	S32 numcomps = llmin((S32)raw_image.getComponents(),(S32)MAX_COMPS); //Clamp avoid overrunning buffer -Shyotl
	S32 width = raw_image.getWidth();
	S32 height = raw_image.getHeight();

	memset(&cmptparm[0], 0, MAX_COMPS * sizeof(opj_image_cmptparm_t));
	for(S32 c = 0; c < numcomps; c++) {
		cmptparm[c].prec = 8;
		cmptparm[c].bpp = 8;
		cmptparm[c].sgnd = 0;
		cmptparm[c].dx = parameters.subsampling_dx;
		cmptparm[c].dy = parameters.subsampling_dy;
		cmptparm[c].w = width;
		cmptparm[c].h = height;
	}

	/* create the image */
	opj_image_t *image = opj_image_create(numcomps, &cmptparm[0], color_space);
	if(!image)
		return FALSE;

	image->x1 = width;
	image->y1 = height;

	S32 i = 0;
	const U8 *src_datap = raw_image.getData();
	for (S32 y = height - 1; y >= 0; y--)
	{
		for (S32 x = 0; x < width; x++)
		{
			const U8 *pixel = src_datap + (y*width + x) * numcomps;
			for (S32 c = 0; c < numcomps; c++)
			{
				image->comps[c].data[i] = *pixel;
				pixel++;
			}
			i++;
		}
	}



	/* encode the destination image */
	/* ---------------------------- */

#ifdef USE_OPENJPEG2
	/* create stream */
	CJ2CStream j2cstream(base,parameters);
	if (!j2cstream.isValid())
		return FALSE;
	const bool bSuccess = j2cstream.Encode(image);
	base.updateData(); // set width, height
	if(!bSuccess)
		llinfos << "Failed to encode image." << llendl;
	if(image)
		opj_image_destroy(image);
	return bSuccess;
#else
	/* get a J2K compressor handle */
	opj_cinfo_t* cinfo = opj_create_compress(CODEC_J2K);
	if(!cinfo)
		return FALSE;;

	/* catch events using our callbacks and give a local context */
	opj_set_event_mgr((opj_common_ptr)cinfo, &event_mgr, stderr);			

	/* setup the encoder parameters using the current image and using user parameters */
	opj_setup_encoder(cinfo, &parameters, image);

	/* open a byte stream for writing */
	/* allocate memory for all tiles */
	opj_cio_t *cio = opj_cio_open((opj_common_ptr)cinfo, NULL, 0);
	if(!cio)
	{
		opj_destroy_compress(cinfo);
		return FALSE;
	}

	/* encode the image */
	bool bSuccess = opj_encode(cinfo, cio, image, NULL);
	if (!bSuccess)
		llinfos << "Failed to encode image." << llendl;
	else
	{
		const U32 codestream_length = cio_tell(cio);

		base.copyData(cio->buffer, codestream_length);
		base.updateData(); // set width, height
	}

	/* close and free the byte stream */
	opj_cio_close(cio);

	/* free remaining compression structures */
	opj_destroy_compress(cinfo);

	/* free user parameters structure */
	if(parameters.cp_matrice) free(parameters.cp_matrice);

	/* free image data */
	opj_image_destroy(image);

	return TRUE;
#endif	
}

BOOL LLImageJ2COJ::getMetadata(LLImageJ2C &base)
{
	//
	// FIXME: We get metadata by decoding the ENTIRE image.
	//
	if (!base.getData()) return FALSE;
	if (!base.getDataSize()) return FALSE;

	// Update the raw discard level
	base.updateRawDiscardLevel();

	opj_image_t *image = NULL;
#ifdef USE_OPENJPEG2
	/* decode the code-stream */
	/* ---------------------- */

	/* JPEG-2000 codestream */
	CJ2CStream j2cstream(base,true);
	if(!j2cstream.isValid())
		return FALSE;

	image = j2cstream.Decode(true);
#else
	opj_dparameters_t parameters;	/* decompression parameters */
	opj_event_mgr_t event_mgr;		/* event manager */
	opj_dinfo_t* dinfo = NULL;		/* handle to a decompressor */
	opj_cio_t *cio = NULL;			/* Input/output buffer stream */

	/* configure the event callbacks (not required) */
	memset(&event_mgr, 0, sizeof(opj_event_mgr_t));
	event_mgr.error_handler = error_callback;
	event_mgr.warning_handler = warning_callback;
	event_mgr.info_handler = info_callback;

	/* set decoding parameters to default values */
	opj_set_default_decoder_parameters(&parameters);

	// Only decode what's required to get the size data.
	parameters.cp_limit_decoding=LIMIT_TO_MAIN_HEADER;

	//parameters.cp_reduce = mRawDiscardLevel;

	/* decode the code-stream */
	/* ---------------------- */

	/* JPEG-2000 codestream */

	/* get a decoder handle */
	dinfo = opj_create_decompress(CODEC_J2K);

	/* catch events using our callbacks and give a local context */
	opj_set_event_mgr((opj_common_ptr)dinfo, &event_mgr, stderr);			

	/* setup the decoder decoding parameters using user parameters */
	opj_setup_decoder(dinfo, &parameters);

	/* open a byte stream */
	cio = opj_cio_open((opj_common_ptr)dinfo, base.getData(), base.getDataSize());

	/* decode the stream and fill the image structure */
	if (!cio) return FALSE;
	if (cio->bp == NULL) return FALSE;
	if (!dinfo) return FALSE;
	image = opj_decode(dinfo, cio);

	/* close the byte stream */
	opj_cio_close(cio);

	/* free remaining structures */
	if(dinfo)
	{
		opj_destroy_decompress(dinfo);
	}
#endif

	if(!image)
	{
		llwarns << "ERROR -> getMetadata: failed to decode image!" << llendl;
		return FALSE;
	}

	// Copy image data into our raw image format (instead of the separate channel format
	S32 width = 0;
	S32 height = 0;

	S32 img_components = image->numcomps;
	width = image->x1 - image->x0;
	height = image->y1 - image->y0;
	base.setSize(width, height, img_components);

	/* free image data structure */
	opj_image_destroy(image);
	
	return TRUE;
}
