# TODO

## Make the code easier to port to other e-paper displays
The drawing code is currently tied to the Waveshare 1.54" (G) 4-color panel
(about 47 uses of `EPD_1IN54G_*` in the .ino and weather_icons.cpp).

- [ ] **Colour names:** define `COL_BG`, `COL_TEXT` and `COL_ACCENT` once, instead of
      using `EPD_1IN54G_*` everywhere. A black/white panel then only needs these remapped.
- [ ] **Panel calls:** put the init, display and sleep calls (`EPD_1IN54G_Init`,
      `EPD_1IN54G_Display`, `EPD_1IN54G_Sleep`) in one small wrapper, so a new panel
      only changes that wrapper.
- [ ] **Layout:** express positions in terms of `EPD_WIDTH` and `EPD_HEIGHT` where
      possible, instead of hard-coded 200x200 coordinates.
