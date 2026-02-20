# 📈 Squawk Box v9.3 - M5Stack Cardputer Edition

**Squawk Box** is a portable, algorithmic stock market momentum tracker designed for the(https://m5stack.com/products/m5stack-cardputer). 

Originally built for the Particle Photon 2, this Advanced Edition leverages the Cardputer's ESP32-S3, built-in TFT display, keyboard, and speaker to create a completely standalone trading companion. It directly polls the Finnhub REST API to track live price action for major ETFs (SPY, QQQ, IWM), calculating dual-EMA momentum velocity to generate real-time audible and visual trading signals.

Whether you're tracking "Bull Rushes," "Bear Dumps," or navigating "Chop Zones," Squawk Box acts as your automated co-pilot, complete with a built-in paper trading engine and local web dashboard.

### ✨ Key Features
* **No Hardcoded Credentials:** First-time boot creates a captive portal (`SQUAWKBOX-SETUP`) to securely enter your WiFi and Finnhub API key.
* **Live TFT Dashboard:** Real-time data visualization including a velocity bar chart, current price, and live paper PnL.
* **Algorithmic Alerts:** Audible buzzer alerts for trend breaks, trend exhaustion, and high-velocity rushes/dumps.
* **Smart API Polling:** Automatically adjusts refresh rates based on active market hours and weekends to respect API rate limits.
* **Built-in Web Server:** Hosts a live HTML dashboard and JSON API on your local network for remote monitoring via phone or PC.
* **On-Device Controls:** Use the Cardputer's physical keyboard to switch symbols, adjust chop limits, toggle audio, and view system diagnostics (Battery, WiFi, IP).
* **Paper Trading Engine:** Automatically logs simulated Long/Short entries based on algorithmic confirmation triggers.

### 🛠️ Hardware Requirements
* **M5Stack Cardputer** (ESP32-S3)
* Free API key from(https://finnhub.io/)
