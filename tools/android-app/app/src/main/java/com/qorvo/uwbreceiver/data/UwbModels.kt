package com.qorvo.uwbreceiver.data

import android.net.Uri

data class CsvSample(
    val ms: Long,
    val sample: Long,
    val iax: Int,
    val iay: Int,
    val iaz: Int,
    val rax: Int,
    val ray: Int,
    val raz: Int,
)

enum class LinkState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
}

data class RuntimeState(
    val linkState: LinkState = LinkState.DISCONNECTED,
    val status: String = "Idle",
    val latest: CsvSample? = null,
    val samples: Long = 0,
    val hz: Float = 0f,
    val sessionStartElapsedMs: Long? = null,
    val recording: Boolean = false,
    val recordingName: String? = null,
    val lastSavedUri: Uri? = null,
    val invalidLines: Long = 0,
)

data class UwbUiState(
    val runtime: RuntimeState = RuntimeState(),
    val elapsedSec: Long = 0,
)
