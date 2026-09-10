package dev.enginehost.plugin.buriko;

import android.os.Bundle;
import android.widget.Toast;
import org.libsdl.app.SDLActivity;
import java.io.File;

/** Programmatic SDL entry point for the native OpenBGI interpreter. */
public final class EngineHostBurikoActivity extends SDLActivity {
    private String gamePath;

    @Override
    protected void onCreate(Bundle state) {
        // Enginehost hands the launch over as dev.enginehost.runtime.* extras.
        // SDLActivity's onCreate must run before anything else here, finish()
        // included: an activity that returns without calling through to it
        // dies with SuperNotCalledException instead of showing its message.
        super.onCreate(state);
        String context = getIntent().getStringExtra("dev.enginehost.runtime.ENGINE_CONTEXT");
        String path = getIntent().getStringExtra("dev.enginehost.runtime.PATH");
        boolean supportedContext = "compiled-script-v1".equals(context)
                || "august-compiled-script-v1".equals(context);
        if (!supportedContext || path == null || !new File(path).isDirectory()) {
            Toast.makeText(this, "Invalid enginehost Buriko launch request", Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        gamePath = new File(path).getAbsolutePath();
    }

    @Override
    protected String getMainSharedObject() {
        // SDLActivity looks for libmain.so in the application's own native
        // library directory, which under Enginehost is the host APK's, not this
        // bundle's. The class loader that loaded this activity knows where the
        // bundle's libraries are; ask it.
        ClassLoader loader = EngineHostBurikoActivity.class.getClassLoader();
        if (loader instanceof dalvik.system.BaseDexClassLoader) {
            String path = ((dalvik.system.BaseDexClassLoader) loader).findLibrary("main");
            if (path != null) return path;
        }
        return super.getMainSharedObject();
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected String[] getArguments() {
        return new String[] { gamePath };
    }
}
