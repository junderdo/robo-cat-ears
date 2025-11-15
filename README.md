## How to set up connection to ESP32 in WSL
If you are using Windows Subsystem for Linux (WSL) to run ESP-IDF, you may encounter issues with serial port access when trying to flash or monitor your ESP32 device. Follow these steps to set up serial port access in WSL:

Preqrequisites:
- Ensure you have WSL 2 installed and set up on your Windows machine.
- Ensure you have ESP-IDF installed and configured in your WSL environment.
- Ensure you have lsusb installed in your WSL environment. You can install it using the following command:
   ```bash
   sudo apt-get install usbutils
   ```
- Install USB drivers https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers
- Install usb-pid in PowerShell: https://github.com/dorssel/usbipd-win/releases

Steps:
1. Open PowerShell as Administrator and run the following command to list available USB devices:
   ```powershell
   usbipd list
   ```
2. Identify your ESP32 device from the list and note its BUSID
3. Make the ESP32 device sharable using the following command, replacing <BUSID> with the actual BUSID of your device:
   ```powershell
   usbipd wsl bind --busid <BUSID>
   ```
4. Attach the ESP32 device to WSL using the following command, replacing <BUSID> with the actual BUSID of your device:
   ```powershell
   usbipd attach --wsl --busid <BUSID>
   ```
5. Open your WSL terminal and verify that the device is accessible by listing the serial devices:
   ```bash
   lsusb
   ```
6. You should see your ESP32 device listed (e.g., Bus 001 Device 002: ID 303a:1001 Espressif USB JTAG/serial debug unit). You can now use this device path in your ESP-IDF commands to flash and monitor your ESP32 device.
7. Set your ESP-IDF serial port at the bottom of your VSCode window eg. /dev/ttyACM0
8. Make sure your serial port permissions are set correctly. Replace the example port with your actual port if different:
   ```bash
   sudo chmod 0666 /dev/ttyACM0
   ```
9. Make sure you have added your user to the dialout group to avoid permission issues:
   ```bash
   sudo usermod -aG dialout $USER
   ```
   After running this command, you may need to log out and log back in for the changes to take effect.
10. Make sure you are using the UART connection for flashing and monitoring in ESP-IDF. You can specify the port using the `-p` option in your ESP-IDF commands.
11. You can now flash and monitor your ESP32 device using ESP-IDF commands in WSL. For example:
   ```bash
   idf.py -p /dev/ttyACM0 flash monitor
   ```
   Or by using the VSCode extention utils on the bottom bar
12. To unbind the device from WSL when you're done, run the following command in PowerShell, replacing <BUSID> with the actual BUSID of your device:
   ```powershell
   usbipd wsl unbind --busid <BUSID>
   ```





## BELOW THIS IS THE ORIGINAL README FROM TEMPLATE PROJECT ##


| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | -------- |

## ESP-IDF GATT SERVER SPP Example

For description of this application please refer to [ESP-IDF GATT CLIENT SPP Example](../ble_spp_client/README.md)

## How to Use Example

Before project configuration and build, be sure to set the correct chip target using:

```bash
idf.py set-target <chip_name>
```

### Hardware Required

* A development board with ESP32/ESP32-C3/ESP32-S3/ESP32-C2/ESP32-H2 SoC (e.g., ESP32-DevKitC, ESP-WROVER-KIT, etc.)
* A USB cable for Power supply and programming

See [Development Boards](https://www.espressif.com/en/products/devkits) for more information about it.

### Build and Flash

Run `idf.py -p PORT flash monitor` to build, flash and monitor the project.

(To exit the serial monitor, type ``Ctrl-]``.)

See the [Getting Started Guide](https://idf.espressif.com/) for full steps to configure and use ESP-IDF to build projects.

## Example Output

```
I (4452) GATTS_SPP_DEMO: EVT 14, gatts if 3
I (4452) GATTS_SPP_DEMO: event = e
I (5022) GATTS_SPP_DEMO: EVT 4, gatts if 3
I (5022) GATTS_SPP_DEMO: event = 4
I (5152) GATTS_SPP_DEMO: EVT 2, gatts if 3
I (5152) GATTS_SPP_DEMO: event = 2
I (5152) GATTS_SPP_DEMO: ESP_GATTS_WRITE_EVT : handle = 5
I (5242) GATTS_SPP_DEMO: EVT 2, gatts if 3
I (5242) GATTS_SPP_DEMO: event = 2
I (5242) GATTS_SPP_DEMO: ESP_GATTS_WRITE_EVT : handle = 10
I (18462) GATTS_SPP_DEMO: EVT 5, gatts if 3
I (18462) GATTS_SPP_DEMO: event = 5
I (27652) GATTS_SPP_DEMO: EVT 2, gatts if 3
I (27652) GATTS_SPP_DEMO: event = 2
I (27652) GATTS_SPP_DEMO: ESP_GATTS_WRITE_EVT : handle = 2
```

## Troubleshooting

For any technical queries, please open an [issue](https://github.com/espressif/esp-idf/issues) on GitHub. We will get back to you soon.
