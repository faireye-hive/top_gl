// Integration regression for saves, combat and temporary effects without GL.
#include "top/Play.h"
#include <cstdio>
int main() {
    if (!top::Play::selfTest()) {
        std::fprintf(stderr, "Play::selfTest failed (05-V)\n");
        return 1;
    }
    return 0;
}
