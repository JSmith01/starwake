# Starwake for LCDWiki ES3C35P / ES3C35P-NS

Native ESP-IDF port of the browser game. Target: ESP32-S3 N16R8 and the LCDWiki 3.5 inch portrait 320x480 ST77922 QSPI display. The game is rendered landscape (480x320) by rotating the panel in software, with a 50 pixel touch control strip.

## Controls

- Drag anywhere in the playfield to move; the ship fires automatically.
- Hold **CHARGE** to charge and release to launch a charged shot.
- Tap **PAUSE** to pause/resume, and **SOUND** to mute.
- GPIO0 BOOT is also sampled as a pause/resume button.

The game has three enemy classes, pickups, three starting lives, a three phase Rift Warden fight, score, title, pause, loss, and victory screens. Graphics are drawn directly into an RGB565 framebuffer with geometric sprites and a built-in pixel font. The panel refreshes full-width RGB565 stripes because ST77922 QSPI writes require aligned X coordinates.

## Build and flash

Install ESP-IDF 5.5.5 or newer and activate its PowerShell environment. With the EIM 5.5.5 install used during development, activation is:

```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1'
$env:IDF_CCACHE_ENABLE = '0' # only needed if this install has no ccache.exe
```

From this directory run:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

The Component Manager fetches the first-party ST77922 panel driver and Espressif codec device component during the first build. Touch is read using the board's integrated ST77922 touch controller registers. Use the COM port assigned to the board. No hardware flash or physical validation has been performed as part of this port.

## Board wiring

The code uses LCDWiki's published pin map: QSPI CS=10, CLK=12, D0=11, D1=13, D2=14, D3=9; backlight=41 active high; integrated ST77922 touch I2C SDA=38/SCL=39 at address `0x55`, touch reset=48, touch interrupt=47, and BOOT=0. The panel has no separate reset GPIO. The game is rendered landscape and software-rotated into aligned portrait stripes because the controller does not support axis swapping.

The firmware configures the ES8311 and synthesizes short PCM tones through I2S on ES3C35P: MCLK=17, BCLK=18, DOUT=15, WS=21 and amplifier enable=1 active low. ES3C35P-NS may omit its speaker/amplifier; in that case audio initialization is skipped if the ES8311 is not detected.
