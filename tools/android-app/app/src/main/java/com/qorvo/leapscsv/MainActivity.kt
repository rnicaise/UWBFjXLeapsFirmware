package com.qorvo.leapscsv

import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

class MainActivity : AppCompatActivity() {
    private val parser = UwbCsvParser()
    private lateinit var logger: CsvFileLogger

    private lateinit var statusText: TextView
    private lateinit var lastLineText: TextView
    private lateinit var manualLineInput: EditText

    private var logging = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        statusText = findViewById(R.id.statusText)
        lastLineText = findViewById(R.id.lastLineText)
        manualLineInput = findViewById(R.id.manualLineInput)
        val startButton: Button = findViewById(R.id.startButton)
        val stopButton: Button = findViewById(R.id.stopButton)
        val ingestButton: Button = findViewById(R.id.ingestButton)

        logger = CsvFileLogger(this)

        startButton.setOnClickListener {
            logging = true
            logger.startNewSession()
            statusText.text = getString(R.string.state_logging)
            lastLineText.text = "Logging to: ${logger.currentFilePath()}"
        }

        stopButton.setOnClickListener {
            logging = false
            statusText.text = getString(R.string.state_idle)
        }

        ingestButton.setOnClickListener {
            ingestRawLine(manualLineInput.text.toString())
        }
    }

    private fun ingestRawLine(rawLine: String) {
        val record = parser.parseLine(rawLine)
        if (record == null) {
            lastLineText.text = "Ignored line"
            return
        }

        if (logging) {
            logger.append(record)
        }

        lastLineText.text = "Last record: ms=${record.ms}, sample=${record.sample}, i=(${record.iax},${record.iay},${record.iaz}), r=(${record.rax},${record.ray},${record.raz})"
    }
}
