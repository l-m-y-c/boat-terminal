package ca.lmyc.lmyc_app

import android.content.Intent
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

    @Deprecated("FlutterActivity still routes plugin results through this.")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        if (BoatWifi.onActivityResult(requestCode, resultCode)) return
        super.onActivityResult(requestCode, resultCode, data)
    }

    override fun onDestroy() {
        if (BoatWifi.currentContext === this) {
            BoatWifi.currentContext = null
        }
        super.onDestroy()
    }
}
