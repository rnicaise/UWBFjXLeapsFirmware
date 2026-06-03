package com.qorvo.leapscsv

data class UwbCsvRecord(
    val ms: Long,
    val sample: Long,
    val iax: Int,
    val iay: Int,
    val iaz: Int,
    val rax: Int,
    val ray: Int,
    val raz: Int
)
