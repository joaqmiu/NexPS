# NexPS v1.1

**NexPS** is a high-performance native PSP and PSX game downloader and converter for the Nintendo Switch. It provides a seamless interface to browse the NoPayStation (NPS) library, download titles in `.pkg` format directly to the console, and perform real-time cryptographic extraction using AES-128-CTR.

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
* **Asynchronous Multi-Threaded Downloading:** Configurable connection pooling (1, 4, or 8 threads) to maximize network throughput and minimize download latency.
* **Dynamic File Management:** Built-in directory browser to assign target installation paths directly via the Switch UI, eliminating the need for manual text entry.
* **Robust Filtering System:** Real-time search and sorting capabilities, including alphabetical index, region isolation (e.g., JP), and platform-specific views.

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
