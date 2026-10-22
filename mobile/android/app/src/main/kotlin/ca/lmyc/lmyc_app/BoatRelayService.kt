package ca.lmyc.lmyc_app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiNetworkSpecifier
import android.os.Build
import android.os.IBinder
import android.os.SystemClock
import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.TimeUnit

/**
 * Serves the terminal's NMEA stream on 127.0.0.1:10110 for chart apps.
 *
 * Android sends each app's traffic over the default network, which is mobile
 * data whenever the boat hotspot has no internet. Navionics then cannot reach
 * 192.168.4.1. This service reads the terminal over the boat network itself
 * and repeats the bytes on loopback, which no routing decision touches.
 */
class BoatRelayService : Service() {
    companion object {
        const val PORT = 10110
        private const val CHANNEL = "boat_relay"
        private const val NOTIFICATION_ID = 4411
        private const val ACTION_STOP = "ca.lmyc.lmyc_app.STOP_RELAY"
        private const val EXTRA_IP = "ip"
        private const val EXTRA_SSID = "ssid"
        private const val EXTRA_PSK = "psk"
        private const val CONNECT_TIMEOUT_MS = 3000
        /** Longer than the terminal's 15 s heartbeat, so a quiet bus is not a drop. */
        private const val READ_TIMEOUT_MS = 25000
        private const val RETRY_MS = 2000L
        private const val CLIENT_QUEUE = 256
        private val UDP_PORTS = intArrayOf(10110, 2000)

        @Volatile var running = false
            private set
        @Volatile var onBoat = false
            private set
        @Volatile var upstream = false
            private set
        @Volatile var lastDataAt = 0L
            private set
        @Volatile var listening = false
            private set

        /** The member said no to Android's Connect dialog for the boat Wi-Fi. */
        @Volatile var declined = false
            private set
        val clients = CopyOnWriteArrayList<Client>()

        /**
         * With [ssid] and [psk] the service joins the boat Wi-Fi for this app
         * alone. Nothing is saved for the phone, and the phone's own Wi-Fi and
         * mobile data stay as they are; chart apps read 127.0.0.1 instead.
         */
        fun start(context: Context, ip: String, ssid: String?, psk: String?) {
            val intent = Intent(context, BoatRelayService::class.java)
                .putExtra(EXTRA_IP, ip)
                .putExtra(EXTRA_SSID, ssid)
                .putExtra(EXTRA_PSK, psk)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, BoatRelayService::class.java))
        }

        /** Milliseconds since the terminal last sent bytes, or -1 if never. */
        fun dataAgeMs(): Long =
            if (lastDataAt == 0L) -1 else SystemClock.elapsedRealtime() - lastDataAt
    }

    private lateinit var cm: ConnectivityManager
    @Volatile private var target: Inet4Address? = null
    @Volatile private var boatNetwork: Network? = null
    @Volatile private var stopping = false
    private var upstreamThread: Thread? = null
    private var serverThread: Thread? = null
    private var server: ServerSocket? = null
    @Volatile private var upstreamSocket: Socket? = null
    private var shownState = ""

    /** Keeps the boat Wi-Fi up. A network no request needs is torn down. */
    private val wifiCallback = object : ConnectivityManager.NetworkCallback() {
        override fun onLinkPropertiesChanged(network: Network, lp: LinkProperties) {
            val ip = target ?: return
            if (BoatWifi.onSubnet(lp, ip)) {
                boatNetwork = network
            } else if (boatNetwork == network) {
                lostBoat()
            }
            refresh()
        }

        override fun onLost(network: Network) {
            if (boatNetwork == network) lostBoat()
            refresh()
        }

        override fun onUnavailable() {
            declined = true
            refresh()
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        cm = getSystemService(ConnectivityManager::class.java)
        createChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            stopSelf()
            return START_NOT_STICKY
        }
        val ip = intent?.getStringExtra(EXTRA_IP)?.let { BoatWifi.parseIpv4(it) }
        if (ip != null) target = ip
        startInForeground()
        if (!running) {
            running = true
            stopping = false
            declined = false
            val builder = NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            val ssid = intent?.getStringExtra(EXTRA_SSID)
            val psk = intent?.getStringExtra(EXTRA_PSK)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && ssid != null && psk != null) {
                builder.setNetworkSpecifier(
                    WifiNetworkSpecifier.Builder().setSsid(ssid).setWpa2Passphrase(psk).build(),
                )
            }
            cm.requestNetwork(builder.build(), wifiCallback)
            serverThread = Thread(::serve, "relay-server").also { it.start() }
            upstreamThread = Thread(::readTerminal, "relay-upstream").also { it.start() }
        }
        refresh()
        // Restarted after Android kills it: the last address is still in target.
        return START_REDELIVER_INTENT
    }

    override fun onDestroy() {
        stopping = true
        running = false
        try {
            cm.unregisterNetworkCallback(wifiCallback)
        } catch (_: IllegalArgumentException) {
        }
        closeQuietly(server)
        closeQuietly(upstreamSocket)
        for (c in clients) c.close()
        clients.clear()
        upstreamThread?.interrupt()
        serverThread?.interrupt()
        onBoat = false
        upstream = false
        listening = false
        super.onDestroy()
    }

    private fun lostBoat() {
        boatNetwork = null
        closeQuietly(upstreamSocket)
    }

    /** Loopback listener. Each chart app gets its own writer and queue. */
    private fun serve() {
        while (!stopping) {
            try {
                ServerSocket().use { s ->
                    s.reuseAddress = true
                    s.bind(InetSocketAddress(InetAddress.getLoopbackAddress(), PORT))
                    server = s
                    listening = true
                    refresh()
                    while (!stopping) {
                        val socket = s.accept()
                        socket.tcpNoDelay = true
                        val client = Client(socket)
                        clients.add(client)
                        client.start()
                        refresh()
                    }
                }
            } catch (_: IOException) {
                listening = false
                refresh()
                // Port held by another app, or the server closed on stop.
                sleepQuietly(RETRY_MS)
            }
        }
    }

    /** One connection to the terminal over the boat network, kept open. */
    private fun readTerminal() {
        val buf = ByteArray(2048)
        val udp = LoopbackUdp()
        while (!stopping) {
            val network = boatNetwork
            val ip = target
            onBoat = network != null
            if (network == null || ip == null) {
                upstream = false
                refresh()
                sleepQuietly(RETRY_MS)
                continue
            }
            try {
                network.socketFactory.createSocket().use { socket ->
                    upstreamSocket = socket
                    socket.soTimeout = READ_TIMEOUT_MS
                    socket.connect(InetSocketAddress(ip, PORT), CONNECT_TIMEOUT_MS)
                    upstream = true
                    refresh()
                    val input = socket.getInputStream()
                    while (!stopping) {
                        val n = input.read(buf)
                        if (n < 0) break
                        lastDataAt = SystemClock.elapsedRealtime()
                        val chunk = buf.copyOf(n)
                        for (c in clients) c.offer(chunk)
                        udp.feed(chunk)
                    }
                }
            } catch (_: IOException) {
            }
            upstream = false
            upstreamSocket = null
            udp.reset()
            refresh()
            if (!stopping) sleepQuietly(RETRY_MS)
        }
        udp.close()
    }

    /**
     * UDP chart apps listen for the terminal's broadcasts, which arrive on a
     * network that belongs to this app alone. Each sentence is repeated as
     * one datagram to 127.0.0.1 on the terminal's two UDP ports.
     */
    private class LoopbackUdp {
        private val socket = try {
            DatagramSocket()
        } catch (_: IOException) {
            null
        }
        private val line = ByteArray(256)
        private var len = 0

        fun feed(bytes: ByteArray) {
            for (b in bytes) {
                if (b == '\n'.code.toByte()) {
                    if (len > 0) send(len)
                    len = 0
                } else if (len < line.size - 2) {
                    line[len++] = b
                } else {
                    len = 0 // longer than any NMEA sentence: drop it
                }
            }
        }

        private fun send(n: Int) {
            val s = socket ?: return
            var end = n
            if (line[end - 1] == '\r'.code.toByte()) end--
            line[end] = '\r'.code.toByte()
            line[end + 1] = '\n'.code.toByte()
            for (port in UDP_PORTS) {
                try {
                    s.send(DatagramPacket(line, end + 2, InetAddress.getLoopbackAddress(), port))
                } catch (_: IOException) {
                }
            }
        }

        fun reset() {
            len = 0
        }

        fun close() {
            socket?.close()
        }
    }

    inner class Client(private val socket: Socket) {
        private val queue = ArrayBlockingQueue<ByteArray>(CLIENT_QUEUE)
        @Volatile private var open = true

        fun start() {
            Thread({
                try {
                    val out = socket.getOutputStream()
                    while (open && !stopping) {
                        val chunk = queue.poll(1, TimeUnit.SECONDS) ?: continue
                        out.write(chunk)
                        out.flush()
                    }
                } catch (_: IOException) {
                } catch (_: InterruptedException) {
                }
                close()
            }, "relay-client").start()
        }

        /** A chart app that stops reading is dropped, not waited for. */
        fun offer(chunk: ByteArray) {
            if (!queue.offer(chunk)) close()
        }

        fun close() {
            if (!open) return
            open = false
            closeQuietly(socket)
            clients.remove(this)
            refresh()
        }
    }

    private fun state(): String = when {
        !listening -> "Starting"
        declined -> "The boat Wi-Fi was not allowed. Open the LMYC app."
        boatNetwork == null -> "Waiting for the boat Wi-Fi"
        !upstream -> "Waiting for the terminal"
        else -> {
            val n = clients.size
            "Receiving boat data. " + when (n) {
                0 -> "No chart app connected yet."
                1 -> "1 chart app connected."
                else -> "$n chart apps connected."
            }
        }
    }

    @Synchronized
    private fun refresh() {
        onBoat = boatNetwork != null
        val now = state()
        if (now == shownState || stopping) return
        shownState = now
        getSystemService(NotificationManager::class.java).notify(NOTIFICATION_ID, notification(now))
    }

    private fun startInForeground() {
        val n = notification(state())
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } else {
            startForeground(NOTIFICATION_ID, n)
        }
    }

    private fun notification(text: String): Notification {
        val open = PendingIntent.getActivity(
            this, 0,
            packageManager.getLaunchIntentForPackage(packageName),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val stop = PendingIntent.getService(
            this, 1,
            Intent(this, BoatRelayService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE,
        )
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(this, CHANNEL)
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(this)
        }
        return builder
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentTitle("Connected to the boat")
            .setContentText(text)
            .setStyle(Notification.BigTextStyle().bigText("$text\nChart apps: UDP or TCP, 127.0.0.1, port $PORT"))
            .setContentIntent(open)
            .setOngoing(true)
            .addAction(Notification.Action.Builder(null, "Stop", stop).build())
            .build()
    }

    private fun createChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val channel = NotificationChannel(CHANNEL, "Boat connection", NotificationManager.IMPORTANCE_LOW)
        channel.description = "Shown while chart apps can get the boat's data."
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    private fun closeQuietly(c: java.io.Closeable?) {
        try {
            c?.close()
        } catch (_: IOException) {
        }
    }

    private fun sleepQuietly(ms: Long) {
        try {
            Thread.sleep(ms)
        } catch (_: InterruptedException) {
        }
    }
}
