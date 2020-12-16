//
// Created by zyh on 2020/12/16.
//

#ifndef MUSICPLAYER_DZPACKETQUEUE_H
#define MUSICPLAYER_DZPACKETQUEUE_H

#include <queue>
#include <pthread.h>

extern "C" {
#include <libavcodec/avcodec.h>
};

class DZPacketQueue {
public:


public:
    std::queue<AVPacket *> *pPacketQueue;
    pthread_mutex_t packetMutex{};
    pthread_cond_t packetCond{};
public:
    DZPacketQueue();

    ~DZPacketQueue();

public:

    void push(AVPacket *pPacket);
    AVPacket * pop();

    /**
     * 清除整个队列
     */
    void clear();

};


#endif //MUSICPLAYER_DZPACKETQUEUE_H
