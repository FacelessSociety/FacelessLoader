# FacelessLoader, a simple bootloader.

### FacelessBootProtocol Specification

Getting started:<br>

Make sure your kernel has ``struct RuntimeAndDataServices`` passed<br>
in as an argument in the entry point.<br><br>

``services.display_terminal(uint32_t x, uint32_t y)``<br>
This allows you to display a terminal at x, y position.

``services.display_wallpaper(void)``<br>
This allows you to refresh the __entire__ screen with the wallpaper
selected at boot.<br>

``services.refresh_wallpaper(void)``<br>
This allows you to refresh only a section of the screen (with wallpaper).

``services.term_write(const char*str, uint32_t color)``<br>
Allows you to write to the terminal.
