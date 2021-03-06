# desktop-toasts
Samples for Win32 desktop apps using toast notifications with COM activation.

This sample demonstrates how a desktop app can display a toast notification and respond to events generated by the user's interaction with the toast. 

This sample is provided as-is in order to indicate or demonstrate the functionality of the programming models and feature APIs.

How to use the sample...

## Desktop Bridge

1. Open the project in VS
2. Set the `DesktopToastsPackageProject` as the startup project
3. Deploy

Toasts will work out-of-the-gate thanks to Desktop Bridge!

## Classic Win32

1. Install WiX Toolset if you haven't yet
2. Open the project in VS
3. Build the SetupProject
4. Install the generated MSI from the bin/Debug folder
5. Launch the Desktop Toasts app from the Start menu

After you've installed with the MSI once, you can debug straight from Visual Studio. Installing via the MSI creates the Start menu shortcut with the AUMID and COM CLSID so your notifications can appear and be actionable.

If you don't install the MSI first, toasts will not appear.
