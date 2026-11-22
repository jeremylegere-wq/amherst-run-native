package ca.amherstrun.game;

import org.libsdl.app.SDLActivity;

/** The game's activity: SDL's own, loading libSDL2.so and the game (libmain.so), then calling its main(). */
public class AmherstRunActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }
}
