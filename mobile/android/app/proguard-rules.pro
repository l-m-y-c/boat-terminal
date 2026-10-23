# shared_preferences_android calls its Kotlin registrar from Dart over JNI.
# R8 cannot see those calls: it renamed the coroutine types in the method
# signatures and dropped CoroutineSingletons, so every prefs read and write
# failed in release builds (settings and the saved boat never persisted).
# The bridge also names kotlin.Result and friends, so keep kotlin.* by name.
-keep class io.flutter.plugins.sharedpreferences.** { *; }
-keep class kotlin.** { *; }
-keep class kotlinx.coroutines.** { *; }
