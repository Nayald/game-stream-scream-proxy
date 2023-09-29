#include "simple_block.h"
#include "logger.h"

SimpleBlock::SimpleBlock(std::string name) : name(std::move(name)) {}

void SimpleBlock::start() {
    if (!initialized) {
        logger::log(logger::INFO, name, ": you need to init me first");
        return;
    }

    if (stop_condition.load()) {
        stop_condition.store(false);
        thread = std::thread(&SimpleBlock::run, this);
    } else {
        logger::log(logger::INFO, name, ": thread(s) already started");
    }
}

void SimpleBlock::stop() {
    if (!stop_condition.load()) {
        stop_condition.store(true);
        thread.join();
    } else {
        logger::log(logger::INFO, name, ": thread(s) already stopped");
    }
}
