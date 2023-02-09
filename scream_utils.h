//
// Created by xavier on 1/17/23.
//

#ifndef SCREAM_SCREAM_UTILS_H
#define SCREAM_SCREAM_UTILS_H

#include <cstdint>

void packet_free(void *buf, uint32_t ssrc);

uint32_t getTimeInNtp();

void initT0();

#endif //SCREAM_SCREAM_UTILS_H
