droidpad-pc-driver
==================

This driver is for DroidPad 2.0's Joystick features.

Driver information
------------------

The vJoyInstall folder contains a slightly modified version of vJoy's installer which can install the driver when run. The driver is normally installed through code in DroidPad, based off this installer.

The actual driver itself is largely based off the hidusbfx2 sample in the DDK samples folder. The source code in the hidmapper is exactly the same as it is in the sample.

The out/ folder is where compiled binaries are copied, to allow them to be collected together.

The sys/ folder contains the main driver itself. Much of this is still the same as the hidusbfx2 sample, but with some USB code removed and some loopback code added.
