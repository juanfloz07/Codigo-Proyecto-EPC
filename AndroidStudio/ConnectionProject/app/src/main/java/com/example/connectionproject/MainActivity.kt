package com.example.connectionproject

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import com.example.connectionproject.ui.theme.ConnectionProjectTheme
import java.io.BufferedReader
import java.io.IOException
import java.io.InputStreamReader
import java.util.UUID
import kotlin.concurrent.thread

class MainActivity : ComponentActivity() {

    private var bluetoothAdapter: BluetoothAdapter? = null
    private var bluetoothSocket: BluetoothSocket? = null
    private var readerThreadRunning = false

    private val hc05Uuid: UUID =
        UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        bluetoothAdapter = BluetoothAdapter.getDefaultAdapter()
        requestBluetoothPermissions()

        setContent {
            ConnectionProjectTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = Color(0xFF0A0F14)
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .background(
                                Brush.verticalGradient(
                                    colors = listOf(
                                        Color(0xFF07111A),
                                        Color(0xFF0A0F14),
                                        Color(0xFF111A22)
                                    )
                                )
                            )
                    ) {
                        ECGMonitorScreen(
                            onConnectClick = { onConnectToHC05() }
                        )
                    }
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        readerThreadRunning = false
        try {
            bluetoothSocket?.close()
        } catch (_: IOException) {
        }
    }

    private fun requestBluetoothPermissions() {
        val permissions = mutableListOf<String>()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT)
                != PackageManager.PERMISSION_GRANTED
            ) {
                permissions.add(Manifest.permission.BLUETOOTH_CONNECT)
            }

            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_SCAN)
                != PackageManager.PERMISSION_GRANTED
            ) {
                permissions.add(Manifest.permission.BLUETOOTH_SCAN)
            }
        }

        if (permissions.isNotEmpty()) {
            permissionLauncher.launch(permissions.toTypedArray())
        }
    }

    @SuppressLint("MissingPermission")
    private fun findHC05Device(): BluetoothDevice? {
        val adapter = bluetoothAdapter ?: return null
        val bondedDevices = adapter.bondedDevices ?: return null

        for (device in bondedDevices) {
            if (device.name == "HC-05") {
                return device
            }
        }
        return null
    }

    @SuppressLint("MissingPermission")
    private fun onConnectToHC05() {
        AppState.status = "Buscando HC-05..."

        val adapter = bluetoothAdapter
        if (adapter == null) {
            AppState.status = "Este celular no tiene Bluetooth"
            return
        }

        if (!adapter.isEnabled) {
            AppState.status = "Bluetooth apagado"
            return
        }

        val device = findHC05Device()
        if (device == null) {
            AppState.status = "HC-05 no emparejado"
            return
        }

        AppState.status = "Conectando a ${device.name}..."

        thread {
            try {
                readerThreadRunning = false
                bluetoothSocket?.close()

                bluetoothSocket = device.createRfcommSocketToServiceRecord(hc05Uuid)
                adapter.cancelDiscovery()
                bluetoothSocket?.connect()

                AppState.status = "Conectado a HC-05"
                startReadingData()

            } catch (e: IOException) {
                AppState.status = "Error al conectar"
                try {
                    bluetoothSocket?.close()
                } catch (_: IOException) {
                }
            }
        }
    }

    private fun startReadingData() {
        val socket = bluetoothSocket ?: return
        if (readerThreadRunning) return

        readerThreadRunning = true

        thread {
            try {
                val reader = BufferedReader(InputStreamReader(socket.inputStream))

                while (readerThreadRunning) {
                    val line = reader.readLine() ?: break
                    val cleanLine = line.trim()

                    if (cleanLine.isEmpty()) continue

                    val value = cleanLine.toIntOrNull()
                    if (value != null) {
                        runOnUiThread {
                            AppState.lastValue = value

                            if (value == -1) {
                                AppState.contactText = "Sin contacto"
                            } else {
                                AppState.contactText = "Contacto OK"
                                AppState.addPoint(value)
                            }
                        }
                    }
                }

                AppState.status = "Conexión cerrada"
            } catch (e: IOException) {
                AppState.status = "Error leyendo datos"
            } finally {
                readerThreadRunning = false
                try {
                    bluetoothSocket?.close()
                } catch (_: IOException) {
                }
            }
        }
    }
}

object AppState {
    var status by mutableStateOf("Sin conexión")
    var lastValue by mutableStateOf(0)
    var contactText by mutableStateOf("Sin datos")
    var graphVersion by mutableStateOf(0)

    private const val  maxPoints = 30
    val points = mutableListOf<Int>()

    fun addPoint(value: Int) {
        points.add(value)
        if (points.size > maxPoints) {
            points.removeAt(0)
        }
        graphVersion++
    }
}

@androidx.compose.runtime.Composable
fun ECGMonitorScreen(onConnectClick: () -> Unit) {
    val status = AppState.status
    val lastValue = AppState.lastValue
    val contactText = AppState.contactText
    val graphVersion = AppState.graphVersion

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(20.dp)
            .statusBarsPadding(),
        verticalArrangement = Arrangement.Top
    ) {
        Text(
            text = "Monitor ECG Bluetooth",
            modifier = Modifier.fillMaxWidth(),
            style = MaterialTheme.typography.headlineSmall,
            color = Color(0xFF18BBDB),
            textAlign = TextAlign.Center,
            fontSize = 28.sp,
            fontWeight = FontWeight.Bold
        )

        Spacer(modifier = Modifier.height(16.dp))

        Text(text = "Estado: $status",
            color = Color(0xFFEAEAEA))
        Text(text = "Último valor: $lastValue",
            color = Color(0xFFEAEAEA))
        Text(text = "Señal: $contactText",
            color = Color(0xFFEAEAEA))

        Spacer(modifier = Modifier.height(16.dp))

        ECGCanvas(graphVersion = graphVersion)

        Spacer(modifier = Modifier.height(24.dp))

        Button(
            onClick = onConnectClick,
            modifier = Modifier.align(Alignment.CenterHorizontally),
            colors = ButtonDefaults.buttonColors(
                containerColor = Color(0xFF18BBDB),
                contentColor = Color(0xFF1B1B1B)
            )
        ) {
            Text(text = "Conectar HC-05")
        }
    }
}
@androidx.compose.runtime.Composable
fun ECGCanvas(graphVersion: Int) {
    val points = AppState.points.toList()

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .height(420.dp)
            .background(
                color = Color(0xFF101010),
                shape = RoundedCornerShape(12.dp)
            )
            .padding(8.dp)
    ) {
        if (points.size < 2) {
            Text(
                text = "Esperando señal ECG...",
                color = Color.White,
                modifier = Modifier.padding(12.dp)
            )
            return@Column
        }

        Canvas(
            modifier = Modifier
                .fillMaxSize()
        ) {
            val localVersion = graphVersion

            val visiblePoints = points.takeLast(40)

            val minVal = 0f
            val maxVal = 750f
            val range = maxVal - minVal

            val stepX = size.width / (points.size - 1).coerceAtLeast(1)

            for (i in 0 until points.size - 1) {
                val x1 = i * stepX
                val x2 = (i + 1) * stepX

                val y1 = size.height - ((points[i] - minVal) / range) * size.height
                val y2 = size.height - ((points[i + 1] - minVal) / range) * size.height

                drawLine(
                    color = Color(0xFF00FF66),
                    start = Offset(x1, y1),
                    end = Offset(x2, y2),
                    strokeWidth = 4f
                )
            }
        }
    }
}
