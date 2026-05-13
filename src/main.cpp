#include "Game.hpp"

int main(int, char**) {
    Game game;
    if (!game.Initialize()) {
        return 1;
    }

    game.Run();
    return 0;
}
