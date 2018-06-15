package com.tinkerforge.brickd;

import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.hardware.usb.UsbRequest;
import android.os.IBinder;
import android.support.annotation.Keep;
import android.util.Log;

import java.nio.ByteBuffer;
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
    public native void transferred(ByteBuffer opaque, int length);

    private static final String ACTION_USB_PERMISSION = "com.tinkerforge.brickd.USB_PERMISSION";

    private UsbManager manager;
    private PendingIntent permissionIntent;
    private Thread mainThread;
    private Map<UsbDeviceConnection, Thread> requestThreads = new Hashtable<UsbDeviceConnection, Thread>();

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();

            if (ACTION_USB_PERMISSION.equals(action)) {
                synchronized (this) {
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
        }
    };

    public MainService() {
        System.out.println(">>>> MainService");
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate() {
        manager = (UsbManager)getSystemService(Context.USB_SERVICE);
        permissionIntent = PendingIntent.getBroadcast(this, 0, new Intent(ACTION_USB_PERMISSION), 0);

        registerReceiver(receiver, new IntentFilter(ACTION_USB_PERMISSION));
    }

    @Override
    public void onDestroy() {
        interrupt();

        try {
            mainThread.join(); // FIXME: use timeout?
        } catch (InterruptedException e) {
            e.printStackTrace();
        }

        unregisterReceiver(receiver);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startid) {
        if (mainThread == null) {
            mainThread = new Thread(new Runnable() {
                @Override
                public void run() {
                    main(MainService.this);
                }
            });

            mainThread.start();
        }

        return START_NOT_STICKY;
    }

    @Keep
    public USBDeviceInfo[] getDeviceList() {
        HashMap<String, UsbDevice> deviceList = manager.getDeviceList();
        Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();
        USBDeviceInfo[] deviceInfos = new USBDeviceInfo[deviceList.size()];
        int i = 0;

        Log.d("brickd","getDeviceList " + deviceList.size());

        while (deviceIterator.hasNext()){
            UsbDevice device = deviceIterator.next();

            deviceInfos[i++] = new USBDeviceInfo(device);

            Log.d("brickd","getDeviceList: " + device.getDeviceName());
        }

        return deviceInfos;
    }

    @Keep
    public UsbDeviceConnection openDevice(UsbDevice device) {
        Log.d("brickd","openDevice 1 " + device.getDeviceName());

        if (!manager.hasPermission(device)) {
            Log.d("brickd", "requestPermission 1 " + device.getDeviceName());
            manager.requestPermission(device, permissionIntent);
            Log.d("brickd", "requestPermission 2 " + device.getDeviceName());

            try {
                synchronized (this) {
                    Log.d("brickd", "wait 1 " + device.getDeviceName());
                    wait();
                    Log.d("brickd", "wait 2 " + device.getDeviceName());
                }
            } catch (InterruptedException e) {
                Log.d("brickd", "InterruptedException");
                return null;
            }
        }

        Log.d("brickd","openDevice 2 " + device.getDeviceName());

        final UsbDeviceConnection connection = manager.openDevice(device);

        Thread requestThread = new Thread(new Runnable() {
            @Override
            public void run() {
                while (true) {
                    UsbRequest request = connection.requestWait();

                    if (request == null) {
                        System.out.println(">>>>>>>>>>> requestWait failed");
                        continue;
                    }

                    ByteBuffer[] clientData = (ByteBuffer[])request.getClientData();

                    transferred(clientData[1], clientData[0].position());

                    request.close();
                }
            }
        });

        requestThread.start();

        requestThreads.put(connection, requestThread);

        return connection;
    }

    @Keep
    public void closeDevice(UsbDeviceConnection connection) {
        Thread requestThread = requestThreads.remove(connection);

        requestThread.interrupt();

        connection.close();
    }

    @Keep
    public byte[] getStringDescriptor(UsbDeviceConnection connection, int descIndex, int languageID, int length) {
        int requestType = 0x81; // 0x81 == direction: in, type: standard, recipient: device
        int request = 0x06; // 0x06 == get-descriptor
        int value = (0x03 << 8) | descIndex; // 0x03 == string-descriptor
        int index = languageID;
        byte[] buffer = new byte[length];
        int result = connection.controlTransfer(requestType, request, value, index, buffer, length, 0);

        if (result < 0) {
            return null;
        }

        byte[] data = new byte[result];

        System.arraycopy(buffer, 0, data, 0, result);

        return data;
    }

    @Keep
    public boolean claimInterface(UsbDevice device, UsbDeviceConnection connection, int index) {
        return connection.claimInterface(device.getInterface(index), true);
    }

    @Keep
    public boolean releaseInterface(UsbDevice device, UsbDeviceConnection connection, int index) {
        return connection.releaseInterface(device.getInterface(index));
    }

    @Keep
    public UsbRequest submitTransfer(UsbDevice device, UsbDeviceConnection connection,
                                     int endpointAddress, ByteBuffer buffer, ByteBuffer opaque) {
        UsbInterface iface = device.getInterface(0); // FIXME: don't hardcode interface here
        UsbEndpoint endpoint = null;

        Log.d("brickd","submitTransfer endpointAddress: " + endpointAddress + ", capacity: " + buffer.capacity() + ", position: " + buffer.position());

        for (int i = 0; i < iface.getEndpointCount(); ++i) {
            //System.out.println("EP: " + iface.getEndpoint(i));

            if (iface.getEndpoint(i).getAddress() == endpointAddress) {
                endpoint = iface.getEndpoint(i);
            }
        }

        if (endpoint == null) {
            Log.d("brickd", String.format("Could not find endpoint 0x%02X", endpointAddress));
            return null;
        }

        UsbRequest request = new UsbRequest();

        ByteBuffer[] clientData = new ByteBuffer[]{buffer, opaque};

        request.setClientData(clientData);

        if (!request.initialize(connection, endpoint)) {
            Log.d("brickd", String.format("Could not initialize USB request for endpoint 0x%02X", endpointAddress));
            return null;
        }

        if (!request.queue(buffer, buffer.capacity())) {
            Log.d("brickd", String.format("Could not queue USB request for endpoint 0x%02X", endpointAddress));
            return null;
        }

        return request;
    }

    @Keep
    public void cancelTransfer(UsbRequest request) {
        request.cancel();
    }
}