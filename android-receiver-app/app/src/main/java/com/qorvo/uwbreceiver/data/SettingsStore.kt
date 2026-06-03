package com.qorvo.uwbreceiver.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.floatPreferencesKey
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

private val Context.dataStore: DataStore<Preferences> by preferencesDataStore(name = "uwb_settings")

class SettingsStore(private val context: Context) {
    private val keyGreen = floatPreferencesKey("threshold_green_max")
    private val keyOrange = floatPreferencesKey("threshold_orange_max")
    private val keyBikeBoxPosition = intPreferencesKey("bike_box_position")
    private val keyVestBoxPosition = intPreferencesKey("vest_box_position")

    val thresholds: Flow<DistanceThresholds> = context.dataStore.data.map { pref ->
        DistanceThresholds(
            greenMax = pref[keyGreen] ?: 1.0f,
            orangeMax = pref[keyOrange] ?: 2.0f,
        )
    }

    val controls: Flow<UwbControlSettings> = context.dataStore.data.map {
        UwbControlSettings()
    }

    val experiment: Flow<ExperimentSettings> = context.dataStore.data.map { pref ->
        ExperimentSettings(
            bikeBoxPosition = sanitizeBoxPosition(pref[keyBikeBoxPosition] ?: 1),
            vestBoxPosition = sanitizeBoxPosition(pref[keyVestBoxPosition] ?: 1),
        )
    }

    suspend fun updateGreenMax(value: Float) {
        context.dataStore.edit { pref ->
            pref[keyGreen] = value
        }
    }

    suspend fun updateOrangeMax(value: Float) {
        context.dataStore.edit { pref ->
            pref[keyOrange] = value
        }
    }

    suspend fun updateBikeBoxPosition(value: Int) {
        context.dataStore.edit { pref ->
            pref[keyBikeBoxPosition] = sanitizeBoxPosition(value)
        }
    }

    suspend fun updateVestBoxPosition(value: Int) {
        context.dataStore.edit { pref ->
            pref[keyVestBoxPosition] = sanitizeBoxPosition(value)
        }
    }

    private fun sanitizeBoxPosition(value: Int): Int {
        return if (value in BOX_POSITIONS) value else 1
    }

    companion object {
        val BOX_POSITIONS = listOf(1, -1, 2, -2, 3, -3)
    }
}
