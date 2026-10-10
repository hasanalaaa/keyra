import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "app.keyra.android"
    compileSdk = 36

    defaultConfig {
        applicationId = "app.keyra.android"
        minSdk = 29
        targetSdk = 36
        versionCode = 1
        versionName = "0.3.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            // Unsigned on purpose: sign with your own keystore (android/README.md).
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    lint {
        warningsAsErrors = false
        abortOnError = true
        checkDependencies = false
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_17)
    }
}

dependencies {
    // No runtime dependencies beyond the Kotlin standard library: the UI is framework Views.
    testImplementation("junit:junit:4.13.2")
    // The platform's org.json is a stub in JVM unit tests; this is the real one.
    testImplementation("org.json:json:20260814")

    androidTestImplementation("androidx.test:runner:1.7.0")
    androidTestImplementation("androidx.test.ext:junit:1.3.0")
}

// The API tests run the web app's Node mock (web/mock/server.mjs). KEYRA_MOCK overrides its path.
tasks.withType<Test>().configureEach {
    val mock = System.getenv("KEYRA_MOCK") ?: rootDir.resolve("../web/mock/server.mjs").absolutePath
    systemProperty("keyra.mock", mock)
    systemProperty("keyra.node", System.getenv("KEYRA_NODE") ?: "node")
}
