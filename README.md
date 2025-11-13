# TlsrSmartSwitch
Alternative firmwares for Smart Switch on the TLSR925x chipset.

## The repository is under development!


Supported device models:

* [Tuya ZigBee "Mini Zigbee Switch"](https://pvvx.github.io/TS0001_TZ3000_kqvb5akv/) (Module ZTU, Sensor: BL0942), FW name: MZSW01
* [Tuya ZigBee "Mini Smart Switch"](https://pvvx.github.io/TS000F_TZ3000_xkap8wtb/) (Module ZT2S, Sensor: BL0937), FW name: MZSW02

---

### Story:

The original FW structure for "Zigbee Smart Switch" was designed by [slacky1965](https://github.com/slacky1965).

The current versions of the published alternative firmware retain compatibility with [slacky1965's](https://github.com/slacky1965/tuya_mini_relay_zrd) "quirks."

Optimizations have been made:

* Compatibility with ZHA and Zigbee 3.0 standards has been added.
* Firmware size for compatibility with chips with 512K flash memory.
* Chip hardware usage.
* Firmware loader for compatibility with OTA BLE and Zigbee variants.
* Added a variant with the BL0937 sensor.

Further development

* Custom settings of coefficients for the sensor.
* Adding a BLE beacon with the BTHome format.
  
