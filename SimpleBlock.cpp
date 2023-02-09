//
// Created by xavier on 12/28/22.
//

#include <iostream>

#include "SimpleBlock.h"

SimpleBlock::SimpleBlock(std::string &&name) : name(std::move(name)) {

}

void SimpleBlock::start() {
    if (!initialized) {
        std::cout << name << ": you need to init me first" << std::endl;
        return;
    }

    if (stop_condition.load(std::memory_order::relaxed)) {
        stop_condition.store(false, std::memory_order::acquire);
        thread = std::thread(&SimpleBlock::run, this);
    } else {
        std::cout << "already started" << std::endl;
    }
}

void SimpleBlock::stop() {
    if (!stop_condition.load(std::memory_order::relaxed)) {
        stop_condition.store(true, std::memory_order::acquire);
        thread.join();
    } else {
        std::cout << "already stopped" << std::endl;
    }
}