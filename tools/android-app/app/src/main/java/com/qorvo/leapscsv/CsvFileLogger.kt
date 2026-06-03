package com.qorvo.leapscsv

import android.content.Context
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class CsvFileLogger(private val context: Context) {
    private var targetFile: File? = null

    fun startNewSession() {
        val dir = File(context.getExternalFilesDir(null), "sessions")
        if (!dir.exists()) {
            dir.mkdirs()
        }

        val stamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        targetFile = File(dir, "uwb_session_$stamp.csv")
        targetFile?.writeText("ms,sample,iax,iay,iaz,rax,ray,raz\n")
    }

    fun append(record: UwbCsvRecord) {
        val file = targetFile ?: return
        val line = "${record.ms},${record.sample},${record.iax},${record.iay},${record.iaz},${record.rax},${record.ray},${record.raz}\n"
        file.appendText(line)
    }

    fun currentFilePath(): String {
        return targetFile?.absolutePath ?: ""
    }
}
