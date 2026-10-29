import java.io.FileInputStream
import java.util.Properties

plugins {
    id("com.android.application")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

// Play upload key: key.properties from $LMYC_KEY_PROPERTIES, else
// ~/.android-keys/lmyc-boat/key.properties (never in the repo; docs/23).
// Without it the release build is signed with the debug key (not uploadable).
val keyPropertiesFile = file(
    System.getenv("LMYC_KEY_PROPERTIES")
        ?: "${System.getProperty("user.home")}/.android-keys/lmyc-boat/key.properties",
)
val keyProperties = Properties()
val hasReleaseKey = keyPropertiesFile.isFile
if (hasReleaseKey) {
    FileInputStream(keyPropertiesFile).use { keyProperties.load(it) }
} else {
    logger.warn(
        "WARNING: no release key at ${keyPropertiesFile.path}: release builds are signed " +
            "with the DEBUG key and Play will reject them. See docs/23-play-store.md.",
    )
}

android {
    namespace = "ca.lmyc.lmyc_app"
    compileSdk = flutter.compileSdkVersion
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    defaultConfig {
        // The Play Console name. The Kotlin namespace stays ca.lmyc.lmyc_app.
        applicationId = "ca.lmyc.boat"
        // You can update the following values to match your application needs.
        // For more information, see: https://flutter.dev/to/review-gradle-config.
        // Android 10 (API 29): the boat Wi-Fi is joined with WifiNetworkSpecifier,
        // which older phones do not have. See docs/06.
        minSdk = 29
        targetSdk = flutter.targetSdkVersion
        // Uses the version code from pubspec.yaml. When using split APKs, 1000 * ABI_VERSION
        // is added automatically by Flutter. (https://developer.android.com/studio/build/configure-apk-splits#configure-APK-versions)
        // You can force using the value of versionCode by specifying the `-P force-version-code-ignoring-abi=true`
        // flag during build.
        versionCode = flutter.versionCode
        versionName = flutter.versionName
    }

    signingConfigs {
        if (hasReleaseKey) {
            create("release") {
                keyAlias = keyProperties.getProperty("keyAlias")
                keyPassword = keyProperties.getProperty("keyPassword")
                storePassword = keyProperties.getProperty("storePassword")
                // storeFile may be relative to key.properties.
                storeFile = keyPropertiesFile.parentFile.resolve(keyProperties.getProperty("storeFile"))
            }
        }
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.getByName(if (hasReleaseKey) "release" else "debug")
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

flutter {
    source = "../.."
}
