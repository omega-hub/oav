#include <omega.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
}

#if LIBAVCODEC_VERSION_MAJOR < 57
    #define AV_PIX_FMT_RGBA PIX_FMT_RGBA
    #define av_frame_alloc avcodec_alloc_frame
#endif

using namespace omega;

///////////////////////////////////////////////////////////////////////////////
class VideoStream : public EngineModule
{
public:
    VideoStream() : EngineModule("VideoStream"),
        myFormatCtx(NULL),
        myCodecCtx(NULL),
        myCodec(NULL),
        myFrame(NULL),
        myFrameRGB(NULL),
        myImgConvertCtx(NULL),
        myPlaying(false),
        myLastFrameTime(0)
    {
       ModuleServices::addModule(this);
    }

    bool open(const String& filename);

    virtual void update(const UpdateContext& context);

    PixelData* getPixels() { return myPixels; }

    void play();
    bool isPlaying() { return myPlaying; }
    bool isLooping() { return myLoop; }
    //Returns the time length of currently loaded video in seconds
    double getVideoLength() {};
    bool seekToTime( double time);
	bool seekToFrame(int seekFrame);

    void setPlaying(bool setPlay) { myPlaying = setPlay; }
    void setLooping(bool setLoop) { myLoop = setLoop; }
	int getDuration() { return myDuration; }

    void loadNextFrame();

private:
    bool myPlaying;
    bool myLoop;

    AVFormatContext* myFormatCtx;
    AVCodecContext* myCodecCtx;
    AVCodec* myCodec;
    AVFrame* myFrame;
    AVFrame* myFrameRGB;
    uint8_t* myBuffer;
 
    struct SwsContext *myImgConvertCtx;
 
    Ref<PixelData> myPixels;

    int myVideoStream;
    int myNumVideoStreams;
	double myCurrentTime;
    bool myTexCreated;
    float myQuadX, myQuadY;
    float myFrameDuration;
    float myLastFrameTime;

	double myDuration;
	double timeBase;
};

