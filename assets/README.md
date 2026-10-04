# Image assets

Screen-ready images are grouped by their exact pixel dimensions:

- `720x720/`: square artwork for the Tab5 layout, including JPEG quality variants.
- `800x800/`: square Q60 artwork for 800-pixel display areas.
- `1280x720/` and `1280x800/`: test-card images with matching landscape canvases.
- `source/`: original high-resolution artwork and test card used to create the screen-ready images.

The build selects matching embedded data sources by `RETROSCOPE_TARGET`: Tab5 uses the 720×720 and 1280×720 sets; JC8012 uses the 800×800 and 1280×800 sets. The ETH-2RO profile currently uses the Tab5-sized set until its display dimensions are known.

Screen images are embedded in C++ source files. Moving the source files here does not change the firmware build inputs. Test-card data is linker-discarded until the application references it.
