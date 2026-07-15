<picture>
<source media="(prefers-color-scheme: dark)" srcset="https://i.imgur.com/8qsV6MH.png">
<source media="(prefers-color-scheme: light)" srcset="https://i.imgur.com/4cpzGnB.png">
<img src="https://i.imgur.com/8qsV6MH.png" width="200">
</picture>

*Part of the Tico ecosystem* - https://www.ticoverse.com

**Azahar** is an open-source emulator for the Nintendo 3DS, based on Citra and built from the merging of PabloMK7's Citra fork and Lime3DS.

This fork adapts Azahar to work with the Tico frontend and provides a standalone build for the Nintendo Switch, adding a small set of practical features while preserving the compatibility and design direction of the original project.

----------

## Summary

This fork focuses on making Azahar more usable in practice on Nintendo Switch without changing its core design.

It adds:

- Custom overlay matching Tico design, including time, date, user avatar, and game title
- Quick menu integration for display, state, and core options
- Explicit control over display orientation, layout, screen size, swap screens, and large-screen proportion
- Runtime-selectable graphics options such as internal resolution, shader options, texture filtering, and right-eye rendering
- Built-in save and load state support
- Touchscreen support mapped to the active 3DS bottom-screen layout
- Tico chainload integration for returning to the frontend

----------

## Content Support

Azahar is intended for legally obtained Nintendo 3DS software and user-provided system data.

This fork follows Azahar's existing content support boundaries:

- It does not bypass Nintendo 3DS encryption
- It does not run encrypted games
- It does not install or run CIA packages directly
- It expects decrypted, user-owned content in supported formats
- It does not include games, firmware, keys, BIOS files, or copyrighted Nintendo data

----------

## Credits

This port is built on top of the official Azahar emulator project.

All core emulation work belongs to the Azahar team and its contributors, including the Citra, PabloMK7 Citra, and Lime3DS projects that Azahar is based on.

Official Azahar repository - [https://github.com/azahar-emu/azahar](https://github.com/azahar-emu/azahar)

Azahar website - [https://azahar-emu.org](https://azahar-emu.org/)

----------

## A Note

A lot of work in this scene disappears over time - not because it lacked value, but because it was never shared.

If you are building something, consider releasing it. Even small contributions can help others move forward.
