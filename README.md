# FacelessLoader, a simple bootloader.

### FacelessBootProtocol Specification

Getting started:<br>

Make sure your kernel has ``struct RuntimeAndDataServices`` passed<br>
in as an argument in the entry point.<br><br>

# ``services.display_terminal(uint32_t x, uint32_t y)``
This allows you to display a terminal at x, y position.
