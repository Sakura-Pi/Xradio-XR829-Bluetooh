## Xradio XR829 Bluetooth

This repo contains multiple branches to store different files.
- main (readme and patches)
- bluez-5.54 (bluez source code)
- bluez-5.54-xradio (bluez source code with xradio patches)

Please use this command to clone the repo
```bash
$ git clone https://github.com/Sakura-Pi/Xradio-XR829-Bluetooh --branch <branch> --depth 1
```

XRadio uses bluez open source Bluetooth stack for their bt/wlan transceivers. When XR829 has been awaken, it use UART to download the firmware to the device. It's totally in the userspace rather than the driver.

**This repo is tested under Allwinner H3 (Armbian bullseye distribution) with kernel version 5.15.93.**

### Installation

#### bluez
You may need install the packages first
```bash
$ sudo apt install -y
    libtool \
    libglib2.0-dev \
    libdbus-1-dev \
    libudev-dev \
    libical-dev \
    libreadline-dev \
    elfutils \
    libdw-dev \
    libalsa-ocaml-dev \
    libsbc-dev \
    python3-docutils \
    libjson-c-dev
```

Then clone this repo with branch `bluez-5.54-xradio`, check it out, run bootstrap, and configure make and ready to compile.
```bash
$ ./bootstrap
$ ./configure \
    --enable-static \
    --enable-shared \
    --enable-client \
    --enable-datafiles \
    --enable-experimental \
    --enable-library \
    --enable-monitor \
    --enable-obex \
    --enable-threads \
    --enable-tools \
    --disable-android \
    --disable-cups \
    --disable-manpages \
    --disable-sixaxis \
    --disable-systemd \
    --disable-test \
    --disable-udev \
    --enable-deprecated \
    --enable-mesh \
    --localstatedir=/etc
```

compile and install it.
```bash
$ make -j<cpu cores>
$ sudo make install
```

#### xradio_btlpm
This driver is sunxi dedicated, It may not work on the other platform. It responded to awake/sleep the ble device or awake linux from ble device via the GPIO pin.

Apply the [patch/xradio_btlpm.patch](patch/xradio_btlpm.patch) to your kernel, and enable the kernel option:
- CONFIG_XR_BT_LPM=m

#### device tree
```dts
/ {

  /* Your board definitions */

  ble_enable: rfkill {
    compatible = "rfkill-gpio";
    label = "rfkill-xr829-ble";
    radio-type = "bluetooth";
    shutdown-gpios = <&pio 0 6 GPIO_ACTIVE_LOW>; /* PA6 */
    status = "okay";
  };

  btlpm: btlpm@0 {
    compatible  = "allwinner,sunxi-btlpm";
    uart_index  = <0x1>;
    bt_wake     = <&pio 6 12 GPIO_ACTIVE_HIGH>; /* PG12 */
    bt_hostwake = <&pio 6 13 GPIO_ACTIVE_HIGH>; /* PG13 */
    status      = "okay";
  };

}
```

### Use the ble

After installation, you can use this command to bring up it:
```bash
$ hciattach -n /dev/ttyS1 xr829
```
It causes the XR829 to reset, awaken, and start accepting firmware which located at '/lib/firmware/fw_xr829_bt.bin'. After the boot, you can use it.

## Thanks
- Upstream bluez maintainers
- [ssp97](https://github.com/ssp97)
