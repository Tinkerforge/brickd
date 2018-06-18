package com.tinkerforge.brickd;

import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
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
                    Log.d("brickd", "permission denied for device " + device.getDeviceName());
                }
            }
        }
    };

    BroadcastReceiver mHotplugReceiver = new BroadcastReceiver() {
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
        mManager = (UsbManager)getSystemService(Context.USB_SERVICE);
        mPermissionIntent = PendingIntent.getBroadcast(this, 0, new Intent(ACTION_USB_PERMISSION), 0);

        registerReceiver(mPermissionReceiver, new IntentFilter(ACTION_USB_PERMISSION));
        registerReceiver(mHotplugReceiver, new IntentFilter(UsbManager.ACTION_USB_DEVICE_ATTACHED));
        registerReceiver(mHotplugReceiver, new IntentFilter(UsbManager.ACTION_USB_DEVICE_DETACHED));
    }

    @Override
    public void onDestroy() {
        interrupt();

        try {
            mThread.join(); // FIXME: use timeout?
        } catch (InterruptedException e) {
            e.printStackTrace();
        }

        unregisterReceiver(mPermissionReceiver);
        unregisterReceiver(mHotplugReceiver);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startid) {
        if (mThread == null) {
            mThread = new Thread(new Runnable() {
                @Override
                public void run() {
                    main(MainService.this);
                }
            });

            mThread.start();
        }

        return START_NOT_STICKY;
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
        Log.d("brickd","openDevice 1 " + device.getDeviceName());

        if (!mManager.hasPermission(device)) {
            Log.d("brickd", "requestPermission 1 " + device.getDeviceName());
            mManager.requestPermission(device, mPermissionIntent);
            Log.d("brickd", "requestPermission 2 " + device.getDeviceName());

            try {
                synchronized (this) {
                    Log.d("brickd", "wait 1 " + device.getDeviceName());
                    wait();
                    Log.d("brickd", "wait 2 " + device.getDeviceName());
                }
            } catch (InterruptedException e) {
                Log.d("brickd", "InterruptedException");
                return -1;
            }
        }

        Log.d("brickd","openDevice 2 " + device.getDeviceName());

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