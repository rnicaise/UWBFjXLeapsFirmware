package com.qorvo.uwbreceiver.data

object CsvParser {
    fun parse(line: String): CsvSample? {
        val trimmed = line.trim()
        if (trimmed.isEmpty() || trimmed.startsWith("#")) {
            return null
        }

        val parts = trimmed.split(',')
        if (parts.size < 3) {
            return null
        }

        return try {
            val radioFieldCount = when {
                parts.size == 11 || parts.size >= 21 -> 8
                parts.size == 7 || parts.size >= 17 -> 4
                else -> 0
            }
            val hasRadioMetrics = radioFieldCount > 0
            val hasNlosMetrics = radioFieldCount >= 8
            val telemetryOffset = radioFieldCount
            val accelStart = 3 + telemetryOffset
            val controlsStart = accelStart + 6
            val gyroStart = when {
                hasRadioMetrics && parts.size >= controlsStart + 10 -> controlsStart + 4
                !hasRadioMetrics && parts.size >= controlsStart + 6 -> controlsStart
                else -> null
            }

            CsvSample(
                ms = parts[0].toLong(),
                sample = parts[1].toLong(),
                dist = parts[2].toFloat(),
                iax = parts.getOrNull(accelStart)?.toIntOrNull() ?: 0,
                iay = parts.getOrNull(accelStart + 1)?.toIntOrNull() ?: 0,
                iaz = parts.getOrNull(accelStart + 2)?.toIntOrNull() ?: 0,
                rax = parts.getOrNull(accelStart + 3)?.toIntOrNull() ?: 0,
                ray = parts.getOrNull(accelStart + 4)?.toIntOrNull() ?: 0,
                raz = parts.getOrNull(accelStart + 5)?.toIntOrNull() ?: 0,
                igx = gyroStart?.let { parts.getOrNull(it)?.toIntOrNull() } ?: 0,
                igy = gyroStart?.let { parts.getOrNull(it + 1)?.toIntOrNull() } ?: 0,
                igz = gyroStart?.let { parts.getOrNull(it + 2)?.toIntOrNull() } ?: 0,
                rgx = gyroStart?.let { parts.getOrNull(it + 3)?.toIntOrNull() } ?: 0,
                rgy = gyroStart?.let { parts.getOrNull(it + 4)?.toIntOrNull() } ?: 0,
                rgz = gyroStart?.let { parts.getOrNull(it + 5)?.toIntOrNull() } ?: 0,
                rxPowerDbm = if (hasRadioMetrics) parts.getOrNull(3)?.toFloatOrNull()?.takeIf { it.isFinite() } else null,
                firstPathPowerDbm = if (hasRadioMetrics) parts.getOrNull(4)?.toFloatOrNull()?.takeIf { it.isFinite() } else null,
                clockOffsetPpm = if (hasRadioMetrics) parts.getOrNull(5)?.toFloatOrNull()?.takeIf { it.isFinite() } else null,
                signalQuality10 = if (hasRadioMetrics) parts.getOrNull(6)?.toFloatOrNull()?.takeIf { it.isFinite() }?.coerceIn(1f, 10f) else null,
                nlosQuality10 = if (hasNlosMetrics) parts.getOrNull(7)?.toFloatOrNull()?.takeIf { it.isFinite() }?.coerceIn(1f, 10f) else null,
                peakToFirstPathSamples = if (hasNlosMetrics) parts.getOrNull(8)?.toFloatOrNull()?.takeIf { it.isFinite() } else null,
                firstPathConfidence = if (hasNlosMetrics) parts.getOrNull(9)?.toIntOrNull() else null,
                stsQuality = if (hasNlosMetrics) parts.getOrNull(10)?.toIntOrNull() else null,
                responderAcquisitionPeriodMs = if (hasRadioMetrics) parts.getOrNull(controlsStart)?.toIntOrNull() else null,
                initiatorAcquisitionPeriodMs = if (hasRadioMetrics) parts.getOrNull(controlsStart + 1)?.toIntOrNull() else null,
                responderProfileOpt = if (hasRadioMetrics) parts.getOrNull(controlsStart + 2)?.toIntOrNull() else null,
                initiatorProfileOpt = if (hasRadioMetrics) parts.getOrNull(controlsStart + 3)?.toIntOrNull() else null,
                firmwareValid = gyroStart?.let { parts.getOrNull(it + 6)?.toIntOrNull() }?.let { it != 0 },
                firmwareDistFilt = gyroStart?.let { parts.getOrNull(it + 7)?.toFloatOrNull()?.takeIf { f -> f.isFinite() } },
            )
        } catch (_: NumberFormatException) {
            null
        }
    }
}
