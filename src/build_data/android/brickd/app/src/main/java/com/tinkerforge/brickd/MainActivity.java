/*
 * brickd
 * Copyright (C) 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * MainActivity.java: Brick Daemon GUI for Android
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

package com.tinkerforge.brickd;

import android.content.Intent;
import android.content.SharedPreferences;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.widget.CompoundButton;
import android.widget.Switch;

public class MainActivity extends AppCompatActivity {
    private Switch mSwitchService;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Log.d("brickd-java",">>>>> MainActivity onCreate " + getIntent().getAction());

        setContentView(R.layout.activity_main);

        mSettings = getSharedPreferences("default", Context.MODE_PRIVATE);

        mSettingsListener = new SharedPreferences.OnSharedPreferenceChangeListener() {
            public void onSharedPreferenceChanged(SharedPreferences settings, String name) {
                Log.d("brickd-java",">>>>> MainActivity onSharedPreferenceChanged " + name);

                mSwitchService.setChecked(mSettings.getBoolean("service", true));
            }
        };

        mSwitchService = findViewById(R.id.switch_service);

        final Intent serviceIntent = new Intent(this, MainService.class);

        mSwitchService.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton button, boolean checked) {
                if (checked) {
                    startService(serviceIntent);
                } else {
                    stopService(serviceIntent);
                }
            }
        });

        Log.d("brickd-java","<<<<< MainActivity onCreate");
    }

    @Override
    protected void onResume() {
        super.onResume();

        Log.d("brickd-java",">>>>> MainActivity onResume");

        mSwitchService.setChecked(mSettings.getBoolean("service", true));
        mSettings.registerOnSharedPreferenceChangeListener(mSettingsListener);

        Log.d("brickd-java","<<<<< MainActivity onResume");
    }

    @Override
    protected void onPause() {
        super.onPause();

        Log.d("brickd-java",">>>>> MainActivity onPause");

        mSettings.unregisterOnSharedPreferenceChangeListener(mSettingsListener);

        Log.d("brickd-java","<<<<< MainActivity onPause");
    }
}
