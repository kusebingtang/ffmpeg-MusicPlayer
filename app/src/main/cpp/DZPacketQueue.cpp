//
// Created by zyh on 2020/12/16.
//

#include "DZPacketQueue.h"

DZPacketQueue::DZPacketQueue() {
    pPacketQueue = new std::queue<AVPacket *>();
    pthread_mutex_init(&packetMutex, nullptr);
    pthread_cond_init(&packetCond, nullptr);
}

DZPacketQueue::~DZPacketQueue() {
    if (pPacketQueue != NULL) {
        clear();
        delete pPacketQueue;
        pPacketQueue = nullptr;
    }
    pthread_mutex_destroy(&packetMutex);
    pthread_cond_destroy(&packetCond);

}

void DZPacketQueue::push(AVPacket *pPacket) {
    pthread_mutex_lock(&packetMutex);
    pPacketQueue->push(pPacket);
    pthread_cond_signal(&packetCond);
    pthread_mutex_unlock(&packetMutex);

}

AVPacket *DZPacketQueue::pop() {
    AVPacket *pPacket;
    pthread_mutex_lock(&packetMutex);
    while (pPacketQueue->empty()) {
        pthread_cond_wait(&packetCond, &packetMutex);
    }
    pPacket = pPacketQueue->front();
    pPacketQueue->pop();
    pthread_mutex_unlock(&packetMutex);
    return pPacket;
}

void DZPacketQueue::clear() {
    while (!pPacketQueue->empty()) {
        pPacketQueue->pop();
    }
}
