//
// Created by hcDarren on 2019/6/22.
//
#include "DZAudio.h"


DZAudio::DZAudio(int audioStreamIndex, DZJNICall *pJniCall, AVCodecContext *pCodecContext,
                 AVFormatContext *pFormatContext, SwrContext *swrContext) {
    this->audioStreamIndex = audioStreamIndex;
    this->pJniCall = pJniCall;
    this->pCodecContext = pCodecContext;
    this->pFormatContext = pFormatContext;
    this->swrContext = swrContext;
    resampleOutBuffer = (uint8_t *) malloc(pCodecContext->frame_size * 2 * 2);
    pPacketQueue = new DZPacketQueue();
    pPlayerStatus = new DZPlayerStatus();
}

void *threadPlay(void *context) {
    DZAudio *pAudio = (DZAudio *) context;
    pAudio->initCrateOpenSLES();
    return 0;
}

void *threadReadPacket(void *context) {
    DZAudio *pAudio = (DZAudio *) context;
    while (pAudio->pPlayerStatus != NULL && !pAudio->pPlayerStatus->isExit) {
        AVPacket *pAvPacket = av_packet_alloc();
        if (av_read_frame(pAudio->pFormatContext, pAvPacket) >= 0) {
            if (pAvPacket->stream_index == pAudio->audioStreamIndex) {
                pAudio->pPacketQueue->push(pAvPacket);
            } else {
                // 1. 解引用数据 data ， 2. 销毁 pPacket 结构体内存  3. pPacket = NULL
                av_packet_free(&pAvPacket);
            }
        } else {
            // 1. 解引用数据 data ， 2. 销毁 pPacket 结构体内存  3. pPacket = NULL
            av_packet_free(&pAvPacket);
            // 睡眠一下，尽量不去消耗 cpu 的资源，也可以退出销毁这个线程
            // break;
        }
    }
}

void DZAudio::play() {
    // 一个线程去读取 Packet
    pthread_t readPacketThreadT;
    pthread_create(&readPacketThreadT, NULL, threadReadPacket, this);
    pthread_detach(readPacketThreadT);

    // 一个线程去解码播放
    pthread_t playThreadT;
    pthread_create(&playThreadT, NULL, threadPlay, this);
    pthread_detach(playThreadT);
}

int DZAudio::resampleAudio() {
    int dataSize = 0;
    AVPacket *pPacket = NULL;
    AVFrame *pFrame = av_frame_alloc();

    while (pPlayerStatus != NULL && !pPlayerStatus->isExit) {
        pPacket = pPacketQueue->pop();
        // Packet 包，压缩的数据，解码成 pcm 数据
        int codecSendPacketRes = avcodec_send_packet(pCodecContext, pPacket);
        if (codecSendPacketRes == 0) {
            int codecReceiveFrameRes = avcodec_receive_frame(pCodecContext, pFrame);
            if (codecReceiveFrameRes == 0) {
                // AVPacket -> AVFrame
                // 调用重采样的方法，返回值是返回重采样的个数，也就是 pFrame->nb_samples
                dataSize = swr_convert(swrContext, &resampleOutBuffer, pFrame->nb_samples,
                                       (const uint8_t **) pFrame->data, pFrame->nb_samples);
                dataSize = dataSize * 2 * 2;
                // write 写到缓冲区 pFrame.data -> javabyte
                // size 是多大，装 pcm 的数据
                // 1s 44100 点  2通道 ，2字节    44100*2*2
                // 1帧不是一秒，pFrame->nb_samples点
                break;
            }
        }
        // 解引用
        av_packet_unref(pPacket);
        av_frame_unref(pFrame);
    }
    // 1. 解引用数据 data ， 2. 销毁 pPacket 结构体内存  3. pPacket = NULL
    av_packet_free(&pPacket);
    av_frame_free(&pFrame);
    return dataSize;
}

void playerCallback(SLAndroidSimpleBufferQueueItf caller, void *pContext) {
    DZAudio *pFFmepg = (DZAudio *) pContext;
    int dataSize = pFFmepg->resampleAudio();
    // 这里为什么报错，留在后面再去解决
    (*caller)->Enqueue(caller, pFFmepg->resampleOutBuffer, dataSize);
}