///////////////////////////////////////////////////////////////////////////////
bool VideoStream::open(const String& filename)
{
    av_register_all();
	myLoop = true;
    String filepath;
    if(!DataManager::findFile(filename, filepath))
    {
        ofwarn("[VideoStream::open] could not find file %1%", %filename);
        return false;
    }

    // Open video file

	//AVFormatContext* pFormatCtx = avformat_alloc_context();
	
    if(avformat_open_input(&myFormatCtx, filepath.c_str(), NULL, NULL) != 0)
    {
        ofwarn("[VideoStream::open] could not open file %1%", %filepath);
        return false;
    }

    // Retrieve stream information
    if(avformat_find_stream_info(myFormatCtx, NULL) < 0)
    {
        ofwarn("[VideoStream::open] could find stream information for %1%", %filepath);
        return false;
    }

    // Dump some info about the file onto stderr
    av_dump_format(myFormatCtx, 0, filepath.c_str(), 0);

    // Fine the first video stream
    myVideoStream = -1;
    for(int i = 0; i<myFormatCtx->nb_streams; i++)
        if(myFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            myVideoStream = i;

    if(myVideoStream == -1)
    {
        ofwarn("[VideoStream::open] could not find a video stream for %1%", %filepath);
        return false;
    }

    // Get a pointer to the codec context for the video stream
    myCodecCtx = myFormatCtx->streams[myVideoStream]->codec;
	//timeBase = (int64_t(myCodecCtx->time_base.num) * AV_TIME_BASE) / int64_t(myCodecCtx->time_base.den);
	timeBase = ((double)(myFormatCtx->streams[myVideoStream]->time_base.num)) / ((double)(myFormatCtx->streams[myVideoStream]->time_base.den));
    // Find the decoder for the video stream
	myDuration = myFormatCtx->streams[myVideoStream]->duration * timeBase;
    myCodec = avcodec_find_decoder(myCodecCtx->codec_id);
    if(myCodec == NULL)
    {
        ofwarn("[VideoStream::open] unsupported codec for %1%", %filepath);
        return false;
    }

    // Open Codec
    if(avcodec_open2(myCodecCtx, myCodec, NULL) < 0)
    {
        ofwarn("[VideoStream::open] could not open codec for %1%", %filepath);
        return false;
    }

    myFrameDuration = myFormatCtx->streams[myVideoStream]->r_frame_rate.den / 
        double(myFormatCtx->streams[myVideoStream]->r_frame_rate.num);

    // Allocate video frame
    if(myFrame) av_free(myFrame);
    myFrame = av_frame_alloc();
    oassert(myFrame != NULL);

    if(myFrameRGB) av_free(myFrameRGB);
    myFrameRGB = av_frame_alloc();
    oassert(myFrameRGB);

    int numBytes = avpicture_get_size(AV_PIX_FMT_RGBA, myCodecCtx->width, myCodecCtx->height);
    myBuffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    oassert(myBuffer); 

    avpicture_fill((AVPicture *)myFrameRGB, myBuffer, AV_PIX_FMT_RGBA, myCodecCtx->width, myCodecCtx->height);

    myImgConvertCtx = sws_getContext(
        myCodecCtx->width, myCodecCtx->height, myCodecCtx->pix_fmt, 
        myCodecCtx->width, myCodecCtx->height, 
        AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, NULL, NULL, NULL);

    myPixels = new PixelData(PixelData::FormatRgba, myCodecCtx->width, myCodecCtx->height);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
void VideoStream::play()
{
	setPlaying(true);
}
/*
bool VideoStream::seekToTime(double time) {
	int frameFinished = 0;
	AVPacket packet;
	if (!myFormatCtx)
		return false;
	int64_t seekTarget = (time * AV_TIME_BASE);
	av_seek_frame(myFormatCtx, -1, seekTarget, AVSEEK_FLAG_BACKWARD);
	//myCodecCtx->hurry_up = 1;
	do {
		av_read_frame(myFormatCtx, &packet);
		// should really be checking that this is a video packet
		int64_t myPts = av_rescale(packet.pts,
			AV_TIME_BASE * (int64_t)myFormatCtx->streams[myVideoStream]->time_base.num,
			myFormatCtx->streams[myVideoStream]->time_base.den);
		// Once we pass the target point, break from the loop
		if (myPts >= seekTarget)
			break;
		// decode the video frame
		avcodec_decode_video2(myCodecCtx, myFrame, &frameFinished, &packet);
		av_free_packet(&packet);
	} while (1);
	//myCodecCtx->hurry_up = 0;
}
*/
/*
bool VideoStream::seekToTime(double time) {
	if (!myFormatCtx)
		return false;
	int temp = (time / timeBase);
	int64_t seekTarget = (int64_t)(temp );
	if (av_seek_frame(myFormatCtx, -1, seekTarget, NULL) < 0)
		return false;
	return true;
}
*/

bool VideoStream::seekToTime(double time)
{
	int frameFinished = 0;
	AVPacket pkt;
	/*if (!isOk())
		return false;*/
	time = time / timeBase;
	//ofwarn("\t avformat_seek_file to %d\n",time);
	int flags = AVSEEK_FLAG_FRAME;
	if (time < myFrame->pkt_dts)
	{
		flags |= AVSEEK_FLAG_BACKWARD;
	}

	if(av_seek_frame(myFormatCtx,-1,time,flags))
	{
	//ofwarn("\nFailed to seek for time %d",time);
	return false;
	}

	avcodec_flush_buffers(myCodecCtx);
	//read frame without converting it
	frameFinished = 0;
	do
	if (av_read_frame(myFormatCtx, &pkt) == 0) {
		avcodec_decode_video2(myCodecCtx, myFrame, &frameFinished, &pkt);
		//decode_packet(&got_frame, 0, false);
		av_free_packet(&pkt);
	}
	else
	{
	//read_cache = true;
	pkt.data = NULL;
	pkt.size = 0;
	break;
	}
	while (!(frameFinished && myFrame->pkt_dts >= time));
	return true;
}


bool VideoStream::seekToFrame(int seekFrame){
	if (!myFormatCtx)
		return false;
	int64_t seekTarget = int64_t(seekFrame) * timeBase;
	if (av_seek_frame(myFormatCtx, -1, seekTarget, AVSEEK_FLAG_ANY) < 0)
		return false;
	return true;
}
///////////////////////////////////////////////////////////////////////////////
void VideoStream::loadNextFrame()
{
    int frameFinished = 0;

    AVPacket packet;
	//Comment
    while(frameFinished == 0)
    {
        if(av_read_frame(myFormatCtx, &packet) < 0)
        {
            myPlaying = false;
            frameFinished = 1;
			if (myLoop) { 
				//av_seek_frame(myFormatCtx, packet.stream_index, 0, AVSEEK_FLAG_ANY);
				myPlaying = true;
				seekToTime(2);
			}
        }
        else
        {
            // is the packet from the video stream?
			if (packet.stream_index == myVideoStream) 
				{
					// decode the video frame
					avcodec_decode_video2(myCodecCtx, myFrame, &frameFinished, &packet);

					// did we get a complete frame?
					if(frameFinished)
					{
						// Convert the image from its native format to GL-friendly RGB values
						sws_scale(myImgConvertCtx, 
							myFrame->data, myFrame->linesize, 0, myCodecCtx->height, myFrameRGB->data, myFrameRGB->linesize);
						//double mult2 = av_q2d(myCodecCtx->time_base) / 1000;
						//double mult = av_q2d(myCodecCtx->time_base) / 2000;
						//double mult2 = av_q2d()
						myCurrentTime = myFrame->best_effort_timestamp*timeBase;
						
						byte* out = myPixels->map();
						int p = myPixels->getPitch();
						int h = myCodecCtx->height;
						for(int y = 0; y < h; y++)
						{
							memcpy(out + (h - 1 - y) * p, myBuffer + y * p, p);
						}

						myPixels->unmap();
						myPixels->setDirty();
					}
				}
			}
			av_free_packet(&packet);
    }
}

///////////////////////////////////////////////////////////////////////////////
void VideoStream::update(const UpdateContext& context)
{
    if(myPlaying)
    {
        if(context.time - myLastFrameTime > myFrameDuration)
        {
            myLastFrameTime = context.time;
            loadNextFrame();
        }
        double incr, pos;
        /*
    if(global_video_state) {
      pos = get_master_clock(global_video_state);
      pos += incr;
      stream_seek(global_video_state, 
                      (int64_t)(pos * AV_TIME_BASE), incr);
					  
    }
    break;
      default:
    break;
      }
    */
    }

}

///////////////////////////////////////////////////////////////////////////////
// Python wrapper code.
#ifdef OMEGA_USE_PYTHON
	#include "omega/PythonInterpreterWrapper.h"
	BOOST_PYTHON_MODULE(oav)
	{
		PYAPI_REF_BASE_CLASS_WITH_CTOR(VideoStream)
			PYAPI_METHOD(VideoStream, open)
			PYAPI_METHOD(VideoStream, play)
			PYAPI_METHOD(VideoStream, isPlaying)
			PYAPI_METHOD(VideoStream, setPlaying)
			PYAPI_METHOD(VideoStream, setLooping)
			PYAPI_METHOD(VideoStream, seekToTime)
			PYAPI_METHOD(VideoStream, loadNextFrame)
			PYAPI_REF_GETTER(VideoStream, getPixels)
			;
	}
#endif
/*
Link Resources (put here by Larry):
http://dranger.com/ffmpeg/tutorial01.html
http://www.ffmpeg.org/doxygen/trunk/structAVFormatContext.html
http://stackoverflow.com/questions/6451814/how-to-use-libavcodec-ffmpeg-to-find-duration-of-video-file
http://www.ffmpeg.org/doxygen/trunk/group__libavc.html
http://stackoverflow.com/questions/13669346/libavcodec-get-video-duration-and-framerate
http://www.mjbshaw.com/2012/04/seeking-in-ffmpeg-know-your-timestamp.html

*/