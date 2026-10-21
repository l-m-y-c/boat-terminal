package ca.lmyc.lmyc_app

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.net.wifi.WifiManager
import android.net.wifi.WifiNetworkSuggestion
import android.os.Build
import android.provider.Settings
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.net.Inet4Address
import java.net.InetAddress

/**
 * Saves the boat hotspot as a normal Wi-Fi network for the whole phone, and
 * reports whether chart apps can reach the terminal over it.
 *
 * Only a saved network keeps the "Always connect" answer Android asks for on
 * a network with no internet. A network suggestion loses that answer on the
 * next join, and the phone then sends chart apps over mobile data.
 */
object BoatWifi : MethodChannel.MethodCallHandler {
    private const val ADD_NETWORKS_REQUEST = 4410

    /** The connect() call waiting for the user to answer the Android prompt. */
    private var pending: MethodChannel.Result? = null

    @Volatile
    var currentContext: Context? = null

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        val context = currentContext ?: run {
            result.error("wifi", "The app is not ready to join Wi-Fi.", null)
            return
        }
        when (call.method) {
            "connect" -> {
                val ssid = call.argument<String>("ssid")?.trim().orEmpty()
                val psk = call.argument<String>("psk") ?: ""
                connect(context, ssid, psk, result)
            }
            "status" -> {
                status(context, call.argument<String>("ip").orEmpty(), result)
            }
            "startRelay" -> {
                val ip = call.argument<String>("ip").orEmpty()
                if (parseIpv4(ip) == null) {
                    result.error("wifi", "The terminal address $ip is not an IPv4 address.", null)
                } else {
                    BoatRelayService.start(context.applicationContext, ip)
                    result.success(null)
                }
            }
            "stopRelay" -> {
                BoatRelayService.stop(context.applicationContext)
                result.success(null)
            }
            "openSettings" -> openSettings(context, result)
            else -> result.notImplemented()
        }
    }

    /** Answers connect() with what the user chose on the Android prompt. */
    fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?): Boolean {
        if (requestCode != ADD_NETWORKS_REQUEST) return false
        val waiting = pending ?: return true
        pending = null
        if (resultCode != Activity.RESULT_OK) {
            waiting.success("declined")
            return true
        }
        val codes = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            data?.getIntegerArrayListExtra(Settings.EXTRA_WIFI_NETWORK_RESULT_LIST)
        } else {
            null
        }
        val failed = codes != null && codes.isNotEmpty() &&
            codes.all { it == Settings.ADD_WIFI_RESULT_ADD_OR_UPDATE_FAILED }
        waiting.success(if (failed) "failed" else "saved")
        return true
    }

    private fun connect(
        context: Context,
        ssid: String,
        psk: String,
        result: MethodChannel.Result,
    ) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            result.error("wifi", "This phone cannot join the boat Wi-Fi from the app.", null)
            return
        }
        if (ssid.isEmpty() || psk.length < 8) {
            result.error("wifi", "The terminal sent an unusable Wi-Fi name or passphrase.", null)
            return
        }
        val wifi = context.applicationContext.getSystemService(WifiManager::class.java)
        val network = WifiNetworkSuggestion.Builder()
            .setSsid(ssid)
            .setWpa2Passphrase(psk)
            .build()

        val activity = context as? Activity
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R || activity == null) {
            // Android 10 has no save prompt. A suggestion is the only way in.
            val status = wifi.addNetworkSuggestions(listOf(network))
            if (status == WifiManager.STATUS_NETWORK_SUGGESTIONS_SUCCESS ||
                status == WifiManager.STATUS_NETWORK_SUGGESTIONS_ERROR_ADD_DUPLICATE
            ) {
                result.success("saved")
            } else {
                result.error("wifi", "Android did not save $ssid for this phone.", null)
            }
            return
        }

        // Earlier versions of the app also registered a suggestion. Android
        // joins through it in preference to the saved network, so drop it.
        val ours = wifi.networkSuggestions
        if (ours.isNotEmpty()) {
            wifi.removeNetworkSuggestions(ours)
        }

        val intent = Intent(Settings.ACTION_WIFI_ADD_NETWORKS)
        intent.putParcelableArrayListExtra(Settings.EXTRA_WIFI_NETWORK_LIST, arrayListOf(network))
        pending?.success("declined")
        pending = result
        try {
            activity.startActivityForResult(intent, ADD_NETWORKS_REQUEST)
        } catch (_: RuntimeException) {
            pending = null
            result.error("wifi", "Android did not show the Save network prompt.", null)
        }
    }

    /**
     * onBoat: a Wi-Fi network has an address next to [ip].
     * isDefault: that network is the one ordinary apps use. Diagnostics only:
     * chart apps go through the relay, so mobile data can stay the default.
     * The relay fields say whether chart apps on 127.0.0.1 are getting data.
     */
    private fun status(context: Context, ip: String, result: MethodChannel.Result) {
        val target = parseIpv4(ip) ?: run {
            result.error("wifi", "The terminal address $ip is not an IPv4 address.", null)
            return
        }
        val cm = context.applicationContext.getSystemService(ConnectivityManager::class.java)
        val boat = findBoatNetwork(cm, target)
        result.success(
            mapOf(
                "onBoat" to (boat != null),
                "isDefault" to (boat != null && boat == cm.activeNetwork),
                "relayRunning" to BoatRelayService.running,
                "relayListening" to BoatRelayService.listening,
                "relayUpstream" to BoatRelayService.upstream,
                "relayDataAgeMs" to BoatRelayService.dataAgeMs(),
                "relayClients" to BoatRelayService.clients.size,
            ),
        )
    }

    @Suppress("DEPRECATION")
    private fun findBoatNetwork(cm: ConnectivityManager, target: Inet4Address): Network? {
        for (network in cm.allNetworks) {
            val caps = cm.getNetworkCapabilities(network) ?: continue
            if (!caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) continue
            val lp = cm.getLinkProperties(network) ?: continue
            if (onSubnet(lp, target)) return network
        }
        return null
    }

    /** The network has an IPv4 address in the same subnet as [target]. */
    fun onSubnet(lp: LinkProperties, target: Inet4Address): Boolean =
        lp.linkAddresses.any { link ->
            val address = link.address as? Inet4Address
            address != null && sameSubnet(address, target, link.prefixLength)
        }

    fun parseIpv4(ip: String): Inet4Address? {
        val parts = ip.split('.')
        if (parts.size != 4) return null
        val bytes = ByteArray(4)
        for (i in 0 until 4) {
            val n = parts[i].toIntOrNull() ?: return null
            if (n !in 0..255) return null
            bytes[i] = n.toByte()
        }
        return InetAddress.getByAddress(bytes) as? Inet4Address
    }

    private fun sameSubnet(a: Inet4Address, b: Inet4Address, prefix: Int): Boolean {
        if (prefix !in 1..32) return false
        val x = a.address
        val y = b.address
        var bits = prefix
        for (i in 0 until 4) {
            val mask = if (bits >= 8) 0xFF else (0xFF shl (8 - bits)) and 0xFF
            if ((x[i].toInt() and mask) != (y[i].toInt() and mask)) return false
            bits = (bits - 8).coerceAtLeast(0)
            if (bits == 0) return true
        }
        return true
    }

    /** The Wi-Fi panel over the app, or the full Wi-Fi settings page. */
    private fun openSettings(context: Context, result: MethodChannel.Result) {
        val action = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            Settings.Panel.ACTION_WIFI
        } else {
            Settings.ACTION_WIFI_SETTINGS
        }
        try {
            context.startActivity(Intent(action))
            result.success(null)
        } catch (_: RuntimeException) {
            result.error("wifi", "Android did not open Wi-Fi settings.", null)
        }
    }
}
