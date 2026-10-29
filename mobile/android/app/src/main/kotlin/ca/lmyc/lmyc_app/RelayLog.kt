package ca.lmyc.lmyc_app

import android.content.Context
import android.util.Log
import java.io.File
import java.io.FileWriter
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/**
 * The relay's own diary, for the bench: Wi-Fi requests and callbacks, the
 * terminal link, chart apps connecting and leaving, and every 30 s what was
 * forwarded (sentences per type, bytes, clients). Each line also goes to
 * logcat under the tag LmycRelay.
 *
 * The file is relay-log.txt in the app's external files directory, which
 * `adb pull /sdcard/Android/data/ca.lmyc.lmyc_app/files/` reads without
 * root (tools/boat_diag.py does), with relay-log.1.txt the previous 512 KB.
 */
object RelayLog {
    const val TAG = "LmycRelay"
    private const val MAX_BYTES = 512 * 1024L
    private var file: File? = null
    private val stamp = SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'", Locale.US).apply {
        timeZone = TimeZone.getTimeZone("UTC")
    }

    private val counts = HashMap<String, Int>()
    private var bytes = 0L
    private var lastSummary = 0L
    private var lastSample = ""

    @Synchronized
    fun init(context: Context) {
        if (file != null) return
        val dir = context.getExternalFilesDir(null) ?: context.filesDir
        file = File(dir, "relay-log.txt")
    }

    @Synchronized
    fun event(text: String) {
        Log.i(TAG, text)
        val f = file ?: return
        try {
            if (f.length() > MAX_BYTES) {
                val old = File(f.parentFile, "relay-log.1.txt")
                old.delete()
                f.renameTo(old)
            }
            FileWriter(f, true).use { it.write("${stamp.format(Date())} $text\n") }
        } catch (_: IOException) {
        }
    }

    /** One NMEA line on its way to chart apps. */
    @Synchronized
    fun sentence(line: ByteArray, n: Int) {
        bytes += n + 2
        if (n >= 6) {
            val key = String(line, 0, 6, Charsets.US_ASCII)
            counts[key] = (counts[key] ?: 0) + 1
            if (key.endsWith("RMC")) lastSample = String(line, 0, n, Charsets.US_ASCII)
        }
    }

    /** Every 30 s: what went to 127.0.0.1 since the last summary. */
    fun maybeSummary(nowMs: Long, clients: Int) {
        val text: String
        synchronized(this) {
            if (nowMs - lastSummary < 30000) return
            lastSummary = nowMs
            val types = counts.entries.sortedBy { it.key }.joinToString(" ") { "${it.key}=${it.value}" }
            text = "forwarded 30s: bytes=$bytes clients=$clients ${types.ifEmpty { "no sentences" }}" +
                if (lastSample.isNotEmpty()) " last=$lastSample" else ""
            counts.clear()
            bytes = 0
            lastSample = ""
        }
        event(text)
    }
}
