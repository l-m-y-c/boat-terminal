package ca.lmyc.lmyc_app

import android.content.Context
import android.content.Intent
import android.net.wifi.WifiManager
import android.net.wifi.WifiNetworkSuggestion
import android.os.Build
import android.provider.Settings
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel

/**
 * Saves the boat hotspot as a normal Wi-Fi network for the whole phone.
 * Android will not leave a network that has internet for one that does not,
 * so a phone already on home Wi-Fi must tap the boat network in Wi-Fi settings.
 * A process-only join would leave Navionics and i-Boating unable to see it.
 */
object BoatWifi : MethodChannel.MethodCallHandler {
    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        val activity = currentContext ?: run {
            result.error("wifi", "The app is not ready to join Wi-Fi.", null)
            return
        }
        when (call.method) {
            "connect" -> {
                val ssid = call.argument<String>("ssid")?.trim().orEmpty()
                val psk = call.argument<String>("psk") ?: ""
                connect(activity, ssid, psk, result)
            }
            else -> result.notImplemented()
        }
    }

    @Volatile
    var currentContext: Context? = null

    private fun connect(
        context: Context,
        ssid: String,
        psk: String,
        result: MethodChannel.Result,
    ) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            result.error(
                "wifi",
                "This phone cannot join the boat Wi-Fi from the app.",
                null,
            )
            return
        }
        if (ssid.isEmpty() || psk.length < 8) {
            result.error("wifi", "The terminal sent an unusable Wi-Fi name or passphrase.", null)
            return
        }
        val app = context.applicationContext
        val suggestion = WifiNetworkSuggestion.Builder()
            .setSsid(ssid)
            .setWpa2Passphrase(psk)
            .setIsInitialAutojoinEnabled(true)
            .build()
        val wifi = app.getSystemService(WifiManager::class.java)
        val status = wifi.addNetworkSuggestions(listOf(suggestion))
        if (status != WifiManager.STATUS_NETWORK_SUGGESTIONS_SUCCESS &&
            status != WifiManager.STATUS_NETWORK_SUGGESTIONS_ERROR_ADD_DUPLICATE
        ) {
            result.error("wifi", "Android did not save $ssid for this phone.", null)
            return
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val intent = Intent(Settings.ACTION_WIFI_ADD_NETWORKS)
            intent.putParcelableArrayListExtra(
                Settings.EXTRA_WIFI_NETWORK_LIST,
                arrayListOf(suggestion),
            )
            try {
                context.startActivity(intent)
            } catch (_: RuntimeException) {
                result.error(
                    "wifi",
                    "Saved $ssid, but Android did not show the Wi-Fi prompt. " +
                        "Open Wi-Fi settings and tap $ssid.",
                    null,
                )
                return
            }
        }
        result.success(null)
    }
}
