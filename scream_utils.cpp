//
// Created by xavier on 1/17/23.
//

#include "scream_utils.h"
#include <sys/time.h>
#include <cstdlib>

double t0 = 0;

void packet_free(void *buf, uint32_t ssrc) {
    free(buf);
}

uint32_t getTimeInNtp() {
    timeval tp;
    gettimeofday(&tp, NULL);
    double time = tp.tv_sec + tp.tv_usec * 1e-6 - t0;
    uint64_t ntp64 = time * 65536.0;
    uint32_t ntp = 0xFFFFFFFF & ntp64;
    return ntp;
}

void initT0() {
    struct timeval tp;
    gettimeofday(&tp, NULL);
    t0 = tp.tv_sec + tp.tv_usec * 1e-6 - 1e-3;
}