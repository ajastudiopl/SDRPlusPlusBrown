package org.sdrppbrown.sdrppbrown

import android.Manifest
import android.annotation.SuppressLint
import android.app.NativeActivity
import android.app.PendingIntent
import android.app.PendingIntent.FLAG_MUTABLE
import android.bluetooth.BluetoothSocket.TYPE_SCO
import android.content.ActivityNotFoundException
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ActivityInfo
import android.content.pm.PackageManager
import android.content.res.AssetManager
import android.database.ContentObserver
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.media.AudioManager
import android.media.ToneGenerator
import android.net.Uri
import android.os.BatteryManager
import android.os.BatteryManager.BATTERY_PROPERTY_CAPACITY
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.os.VibrationEffect
import android.os.Vibrator
import android.provider.Settings
import android.util.Log
import android.view.KeyEvent
import android.view.View
import android.view.inputmethod.InputMethodManager
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.core.content.PermissionChecker
import java.io.BufferedReader
import java.io.File
import java.io.FileOutputStream
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale
import java.util.concurrent.LinkedBlockingQueue


private const val ACTION_USB_PERMISSION = "org.sdrppbrown.sdrppbrown.USB_PERMISSION";

class MainActivity : NativeActivity(), SensorEventListener {
    private lateinit var audioManager: AudioManager
    private lateinit var toneG: ToneGenerator
    private lateinit var thisCacheDir: String
    private val TAG: String = "SDR++Brown";
    public var usbManager: UsbManager? = null;
    public var SDR_device: UsbDevice? = null;
    public var SDR_conn: UsbDeviceConnection? = null;
    public var SDR_VID: Int = -1;
    public var SDR_PID: Int = -1;
    public var SDR_FD: Int = -1;
    public var density: Float = 1.0f;
    var batteryManager: BatteryManager? = null;

    private val usbReceiver = object : BroadcastReceiver() {
        var devList: HashMap<String, UsbDevice> = HashMap<String, UsbDevice>();
        var devListRequested = 1;

        override fun onReceive(context: Context, intent: Intent) {
            Log.w("SDR++", intent.action.toString())
            if (ACTION_USB_PERMISSION == intent.action) {
                Log.w("SDR++", "USB Permission on receive")
                synchronized(this) {
                    var _this = context as MainActivity;
//                _this.SDR_device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    val dev: UsbDevice? = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
                    _this.usbPermissionPendingUntilMs = 0
                    if (dev != null && intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                        Log.w("SDR++", "USB Permission granted")
                        _this.openSdrDevice(dev)
                    } else {
                        Log.w("SDR++", "USB Permission non-granted")
                        if (dev != null) { _this.usbPermissionDenied.add(dev.deviceName) }
                    }

                    // Whatever the hell this does
                    context.unregisterReceiver(this);

                    // Hide again the system bars
                    _this.hideSystemBars();
                }

//                devListRequested++;
//                if (devListRequested <= devList.size) {
//                    usbManager!!.requestPermission(devList[devListRequested-1], makePermissionIntent());
//                }

            }
        }
    }

    val REQUIRED_PERMISSIONS = arrayOf(
        Manifest.permission.WRITE_EXTERNAL_STORAGE,
        Manifest.permission.READ_EXTERNAL_STORAGE,
        Manifest.permission.RECORD_AUDIO,
        Manifest.permission.INTERNET,
        Manifest.permission.BATTERY_STATS,
        Manifest.permission.HIGH_SAMPLING_RATE_SENSORS);
    val PERMISSION_REQUEST_CODE = 1001;

    fun checkAndAsk(permission: String) {
        if (PermissionChecker.checkSelfPermission(
                this,
                permission
            ) != PermissionChecker.PERMISSION_GRANTED
        ) {
            ActivityCompat.requestPermissions(this, arrayOf(permission), 1);
        }
    }

    public fun hideSystemBars() {
        val decorView = getWindow().getDecorView();
        val uiOptions = View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
        decorView.setSystemUiVisibility(uiOptions);
    }

    var playStarted = 0L

    var recentz = ArrayList<Float>();

    var compx : Float = 0.0f
    var compy : Float = 0.0f
    var compz : Float = 0.0f
    var accx : Float = 0.0f
    var accy : Float = 0.0f
    var accz : Float = 0.0f

