# NexPS

**NexPS** is a high-performance native PSP and PSX game downloader and converter for the Nintendo Switch. It provides a seamless interface to browse the NoPayStation (NPS) library, download titles in `.pkg` format directly to the console, and perform real-time cryptographic extraction. Additionally, it functions as a dynamic ROM hub, allowing users to expand their library with custom consoles via the Internet Archive.

---

## Installation

1. Download the latest compiled `.nro` release.
2. Copy the `NexPS` folder to the `/switch/` directory on your microSD card.
3. Launch the application via the Homebrew Menu.

*Note:* By default, the application routes PSP games to `/tico/roms/psp` and PSX games to `/tico/roms/psx`. Both target directories can be modified within the application settings.

---

## Key Features (v1.1)

* **Multi-Platform Support:** Full integration of the PSX library alongside the existing PSP database. Includes platform-specific filtering and dedicated installation paths.
* **Advanced Cryptographic Extraction:** Native on-device C implementation to extract and decrypt `EBOOT.PBP` files from PKG containers. For PSX titles, NexPS automatically unpacks the `DATA.PSAR` archive into a `.BIN` file, generates a corresponding `.CUE` sheet, and dumps the `KEYS.BIN` license file.
* **Dynamic Custom Consoles:** Add new consoles and ROM lists on the fly by fetching metadata directly from public Internet Archive collections.

---

## Adding Custom Consoles

NexPS allows you to expand your library by adding custom ROM collections directly from the Internet Archive (IA).

1. On the main menu, navigate to and select **+ ADD CONSOLE**.
2. Enter a display name for the new console (e.g., `Super Nintendo`).
3. Enter the **Internet Archive Item ID**. This ID is the final segment of the collection's URL.
   * *Example URL:* `https://archive.org/details/roms-snes-smc-3126-jogosj`
   * *Item ID to enter:* `roms-snes-smc-3126-jogosj`
4. Use the built-in directory browser to select the target folder on your SD card where these ROMs should be downloaded.
5. Confirm your selection. The new console will appear on your main menu, and NexPS will automatically parse the IA metadata to list the available games.

---

## Build Instructions

### Requirements
Compilation requires the **devkitPro** toolchain with the following packages installed:
* `devkitA64`
* `libnx`
* `switch-curl`
* `switch-mbedtls`
* `switch-zlib`

### Compilation
1. Clone the repository.
2. Ensure the `icon.png` asset is present in the root directory.
3. Execute `make` in your terminal.
4. The compiled `NexPS.nro` will be output to the project root.

---

## Credits

* **Development:** [joaqmiu](https://github.com/joaqmiu) (Joaquim)
* **Special Thanks:** Mestre Sion/Jann, NewsInside.org, Switchnamão
* **Core Libraries:** libnx, mbedTLS, cURL, zlib
* **Database Provision:** NoPayStation (TSV)

