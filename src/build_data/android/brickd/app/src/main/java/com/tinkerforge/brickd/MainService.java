/*
 * brickd
 * Copyright (C) 2018 Matthias Bolte <matthias@tinkerforge.com>
 *
 * MainService.java: Brick Daemon GUI for Android
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

import android.app.Notification;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;
import android.os.IBinder;
import android.support.annotation.Keep;
import android.util.Log;

import java.util.HashMap;
import java.util.Hashtable;
import java.util.Iterator;
import java.util.Map;

public class MainService extends Service {
    static {
        System.loadLibrary("brickd-android");
    }

    public native void main(MainService service);
    public native void interrupt();
    public native void hotplug();

    private static final String ACTION_USB_PERMISSION = "com.tinkerforge.brickd.USB_PERMISSION";
    private static final int NOTIFICATION_ID = 1;

    private UsbManager mManager;
    private PendingIntent mPermissionIntent;
    private Thread mThread;
    private Map<Integer, UsbDeviceConnection> mOpenConnections = new Hashtable<Integer, UsbDeviceConnection>();

    private final BroadcastReceiver mPermissionReceiver = new BroadcastReceiver() {
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();

            if (ACTION_USB_PERMISSION.equals(action)) {
                UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);

                if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                    if(device != null) {
                        synchronized (context) {
                            context.notifyAll();
                        }
                    }
                }
                else {
                    Log.d("brickd-java", "Permission was denied: " + device.getDeviceName());
                }
            }
        }
    };

    private final BroadcastReceiver mHotplugReceiver = new BroadcastReceiver() {
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();

            if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(action) ||
                    UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {
                hotplug();
            }
        }
    };

    public MainService() {
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate() {
        Log.d("brickd-java",">>>>> MainService onCreate");

        mManager = (UsbManager)getSystemService(Context.USB_SERVICE);
        mPermissionIntent = PendingIntent.getBroadcast(this, 0, new Intent(ACTION_USB_PERMISSION), 0);

        registerReceiver(mPermissionReceiver, new IntentFilter(ACTION_USB_PERMISSION));
        registerReceiver(mHotplugReceiver, new IntentFilter(UsbManager.ACTION_USB_DEVICE_ATTACHED));
        registerReceiver(mHotplugReceiver, new IntentFilter(UsbManager.ACTION_USB_DEVICE_DETACHED));

        Intent notificationIntent = new Intent(this, MainActivity.class);
        PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, notificationIntent, 0);
        Notification notification = new Notification.Builder(this)
                                    .setSmallIcon(R.mipmap.ic_launcher)
                                    .setContentTitle("Brick Daemon")
                                    .setContentText("Background Service is running")
                                    .setContentIntent(pendingIntent).build();

        startForeground(NOTIFICATION_ID, notification);

        Log.d("brickd-java","<<<<< MainService onCreate");
    }

    @Override
    public void onDestroy() {
        Log.d("brickd-java",">>>>> MainService onDestroy");

        stopThread();

        unregisterReceiver(mPermissionReceiver);
        unregisterReceiver(mHotplugReceiver);

        Log.d("brickd-java","<<<<< MainService onDestroy");
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startid) {
        Log.d("brickd-java",">>>>> MainService onStartCommand");

        startThread();

        Log.d("brickd-java","<<<<< MainService onStartCommand");

        return START_STICKY;
    }

    private void startThread() {
        if (mThread != null) {
            return;
        }

        mThread = new Thread(new Runnable() {
            @Override
            public void run() {
                main(MainService.this);
            }
        });

        mThread.setDaemon(true);
        mThread.start();

        SharedPreferences.Editor editor = getSharedPreferences("default", Context.MODE_PRIVATE).edit();

        editor.putBoolean("service", true);
        editor.apply();
    }

    private void stopThread() {
        if (mThread == null) {
            return;
        }

        interrupt();

        try {
            mThread.join(); // FIXME: use timeout?
        } catch (InterruptedException e) {
            e.printStackTrace();
        }

        mThread = null;

        SharedPreferences.Editor editor = getSharedPreferences("default", Context.MODE_PRIVATE).edit();

        editor.putBoolean("service", false);
        editor.apply();
    }

    @Keep
    public USBDeviceInfo[] getDeviceList() {
        HashMap<String, UsbDevice> deviceList = mManager.getDeviceList();
        Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();
        USBDeviceInfo[] deviceInfos = new USBDeviceInfo[deviceList.size()];
        int i = 0;

        while (deviceIterator.hasNext()){
            UsbDevice device = deviceIterator.next();

            deviceInfos[i++] = new USBDeviceInfo(device);
        }

        return deviceInfos;
    }

    @Keep
    public int openDevice(UsbDevice device) {
        Log.d("brickd-java","openDevice 1 " + device.getDeviceName());

        if (!mManager.hasPermission(device)) {
            Log.d("brickd-java", "requestPermission 1 " + device.getDeviceName());
            mManager.requestPermission(device, mPermissionIntent);
            Log.d("brickd-java", "requestPermission 2 " + device.getDeviceName());

            try {
                synchronized (this) {
                    Log.d("brickd-java", "wait 1 " + device.getDeviceName());
                    wait();
                    Log.d("brickd-java", "wait 2 " + device.getDeviceName());
                }
            } catch (InterruptedException e) {
                Log.d("brickd-java", "InterruptedException");
                return -1;
            }
        }

        Log.d("brickd-java","openDevice 2 " + device.getDeviceName());

        UsbDeviceConnection connection = mManager.openDevice(device);

        if (connection == null) {
            return -1;
        }

        int fd = connection.getFileDescriptor();

        // Android API documentation suggests that it could happen that the connection
        // objects exists but is not open
        if (fd < 0) {
            connection.close();

            return -1;
        }

        mOpenConnections.put(fd, connection);

        return fd;
    }

    @Keep
    public void closeDevice(int fd) {
        UsbDeviceConnection connection = mOpenConnections.remove(fd);

        if (connection != null) {
            connection.close();
        }
    }
}