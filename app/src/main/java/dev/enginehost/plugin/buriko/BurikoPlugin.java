package dev.enginehost.plugin.buriko;

import android.os.ParcelFileDescriptor;
import dev.enginehost.api.EngineControllerEvent;
import dev.enginehost.api.EngineHost;
import dev.enginehost.api.EnginePlugin;
import dev.enginehost.api.EnginePluginSession;
import dev.enginehost.api.EngineProcess;
import dev.enginehost.api.EngineStepDriven;
import java.io.IOException;

/**
 * OpenBGI in Enginehost's sandbox (Enginehost docs/engine-sandbox.md).
 *
 * <p>The engine runs on its own thread in Enginehost's isolated process,
 * exactly as main() runs it on a desktop: handed the game folder, it enters
 * it and reads what it needs. It reaches the files through Enginehost's file
 * layer (plugin-native/enginehost_vfs_forward.c), composes each frame in
 * memory, which {@link #step} hands to the host to draw, and mixes its sound
 * into the host's audio ring (os_enginehost.c). Input arrives here as the
 * host's controller actions and taps and goes to the engine as the virtual
 * keys and mouse its own Windows input layer reads.
 */
public final class BurikoPlugin implements EnginePlugin, EngineStepDriven {
    static {
        try {
            System.loadLibrary("main");
        } catch (UnsatisfiedLinkError e) {
            // Under isolation the host loads the library from a descriptor
            // and binds these natives itself (enginehost_register_natives);
            // System.loadLibrary cannot find it there and does not need to.
            if (!EngineProcess.isIsolated()) throw e;
        }
    }

    // Windows virtual keys (WinUser.h).
    private static final int VK_RETURN = 0x0D;
    private static final int VK_CONTROL = 0x11;
    private static final int VK_ESCAPE = 0x1B;
    private static final int VK_SPACE = 0x20;
    private static final int VK_PRIOR = 0x21;
    private static final int VK_NEXT = 0x22;
    private static final int VK_LEFT = 0x25;
    private static final int VK_UP = 0x26;
    private static final int VK_RIGHT = 0x27;
    private static final int VK_DOWN = 0x28;
    private static final int MOUSE_RIGHT = 1;

    private long engine;

    @Override
    public void onCreate(EnginePluginSession session) throws Exception {
        String context = session.engineContext();
        if (!"compiled-script-v1".equals(context) && !"august-compiled-script-v1".equals(context)) {
            throw new IOException("Unsupported BURIKO context " + context);
        }
        if (session.display() != null) {
            throw new IOException("OpenBGI runs only in Enginehost's sandbox");
        }
        EngineHost host = session.host();
        ParcelFileDescriptor ring = host.isolatedAudioBuffer();
        engine = nativeStart(session.gamePath(), ring == null ? -1 : ring.getFd(), host.isolatedAudioSampleRate());
        if (ring != null) ring.close();
        if (engine == 0) throw new IOException(nativeError());
    }

    @Override public int pixelWidth() { return engine == 0 ? 0 : nativeWidth(engine); }
    @Override public int pixelHeight() { return engine == 0 ? 0 : nativeHeight(engine); }
    @Override public int step(int[] pixels) { return engine == 0 ? -1 : nativeStep(engine, pixels); }

    @Override public void onPointerMove(int x, int y) {
        if (engine != 0) nativePointer(engine, x, y, true);
    }

    @Override public void onPointerUp(int x, int y) {
        if (engine != 0) nativePointer(engine, x, y, false);
    }

    /**
     * The host's actions (its common set: Enginehost gives BURIKO no input
     * model of its own), as the original reads a keyboard and mouse: the
     * d-pad is the arrow keys, confirm is Enter, cancel is Escape, menu is
     * the right button most BGI games open their menu with, skip holds
     * Control, auto is the space bar, history turns the wheel back (the
     * backlog), and the page actions are Page Up and Page Down.
     */
    @Override
    public boolean onControllerEvent(EngineControllerEvent event) {
        if (engine == 0) return false;
        boolean down = event.pressed();
        switch (event.action()) {
            case "up": return key(VK_UP, down);
            case "down": return key(VK_DOWN, down);
            case "left": return key(VK_LEFT, down);
            case "right": return key(VK_RIGHT, down);
            case "confirm": return key(VK_RETURN, down);
            case "cancel": return key(VK_ESCAPE, down);
            case "skip": return key(VK_CONTROL, down);
            case "auto": return key(VK_SPACE, down);
            case "page_previous": return key(VK_PRIOR, down);
            case "page_next": return key(VK_NEXT, down);
            case "menu":
                nativeMouseButton(engine, MOUSE_RIGHT, down);
                return true;
            case "history":
                if (down) nativeWheel(engine, 1);
                return true;
            default:
                return false;
        }
    }

    private boolean key(int vk, boolean down) {
        nativeKey(engine, vk, down);
        return true;
    }

    @Override public void onPause() { if (engine != 0) nativeFocus(engine, false); }
    @Override public void onResume() { if (engine != 0) nativeFocus(engine, true); }

    @Override
    public void onDestroy() {
        if (engine != 0) nativeStop(engine);
        engine = 0;
    }

    static native long nativeStart(String gamePath, int audioRingFd, int audioSampleRate);
    static native String nativeError();
    static native int nativeWidth(long engine);
    static native int nativeHeight(long engine);
    static native int nativeStep(long engine, int[] pixels);
    static native void nativePointer(long engine, int x, int y, boolean held);
    static native void nativeKey(long engine, int vk, boolean down);
    static native void nativeMouseButton(long engine, int button, boolean down);
    static native void nativeWheel(long engine, int steps);
    static native void nativeFocus(long engine, boolean active);
    static native void nativeStop(long engine);
}
