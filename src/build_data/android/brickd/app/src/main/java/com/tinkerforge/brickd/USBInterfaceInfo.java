package com.tinkerforge.brickd;

import android.hardware.usb.UsbInterface;

public class USBInterfaceInfo {
    public int numEndpoints;
    public int[] endpointAddresses;

    public USBInterfaceInfo(UsbInterface iface) {
        numEndpoints = iface.getEndpointCount();
        endpointAddresses = new int[numEndpoints];

        for (int i = 0; i < numEndpoints; ++i) {
            endpointAddresses[i] = iface.getEndpoint(i).getAddress();
        }
    }
}
