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

        Log.d("brickd",">>>>> MainActivity onCreate " + getIntent().getAction());

        setContentView(R.layout.activity_main);

        mSwitchService = (Switch)findViewById(R.id.switch_service);

        SharedPreferences settings = getPreferences(0);
        final Intent serviceIntent = new Intent(this, MainService.class);

        mSwitchService.setChecked(settings.getBoolean("service", true));
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

        if (mSwitchService.isChecked()) {
            startService(serviceIntent);
        } else {
            stopService(serviceIntent);
        }

        Log.d("brickd","<<<<< MainActivity onCreate");
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();

        Log.d("brickd",">>>>> MainActivity onDestroy");

        //stopService(new Intent(this, MainService.class));

        Log.d("brickd","<<<<< MainActivity onDestroy");
    }

    @Override
    protected void onStop() {
        super.onStop();

        Log.d("brickd",">>>>> MainActivity onStop");

        SharedPreferences.Editor editor = getPreferences(0).edit();

        editor.putBoolean("service", mSwitchService.isChecked());
        editor.apply();

        Log.d("brickd","<<<<< MainActivity onStop");
    }
}
