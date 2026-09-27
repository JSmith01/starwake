# Starwake

Starwake is a compact horizontal space shooter available as a browser game and as a native ESP-IDF port for the LCDWiki ES3C35P / ES3C35P-NS ESP32-S3 handheld.

## Browser game

Open `index.html` in a modern browser to play. Move with **WASD** or the **arrow keys**, fire with **Space**, charge a shot by holding **Shift** and releasing, and pause with **P**. Sound starts after the first input.

## ESP32-S3 port

The firmware lives in [`esp32s3-hosyond/`](esp32s3-hosyond/). It renders the game on the board's 320×480 display and uses the touchscreen for flight, charge, pause, and sound controls. The firmware README documents the board setup, pins, ESP-IDF requirements, build and flash commands, and known hardware validation limits.

The firmware uses ESP-IDF 5.5.5 or newer. From an activated ESP-IDF PowerShell environment:

```powershell
cd esp32s3-hosyond
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

## License

See the firmware's [`LICENSES/`](esp32s3-hosyond/LICENSES/) directory for bundled component license notices. The browser game's source is provided in this repository.
