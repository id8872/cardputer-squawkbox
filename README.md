# 📈 Squawk Box v9.9 — M5Stack Cardputer Edition

**Squawk Box** is a portable, algorithmic stock market momentum tracker designed for the [M5Stack Cardputer](https://m5stack.com/products/m5stack-cardputer).

Originally built for the Particle Photon 2, this edition leverages the Cardputer's ESP32-S3, built-in TFT display, keyboard, and speaker to create a completely standalone trading companion. It directly polls the [Finnhub REST API](https://finnhub.io/) to track live price action for major ETFs (SPY, QQQ, IWM), calculating dual-EMA momentum velocity to generate real-time audible and visual trading signals.

Whether you're tracking Bull Rushes, Bear Dumps, or navigating Chop Zones, Squawk Box acts as your automated co-pilot — complete with a built-in paper trading engine and a local web dashboard accessible from any device on your network.

---

## ✨ Features

- **Zero hardcoded credentials** — First boot launches a captive portal (`SQUAWKBOX-SETUP`) to enter your WiFi and Finnhub API key. No reflashing ever needed to change them.
- **Live TFT display** — Real-time velocity bar chart, current price, position status, open and closed paper PnL, and clock.
- **Algorithmic signal engine** — Two-step confluence confirmation (BREAK → RUSH/DUMP within 15s) filters out false breakouts before firing a signal.
- **Full-screen flash alerts** — BUY, SELL, and EXIT signals take over the entire display with 3 colour flashes so nothing gets missed.
- **Audible alerts** — Distinct tones for bull, bear, and momentum surge events. Fully mutable.
- **Smart API polling** — Poll rate adapts to market hours (2s at open/close, 10s at lunch, 60s off-hours, 5min on weekends) to stay within Finnhub's free-tier rate limit.
- **Local web dashboard** — Full HTML dashboard and JSON API served on your LAN. Change symbols, adjust chop limit, mute audio, test tones, and reboot — all from a browser.
- **Paper trading engine** — Automatically opens and closes simulated Long/Short positions based on signal confirmations. Tracks open PnL, closed PnL, and trade count.
- **Persistent settings** — All configuration (symbol, chop limit, mute state, backlight) survives reboots via ESP32 NVS flash.

---

## 🛠️ Hardware

- [M5Stack Cardputer](https://m5stack.com/products/m5stack-cardputer) (ESP32-S3)
- Free API key from [finnhub.io](https://finnhub.io/)

---

## 📦 Required Libraries

Install via Arduino Library Manager:

| Library | Source |
|---|---|
| M5Cardputer | M5Stack official |
| M5GFX | M5Stack official |
| ArduinoJson v7+ | Benoit Blanchon |
| Preferences | Built-in ESP32 core |
| DNSServer | Built-in ESP32 core |

---

## 🚀 First Time Setup

1. Flash `squawkbox_cardputer.ino` to your Cardputer as-is — no configuration needed beforehand.
2. On first boot, the device creates a WiFi hotspot: **`SQUAWKBOX-SETUP`** (no password).
3. Connect your phone or laptop to that network.
4. A setup page opens automatically, or visit **`192.168.4.1`** in your browser.
5. Enter your WiFi network name, password, and Finnhub API key → tap **Save & Connect**.
6. The device saves your credentials and reboots into normal operating mode.

**To change credentials later:** Hold **`W`** while powering on (or while rebooting with `R`). The setup portal will launch again.

---

## ⌨️ Keyboard Shortcuts

| Key | Action |
|---|---|
| `M` | Toggle mute |
| `B` | Toggle backlight |
| `1` | Switch to SPY |
| `2` | Switch to QQQ |
| `3` | Switch to IWM |
| `+` | Increase chop limit by 0.001 |
| `-` | Decrease chop limit by 0.001 |
| `T` | Test bull tone |
| `Y` | Test bear tone |
| `R` | Reboot |
| Hold `W` at boot | Enter WiFi setup portal |

---

## 📡 Web Dashboard

Once connected, the IP address is shown on the splash screen and in the bottom-right of the display. Open it in any browser on the same network.

The dashboard provides:
- Live price and velocity chart (auto-refreshes every 3 seconds)
- Paper trading PnL tracker
- Recent alert log
- Symbol preset buttons
- Chop limit tuning
- Mute / test tones / reboot controls

The raw JSON data endpoint is available at `http://<device-ip>/data` if you want to integrate with other tools.

---

## 📊 Signal Logic

Squawk Box uses a **two-step confluence model** to reduce false signals:

| Step | Event | Meaning |
|---|---|---|
| 1 | **BULL BREAK** | Velocity crosses above the chop limit — trend starting upward |
| 2 | **BULL RUSH** | Velocity accelerates ≥ 0.016% within 15s of the BREAK — **BUY signal fires** |
| 1 | **BEAR BREAK** | Velocity crosses below the chop limit — trend starting downward |
| 2 | **BEAR DUMP** | Velocity drops ≤ -0.025% within 15s of the BREAK — **SELL signal fires** |
| — | **TREND END** | Velocity returns inside the chop band — **position closed** |

If the confirmation (RUSH or DUMP) doesn't arrive within 15 seconds of the BREAK, the pending signal is discarded.

**Lunch exit:** A TREND END between 12:00–1:30 PM EST shows a dedicated LUNCH CHOP ZONE alert, reflecting the historically low signal quality during midday.

---

## 🔒 API Key & Security

Your WiFi password and Finnhub API key are stored in ESP32 NVS (non-volatile storage) flash. This storage is **not encrypted by default**.

- Anyone with physical access to the device and a USB cable could potentially read the flash contents.
- **Do not use a paid or premium Finnhub API key** with this firmware unless you are the sole owner of the device and it is kept physically secure.
- The Finnhub free tier has no billing attached — the worst-case scenario of a leaked free key is hitting the 60 calls/minute rate limit, not financial exposure.
- Users are responsible for complying with [Finnhub's Terms of Service](https://finnhub.io/terms).

---

## 📋 Symbol Presets

Each symbol has tuned EMA and chop parameters to match its typical volatility:

| Symbol | Fast α | Slow α | Chop Limit |
|---|---|---|---|
| SPY | 0.22 | 0.10 | 0.010% |
| QQQ | 0.25 | 0.12 | 0.014% |
| IWM | 0.28 | 0.14 | 0.035% |

The chop limit can be fine-tuned at runtime via `+`/`-` keys or the web dashboard.

---

## ⚠️ Disclaimer

**FOR EDUCATIONAL AND ENTERTAINMENT PURPOSES ONLY.**

Squawk Box is a personal hobby project and paper-trading simulator. It does **not** constitute financial advice, investment advice, or a recommendation to buy or sell any security. All signals are experimental and based on short-term EMA velocity — they have no guaranteed predictive value. Finnhub data is delayed and subject to their API terms; it is not suitable as the sole basis for real trading decisions. Past paper performance does not imply future real-world results. Never trade real money based solely on this tool. The author accepts no responsibility for any financial losses incurred from use of this software. Consult a licensed financial advisor before making any investment decisions.

---

## 📜 License

MIT License — see `LICENSE` for full text.

Copyright (c) 2025 Jason Edgar