void DZAudio::initCrateOpenSLES() {
    /*OpenSLES OpenGLES 都是自带的
    XXXES 与 XXX 之间可以说是基本没有区别，区别就是 XXXES 是 XXX 的精简
    而且他们都有一定规则，命名规则 slXXX() , glXXX3f*/
    // 3.1 创建引擎接口对象
    SLObjectItf engineObject = NULL;
    SLEngineItf engineEngine;
    slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
    // realize the engine
    (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    // get the engine interface, which is needed in order to create other objects
    (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);

    // 3.2 设置混音器
    static SLObjectItf outputMixObject = NULL;
    const SLInterfaceID ids[1] = {SL_IID_ENVIRONMENTALREVERB};
    const SLboolean req[1] = {SL_BOOLEAN_FALSE};
    (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 1, ids, req);
    (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    SLEnvironmentalReverbItf outputMixEnvironmentalReverb = NULL;
    (*outputMixObject)->GetInterface(outputMixObject, SL_IID_ENVIRONMENTALREVERB,
                                     &outputMixEnvironmentalReverb);
    SLEnvironmentalReverbSettings reverbSettings = SL_I3DL2_ENVIRONMENT_PRESET_STONECORRIDOR;
    (*outputMixEnvironmentalReverb)->SetEnvironmentalReverbProperties(outputMixEnvironmentalReverb,
                                                                      &reverbSettings);
    // 3.3 创建播放器
    SLObjectItf pPlayer = NULL;
    SLPlayItf pPlayItf = NULL;
    SLDataLocator_AndroidSimpleBufferQueue simpleBufferQueue = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM formatPcm = {
            SL_DATAFORMAT_PCM,
            2,
            SL_SAMPLINGRATE_44_1,
            SL_PCMSAMPLEFORMAT_FIXED_16,
            SL_PCMSAMPLEFORMAT_FIXED_16,
            SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
            SL_BYTEORDER_LITTLEENDIAN};
    SLDataSource audioSrc = {&simpleBufferQueue, &formatPcm};
    SLDataLocator_OutputMix outputMix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
    SLDataSink audioSnk = {&outputMix, NULL};
    SLInterfaceID interfaceIds[3] = {SL_IID_BUFFERQUEUE, SL_IID_VOLUME, SL_IID_PLAYBACKRATE};
    SLboolean interfaceRequired[3] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
    (*engineEngine)->CreateAudioPlayer(engineEngine, &pPlayer, &audioSrc, &audioSnk, 3,
                                       interfaceIds, interfaceRequired);
    (*pPlayer)->Realize(pPlayer, SL_BOOLEAN_FALSE);
    (*pPlayer)->GetInterface(pPlayer, SL_IID_PLAY, &pPlayItf);
    // 3.4 设置缓存队列和回调函数
    SLAndroidSimpleBufferQueueItf playerBufferQueue;
    (*pPlayer)->GetInterface(pPlayer, SL_IID_BUFFERQUEUE, &playerBufferQueue);
    (*playerBufferQueue)->RegisterCallback(playerBufferQueue, playerCallback, this);
    // 3.5 设置播放状态
    (*pPlayItf)->SetPlayState(pPlayItf, SL_PLAYSTATE_PLAYING);
    // 3.6 调用回调函数
    playerCallback(playerBufferQueue, this);
}

DZAudio::~DZAudio() {
    release();
}

void DZAudio::release() {
    if (pPacketQueue) {
        delete (pPacketQueue);
        pPacketQueue = nullptr;
    }
    if (resampleOutBuffer) {
        free(resampleOutBuffer);
        resampleOutBuffer = nullptr;
    }
    if (pPlayerStatus) {
        delete (pPlayerStatus);
        pPlayerStatus = nullptr;
    }
    if (pCodecContext != nullptr) {
        avcodec_close(pCodecContext);
        avcodec_free_context(&pCodecContext);
        pCodecContext = nullptr;
    }
}

