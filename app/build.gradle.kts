plugins { id("com.android.application") }

android {
    namespace = "dev.enginehost.plugin.buriko"
    compileSdk = 36
    defaultConfig {
        applicationId = "dev.enginehost.plugin.buriko.openbgi.slot1"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"
        // One bundle serves every device: the host picks lib/<abi> out of the
        // bundle from Build.SUPPORTED_ABIS, so both ABIs ship side by side.
        // arm64-v8a is the console; x86_64 is x86 hardware and the Android
        // emulator rig we test on. Nothing in the engine depends on either.
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild { cmake { arguments += "-DANDROID_STL=c++_shared" } }
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    // Enginehost loads this runtime's resources beside its own, which sit at
    // package id 0x7f; a second table at 0x7f is refused. Compile at 0x80.
    androidResources {
        additionalParameters += listOf("--package-id", "0x80", "--allow-reserved-package-id")
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
