package ca.lmyc.lmyc_app

import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        BoatWifi.currentContext = this
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, "ca.lmyc/boat_wifi")
            .setMethodCallHandler(BoatWifi)
    }

    override fun onDestroy() {
        if (BoatWifi.currentContext === this) {
            BoatWifi.currentContext = null
        }
        super.onDestroy()
    }
}
