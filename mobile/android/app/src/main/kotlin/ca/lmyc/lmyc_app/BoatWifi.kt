package ca.lmyc.lmyc_app

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiNetworkSpecifier
import android.os.Build
import android.os.Handler
import android.os.Looper
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Joins the boat SoftAP for this app only. Android shows one Connect prompt.
 * The boat network has no internet, so the request drops that capability and
 * the process is bound to the resulting network. Other apps keep their own route.
 */
object BoatWifi : MethodChannel.MethodCallHandler {
    private val main = Handler(Looper.getMainLooper())
    private var callback: ConnectivityManager.NetworkCallback? = null
    private var joinedSsid: String? = null

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
            "release" -> {
                release(activity)
                result.success(null)
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
        if (joinedSsid == ssid && callback != null) {
            result.success(null)
            return
        }
        val cm = app.getSystemService(ConnectivityManager::class.java)
        release(app)

        val specifier = WifiNetworkSpecifier.Builder()
            .setSsid(ssid)
            .setWpa2Passphrase(psk)
            .build()
        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .setNetworkSpecifier(specifier)
            .build()

        val pending = AtomicBoolean(true)
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                main.post {
                    val bound = cm.bindProcessToNetwork(network)
                    if (!pending.compareAndSet(true, false)) {
                        return@post
                    }
                    if (!bound) {
                        release(app)
                        result.error(
                            "wifi",
                            "Android joined $ssid but this app could not use it.",
                            null,
                        )
                        return@post
                    }
                    joinedSsid = ssid
                    result.success(null)
                }
            }

            override fun onUnavailable() {
                main.post {
                    if (!pending.compareAndSet(true, false)) {
                        return@post
                    }
                    release(app)
                    result.error(
                        "wifi",
                        "Android did not join $ssid. Tap Connect on the system prompt. " +
                            "The network has no internet; the book still uses 192.168.4.1.",
                        null,
                    )
                }
            }
        }
        callback = cb
        try {
            cm.requestNetwork(request, cb, 60_000)
        } catch (e: SecurityException) {
            release(app)
            result.error(
                "wifi",
                "Android blocked the Wi-Fi request. Allow nearby Wi-Fi for LMYC and try again.",
                null,
            )
        } catch (e: RuntimeException) {
            release(app)
            result.error("wifi", "Could not ask Android to join $ssid.", null)
        }
    }

    private fun release(context: Context) {
        val cm = context.getSystemService(ConnectivityManager::class.java)
        callback?.let { runCatching { cm.unregisterNetworkCallback(it) } }
        callback = null
        joinedSsid = null
        runCatching { cm.bindProcessToNetwork(null) }
    }
}
