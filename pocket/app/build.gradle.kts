// Pocket Loomy: plain Android + Kotlin, no libraries. The crypto and the wire protocol are Loomy's own
// src/pocket.c + Monocypher, compiled through the NDK (see src/main/cpp/CMakeLists.txt).
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// CI passes these; local builds get "dev"
val pocketVersion = System.getenv("POCKET_VERSION") ?: "dev"
val pocketCode = (System.getenv("POCKET_VERSION_CODE") ?: "1").toInt()
val releaseStore = System.getenv("POCKET_KEYSTORE")

android {
    namespace = "io.github.jimmyuns.pocketloomy"
    compileSdk = 35

    defaultConfig {
        applicationId = "io.github.jimmyuns.pocketloomy"
        minSdk = 26
        targetSdk = 35
        versionCode = pocketCode
        versionName = pocketVersion
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a") }
        externalNativeBuild { cmake { cFlags += listOf("-fvisibility=hidden") } }
    }

    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt") }
    }

    signingConfigs {
        // dev builds: a throwaway key that lives in the repo ON PURPOSE (not a secret), so each dev build
        // installs over the last one. Dev builds are a separate app (".dev"), they can't replace the real one.
        create("dev") {
            storeFile = rootProject.file("dev.keystore")
            storePassword = "pocketdev"
            keyAlias = "dev"
            keyPassword = "pocketdev"
        }
        // release builds: Jimmy's key, only ever given to builds of a version tag (GitHub secret)
        if (releaseStore != null) create("release") {
            storeFile = file(releaseStore)
            storePassword = System.getenv("POCKET_KEYSTORE_PASSWORD")
            keyAlias = System.getenv("POCKET_KEY_ALIAS") ?: "pocket"
            keyPassword = System.getenv("POCKET_KEYSTORE_PASSWORD")
        }
    }

    buildTypes {
        debug {
            applicationIdSuffix = ".dev"
            signingConfig = signingConfigs.getByName("dev")
            resValue("string", "app_name", "Pocket Loomy dev")
        }
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            if (releaseStore != null) signingConfig = signingConfigs.getByName("release")
            resValue("string", "app_name", "Pocket Loomy")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}
