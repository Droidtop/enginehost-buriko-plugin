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
        String context = getIntent().getStringExtra("dev.enginehost.runtime.ENGINE_CONTEXT");
        String version = getIntent().getStringExtra("dev.enginehost.runtime.ENGINE_VERSION");
        String path = getIntent().getStringExtra("dev.enginehost.runtime.PATH");
        boolean supportedContext = "compiled-script-v1".equals(context)
                || "august-compiled-script-v1".equals(context);
        if (!supportedContext || !"1.0".equals(version)
                || path == null || !new File(path).isDirectory()) {
            Toast.makeText(this, "Invalid enginehost Buriko launch request", Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        gamePath = new File(path).getAbsolutePath();
        super.onCreate(state);
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
