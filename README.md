# Dialogue Component Editor

A browser-based visual node-graph editor for the [Dialogue Component](https://www.fab.com) in Unreal Engine. Edit dialogue branches, player choices, and component settings in a fast, visual interface that syncs live with UE.

![Editor Preview](https://img.shields.io/badge/UE-5.0%20|%205.7-blue) ![License](https://img.shields.io/badge/license-proprietary-red)

## Features

- **Visual Node Graph** — Drag-and-drop dialogue branch editor in your browser
- **Live Sync** — Changes push to Unreal instantly, no manual import/export
- **Rich Text Style Editor** — Spreadsheet view for bulk-editing DT_RichText (font size, color, spacing across all rows at once)
- **Full Property Editing** — Booleans, enums, colors, structs, asset references — all editable with instant UE sync
- **Multi-Project Support** — Dynamic port allocation lets you run editors for multiple UE projects simultaneously
- **One-Click Launch** — Click the toolbar button in UE, editor opens in your browser

## Requirements

- **Unreal Engine 5.0+** (branches available for 5.0 and 5.7)
- **[Dialogue Component](https://www.fab.com/listings/340335dd-aece-4e31-bc5d-8736a5fc4c3f)** asset installed in your project
- **Python Editor Script Plugin** (auto-enabled as a dependency)

## Installation

1. Download or clone the branch matching your engine version:
   - UE 5.0: `git clone -b 5.0 https://github.com/dylogaming/Dialogue-Component-Editor.git`
   - UE 5.7: `git clone -b 5.7 https://github.com/dylogaming/Dialogue-Component-Editor.git`
2. Copy the `DialogueComponentEditor` folder into your project's `Plugins/` directory
3. Open your UE project — the plugin loads automatically
4. Click the **Dialogue Editor** button in the toolbar

## How to Use

1. **Select an actor** with a Dialogue Component in your level
2. Click the **Dialogue Editor** toolbar button — your browser opens the editor
3. The editor auto-detects the selected actor and loads its dialogue data
4. Edit branches, choices, and settings visually — changes sync to UE in real time
5. Use **Alt+R** to refresh from UE, **Alt+G** to toggle global variables
6. Use **Ctrl+C/V** to copy/paste branches, **Delete** to remove them

### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Alt+R | Refresh from UE |
| Alt+G | Toggle Global Variables |
| Ctrl+Z / Ctrl+Y | Undo / Redo |
| Ctrl+C / Ctrl+V | Copy / Paste branches |
| Ctrl+Shift+C/V | Copy / Paste branch attributes |
| Delete | Delete selected branch(es) |
| Home | Snap to start branch |
| F2 | Rename selected branch |
| Right-click drag | Pan canvas (works anywhere) |
| Scroll wheel | Zoom |
| Double-click output pin | Spawn and link a new branch |

### Rich Text Style Editor

Access from the **Main** panel next to "Rich Text" or from **View > Rich Text Style Editor**. Edit font size, color, letter spacing, and other properties across all rich text rows in a spreadsheet view. Use the **ALL** row to apply a value to every row at once.

## Support

- **Documentation:** [dylo-gaming.gitbook.io/documentation](https://dylo-gaming.gitbook.io/documentation/install)
- **Issues:** [GitHub Issues](https://github.com/dylogaming/Dialogue-Component-Editor/issues)
- **Support:** dylogamingofficial@gmail.com
- **Ko-fi:** [ko-fi.com/dylogaming](https://ko-fi.com/dylogaming)

## License

Copyright 2026 DYLO Gaming. All Rights Reserved.

This plugin is distributed as a free add-on for the Dialogue Component.
