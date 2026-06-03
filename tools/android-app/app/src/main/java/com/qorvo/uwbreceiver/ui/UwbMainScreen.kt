package com.qorvo.uwbreceiver.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.qorvo.uwbreceiver.data.CsvSample
import com.qorvo.uwbreceiver.data.LinkState
import com.qorvo.uwbreceiver.data.UwbUiState
import com.qorvo.uwbreceiver.ui.theme.SurfaceCard
import com.qorvo.uwbreceiver.ui.theme.SurfaceCardAlt
import com.qorvo.uwbreceiver.ui.theme.TextSecondary

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun UwbMainScreen(
    state: UwbUiState,
    onConnect: () -> Unit,
    onDisconnect: () -> Unit,
    onStartRecording: () -> Unit,
    onStopRecording: () -> Unit,
    onShare: () -> Unit,
) {
    val sample = state.runtime.latest

    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(horizontal = 14.dp, vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item {
            Text(
                text = "Leaps UWB CSV Logger",
                fontSize = 26.sp,
                fontWeight = FontWeight.ExtraBold,
            )
            Text(
                text = "Fixed firmware profile, USB OTG CSV acquisition",
                color = TextSecondary,
            )
        }

        item {
            CardBlock {
                Text("Accelerometer exchange", fontWeight = FontWeight.Bold)
                Text("CSV: ms,sample,iax,iay,iaz,rax,ray,raz", color = TextSecondary)

                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    CardBlock(modifier = Modifier.weight(1f)) {
                        Text("Initiator", fontWeight = FontWeight.Bold)
                        TripleValues(sample, true)
                    }
                    CardBlock(modifier = Modifier.weight(1f)) {
                        Text("Responder", fontWeight = FontWeight.Bold)
                        TripleValues(sample, false)
                    }
                }
            }
        }

        item {
            CardBlock {
                Text("Session", fontWeight = FontWeight.Bold)
                StatRow("Connection", state.runtime.linkState.name)
                StatRow("Status", state.runtime.status)
                StatRow("Hz", String.format("%.1f", state.runtime.hz))
                StatRow("Samples", state.runtime.samples.toString())
                StatRow("Duration", formatDuration(state.elapsedSec))
                StatRow("Recording", if (state.runtime.recording) "ON" else "OFF")
                StatRow("File", state.runtime.recordingName ?: "-")
                StatRow("Invalid lines", state.runtime.invalidLines.toString())
            }
        }

        item {
            CardBlock {
                Text("Controls", fontWeight = FontWeight.Bold)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = onConnect, modifier = Modifier.weight(1f)) { Text("Connect") }
                    Button(onClick = onDisconnect, modifier = Modifier.weight(1f)) { Text("Disconnect") }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = onStartRecording,
                        modifier = Modifier.weight(1f),
                        enabled = state.runtime.linkState == LinkState.CONNECTED && !state.runtime.recording,
                    ) { Text("Start Recording") }
                    Button(
                        onClick = onStopRecording,
                        modifier = Modifier.weight(1f),
                        enabled = state.runtime.recording,
                    ) { Text("Stop Recording") }
                }
                Button(
                    onClick = onShare,
                    enabled = state.runtime.lastSavedUri != null,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("Share last CSV")
                }
            }
        }
    }
}

@Composable
private fun CardBlock(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = SurfaceCard),
        shape = RoundedCornerShape(8.dp),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
            content = content,
        )
    }
}

@Composable
private fun TripleValues(sample: CsvSample?, initiator: Boolean) {
    val x = if (initiator) sample?.iax else sample?.rax
    val y = if (initiator) sample?.iay else sample?.ray
    val z = if (initiator) sample?.iaz else sample?.raz

    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text("X: ${x ?: "--"}")
        Text("Y: ${y ?: "--"}")
        Text("Z: ${z ?: "--"}")
    }
}

@Composable
private fun StatRow(label: String, value: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .background(SurfaceCardAlt)
            .padding(horizontal = 10.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(label, color = TextSecondary)
        Text(value, fontWeight = FontWeight.Medium)
    }
}

private fun formatDuration(sec: Long): String {
    val h = sec / 3600
    val m = (sec % 3600) / 60
    val s = sec % 60
    return "%02d:%02d:%02d".format(h, m, s)
}
