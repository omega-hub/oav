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

    void loadNextFrame();

private:
    bool myPlaying;

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
    bool myTexCreated;
    float myQuadX, myQuadY;
    float myFrameDuration;
    float myLastFrameTime;
};

///////////////////////////////////////////////////////////////////////////////
bool VideoStream::open(const String& filename)
{
    av_register_all();

    String filepath;
    if(!DataManager::findFile(filename, filepath))
    {
        ofwarn("[VideoStream::open] could not find file %1%", %filename);
        return false;
    }

    // Open video file
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

    // Find the decoder for the video stream
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
    oassert(myFrame == NULL);

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
    myPlaying = true;
}

///////////////////////////////////////////////////////////////////////////////
void VideoStream::loadNextFrame()
{
    int frameFinished = 0;
    AVPacket packet;

    while(frameFinished == 0)
    {
        if(av_read_frame(myFormatCtx, &packet) < 0)
        {
            myPlaying = false;
            frameFinished = 1;
        }
        else
        {
            // is the packet from the video stream?
            if(packet.stream_index == myVideoStream) 
            {
                // decode the video frame
                avcodec_decode_video2(myCodecCtx, myFrame, &frameFinished, &packet);

                // did we get a complete frame?
                if(frameFinished)
                {
                    // Convert the image from its native format to GL-friendly RGB values
                    sws_scale(myImgConvertCtx, 
                        myFrame->data, myFrame->linesize, 0, myCodecCtx->height, myFrameRGB->data, myFrameRGB->linesize);

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
        PYAPI_METHOD(VideoStream, loadNextFrame)
        PYAPI_REF_GETTER(VideoStream, getPixels)
        ;
}
#endif