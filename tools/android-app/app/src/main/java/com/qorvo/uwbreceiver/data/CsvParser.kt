package com.qorvo.uwbreceiver.data

object CsvParser {
    fun parse(line: String): CsvSample? {
        val trimmed = line.trim()
        if (trimmed.isEmpty() || trimmed.startsWith("#")) {
            return null
        }

        val parts = trimmed.split(',')
        if (parts.size != 8) {
            return null
        }

        return try {
            CsvSample(
                ms = parts[0].toLong(),
                sample = parts[1].toLong(),
                iax = parts[2].toInt(),
                iay = parts[3].toInt(),
                iaz = parts[4].toInt(),
                rax = parts[5].toInt(),
                ray = parts[6].toInt(),
                raz = parts[7].toInt(),
            )
        } catch (_: NumberFormatException) {
            null
        }
    }
}
