package com.tinkerforge.brickd;

import android.hardware.usb.UsbDevice;

public class USBDeviceInfo {
    public UsbDevice device;

    public String name;
    public int vendorID;
    public int productID;
    public int numInterfaces;
    public USBInterfaceInfo[] interfaceInfos;

    public USBDeviceInfo(UsbDevice device) {
        this.device = device;

        name = device.getDeviceName();
        vendorID = device.getVendorId();
        productID = device.getProductId();
        numInterfaces = device.getInterfaceCount();
        interfaceInfos = new USBInterfaceInfo[numInterfaces];

        for (int i = 0; i < numInterfaces; ++i) {
            interfaceInfos[i] = new USBInterfaceInfo(device.getInterface(i));
        }
    }
}
