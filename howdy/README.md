# Howdy (Windows Hello Face Unlock) Tuning for Intel IPU7

On the Surface Laptop 7 (Intel Core Ultra Series 2), the webcam runs through a software ISP to `/dev/video42`. To ensure reliable and fast face authentication across all lighting conditions, desktop logins, and polkit dialogs (e.g. App Store):

### Key `/etc/howdy/config.ini` Settings:
- **`device_path = /dev/video42`**: Point directly to the loopback camera node.
- **`timeout = 15`**: Gives adequate time (up from 10s default) for the hardware sensor to cold-wake and converge auto-exposure.
- **`max_height = 480`**: Increases face detection resolution from 320px so facial landmarks (eyes, brow, nose, mouth) can be detected accurately even with hats, glasses, or low-contrast shadows.
- **`certainty = 5.8`**: Calibrated threshold allowing reliable recognition across varying head angles and headwear.
