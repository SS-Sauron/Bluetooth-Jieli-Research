# Third-Party Tools Used in This Research

| Tool | Version | Purpose | Source |
| :--- | :--- | :--- | :--- |
| **bletools** | Git (2024) | BLE scanning and service enumeration | https://github.com/nmatt0/bletools |
| **bettercap** | v2.40.0 (built from source) | BLE GATT interaction and fuzzing | https://github.com/bettercap/bettercap |
| **btlejuice** | Git (2024) | BLE MITM framework (requires second adapter) | https://github.com/DigitalSecurity/btlejuice |
| **Spooftooph** | Git mirror | Bluetooth MAC spoofing (requires CSR adapter) | https://github.com/Afsalmc/Spooftooph-mirror |

## Installation Notes

- **bettercap**: Built from source with `CGO_ENABLED=1 make build` to include BLE module.
- **btlejuice**: Run directly from source using `node core.js` and `node proxy.js`.
- **bletools**: Used as reference; custom scripts in `../scripts/ble/` supersede it.

## Hardware Requirements

- For btlejuice: Two Bluetooth adapters (one Realtek, one CSR recommended).
- For MAC spoofing: CSR-based adapter required (Realtek does not support).
