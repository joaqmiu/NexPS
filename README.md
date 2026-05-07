# NexPS rebirth

**NexPS** is a high-performance native PSP game downloader and converter for the Nintendo Switch. A spiritual successor to the npsp project, it allows users to browse the **NoPayStation (NPS)** library, download titles in `.pkg` format directly to the console, and extract the decrypted `EBOOT.PBP` in real-time using AES-128-CTR.

---

## 🚀 Installation Guide

1.  **Download the tool**: Get the latest stable release.
2.  **Transfer to SD Card**: Copy the `NexPS` folder into the `/switch/` directory on your microSD card.
3.  **Default Path**: By default, games are saved to `/tico/roms/psp`, though this can be changed in the settings.
4.  **Launch**: Open the **Homebrew Menu** or **Sphaira** on your Switch and select **NexPS** to begin.

---

## ✨ Features & What's New

* **Intuitive Folder Browser**: No more manual typing. Select your installation directory by navigating your SD card folders through a visual interface.
* **Customizable Speed**: Choose between three download modes (Slow, Recommended, or Fast) utilizing multi-threading to maximize your bandwidth.
* **Native Decryption**: Direct extraction of `EBOOT.PBP` from PKG files via a native C implementation, ensuring high-speed processing and compatibility.
* **Advanced Filtering**: Find games by name, filter by region (JP), numeric titles, or browse via a vertical alphabetical list.

---

## 🎮 Controls

| Button | Action |
| :--- | :--- |
| **D-Pad Up/Down** | Navigate through lists (Hold for rapid scroll) |
| **D-Pad Left/Right** | Skip pages or 10 entries at once |
| **Button A** | Select / Confirm / Open Folder |
| **Button B** | Back to Menu / Cancel Download |
| **Button X** | Confirm current directory (in Folder Browser) |

---

## 🛠️ Technical Specifications

### Build Requirements
To compile **NexPS**, you must have the **devkitPro** environment installed with the following libraries:
* `devkitA64`
* `libnx`
* `switch-curl`
* `switch-mbedtls`
* `switch-zlib`

### Building from Source
1.  Clone the repository.
2.  Ensure your `icon.png` is in the root directory.
3.  Run `make` in your terminal.
4.  The `NexPS.nro` file will be generated in the project root.

---

## ⭐ Credits

* **Developer**: [joaqmiu](https://github.com/joaqmiu) (Joaquim Sintra e Olivares)
* **Special Thanks**: 
    * Mestre Sion/Jann
    * NewsInside
    * Switchnamão
* **Libraries**: libnx, mbedTLS, cURL.
* **Data Sources**: NoPayStation (TSV).
