package com.tinkerforge.brickd;

import android.hardware.usb.UsbDevice;

import java.util.Hashtable;
import java.util.Map;

public class USBDeviceInfo {
    private static Map<String, Integer> fakeBusNumbersAndDeviceAddresses = new Hashtable<String, Integer>();

    public UsbDevice device;

    public String name;
    public int busNumber = -1;
    public int deviceAddress = -1;
    public int vendorID;
    public int productID;
    public int numInterfaces;
    public USBInterfaceInfo[] interfaceInfos;

    public USBDeviceInfo(UsbDevice device) {
        this.device = device;

        name = device.getDeviceName();

        if (name.startsWith("/dev/bus/usb/")) {
            String[] parts = name.substring("/dev/bus/usb/".length()).split("/");

            if (parts.length == 2) {
                try {
                    busNumber = Integer.parseInt(parts[0]);
                    deviceAddress = Integer.parseInt(parts[1]);
                } catch (NumberFormatException e) {
                    busNumber = -1;
                    deviceAddress = -1;
                }
            }
        }

        if (busNumber < 0 || deviceAddress < 0) {
            Integer busNumberAndDeviceAddress = fakeBusNumbersAndDeviceAddresses.get(name);
            int value;

            if (busNumberAndDeviceAddress != null) {
                value = busNumberAndDeviceAddress;
            } else {
                // FIXME: after 65536 different IDs this will start to reuse bus numbers
                //        and device addresses. this will probably never be a problem
                value = fakeBusNumbersAndDeviceAddresses.size() % 0xFFFF;
                fakeBusNumbersAndDeviceAddresses.put(name, value);
            }

            busNumber = (value >> 8) & 0xFF;
            deviceAddress = value & 0xFF;
        }

        vendorID = device.getVendorId();
        productID = device.getProductId();
        numInterfaces = device.getInterfaceCount();
        interfaceInfos = new USBInterfaceInfo[numInterfaces];

        for (int i = 0; i < numInterfaces; ++i) {
            interfaceInfos[i] = new USBInterfaceInfo(device.getInterface(i));
        }
    }
}
