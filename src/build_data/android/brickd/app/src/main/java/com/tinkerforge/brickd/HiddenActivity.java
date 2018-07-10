/*
 * brickd
 * Copyright (C) 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * HiddenActivity.java: Brick Daemon GUI for Android
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

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.hardware.usb.UsbManager;
import android.support.v7.app.AppCompatActivity;
import android.util.Log;

public class HiddenActivity extends AppCompatActivity {
    @Override
    protected void onResume() {
        super.onResume();

        Log.d("brickd-java",">>>>> HiddenActivity onResume " + getIntent().getAction());

        Intent intent = getIntent();

        if (intent != null) {
            String action = intent.getAction();

            if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(action)) {
                startService(new Intent(this, MainService.class));
            }
        }

        Log.d("brickd-java","<<<<< HiddenActivity onResume");

        finish();
    }
}
