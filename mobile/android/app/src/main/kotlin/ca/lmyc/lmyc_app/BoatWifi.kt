package ca.lmyc.lmyc_app

import android.content.Context
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.net.wifi.WifiManager
import android.os.Build
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import java.net.Inet4Address
import java.net.InetAddress

/**
 * The app's side of the boat Wi-Fi: start and stop [BoatRelayService], and
 * report where the phone stands.
 *
 * The relay joins the hotspot for this app alone. Nothing is saved for the
 * phone, so there is no Save prompt, no Wi-Fi list and no "Always connect";
 * chart apps read 127.0.0.1 and mobile data stays the phone's internet.
 */
object BoatWifi : MethodChannel.MethodCallHandler {
    @Volatile
    var currentContext: Context? = null

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        val context = currentContext ?: run {
            result.error("wifi", "The app is not ready to join Wi-Fi.", null)
            return
        }
        when (call.method) {
            "status" -> status(context, call.argument<String>("ip").orEmpty(), result)
            "startRelay" -> {
                val ip = call.argument<String>("ip").orEmpty()
                if (parseIpv4(ip) == null) {
                    result.error("wifi", "The terminal address $ip is not an IPv4 address.", null)
                    return
                }
                removeOldSuggestions(context)
                BoatRelayService.start(
                    context.applicationContext,
                    ip,
                    call.argument<String>("ssid"),
                    call.argument<String>("psk"),
                )
                result.success(null)
            }
            "stopRelay" -> {
                BoatRelayService.stop(context.applicationContext)
                result.success(null)
            }
            else -> result.notImplemented()
        }
    }

    /**
     * Earlier versions registered the hotspot as a network suggestion. Android
     * joins it for the whole phone through that, ahead of the relay's own link.
     */
    private fun removeOldSuggestions(context: Context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return
        val wifi = context.applicationContext.getSystemService(WifiManager::class.java)
        val ours = wifi.networkSuggestions
        if (ours.isNotEmpty()) wifi.removeNetworkSuggestions(ours)
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
                "relayDeclined" to BoatRelayService.declined,
                "relayNeedsApp" to BoatRelayService.needsApp,
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
}
