# Hardware test checklist (Windows)

Automated tests only cover the protocol encoding, so every camera feature needs
a real test on Windows with the OBSBOT plugged in. Run through this list after
each release.

## Before you start

1. Close OBSBOT Center, Teams, Zoom, OBS and the Camera app (only one app can
   use the video stream at a time).
2. Download `osbot-oss-windows.exe` from the latest GitHub Release (or the
   **Actions** tab → latest run → *Artifacts*), and run it. Windows SmartScreen
   may warn because the exe isn't code-signed: choose *More info → Run anyway*.

## Checklist

Tick each box or note what happened.

Some checks use the **Developer** tab. Turn it on with *Camera → Advanced → Show
developer tools*.

### Connection and preview
- [ ] The header shows "OBSBOT Tiny 2 Lite" with a green dot.
- [ ] The live preview appears. Note the format shown at the top left.
- [ ] Unplug the camera: the header goes red within a few seconds. Plug it back in: it reconnects.
- [ ] Untick *Camera → Show live preview*: the preview stops and controls still work.

### AI tracking (Control tab)
- [ ] *Normal* starts tracking you. The button stays highlighted.
- [ ] *Upper body*, *Close-up*, *Headless*, *Lower body*: framing changes.
- [ ] *Group*, *Hand*, *Whiteboard*, *Desk*: note which work.
- [ ] *Off* stops tracking.
- [ ] *Sport* tracking speed: does the Log say "camera acknowledged"?

### Gimbal and zoom
- [ ] *Smooth (vendor)*: hold each arrow; the camera moves and **stops when you let go**.
- [ ] Is left/right the right way round? If not, tick *Invert left/right* and note it.
- [ ] Is up/down the right way round? (*Developer → Invert up/down*)
- [ ] *Steps (UVC)*: hold each arrow; does the camera move?
- [ ] *Pan*, *Tilt* and *Zoom* sliders move the camera.
- [ ] *Centre* returns the camera to the middle. What does the Log say?

### Presets
- [ ] Save preset 1, move away, press *Go*: it returns.

### Image tab
- [ ] Brightness, contrast, saturation, sharpness change the picture.
- [ ] White balance, exposure and focus *Auto* checkboxes toggle.
- [ ] *Reset image to camera defaults* works.

### Camera tab
- [ ] *HDR* toggles (the picture changes subtly; the checkbox stays ticked).
- [ ] *Field of view* Wide/Medium/Narrow changes the picture. If the wrong one is
      selected, or nothing happens, try *Developer → Protocol variant → Tiny 2 codes* and note it.
- [ ] *Anti-flicker* 50 Hz is selected by default.
- [ ] *Sleep camera* puts it to sleep (the lens usually points down); *Wake camera* wakes it.

## Reporting results

1. Go to the **Log** tab and press **Copy diagnostics**.
2. Open a [GitHub issue](../../../issues) and paste that, plus your ticked
   checklist. The diagnostics include the camera's raw status block and control
   ranges, which is exactly what's needed to fix the protocol tables in
   `docs/PROTOCOL.md`.
