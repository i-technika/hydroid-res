plugins {
    id("com.android.application")
}

android {
    namespace = "com.itechnika.hydroid.golden.nolocale"
    compileSdk = 37

    defaultConfig {
        applicationId = "com.itechnika.hydroid.golden.nolocale"
        minSdk = 24
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    // Keep every resource, even unused ones: the golden dump walks all of R.
    buildTypes {
        debug { isMinifyEnabled = false }
    }
}

dependencies {
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
}