    override fun onSensorChanged(event: SensorEvent?) {
        if (event?.sensor?.type == Sensor.TYPE_MAGNETIC_FIELD) {
            compx = event.values[0]
            compy = event.values[1]
            compz = event.values[2]
//            Log.w("SDR++", "Sensors: $accx $accy $accz $compx $compy $compz")
        }
        if (event?.sensor?.type == Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED) {
            compx = event.values[0]
            compy = event.values[1]
            compz = event.values[2]
//            Log.w("SDR++", "Sensors: $accx $accy $accz $compx $compy $compz")
        }
        if (event?.sensor?.type == Sensor.TYPE_ACCELEROMETER) {
            accx = event.values[0]
            accy = event.values[1]
            accz = event.values[2]
            if (recentz.size > 0) {
                val prev = recentz[recentz.size-1]
                if (Math.abs(prev) < 0.5 ) {
                    if (accz - prev > 0.5) {
                        if (System.currentTimeMillis() > playStarted + 70) {
                            playStarted = System.currentTimeMillis()
//                            toneG.startTone(ToneGenerator.TONE_DTMF_1, 50)
                        }
                    }
                    if (accz - prev < -0.5) {
                        if (System.currentTimeMillis() > playStarted + 170) {
                            playStarted = System.currentTimeMillis()
//                            toneG.startTone(ToneGenerator.TONE_DTMF_2, 150)
                        }
                    }
                }
            }
            recentz.add(accz);
            if (recentz.size > 50) {
                recentz.removeAt(0);
            }

//            Log.w("SDR++", "Sensors: $accx $accy $accz $compx $compy $compz")


            // handle accelerometer values
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {
    }

    private lateinit var sensorManager: SensorManager
    private var accelerometer: Sensor? = null
    private var compass: Sensor? = null

    override fun onDestroy() {
        super.onDestroy()
        sensorManager.unregisterListener(this)
        contentResolver.unregisterContentObserver(autoRotateObserver)
        unregisterReceiver(usbDetachReceiver)
    }

    fun proceedWithPermissions() {
        Log.w("SDR++", "PERM: Proceed with permissions...")
        if (REQUIRED_PERMISSIONS.all {
            ContextCompat.checkSelfPermission(baseContext, it) == PackageManager.PERMISSION_GRANTED
        }) {
            Log.w("SDR++", "PERM: All REQUIRED_PERMISSIONS passed..")
            this.permissionsPassed.add(PERMISSION_REQUEST_CODE);
        }
        if (permissionsPassed.contains(PERMISSION_REQUEST_CODE)) {
            Log.w("SDR++", "PERM: Requesting REQUIRED_PERMISSIONS..")
            ActivityCompat.requestPermissions(this, REQUIRED_PERMISSIONS, PERMISSION_REQUEST_CODE);
            return;
        }
        if (!permissionsPassed.contains(PERMISSION_REQUEST_CODE+1)) {
            Log.w("SDR++", "PERM: Requesting USB permissions..")
            // USB stuff
            usbManager = getSystemService(USB_SERVICE) as UsbManager;
            val filter = IntentFilter(ACTION_USB_PERMISSION)
            registerReceiver(usbReceiver, filter)


            // Get permission for all USB devices
            usbReceiver.devList = usbManager!!.deviceList;
            Log.w("SDR++", "PERM: Dev list size: " + usbReceiver.devList.size.toString())

            val permissionIntent = makeUsbPermissionIntent()

            for ((name, dev) in usbReceiver.devList) {
                Log.w("SDR++", "PERM: Dev list item: $name $dev")
                // ignore hubs, keyboards, storage and other devices that cannot be an SDR.
                if (!looksLikeSdr(dev))
                    continue
                Log.w("SDR++", "PERM: Req USB perm: $dev")
                usbPermissionPendingUntilMs = System.currentTimeMillis() + 60000
                usbManager!!.requestPermission(dev, permissionIntent);
                return
            }
            Log.w("SDR++", "PERM: End req all USB perm:")
            permissionsPassed.add(PERMISSION_REQUEST_CODE+1)
        }

        if (!permissionsPassed.contains(PERMISSION_REQUEST_CODE+2)) {
            // filesystem crap
            Log.w("SDR++", "PERM: Req ext storage.. testing")
            val externalStorageDirectory = Environment.getExternalStorageDirectory()
            val testFile = File(externalStorageDirectory, "sdrppbrown.test")
            Log.w("SDR++", "PERM Trying Test file: $testFile")
            var success = false;
            try {
                testFile.delete()
                success = testFile.createNewFile()
            } catch (e: java.lang.Exception) {
                Log.w("SDR++", "PERM Test file create: Exception: ${e.message}")
                // no luck
            }
            if (success) {
                testFile.delete()
            } else {
                Log.w("SDR++", "PERM: Requesting ext storage.")
                try {
                    val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                    val uri: Uri = Uri.fromParts("package", packageName, null)
                    intent.data = uri
                    startActivityForResult(intent, PERMISSION_REQUEST_CODE+2);
                    return;
                } catch (e: ActivityNotFoundException) {
                    Log.w("SDR++", "PERM: MANAGE_APP_ALL_FILES_ACCESS_PERMISSION not available, skipping");
                }
            }
            permissionsPassed.add(PERMISSION_REQUEST_CODE+2);

        }


    }

    var permissionsPassed = HashSet<Int>();

    var autoPermissionProceed = true;

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        if (requestCode > PERMISSION_REQUEST_CODE && requestCode < PERMISSION_REQUEST_CODE + 10) {
            Log.w("SDR++", "PERM: got perm response: $requestCode")
            this.permissionsPassed.add(requestCode);
        }
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (autoPermissionProceed) {
            Log.w("SDR++", "PERM: auto proceed")
            proceedWithPermissions();
        }
    }

    public override fun onCreate(savedInstanceState: Bundle?) {
        // Hide bars
        hideSystemBars();

        // temporary directory for C++
        val cacheDir = cacheDir
        this.thisCacheDir = cacheDir.absolutePath

        sensorManager = getSystemService(Context.SENSOR_SERVICE) as SensorManager
        accelerometer = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
        accelerometer?.let { sensorManager.registerListener(this, it, SensorManager.SENSOR_DELAY_FASTEST) }

        compass = sensorManager.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED)
        if (compass != null) {
            sensorManager.registerListener(this, compass, SensorManager.SENSOR_DELAY_FASTEST)
        } else {
            compass = sensorManager.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD)
            if (compass != null) {
                sensorManager.registerListener(this, compass, SensorManager.SENSOR_DELAY_FASTEST)
            }
        }
        compass?.let { sensorManager.registerListener(this, it, SensorManager.SENSOR_DELAY_FASTEST) }
        compass = sensorManager.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD_UNCALIBRATED)
        compass?.let { sensorManager.registerListener(this, it, SensorManager.SENSOR_DELAY_FASTEST) }

        toneG = ToneGenerator(AudioManager.STREAM_ACCESSIBILITY, 100)




        val dm = resources.displayMetrics
        density = dm.density;
        Log.w("SDR++", "Display density: ${dm.density} scaled: ${dm.scaledDensity} densityDpi: ${dm.densityDpi} width: ${dm.widthPixels} height: ${dm.heightPixels} xdpi: ${dm.xdpi} ydpi: ${dm.ydpi} widthDp: ${dm.widthPixels/dm.density} heightDp: ${dm.heightPixels/dm.density}")

        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE;
        contentResolver.registerContentObserver(Settings.System.getUriFor(Settings.System.ACCELEROMETER_ROTATION), false, autoRotateObserver)

        //// cont here



        super.onCreate(savedInstanceState)


        registerReceiver(this.batteryBroadcastReceiver, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
        registerReceiver(usbDetachReceiver, IntentFilter(UsbManager.ACTION_USB_DEVICE_DETACHED))

        batteryManager = getSystemService(BATTERY_SERVICE) as BatteryManager

        scanAudioDevices()

        proceedWithPermissions();
    }

    fun scanAudioDevices() : String {
        audioManager = getSystemService(AUDIO_SERVICE) as AudioManager
        val adi = audioManager.getDevices(AudioManager.GET_DEVICES_ALL)
        _audioSinkIds = "";
        _audioSourceIds = "";
        adi.forEach {
            if (it.isSink)
                _audioSinkIds += "${it.id}\t${it.type}\t${it.productName}\t"
            if (it.isSource)
                _audioSourceIds += "${it.id}\t${it.type}\t${it.productName}\t"
        }
        if (_audioSinkIds.isNotEmpty()) _audioSinkIds =
            _audioSinkIds.substring(0, _audioSinkIds.length - 1)
        if (_audioSourceIds.isNotEmpty()) _audioSourceIds =
            _audioSourceIds.substring(0, _audioSourceIds.length - 1)
        return "OK";
    }

    var _audioSinkIds = "";
    var _audioSourceIds = "";

    fun getAudioSinkIds() = _audioSinkIds;
    fun getAudioSourceIds() = _audioSourceIds;

    fun startBtSco() : String {
        audioManager.isBluetoothScoOn = true
        audioManager.startBluetoothSco()
        audioManager.isMicrophoneMute = false;
        audioManager.mode = AudioManager.MODE_IN_COMMUNICATION
        return "OK";
    }

    fun stopBtSco() : String {
        audioManager.isBluetoothScoOn = false
        audioManager.stopBluetoothSco()
        audioManager.mode = AudioManager.MODE_NORMAL;
        return "OK";
    }

    var batteryStatusStr: String = "?";

    private val batteryBroadcastReceiver: BroadcastReceiver = object : BroadcastReceiver() {
        @SuppressLint("SetTextI18n")
        override fun onReceive(context: Context?, batteryStatus: Intent) {
            val level = batteryStatus?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
            val scale = batteryStatus?.getIntExtra(BatteryManager.EXTRA_SCALE, -1) ?: -1

            // Handle cases when valid level isn't available
            if (level == -1 || scale == -1) {
                batteryStatusStr = "??"
                return
            }

            val batteryPercentage = level / scale.toFloat() * 100.0f
            batteryStatusStr = String.format("%d", batteryPercentage.toInt())
        }
    }

    // Puts a text file in the public Downloads folder (visible in Files and in the Drive upload picker); "" = done, otherwise the reason
    public fun saveToDownloads(name: String, text: String): String {
        try {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) { return "Saving to Downloads needs Android 10 or newer" }
            val resolver = contentResolver
            val collection = android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI
            var uri: Uri? = null
            resolver.query(collection, arrayOf(android.provider.MediaStore.Downloads._ID), android.provider.MediaStore.Downloads.DISPLAY_NAME + "=?", arrayOf(name), null)?.use { c ->
                if (c.moveToFirst()) { uri = android.content.ContentUris.withAppendedId(collection, c.getLong(0)) }
            }
            if (uri == null) {
                val values = android.content.ContentValues()
                values.put(android.provider.MediaStore.Downloads.DISPLAY_NAME, name)
                values.put(android.provider.MediaStore.Downloads.MIME_TYPE, "application/json")
                values.put(android.provider.MediaStore.Downloads.RELATIVE_PATH, "Download")
                uri = resolver.insert(collection, values)
            }
            if (uri == null) { return "Could not create the file" }
            resolver.openOutputStream(uri!!, "wt")?.use { it.write(text.toByteArray(Charsets.UTF_8)) }
            return ""
        } catch (e: Exception) {
            return e.toString()
        }
    }

    public fun httpGet(url: String): String {
        var response = ""
        var connection : HttpURLConnection? = null
        try {
            connection = URL(url).openConnection() as HttpURLConnection
            connection.requestMethod = "GET"
            val responseCode = connection.responseCode
            if (responseCode == HttpURLConnection.HTTP_OK) {
                val reader = BufferedReader(InputStreamReader(connection.inputStream))
                response = reader.readText()
                reader.close()
            } else {
                throw Exception("HTTP error code: $responseCode")
            }
        } catch (e: RuntimeException) {
            response = "#ERROR: + "+e.toString();
        } finally {
            connection?.disconnect()
        }
        return response
    }

    private fun makeUsbPermissionIntent(): PendingIntent? {
        try {
            return PendingIntent.getBroadcast(
                this@MainActivity,
                PERMISSION_REQUEST_CODE+1,
                Intent(ACTION_USB_PERMISSION),
                0
            )
        } catch (e: Exception) {
            return PendingIntent.getBroadcast(
                this@MainActivity,
                PERMISSION_REQUEST_CODE+1,
                Intent(ACTION_USB_PERMISSION),
                FLAG_MUTABLE
            )
        }
    }

    // The SDR was unplugged: forget its file descriptor so the source modules don't reuse it.
    // The old connection is left open on purpose, native code may still hold its fd until the source is stopped.
    private val usbDetachReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (UsbManager.ACTION_USB_DEVICE_DETACHED != intent.action) { return }
            val dev: UsbDevice? = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
            if (dev != null) { forgetUsbDevice(dev.deviceName) }
            if (dev == null || SDR_device == null || dev.deviceName != SDR_device!!.deviceName) { return }
            Log.w("SDR++", "USB SDR detached: ${dev.deviceName}")
            SDR_FD = -1
            SDR_VID = -1
            SDR_PID = -1
        }
    }

    // Some phones (e.g. Galaxy A23, Android 14) leave a vendor-class device unconfigured
    // (bConfigurationValue = 0); libusb then fails with LIBUSB_ERROR_NOT_FOUND. Select config 1.
    fun ensureConfigured(dev: UsbDevice, conn: UsbDeviceConnection) {
        try {
            val cur = ByteArray(1)
            val r = conn.controlTransfer(0x80, 0x08, 0, 0, cur, 1, 1000)
            if (r == 1 && cur[0].toInt() == 0 && dev.configurationCount > 0) {
                val ok = conn.setConfiguration(dev.getConfiguration(0))
                Log.w("SDR++", "USB device was unconfigured, setConfiguration: $ok")
            }
        } catch (e: Exception) {
            Log.w("SDR++", "USB configuration check failed: ${e.message}")
        }
    }

    // Open connections by device name. A connection that is dropped gets closed by the garbage collector,
    // so the ones of unplugged devices are kept: native code may still hold their fd.
    private val usbConns = HashMap<String, UsbDeviceConnection>()
    private val usbRetiredConns = ArrayList<UsbDeviceConnection>()
    private val usbPermissionDenied = HashSet<String>()
    private var usbPermissionPendingUntilMs: Long = 0

    @Synchronized
    private fun forgetUsbDevice(name: String) {
        usbPermissionDenied.remove(name)
        usbConns.remove(name)?.let { usbRetiredConns.add(it) }
    }

    @Synchronized
    private fun openSdrDevice(dev: UsbDevice): Boolean {
        val conn = usbConns[dev.deviceName] ?: usbManager!!.openDevice(dev)
        if (conn == null) {
            Log.w("SDR++", "USB openDevice failed: ${dev.deviceName}")
            return false
        }
        usbConns[dev.deviceName] = conn
        ensureConfigured(dev, conn)
        SDR_device = dev
        SDR_conn = conn
        SDR_VID = dev.getVendorId()
        SDR_PID = dev.getProductId()
        SDR_FD = conn.getFileDescriptor()
        Log.w("SDR++", "USB SDR opened: ${dev.deviceName} fd=$SDR_FD")
        return true
    }

    // SDRs use the vendor specific class; hubs, keyboards, storage and serial ports don't.
    private fun looksLikeSdr(dev: UsbDevice): Boolean {
        if (dev.deviceClass == UsbConstants.USB_CLASS_VENDOR_SPEC) { return true }
        return (0 until dev.interfaceCount).any { dev.getInterface(it).interfaceClass == UsbConstants.USB_CLASS_VENDOR_SPEC }
    }

    // allowed: (vid << 16) | pid of the devices the caller can use, empty = anything that looks like an SDR.
    private fun usbMatches(dev: UsbDevice, allowed: IntArray): Boolean {
        if (allowed.isEmpty()) { return looksLikeSdr(dev) }
        return allowed.contains((dev.vendorId shl 16) or dev.productId)
    }

    // Called from native code (Display menu): 0 = landscape, 1 = sensor (default), 2 = portrait.
    fun setScreenOrientation(mode: Int) {
        screenOrientationMode = mode
        applyScreenOrientation()
    }

    // Auto mode follows the sensor only while the system auto-rotate is on.
    // With the system rotation lock on, the screen stays where it is.
    private fun applyScreenOrientation() {
        runOnUiThread {
            requestedOrientation = when (screenOrientationMode) {
                1 -> if (isAutoRotateOn()) ActivityInfo.SCREEN_ORIENTATION_SENSOR else ActivityInfo.SCREEN_ORIENTATION_LOCKED
                2 -> ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                else -> ActivityInfo.SCREEN_ORIENTATION_USER_LANDSCAPE
            }
        }
    }

    private fun isAutoRotateOn(): Boolean =
        Settings.System.getInt(contentResolver, Settings.System.ACCELEROMETER_ROTATION, 1) == 1

    private var screenOrientationMode = 1
    private val autoRotateObserver = object : ContentObserver(Handler(Looper.getMainLooper())) {
        override fun onChange(selfChange: Boolean) { applyScreenOrientation() }
    }

    // Called from native code (backend::getDeviceFD, e.g. a source's Refresh or Play).
    // Keeps SDR_FD on a device the caller can use: if the current one is gone or of another kind,
    // look for a matching one. Open it when we already have permission, otherwise ask once
    // (a refused device is asked again after a Refresh or a re-plug). Returns SDR_FD.
    @Synchronized
    fun rescanUsb(allowed: IntArray): Int {
        try {
            if (usbManager == null) { usbManager = getSystemService(USB_SERVICE) as UsbManager }
            val devices = usbManager!!.deviceList
            for (name in usbConns.keys.filter { !devices.containsKey(it) }) { forgetUsbDevice(name) }
            val current = SDR_device
            if (SDR_FD >= 0 && (current == null || !devices.containsKey(current.deviceName))) {
                Log.w("SDR++", "USB SDR no longer present, forgetting fd")
                SDR_FD = -1
                SDR_VID = -1
                SDR_PID = -1
            }
            if (SDR_FD >= 0 && current != null && usbMatches(current, allowed)) { return SDR_FD }
            for ((name, dev) in devices) {
                if (!usbMatches(dev, allowed)) { continue }
                if (usbManager!!.hasPermission(dev)) {
                    if (openSdrDevice(dev)) { return SDR_FD }
                } else if (!usbPermissionDenied.contains(name)) {
                    val now = System.currentTimeMillis()
                    if (now >= usbPermissionPendingUntilMs) {
                        usbPermissionPendingUntilMs = now + 60000
                        Log.w("SDR++", "USB rescan: requesting permission for $name")
                        runOnUiThread {
                            try { registerReceiver(usbReceiver, IntentFilter(ACTION_USB_PERMISSION)) } catch (e: Exception) { }
                            usbManager!!.requestPermission(dev, makeUsbPermissionIntent())
                        }
                    }
                    return SDR_FD
                }
            }
        } catch (e: Exception) {
            Log.w("SDR++", "USB rescan failed: ${e.message}")
        }
        return SDR_FD
    }

    // Called from native code: what the phone sees on USB, one device per line, empty = nothing.
    @Synchronized
    fun getUsbStatus(): String {
        try {
            if (usbManager == null) { usbManager = getSystemService(USB_SERVICE) as UsbManager }
            return usbManager!!.deviceList.values.joinToString("\n") { dev ->
                val state = if (usbManager!!.hasPermission(dev)) "ready"
                    else if (usbPermissionDenied.contains(dev.deviceName)) "permission refused" else "no permission"
                String.format(Locale.US, "%04x:%04x %s", dev.vendorId, dev.productId, state)
            }
        } catch (e: Exception) {
            return "USB status failed: ${e.message}"
        }
    }

    // Called from native code (a source's Refresh): ask again for devices that were refused.
    @Synchronized
    fun retryUsbPermission(): String {
        usbPermissionDenied.clear()
        usbPermissionPendingUntilMs = 0
        return ""
    }

    // With launchMode=singleTask a re-plugged SDR is delivered here instead of starting a second
    // NativeActivity, which the native side can't handle (black screen).
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        if (UsbManager.ACTION_USB_DEVICE_ATTACHED != intent.action) { return }
        val dev: UsbDevice = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE) ?: return
        Log.w("SDR++", "USB SDR attached: ${dev.deviceName}")
        if (usbManager == null) { usbManager = getSystemService(USB_SERVICE) as UsbManager }
        if (usbManager!!.hasPermission(dev)) {
            openSdrDevice(dev)
        } else {
            usbPermissionPendingUntilMs = System.currentTimeMillis() + 60000
            registerReceiver(usbReceiver, IntentFilter(ACTION_USB_PERMISSION))
            usbManager!!.requestPermission(dev, makeUsbPermissionIntent())
        }
        hideSystemBars()
    }

    fun getThisCacheDir(): String {
        return thisCacheDir;
    }

    fun getDisplayDensityStr(): String {
        return this.density.toString();
    }


    public override fun onResume() {
        // Hide bars again
        hideSystemBars();
        super.onResume();
    }

    fun showSoftInput() {
        val inputMethodManager = getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager;
        inputMethodManager.showSoftInput(window.decorView, 0);
    }

    fun hideSoftInput() {
        val inputMethodManager = getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager;
        inputMethodManager.hideSoftInputFromWindow(window.decorView.windowToken, 0);
        hideSystemBars();
    }

    // Queue for the Unicode characters to be polled from native code (via pollUnicodeChar())
    private var unicodeCharacterQueue: LinkedBlockingQueue<Int> = LinkedBlockingQueue()

    // We assume dispatchKeyEvent() of the NativeActivity is actually called for every
    // KeyEvent and not consumed by any View before it reaches here
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.action == KeyEvent.ACTION_DOWN) {
            unicodeCharacterQueue.offer(event.getUnicodeChar(event.metaState))
        }
        return super.dispatchKeyEvent(event)
    }

    fun pollUnicodeChar(): Int {
        return unicodeCharacterQueue.poll() ?: 0
    }

    public fun createIfDoesntExist(path: String) {
        // This is a directory, create it in the filesystem
        var folder = File(path);
        var success = true;
        if (!folder.exists()) {
            success = folder.mkdirs();
        }
        if (!success) {
            Log.e(TAG, "Could not create folder with path " + path);
        }
    }

    public fun extractDir(aman: AssetManager, local: String, rsrc: String): Int {
        val flist = aman.list(rsrc);
        var ecount = 0;
        if (flist != null) {
            for (fp in flist) {
                val lpath = local + "/" + fp;
                val rpath = rsrc + "/" + fp;

                Log.w(TAG, "Extracting '" + rpath + "' to '" + lpath + "'");

                // Create local path if non-existent
                createIfDoesntExist(local);

                // Create if directory
                val ext = extractDir(aman, lpath, rpath);

                // Extract if file
                if (ext == 0) {
                    // This is a file, extract it
                    val _os = FileOutputStream(lpath);
                    val _is = aman.open(rpath);
                    val ilen = _is.available();
                    var fbuf = ByteArray(ilen);
                    _is.read(fbuf, 0, ilen);
                    _os.write(fbuf);
                    _os.close();
                    _is.close();
                }

                ecount++;
            }
        }
        return ecount;
    }

    fun getArchitectureString(): String {
        return System.getProperty("os.arch");
    }

    fun getAppDir(): String {
        val fdir = getFilesDir().getAbsolutePath();

        // Extract all resources to the app directory
        val aman = getAssets();
        extractDir(aman, fdir + "/res", "res");
        createIfDoesntExist(fdir + "/modules");

        return fdir;
    }

    fun getBatteryLevel(): String {
        return batteryManager?.getIntProperty(BATTERY_PROPERTY_CAPACITY)?.toString()?:"n/a";
//        return batteryStatusStr;
    }


    fun doPermissionsDialogs() {
        autoPermissionProceed = false;
        proceedWithPermissions();
    }
    fun performHapticFeedback() {
        val vibrator = getSystemService(VIBRATOR_SERVICE) as Vibrator
        if (vibrator != null) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                vibrator.vibrate(
                    VibrationEffect.createOneShot(
                        100,
                        VibrationEffect.DEFAULT_AMPLITUDE
                    )
                )
            } else {
                // Deprecated in API 26
                vibrator.vibrate(100)
            }
        }
    }
}
