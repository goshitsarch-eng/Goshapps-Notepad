# Goshapps Notepad

A faithful Windows 98 Notepad clone built with C, GTK4, and libadwaita. It looks and feels native on GNOME/Linux while keeping the classic Notepad menu layout and behavior.

All the usual stuff works: File, Edit, Format, Help menus with the same shortcuts you'd expect (Ctrl+S, Ctrl+F, F3, etc). Find and Replace dialogs stay open while you work. Word wrap toggles on and off. Font picker changes the editor font. Status bar shows your current line and column. Unsaved changes get a proper save prompt before anything destructive happens.

## Install (Arch Linux)

Available on the AUR:

```
yay -S goshapps-notepad
```

## Building from source

Needs GTK4 (4.10+) and libadwaita installed.

```
meson setup build
ninja -C build
./build/notepad
```

## AppImage

There's a prebuilt AppImage in releases. It uses system GTK4/libadwaita rather than bundling them since GTK4 doesn't play well with bundled libraries in AppImages.

## Screenshots

*coming soon*
