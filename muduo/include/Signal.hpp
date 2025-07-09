#pragma once

#include <signal.h>

class Signal {
public:
    Signal() {
        signal(SIGPIPE, SIG_IGN);
    }
};
