# Dialogue Component Editor

A browser-based visual node-graph editor for the [Dialogue Component](https://www.fab.com/listings/340335dd-aece-4e31-bc5d-8736a5fc4c3f) in Unreal Engine. Edit dialogue branches, player choices, and component settings in a fast, visual interface that syncs live with UE.

![UE Versions](https://img.shields.io/badge/UE-5.0%20%7C%205.5%20%7C%205.6%20%7C%205.7%20%7C%205.8-blue) ![License](https://img.shields.io/badge/license-proprietary-red) ![Version](https://img.shields.io/badge/version-1.005-amber)

## Features

- **Visual Node Graph** — Drag-and-drop dialogue branch editor in your browser
- **Live Sync** — Changes push to Unreal instantly, no manual import/export
- **Split-Button Toolbar** — Main action launches the editor; arrow dropdown exposes per-project settings (auto-launch EUW, open-browser-after-launch, verbose logging)
- **Rich Text Style Editor** — Spreadsheet view for bulk-editing DT_RichText (font size, color, spacing across all rows at once)
- **Full Property Editing** — Booleans, enums, colors, structs, asset references, all editable with instant UE sync
- **Multi-Project Support** — Dynamic port allocation lets you run editors for multiple UE projects simultaneously
- **One-Click Launch** — Click the toolbar button in UE, editor opens in your browser
- **Optional Legacy EUW** — A dedicated EUW button in the editor header opens the legacy Editor Utility Widget when you want it (off by default, never auto-spawns)
- **Update Notifier** — Editor checks GitHub on load and surfaces an amber chip if a newer version is available for your engine

## Requirements

- **Unreal Engine 5.0, 5.5, 5.6, or 5.7** (separate branches and release zips per engine)
- **[Dialogue Component](https://www.fab.com/listings/340335dd-aece-4e31-bc5d-8736a5fc4c3f)** asset installed in your project
- **Python Editor Script Plugin** (auto-enabled as a dependency)

## Installation

### Option A: Drop-in download (recommended for end users)

1. Go to [Releases](https://github.com/dylogaming/Dialogue-Component-Editor/releases) and download the zip matching your engine version, e.g. `DialogueComponentEditor-UE_5.5-v1.001.zip`
2. Extract the `DialogueComponentEditor` folder into your project's `Plugins/` directory
3. Open your UE project. The plugin loads automatically.
4. Click the **Dialogue Editor** button in the toolbar.

### Option B: Clone from source (for developers)

Pick the branch matching your engine version:

```
git clone --branch 5.0 https://github.com/dylogaming/Dialogue-Component-Editor.git
git clone --branch 5.5 https://github.com/dylogaming/Dialogue-Component-Editor.git
git clone --branch 5.6 https://github.com/dylogaming/Dialogue-Component-Editor.git
git clone --branch 5.7 https://github.com/dylogaming/Dialogue-Component-Editor.git
```

Copy the `DialogueComponentEditor` folder into your project's `Plugins/` directory and open the project.

## How to Use

1. **Select an actor** with a Dialogue Component in your level
2. Click the **Dialogue Editor** toolbar button. Your browser opens the editor.
3. The editor auto-detects the selected actor and loads its dialogue data
4. Edit branches, choices, and settings visually. Changes sync to UE in real time.
5. Click the **arrow** on the toolbar button for settings: toggle the auto-launch EUW, browser-after-launch, verbose logging.
6. Click the **EUW** button in the editor header if you want to manually open the legacy Editor Utility Widget tab in Unreal.

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

## Versioning

Versions follow `MAJOR.PPP` (three-digit zero-padded minor). Current release is **1.001**. Each release on this repo includes one zip per supported engine version with pre-compiled binaries.

The plugin reads its own version from the bundled `.uplugin` and displays it in the editor's menu bar and header. If the editor detects a newer version is available on GitHub for your engine, the version chip turns amber with a tooltip and click-through to the matching branch.

## Support

- **Documentation:** [dylo-gaming.gitbook.io/documentation](https://dylo-gaming.gitbook.io/documentation/install)
- **Issues:** [GitHub Issues](https://github.com/dylogaming/Dialogue-Component-Editor/issues)
- **Support:** dylogamingofficial@gmail.com
- **Donations:** [ko-fi.com/dylogaming](https://ko-fi.com/dylogaming)

## License

Copyright 2026 DYLO Gaming LLC. All Rights Reserved.

This plugin is distributed as a free add-on for the Dialogue Component.
