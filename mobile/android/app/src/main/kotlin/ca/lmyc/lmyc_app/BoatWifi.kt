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
 *
 * While the request is held the boat Wi-Fi stays up, so the binding can be
 * flipped: useInternet unbinds for a club call, useBoat binds back. Without
 * that the club host is unreachable until the process restarts.
 */
object BoatWifi : MethodChannel.MethodCallHandler {
    private val main = Handler(Looper.getMainLooper())
    private var callback: ConnectivityManager.NetworkCallback? = null
    private var joinedSsid: String? = null
    private var boatNetwork: Network? = null

    /** Answers a connect() that has not finished when it is superseded. */
    private var failPending: ((String) -> Unit)? = null

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        val activity = currentContext ?: run {
            result.error("wifi", "The app is not ready to join Wi-Fi.", null)
            return
        }
        val app = activity.applicationContext
        when (call.method) {
            "connect" -> {
                val ssid = call.argument<String>("ssid")?.trim().orEmpty()
                val psk = call.argument<String>("psk") ?: ""
                connect(activity, ssid, psk, result)
            }
            "release" -> {
                release(app)
                result.success(null)
            }
            "useInternet" -> {
                bind(app, null)
                result.success(null)
            }
            "useBoat" -> {
                result.success(boatNetwork?.let { bind(app, it) } ?: false)
            }
            else -> result.notImplemented()
        }
    }

    @Volatile
    var currentContext: Context? = null

    private fun bind(context: Context, network: Network?): Boolean {
        val cm = context.getSystemService(ConnectivityManager::class.java)
        return runCatching { cm.bindProcessToNetwork(network) }.getOrDefault(false)
    }

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
        if (joinedSsid == ssid && boatNetwork != null && callback != null) {
            bind(app, boatNetwork)
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
        failPending = { message ->
            if (pending.compareAndSet(true, false)) {
                result.error("wifi", message, null)
            }
        }
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                main.post {
                    if (callback !== this) {
                        return@post
                    }
                    boatNetwork = network
                    val bound = bind(app, network)
                    if (bound) {
                        joinedSsid = ssid
                    }
                    if (!pending.compareAndSet(true, false)) {
                        return@post
                    }
                    failPending = null
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

            override fun onLost(network: Network) {
                main.post {
                    if (callback !== this || boatNetwork != network) {
                        return@post
                    }
                    /* The request stays registered, so Android may bring the
                     * boat back and onAvailable binds again. Until then the
                     * next connect() must not report success. */
                    boatNetwork = null
                    joinedSsid = null
                    bind(app, null)
                }
            }

            override fun onUnavailable() {
                main.post {
                    if (!pending.compareAndSet(true, false)) {
                        return@post
                    }
                    failPending = null
                    release(app)
                    result.error(
                        "wifi",
                        "Android did not join $ssid. Tap Connect on the system prompt. " +
                            "The network has no internet; the book still uses the boat address.",
                        null,
                    )
                }
            }
        }
        callback = cb
        try {
            cm.requestNetwork(request, cb, 60_000)
        } catch (e: SecurityException) {
            failNow(app, "Android blocked the Wi-Fi request. Allow nearby Wi-Fi for LMYC and try again.")
        } catch (e: RuntimeException) {
            failNow(app, "Could not ask Android to join $ssid.")
        }
    }

    /** Answers the pending connect() once with message, then drops the request. */
    private fun failNow(context: Context, message: String) {
        val fail = failPending
        failPending = null
        release(context)
        fail?.invoke(message)
    }

    private fun release(context: Context) {
        val cm = context.getSystemService(ConnectivityManager::class.java)
        callback?.let { runCatching { cm.unregisterNetworkCallback(it) } }
        callback = null
        joinedSsid = null
        boatNetwork = null
        failPending?.invoke("A newer Wi-Fi request replaced this one.")
        failPending = null
        runCatching { cm.bindProcessToNetwork(null) }
    }
}
