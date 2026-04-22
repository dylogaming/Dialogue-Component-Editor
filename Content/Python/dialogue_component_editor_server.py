"""
Dialogue Component Editor — bundled bridge server.

Shipped inside the DialogueComponentEditor UE plugin. Launched automatically
by the plugin's toolbar button. Serves the bundled HTML editor and bridges
HTTP requests to UE via file-drop Python scripts.

Usage (normally launched by the plugin, not manually):
  python dialogue_component_editor_server.py [--bridge PATH] [--port 8766] [--webdir PATH]
"""
import argparse, http.server, json, os, re, socketserver, sys, threading, time, uuid

DEFAULT_BRIDGE = ""
DEFAULT_PORT = 8766
DEFAULT_WEBDIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Web")
POLL_INTERVAL = 0.02
TIMEOUT = 15.0

# PROTECTED - do not modify without explicit request
def run_script(bridge_dir: str, body: str, op: str = "") -> dict:
    """Drop a Python script into pending/, wait for results/<name>_result.json.
    `op` is tucked into the filename so the UE log shows what ran instead of
    a bare uuid (e.g. `web_select_branch_abc123.py`). Safe-charset only."""
    uid = uuid.uuid4().hex[:12]
    safe_op = re.sub(r"[^A-Za-z0-9]+", "_", op)[:40].strip("_")
    name = f"web_{safe_op}_{uid}" if safe_op else f"web_{uid}"
    pending = os.path.join(bridge_dir, "pending", name + ".py")
    result_path = os.path.join(bridge_dir, "results", name + "_result.json")
    # Remove any stale result
    if os.path.exists(result_path):
        try: os.remove(result_path)
        except Exception: pass
    # Wrap body — gives it CLAUDE_BRIDGE_* like the real harness does, plus auto result write
    full = (
        "import unreal, json, os, traceback\n"
        "_result = {}\n"
        "try:\n"
        + "".join("    " + ln + "\n" for ln in body.splitlines())
        + "except Exception as _e:\n"
        "    _result = {'ok': False, 'error': str(_e), 'traceback': traceback.format_exc()}\n"
        f"_path = os.path.join(CLAUDE_BRIDGE_RESULTS_DIR, CLAUDE_BRIDGE_SCRIPT_NAME.replace('.py','') + '_result.json')\n"
        "with open(_path, 'w', encoding='utf-8') as _f:\n"
        "    json.dump(_result, _f, ensure_ascii=False, default=str)\n"
    )
    with open(pending, "w", encoding="utf-8") as f:
        f.write(full)
    # Poll
    deadline = time.time() + TIMEOUT
    while time.time() < deadline:
        if os.path.exists(result_path):
            try:
                with open(result_path, "r", encoding="utf-8") as f:
                    return json.load(f)
            except Exception as e:
                # might be mid-write
                time.sleep(POLL_INTERVAL); continue
        time.sleep(POLL_INTERVAL)
    return {"ok": False, "error": f"Timeout after {TIMEOUT}s (is UE running?)"}


def script_selected() -> str:
    return (
        "eas = unreal.EditorActorSubsystem()\n"
        "sel = eas.get_selected_level_actors()\n"
        "found = None\n"
        "for a in sel:\n"
        "    try:\n"
        "        for c in a.get_components_by_class(unreal.ActorComponent):\n"
        "            if 'Dialogue' in c.get_class().get_name():\n"
        "                found = {'label': a.get_actor_label(), 'path': a.get_path_name(), 'component': c.get_class().get_name()}\n"
        "                break\n"
        "        if found: break\n"
        "    except Exception: pass\n"
        "_result = {'ok': True, 'actor': found}\n"
    )

def script_select_actor(key: str) -> str:
    key_r = json.dumps(key, ensure_ascii=False)
    return (
        "eas = unreal.EditorActorSubsystem()\n"
        f"k = {key_r}\n"
        "target = None\n"
        "for a in eas.get_all_level_actors():\n"
        "    if a.get_path_name() == k: target = a; break\n"
        "if not target:\n"
        "    for a in eas.get_all_level_actors():\n"
        "        if a.get_actor_label() == k: target = a; break\n"
        "if target:\n"
        "    eas.set_selected_level_actors([target])\n"
        "    _result = {'ok': True, 'resolved_label': target.get_actor_label(), 'path': target.get_path_name()}\n"
        "else:\n"
        "    _result = {'ok': False, 'error': 'actor not found'}\n"
    )

def script_list_component_vars(actor: str) -> str:
    actor_r = json.dumps(actor, ensure_ascii=False)
    return (
        "import unreal, json\n"
        "lib = unreal.DCEditorLibrary\n"
        "out = {'ok': True, 'vars': []}\n"
        "try:\n"
        "    desc = json.loads(lib.describe_blueprint('/Game/DialogueComponent/Blueprints/Dialogue_Component'))\n"
        "    for v in desc.get('variables', []):\n"
        f"        nm = v.get('name') if isinstance(v, dict) else str(v)\n"
        "        if not nm: continue\n"
        "        try:\n"
        f"            r = json.loads(lib.get_actor_property_as_text({actor_r}, nm))\n"
        "            if r.get('ok'):\n"
        "                cat = v.get('category','') if isinstance(v, dict) else ''\n"
        "                out['vars'].append({'name': nm, 'display': v.get('display', nm) if isinstance(v, dict) else nm, 'type': v.get('type','') if isinstance(v, dict) else '', 'text': r.get('text',''), 'category': cat})\n"
        "        except Exception: pass\n"
        "except Exception as e: out['error'] = str(e)\n"
        "_result = out\n"
    )

def script_list_all_actors() -> str:
    return (
        "eas = unreal.EditorActorSubsystem()\n"
        "actors = []\n"
        "for a in eas.get_all_level_actors():\n"
        "    try:\n"
        "        has_cam = False\n"
        "        try:\n"
        "            for c in a.get_components_by_class(unreal.ActorComponent):\n"
        "                cn = c.get_class().get_name()\n"
        "                if 'Camera' in cn and 'Shake' not in cn:\n"
        "                    has_cam = True; break\n"
        "        except Exception: pass\n"
        "        actors.append({\n"
        "            'label': a.get_actor_label(),\n"
        "            'path': a.get_path_name(),\n"
        "            'class': a.get_class().get_name(),\n"
        "            'has_camera': has_cam,\n"
        "        })\n"
        "    except Exception: pass\n"
        "_result = {'ok': True, 'actors': actors}\n"
    )

def script_list_assets_by_class(class_names: list) -> str:
    cns = json.dumps(class_names)
    return (
        "ar = unreal.AssetRegistryHelpers.get_asset_registry()\n"
        "out = []\n"
        f"for cn in {cns}:\n"
        "    try:\n"
        "        for a in ar.get_assets_by_class(cn, True):\n"
        "            out.append({'name': str(a.asset_name), 'path': str(a.package_name), 'class': cn})\n"
        "    except Exception: pass\n"
        "_result = {'ok': True, 'assets': out}\n"
    )

def script_list_camera_shakes() -> str:
    return (
        "ar = unreal.AssetRegistryHelpers.get_asset_registry()\n"
        "shakes = []\n"
        "# Native class + Blueprint subclasses\n"
        "seen = set()\n"
        "for cls_name in ('CameraShakeBase',):\n"
        "    try:\n"
        "        c = unreal.load_class(None, f'/Script/Engine.{cls_name}')\n"
        "    except Exception: c = None\n"
        "    if c and cls_name not in seen: seen.add(cls_name); shakes.append({'name': cls_name, 'path': c.get_path_name()})\n"
        "# Blueprint subclasses of CameraShakeBase\n"
        "try:\n"
        "    bps = ar.get_assets_by_class('Blueprint', True)\n"
        "    for bp in bps:\n"
        "        try:\n"
        "            tags = bp.get_tag_value('ParentClass')\n"
        "            if tags and ('CameraShake' in str(tags)):\n"
        "                n = str(bp.asset_name)\n"
        "                if n not in seen: seen.add(n); shakes.append({'name': n, 'path': str(bp.package_name)+'.'+n+'_C'})\n"
        "        except Exception: pass\n"
        "except Exception as _e: pass\n"
        "_result = {'ok': True, 'shakes': shakes}\n"
    )

def script_describe_all_user_structs() -> str:
    return (
        "import unreal, json\n"
        "lib = unreal.DCEditorLibrary\n"
        "ar = unreal.AssetRegistryHelpers.get_asset_registry()\n"
        "out = []\n"
        "for a in ar.get_assets_by_class('UserDefinedStruct', True):\n"
        "    try:\n"
        "        path = str(a.package_name) + '.' + str(a.asset_name)\n"
        "        r = json.loads(lib.describe_struct(path))\n"
        "        if r.get('ok'): out.append(r.get('root'))\n"
        "    except Exception: pass\n"
        "_result = {'ok': True, 'structs': out}\n"
    )

def script_describe_struct(path: str) -> str:
    return (
        "lib = unreal.DCEditorLibrary\n"
        f"r = json.loads(lib.describe_struct({json.dumps(path)}))\n"
        "_result = r\n"
    )

def script_dump_enums(prefix: str = "E_") -> str:
    return (
        "lib = unreal.DCEditorLibrary\n"
        f"r = json.loads(lib.dump_enums({json.dumps(prefix)}))\n"
        "_result = r\n"
    )

def script_list_data_tables() -> str:
    return (
        "ar = unreal.AssetRegistryHelpers.get_asset_registry()\n"
        "out = []\n"
        "try:\n"
        "    assets = ar.get_assets_by_class('DataTable', True)\n"
        "    for a in assets:\n"
        "        out.append({'name': str(a.asset_name), 'path': str(a.package_name) + '.' + str(a.asset_name)})\n"
        "except Exception as e1:\n"
        "    # UE 5.1+ may need TopLevelAssetPath\n"
        "    try:\n"
        "        tlap = unreal.TopLevelAssetPath('/Script/Engine', 'DataTable')\n"
        "        assets = ar.get_assets_by_class(tlap)\n"
        "        for a in assets:\n"
        "            out.append({'name': str(a.asset_name), 'path': str(a.package_name) + '.' + str(a.asset_name)})\n"
        "    except Exception as e2:\n"
        "        _result = {'ok': False, 'error': 'get_assets_by_class failed: ' + str(e1) + ' / ' + str(e2)}\n"
        "if 'ok' not in _result:\n"
        "    _result = {'ok': True, 'tables': sorted(out, key=lambda x: x['name'])}\n"
    )

def script_get_dt_rows(dt_path: str) -> str:
    dt_r = json.dumps(dt_path, ensure_ascii=False)
    return (
        f"dt_path = {dt_r}\n"
        "dt = unreal.EditorAssetLibrary.load_asset(dt_path)\n"
        "if not dt:\n"
        "    _result = {'ok': False, 'error': 'DataTable not found: ' + dt_path}\n"
        "else:\n"
        "    raw = unreal.DataTableFunctionLibrary.get_data_table_as_json(dt)\n"
        "    rows = json.loads(raw)\n"
        "    # Also get column names from first row\n"
        "    columns = list(rows[0].keys()) if rows else []\n"
        "    _result = {'ok': True, 'rows': rows, 'columns': columns, 'row_count': len(rows)}\n"
    )

def script_set_dt_cell(dt_path: str, row_name: str, prop: str, value) -> str:
    dt_r = json.dumps(dt_path, ensure_ascii=False)
    row_r = json.dumps(row_name, ensure_ascii=False)
    prop_r = json.dumps(prop, ensure_ascii=False)
    val_r = json.dumps(value, ensure_ascii=False)
    return (
        f"dt = unreal.EditorAssetLibrary.load_asset({dt_r})\n"
        "if not dt:\n"
        f"    _result = {{'ok': False, 'error': 'DataTable not found'}}\n"
        "else:\n"
        f"    row_name = {row_r}\n"
        f"    prop_name = {prop_r}\n"
        f"    new_val = {val_r}\n"
        "    # Get current row JSON, modify the field, set it back\n"
        "    raw = unreal.DataTableFunctionLibrary.get_data_table_as_json(dt)\n"
        "    rows = json.loads(raw)\n"
        "    found = False\n"
        "    for r in rows:\n"
        "        rn = r.get('Name', r.get('RowName', r.get('name', '')))\n"
        "        if rn == row_name:\n"
        "            r[prop_name] = new_val\n"
        "            found = True\n"
        "            break\n"
        "    if not found:\n"
        "        _result = {'ok': False, 'error': 'Row not found: ' + row_name}\n"
        "    else:\n"
        "        # Write back via fill_data_table_from_json_string\n"
        "        unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(dt, json.dumps(rows))\n"
        "        unreal.EditorAssetLibrary.save_loaded_asset(dt)\n"
        "        _result = {'ok': True, 'row': row_name, 'prop': prop_name, 'value': new_val}\n"
    )

def script_bulk_set_dt(dt_path: str, prop: str, value) -> str:
    dt_r = json.dumps(dt_path, ensure_ascii=False)
    prop_r = json.dumps(prop, ensure_ascii=False)
    val_r = json.dumps(value, ensure_ascii=False)
    return (
        f"dt = unreal.EditorAssetLibrary.load_asset({dt_r})\n"
        "if not dt:\n"
        f"    _result = {{'ok': False, 'error': 'DataTable not found'}}\n"
        "else:\n"
        f"    prop_name = {prop_r}\n"
        f"    new_val = {val_r}\n"
        "    raw = unreal.DataTableFunctionLibrary.get_data_table_as_json(dt)\n"
        "    rows = json.loads(raw)\n"
        "    count = 0\n"
        "    for r in rows:\n"
        "        if prop_name in r:\n"
        "            r[prop_name] = new_val\n"
        "            count += 1\n"
        "    unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(dt, json.dumps(rows))\n"
        "    unreal.EditorAssetLibrary.save_loaded_asset(dt)\n"
        "    _result = {'ok': True, 'updated': count, 'prop': prop_name, 'value': new_val}\n"
    )

def script_list_actors() -> str:
    return (
        "lib = unreal.DCEditorLibrary\n"
        "actors = []\n"
        "eas = unreal.EditorActorSubsystem()\n"
        "for a in eas.get_all_level_actors():\n"
        "    try:\n"
        "        for c in a.get_components_by_class(unreal.ActorComponent):\n"
        "            cn = c.get_class().get_name()\n"
        "            if 'Dialogue' in cn:\n"
        "                actors.append({'label': a.get_actor_label(), 'path': a.get_path_name(), 'component': cn})\n"
        "                break\n"
        "    except Exception: pass\n"
        "_result = {'ok': True, 'actors': actors}\n"
    )

# PROTECTED - do not modify without explicit request
def quote_bare_enum_values(text: str) -> str:
    """
    UE exports with PPF_ExternalEditor emit enum display names with spaces unquoted
    (e.g., =Disable Both Player & NPC Movement, or =Subtract (-)).
    UE's enum importer uses ReadToken which truncates at whitespace, so the value must
    be quoted. Walks the text with paren-depth tracking, inside-quoted-string awareness.
    Skips: struct values =(...), numbers, booleans, None, NSLOCTEXT, asset refs Class'"..."',
    and values without spaces.
    """
    LITERALS = {'True', 'False', 'None'}
    out = []
    i = 0; n = len(text)
    while i < n:
        c = text[i]
        # Pass through quoted strings as-is
        if c == '"':
            out.append(c); i += 1
            while i < n:
                out.append(text[i])
                if text[i] == '"' and text[i-1] != '\\':
                    i += 1; break
                i += 1
            continue
        if c != '=':
            out.append(c); i += 1; continue
        # At '=' — examine the following value
        out.append('='); i += 1
        vstart = i
        # Starts with `(` => struct; `"` => already quoted; digit/-/+ => number
        if i >= n or text[i] in '("-+0123456789':
            continue
        if not text[i].isalpha():
            continue
        # Scan until top-level `,` or `)` (respect nested parens)
        depth = 0
        j = i
        while j < n:
            ch = text[j]
            if ch == '(': depth += 1
            elif ch == ')':
                if depth == 0: break
                depth -= 1
            elif ch == ',' and depth == 0: break
            elif ch == '"':
                # Embedded quote — bail, don't touch this value
                j = -1; break
            j += 1
        if j == -1 or j == i:
            # pass through as-is
            continue
        val = text[i:j].rstrip()
        # Heuristics: only quote if value contains a space and isn't a known literal/NSLOCTEXT/asset-ref
        if (' ' in val
            and val not in LITERALS
            and not val.startswith('NSLOCTEXT')
            and "'\"" not in val  # asset ref form: Class'"/Path"'
        ):
            out.append('"' + val + '"')
            # preserve any trailing whitespace we stripped
            trailing = text[i + len(val):j]
            out.append(trailing)
        else:
            out.append(text[i:j])
        i = j
    return ''.join(out)


def script_pull(actor: str, prop: str) -> str:
    actor_r = json.dumps(actor, ensure_ascii=False); prop_r = json.dumps(prop, ensure_ascii=False)
    return (
        "lib = unreal.DCEditorLibrary\n"
        f"r = json.loads(lib.get_actor_property_as_text({actor_r}, {prop_r}))\n"
        "_result = r\n"
    )

def script_export_texture_png(asset_path: str, out_dir: str) -> str:
    """Export a Texture2D to PNG inside out_dir; returns the resulting file path.
    Uses DCEditorLibrary.export_texture_as_png which renders via
    FImageUtils and bypasses UE's UExporter path entirely — so there's no
    "No png exporter found for Texture2D" LogExporter warning."""
    ap = json.dumps(asset_path, ensure_ascii=False)
    od = json.dumps(out_dir, ensure_ascii=False)
    return (
        "import unreal, os, json\n"
        "r = {'ok': False}\n"
        f"p = {ap}\n"
        f"out_dir = {od}\n"
        "try:\n"
        "    os.makedirs(out_dir, exist_ok=True)\n"
        "    name = p.rsplit('.', 1)[-1] + '.png'\n"
        "    out = os.path.join(out_dir, name)\n"
        "    if not os.path.exists(out):\n"
        "        lib = unreal.DCEditorLibrary\n"
        "        res = json.loads(lib.export_texture_as_png(p, out))\n"
        "        if not res.get('ok'): raise Exception(res.get('error','export failed'))\n"
        "    r = {'ok': True, 'path': out, 'exists': os.path.exists(out)}\n"
        "except Exception as e: r = {'ok': False, 'error': str(e)}\n"
        "_result = r\n"
    )

def script_export_montage_thumb(asset_path: str, out_dir: str) -> str:
    """Export an AnimMontage thumbnail PNG to out_dir.

    Strategy (tried in order):
    1.  Load the package via unreal.load_package and pull the cached
        FObjectThumbnail that UE stores inside every .uasset.  This
        gives the real character-pose preview the Content Browser shows.
    2.  Use KismetRenderingLibrary.export_render_target after asking
        ThumbnailManager to render into a transient RenderTarget2D.
    3.  Fallback: generate a 64x64 silhouette PNG whose hue is derived
        from the asset name so each montage is visually distinct.
    """
    ap = json.dumps(asset_path, ensure_ascii=False)
    od = json.dumps(out_dir, ensure_ascii=False)
    return (
        "import unreal, os, json, struct, zlib, hashlib, math\n"
        "r = {'ok': False}\n"
        f"p = {ap}\n"
        f"out_dir = {od}\n"
        "try:\n"
        "    os.makedirs(out_dir, exist_ok=True)\n"
        "    name = p.rsplit('.', 1)[-1]\n"
        "    out = os.path.join(out_dir, 'montage_' + name + '.png')\n"
        "    exported = False\n"
        "    if not os.path.exists(out):\n"
        # ── Method 1: extract the package's cached ObjectThumbnail ──
        "        try:\n"
        "            pkg_name = p.rsplit('.', 1)[0]  # /Game/Foo/Bar\n"
        "            pkg = unreal.load_package(pkg_name)\n"
        "            if pkg:\n"
        "                thumb = unreal.ThumbnailTools.find_cached_thumbnail(pkg_name, name)\n"
        "                if not thumb:\n"
        "                    thumb = unreal.ThumbnailTools.find_cached_thumbnail(p, '')\n"
        "                if thumb and thumb.get_image_width() >= 32:\n"
        "                    w_ = thumb.get_image_width()\n"
        "                    h_ = thumb.get_image_height()\n"
        "                    src = thumb.access_image_data()  # TArray<uint8> BGRA\n"
        "                    # Convert BGRA to RGB rows and write PNG\n"
        "                    def chunk(ct, d):\n"
        "                        return struct.pack('>I', len(d)) + ct + d + struct.pack('>I', zlib.crc32(ct + d) & 0xffffffff)\n"
        "                    ihdr = struct.pack('>IIBBBBB', w_, h_, 8, 2, 0, 0, 0)\n"
        "                    raw_rows = b''\n"
        "                    for y_ in range(h_):\n"
        "                        raw_rows += b'\\x00'\n"
        "                        for x_ in range(w_):\n"
        "                            idx = (y_ * w_ + x_) * 4\n"
        "                            b_ = src[idx]; g_ = src[idx+1]; r_ = src[idx+2]\n"
        "                            raw_rows += struct.pack('BBB', r_, g_, b_)\n"
        "                    png = b'\\x89PNG\\r\\n\\x1a\\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw_rows, 9)) + chunk(b'IEND', b'')\n"
        "                    with open(out, 'wb') as f_: f_.write(png)\n"
        "                    exported = True\n"
        "        except Exception as e1:\n"
        "            unreal.log_warning('Montage thumb method 1 failed: ' + str(e1))\n"
        # ── Method 2: render thumbnail via ThumbnailManager ──
        "        if not exported:\n"
        "            try:\n"
        "                asset = unreal.load_asset(p)\n"
        "                if asset:\n"
        "                    world = unreal.EditorLevelLibrary.get_editor_world()\n"
        "                    rt = unreal.KismetRenderingLibrary.create_render_target2d(world, 128, 128)\n"
        "                    tm = unreal.ThumbnailManager.get()\n"
        "                    if hasattr(tm, 'render_thumbnail') and rt:\n"
        "                        tm.render_thumbnail(asset, 128, 128, rt)\n"
        "                        unreal.KismetRenderingLibrary.export_render_target(world, rt, out_dir, 'montage_' + name)\n"
        "                        # export_render_target writes .HDR by default; check for .png\n"
        "                        for ext in ['.png', '.hdr', '.exr', '']:\n"
        "                            candidate = os.path.join(out_dir, 'montage_' + name + ext)\n"
        "                            if os.path.exists(candidate):\n"
        "                                if candidate != out: os.rename(candidate, out)\n"
        "                                exported = True; break\n"
        "            except Exception as e2:\n"
        "                unreal.log_warning('Montage thumb method 2 failed: ' + str(e2))\n"
        # ── Method 3: fallback — a 64x64 PNG with a humanoid silhouette ──
        "        if not exported:\n"
        "            hv = int(hashlib.md5(name.encode()).hexdigest()[:6], 16)\n"
        "            # Hue-shifted base color\n"
        "            hue = (hv & 0xFFFF) / 65535.0\n"
        "            def hsv(h0, s0, v0):\n"
        "                i = int(h0 * 6.0); f = h0 * 6.0 - i; q = v0*(1-s0); t = v0*(1-(1-f)*s0); p0 = v0*(1-f*s0)\n"
        "                r_,g_,b_ = [(v0,t,q),(p0,v0,q),(q,v0,t),(q,p0,v0),(t,q,v0),(v0,q,p0)][i%6]\n"
        "                return (int(r_*255), int(g_*255), int(b_*255))\n"
        "            fg = hsv(hue, 0.5, 0.85)\n"
        "            bg = (24, 24, 38)\n"
        "            W, H = 64, 64\n"
        "            # Simple humanoid silhouette mask\n"
        "            def in_silhouette(x, y):\n"
        "                cx, cy = W/2, H/2\n"
        "                # Head: circle at top\n"
        "                hx, hy, hr = cx, 14, 7\n"
        "                if (x-hx)**2 + (y-hy)**2 <= hr**2: return True\n"
        "                # Torso: rectangle\n"
        "                if 22 <= y <= 42 and abs(x - cx) <= 9: return True\n"
        "                # Arms\n"
        "                if 24 <= y <= 34 and (abs(x - cx) <= 18) and abs(x - cx) >= 8: return True\n"
        "                # Legs\n"
        "                if 42 <= y <= 58 and (abs(x - cx - 5) <= 4 or abs(x - cx + 5) <= 4): return True\n"
        "                return False\n"
        "            def chunk(ct, d):\n"
        "                return struct.pack('>I', len(d)) + ct + d + struct.pack('>I', zlib.crc32(ct + d) & 0xffffffff)\n"
        "            ihdr = struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)\n"
        "            raw = b''\n"
        "            for y_ in range(H):\n"
        "                raw += b'\\x00'\n"
        "                for x_ in range(W):\n"
        "                    c = fg if in_silhouette(x_, y_) else bg\n"
        "                    raw += struct.pack('BBB', c[0], c[1], c[2])\n"
        "            png = b'\\x89PNG\\r\\n\\x1a\\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')\n"
        "            with open(out, 'wb') as f_: f_.write(png)\n"
        "    r = {'ok': True, 'path': out, 'exists': os.path.exists(out)}\n"
        "except Exception as e: r = {'ok': False, 'error': str(e)}\n"
        "_result = r\n"
    )

def script_play_montage(asset_path: str, target_actor: str = "") -> str:
    """Play an AnimMontage on the selected actor's skeletal mesh in UE."""
    ap = json.dumps(asset_path, ensure_ascii=False)
    ta = json.dumps(target_actor, ensure_ascii=False)
    return (
        "import unreal, json\n"
        "r = {'ok': False}\n"
        f"asset_path = {ap}\n"
        f"target_actor = {ta}\n"
        "try:\n"
        "    asset = unreal.load_asset(asset_path)\n"
        "    if not asset:\n"
        "        r = {'ok': False, 'error': 'Could not load montage: ' + asset_path}\n"
        "    else:\n"
        "        eas = unreal.EditorActorSubsystem()\n"
        "        actor = None\n"
        "        if target_actor:\n"
        "            for a in eas.get_all_level_actors():\n"
        "                try:\n"
        "                    if a.get_actor_label() == target_actor or a.get_path_name() == target_actor:\n"
        "                        actor = a; break\n"
        "                except: pass\n"
        "        else:\n"
        "            sel = eas.get_selected_level_actors()\n"
        "            if sel: actor = sel[0]\n"
        "        if not actor:\n"
        "            r = {'ok': False, 'error': 'No actor found to play montage on'}\n"
        "        else:\n"
        "            mesh = None\n"
        "            for c in actor.get_components_by_class(unreal.SkeletalMeshComponent):\n"
        "                mesh = c; break\n"
        "            if not mesh:\n"
        "                r = {'ok': False, 'error': 'No SkeletalMeshComponent on actor'}\n"
        "            else:\n"
        "                inst = mesh.get_anim_instance()\n"
        "                if inst:\n"
        "                    inst.montage_play(asset)\n"
        "                    r = {'ok': True, 'playing': asset_path, 'actor': actor.get_actor_label()}\n"
        "                else:\n"
        "                    r = {'ok': False, 'error': 'No anim instance on skeletal mesh'}\n"
        "except Exception as e: r = {'ok': False, 'error': str(e)}\n"
        "_result = r\n"
    )

def script_rename_actor(key: str, new_label: str) -> str:
    """Rename a level actor. `key` can be an actor label or a full path name."""
    k = json.dumps(key, ensure_ascii=False)
    n = json.dumps(new_label, ensure_ascii=False)
    return (
        "import unreal\n"
        f"k = {k}\n"
        f"new_label = {n}\n"
        "r = {'ok': False}\n"
        "try:\n"
        "    eas = unreal.EditorActorSubsystem()\n"
        "    found = None\n"
        "    for a in eas.get_all_level_actors():\n"
        "        try:\n"
        "            if a.get_path_name() == k or a.get_actor_label() == k:\n"
        "                found = a; break\n"
        "        except Exception: pass\n"
        "    if not found:\n"
        "        r = {'ok': False, 'error': 'Actor not found: ' + k}\n"
        "    else:\n"
        "        found.set_actor_label(new_label)\n"
        "        r = {'ok': True, 'label': found.get_actor_label(), 'path': found.get_path_name()}\n"
        "except Exception as e: r = {'ok': False, 'error': str(e)}\n"
        "_result = r\n"
    )

# Shared EUW refresh snippet: try all known function names, then close+respawn as fallback.
# Expects `lib`, `r` (dict with 'euw_called' list), and `json` to be available.
_EUW_BP = "/Game/DialogueComponent/Blueprints/EUW_DC"
_REFRESH_NAMES = (
    'Refresh Function','Refresh On Current Branch','Refresh on Current Branch',
    'RefreshFunction','RefreshOnCurrentBranch','Refresh','refresh',
    'Run','run','Construct','OnRefresh','DoRefresh',
    'Rebuild','Reload','Update','Sync','Repaint','Redraw',
)
_REFRESH_EUW_SNIPPET = (
    "try:\n"
    f"    _pre_sb = json.loads(lib.get_live_widget_int_property('{_EUW_BP}', 'Selected Branch'))\n"
    "    _pre_sb_idx = _pre_sb.get('idx', 0) if _pre_sb.get('ok') else 0\n"
    "    r['pre_selected_branch'] = _pre_sb_idx\n"
    "except Exception as _capt:\n"
    "    _pre_sb_idx = 0\n"
    "    r['pre_selected_branch_err'] = str(_capt)\n"
    "try:\n"
    "    r['euw_called'] = r.get('euw_called', [])\n"
    "    _hit = False\n"
    f"    for name in {_REFRESH_NAMES!r}:\n"
    f"        rr = json.loads(lib.call_function_on_live_widget('{_EUW_BP}', name))\n"
    "        if rr.get('count',0) > 0:\n"
    "            r['euw_called'].append(name)\n"
    "            _hit = True\n"
    "    if not _hit:\n"
    "        # Fallback: close and respawn the EUW tab\n"
    "        try:\n"
    "            _eusub = unreal.EditorUtilitySubsystem()\n"
    f"            _ewbp = unreal.EditorAssetLibrary.load_asset('{_EUW_BP}')\n"
    "            if _ewbp:\n"
    "                _tid = _eusub.register_tab_and_get_id(_ewbp)\n"
    "                if _eusub.does_tab_exist(_tid): _eusub.close_tab_by_id(_tid)\n"
    "                _eusub.spawn_and_register_tab(_ewbp)\n"
    "                r['euw_respawned'] = True\n"
    "        except Exception as _re:\n"
    "            r['euw_respawn_err'] = str(_re)\n"
    "except Exception as _e:\n"
    "    r['euw_refresh_err'] = str(_e)\n"
    "try:\n"
    f"    _post_sb = json.loads(lib.get_live_widget_int_property('{_EUW_BP}', 'Selected Branch'))\n"
    "    _post_sb_idx = _post_sb.get('idx', 0) if _post_sb.get('ok') else 0\n"
    "    if _pre_sb_idx != _post_sb_idx:\n"
    f"        _restore = json.loads(lib.select_branch_on_live_widget('{_EUW_BP}', 'Selected Branch', _pre_sb_idx, 'Refresh Function X'))\n"
    "        r['selected_branch_restored'] = {'from': _post_sb_idx, 'to': _pre_sb_idx, 'var_set': _restore.get('var_set', 0), 'called': _restore.get('called', 0)}\n"
    f"        json.loads(lib.select_branch_on_live_widget('{_EUW_BP}', 'Selected Branch', _pre_sb_idx, ''))\n"
    "except Exception as _rest:\n"
    "    r['selected_branch_restore_err'] = str(_rest)\n"
)

# Select-branch-only refresh (NO respawn fallback — respawn wipes Selected Branch)
_REFRESH_EUW_PROBE_ONLY = (
    "try:\n"
    "    r['euw_called'] = r.get('euw_called', [])\n"
    f"    for name in {_REFRESH_NAMES!r}:\n"
    f"        rr = json.loads(lib.call_function_on_live_widget('{_EUW_BP}', name))\n"
    "        if rr.get('count',0) > 0: r['euw_called'].append(name)\n"
    "except Exception as _e:\n"
    "    r['euw_refresh_err'] = str(_e)\n"
)

def script_select_branch(branch_index: int) -> str:
    """Sets `Selected Branch` and fires every known parameterized refresh.
    The generic no-arg refresh fallback (Construct/Run/Refresh Function/etc.)
    is INTENTIONALLY skipped when any parameterized refresh succeeded — those
    no-arg functions on some EUW variants reset `Selected Branch` back to 0,
    which fights the index we just set. As belt-and-braces, re-apply the index
    at the very end so the final state always reflects the user's choice even
    if a refresh path silently clobbered it."""
    idx_r = json.dumps(int(branch_index))
    parameterized_refresh_names = (
        'Refresh Function X', 'Refresh On X Branch', 'Refresh on X Branch',
        'RefreshFunctionX', 'RefreshOnXBranch',
        'Refresh On Current Branch', 'Refresh on Current Branch', 'RefreshOnCurrentBranch',
        'Set Selected Branch', 'SetSelectedBranch', 'Goto Branch', 'GotoBranch',
    )
    calls = ""
    for fname in parameterized_refresh_names:
        fname_r = json.dumps(fname)
        calls += (
            f"    _rr = json.loads(lib.select_branch_on_live_widget('{_EUW_BP}', 'Selected Branch', {idx_r}, {fname_r}))\n"
            f"    if _rr.get('called', 0) > 0: r.setdefault('param_refresh_called', []).append({fname_r})\n"
        )
    return (
        "import unreal, json\n"
        "lib = unreal.DCEditorLibrary\n"
        f"r = json.loads(lib.select_branch_on_live_widget('{_EUW_BP}', 'Selected Branch', {idx_r}, 'Refresh Function X'))\n"
        "try:\n"
        + calls +
        "except Exception as _pe:\n"
        "    r['param_refresh_err'] = str(_pe)\n"
        "if not r.get('param_refresh_called') and (r.get('called', 0) == 0):\n"
        + "".join("    " + ln + "\n" for ln in _REFRESH_EUW_PROBE_ONLY.splitlines() if ln.strip()) +
        f"_final = json.loads(lib.select_branch_on_live_widget('{_EUW_BP}', 'Selected Branch', {idx_r}, ''))\n"
        "r['final_var_set'] = _final.get('var_set', 0)\n"
        "_result = r\n"
    )

def script_select_and_refresh(label: str) -> str:
    label_r = json.dumps(label, ensure_ascii=False)
    return (
        "import unreal, json\n"
        "lib = unreal.DCEditorLibrary\n"
        "r = {'ok': True, 'euw_called': []}\n"
        "try:\n"
        "    eas = unreal.EditorActorSubsystem()\n"
        f"    target = None\n    for a in eas.get_all_level_actors():\n"
        f"        try:\n            if a.get_actor_label() == {label_r} or a.get_path_name() == {label_r}: target = a; break\n        except Exception: pass\n"
        "    if target:\n"
        "        eas.set_selected_level_actors([target])\n"
        "        r['selected'] = target.get_actor_label()\n"
        "except Exception as _e: r['ok']=False; r['error']=str(_e)\n"
        + _REFRESH_EUW_SNIPPET +
        "_result = r\n"
    )

def script_refresh_euw() -> str:
    return (
        "lib = unreal.DCEditorLibrary\n"
        "r = {'ok': True, 'euw_called': []}\n"
        + _REFRESH_EUW_SNIPPET +
        "_result = r\n"
    )

def script_push(actor: str, prop: str, text: str) -> str:
    actor_r = json.dumps(actor, ensure_ascii=False); prop_r = json.dumps(prop, ensure_ascii=False); text_r = json.dumps(text, ensure_ascii=False)
    return (
        "lib = unreal.DCEditorLibrary\n"
        f"r = json.loads(lib.set_actor_property_from_text({actor_r}, {prop_r}, {text_r}))\n"
        + _REFRESH_EUW_SNIPPET +
        "_result = r\n"
    )


class Handler(http.server.BaseHTTPRequestHandler):
    bridge_dir = DEFAULT_BRIDGE

    def log_message(self, fmt, *args):
        sys.stdout.write(f"[{time.strftime('%H:%M:%S')}] {self.address_string()} {fmt % args}\n")
        sys.stdout.flush()

    def _cors(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def _json(self, code: int, obj: dict):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self._cors()
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204); self._cors(); self.end_headers()

    def do_GET(self):
        # Serve the bundled HTML editor on / or by filename
        if self.path in ("/", "/dialogue_component_editor.html", "/dialogue_editor.html"):
            html_path = os.path.join(self.server.webdir, "dialogue_component_editor.html")
            if os.path.exists(html_path):
                try:
                    with open(html_path, "rb") as f:
                        data = f.read()
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html; charset=utf-8")
                    self.send_header("Content-Length", str(len(data)))
                    self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
                    self.send_header("Pragma", "no-cache")
                    self.send_header("Expires", "0")
                    self._cors()
                    self.end_headers()
                    self.wfile.write(data)
                except Exception as e:
                    self._json(500, {"ok": False, "error": str(e)})
            else:
                self._json(404, {"ok": False, "error": "HTML not found at " + html_path})
            return
        if self.path == "/health":
            # Check if UE is actually running by looking for recent script processing.
            # If pending/ has stale scripts that haven't been picked up, UE is down.
            pending_dir = os.path.join(self.bridge_dir, "pending")
            stale = False
            try:
                for f in os.listdir(pending_dir):
                    fp = os.path.join(pending_dir, f)
                    if f.endswith('.py') and os.path.isfile(fp):
                        age = time.time() - os.path.getmtime(fp)
                        if age > 5:  # script sitting >5s means UE isn't processing
                            stale = True; break
            except Exception:
                pass
            if stale:
                self._json(200, {"ok": False, "bridge": self.bridge_dir, "error": "UE not responding (stale scripts in pending/)"}); return
            self._json(200, {"ok": True, "bridge": self.bridge_dir}); return
        if self.path == "/actors":
            r = run_script(self.bridge_dir, script_list_actors(), op="list_actors")
            self._json(200, r); return
        if self.path == "/selected":
            r = run_script(self.bridge_dir, script_selected(), op="get_selected")
            self._json(200, r); return
        if self.path == "/enums":
            r = run_script(self.bridge_dir, script_dump_enums(), op="dump_enums")
            self._json(200, r); return
        if self.path == "/actors_all":
            self._json(200, run_script(self.bridge_dir, script_list_all_actors(), op="list_all_actors")); return
        if self.path.startswith("/component_vars"):
            from urllib.parse import urlparse, parse_qs
            q = parse_qs(urlparse(self.path).query)
            actor = (q.get("actor") or [""])[0]
            if not actor: self._json(400, {"ok": False, "error": "missing ?actor="}); return
            self._json(200, run_script(self.bridge_dir, script_list_component_vars(actor), op="component_vars")); return
        if self.path == "/camera_shakes":
            self._json(200, run_script(self.bridge_dir, script_list_camera_shakes(), op="camera_shakes")); return
        if self.path.startswith("/assets"):
            from urllib.parse import urlparse, parse_qs
            q = parse_qs(urlparse(self.path).query)
            classes = q.get("class", [])
            if not classes:
                self._json(400, {"ok": False, "error": "missing ?class="}); return
            self._json(200, run_script(self.bridge_dir, script_list_assets_by_class(classes), op="list_assets")); return
        if self.path == "/describe_all_user_structs":
            self._json(200, run_script(self.bridge_dir, script_describe_all_user_structs(), op="describe_all_structs")); return
        if self.path.startswith("/texture_thumb"):
            from urllib.parse import urlparse, parse_qs
            q = parse_qs(urlparse(self.path).query)
            tex_path = (q.get("path") or [""])[0]
            if not tex_path:
                self._json(400, {"ok": False, "error": "missing ?path="}); return
            thumbs_dir = os.path.join(self.bridge_dir, "thumbs")
            # Fast path: serve cached PNG without queueing any UE script.
            # The asset path's last segment ("/Path/Foo.Foo" → "Foo") is how
            # the exporter names the PNG — match that here so we never hit
            # the pending/ → UE round-trip when the thumbnail is already on
            # disk. This alone eliminated ~all of the texture_thumb spam.
            cached = os.path.join(thumbs_dir, tex_path.rsplit(".", 1)[-1] + ".png")
            if not os.path.exists(cached):
                os.makedirs(thumbs_dir, exist_ok=True)
                r = run_script(self.bridge_dir, script_export_texture_png(tex_path, thumbs_dir), op="texture_thumb")
                if not r.get("ok") or not r.get("path") or not os.path.exists(r["path"]):
                    self._json(404, {"ok": False, "error": r.get("error") or "export failed"}); return
                cached = r["path"]
            try:
                with open(cached, "rb") as f:
                    data = f.read()
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Cache-Control", "public, max-age=3600")
                self._cors()
                self.end_headers()
                self.wfile.write(data)
            except Exception as e:
                self._json(500, {"ok": False, "error": str(e)})
            return
        if self.path.startswith("/montage_thumb"):
            from urllib.parse import urlparse, parse_qs
            q = parse_qs(urlparse(self.path).query)
            montage_path = (q.get("path") or [""])[0]
            if not montage_path:
                self._json(400, {"ok": False, "error": "missing ?path="}); return
            thumbs_dir = os.path.join(self.bridge_dir, "thumbs")
            # Cache key uses 'montage_' prefix to avoid collision with texture thumbs
            safe_name = "montage_" + montage_path.rsplit(".", 1)[-1] + ".png"
            cached = os.path.join(thumbs_dir, safe_name)
            if not os.path.exists(cached):
                os.makedirs(thumbs_dir, exist_ok=True)
                r = run_script(self.bridge_dir, script_export_montage_thumb(montage_path, thumbs_dir), op="montage_thumb")
                if not r.get("ok") or not r.get("path") or not os.path.exists(r["path"]):
                    self._json(404, {"ok": False, "error": r.get("error") or "export failed"}); return
                cached = r["path"]
            try:
                with open(cached, "rb") as f:
                    data = f.read()
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Cache-Control", "public, max-age=3600")
                self._cors()
                self.end_headers()
                self.wfile.write(data)
            except Exception as e:
                self._json(500, {"ok": False, "error": str(e)})
            return
        if self.path.startswith("/play_montage"):
            from urllib.parse import urlparse, parse_qs
            q = parse_qs(urlparse(self.path).query)
            montage_path = (q.get("path") or [""])[0]
            actor = (q.get("actor") or [""])[0]
            if not montage_path:
                self._json(400, {"ok": False, "error": "missing ?path="}); return
            self._json(200, run_script(self.bridge_dir, script_play_montage(montage_path, actor), op="play_montage")); return
        if self.path.startswith("/describe_struct"):
            # ?path=... or default to S_Master_D_Array
            from urllib.parse import urlparse, parse_qs
            q = parse_qs(urlparse(self.path).query)
            path = q.get("path", ["/Game/DialogueComponent/Data/Structs/Dialogue/Essentials/S_Master_D_Array.S_Master_D_Array"])[0]
            r = run_script(self.bridge_dir, script_describe_struct(path), op="describe_struct")
            self._json(200, r); return
        if self.path.startswith("/log_level"):
            from urllib.parse import urlparse, parse_qs
            q = parse_qs(urlparse(self.path).query)
            verbose = (q.get("verbose") or ["1"])[0]
            quiet_flag = os.path.join(self.bridge_dir, ".quiet")
            try:
                if verbose in ("0", "false", "False"):
                    with open(quiet_flag, "w") as f: f.write("1")
                    self._json(200, {"ok": True, "verbose": False})
                else:
                    if os.path.exists(quiet_flag):
                        try: os.remove(quiet_flag)
                        except Exception: pass
                    self._json(200, {"ok": True, "verbose": True})
            except Exception as e:
                self._json(500, {"ok": False, "error": str(e)})
            return
        if self.path == "/data_tables":
            self._json(200, run_script(self.bridge_dir, script_list_data_tables(), op="list_data_tables")); return
        self._json(404, {"ok": False, "error": "unknown path"})

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        try:
            data = json.loads(self.rfile.read(length).decode("utf-8") or "{}")
        except Exception as e:
            self._json(400, {"ok": False, "error": f"bad json: {e}"}); return
        # Browser devtools forwarder: the web editor POSTs console.error,
        # console.warn, window.onerror, and unhandledrejection events to
        # this endpoint so we can tail them from disk when debugging. No
        # UE round-trip, just append to a local file.
        if self.path == "/console_log":
            try:
                from datetime import datetime
                log_path = os.path.join(self.bridge_dir, "console.log")
                os.makedirs(self.bridge_dir, exist_ok=True)
                ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                lvl = (data.get("level") or "?").upper()
                msg = data.get("msg") or ""
                url = data.get("url") or ""
                stk = data.get("stack") or ""
                with open(log_path, "a", encoding="utf-8", errors="replace") as f:
                    f.write(f"[{ts}] [{lvl}] {msg}")
                    if url: f.write(f"  @ {url}")
                    f.write("\n")
                    if stk:
                        for line in str(stk).splitlines():
                            f.write(f"    {line}\n")
                self._json(200, {"ok": True}); return
            except Exception as e:
                self._json(500, {"ok": False, "error": str(e)}); return
        prop = data.get("property", "🌿 Dialogue 🌿")
        if self.path == "/pull":
            actor = data.get("actor")
            if not actor: self._json(400, {"ok": False, "error": "missing actor"}); return
            r = run_script(self.bridge_dir, script_pull(actor, prop), op="pull")
            if r.get("ok") and r.get("text"):
                r["text"] = quote_bare_enum_values(r["text"])
            self._json(200, r); return
        if self.path == "/py":
            code = data.get("code","")
            if not code: self._json(400, {"ok": False, "error": "missing code"}); return
            op = data.get("op") or "py"
            self._json(200, run_script(self.bridge_dir, code, op=op)); return
        if self.path == "/select_actor":
            label = data.get("label")
            if not label: self._json(400, {"ok": False, "error": "missing label"}); return
            self._json(200, run_script(self.bridge_dir, script_select_actor(label), op="select_actor")); return
        if self.path == "/push_property":
            actor = data.get("actor"); prop = data.get("property"); text = data.get("text")
            if not actor or not prop or text is None:
                self._json(400, {"ok": False, "error": "need actor + property + text"}); return
            self._json(200, run_script(self.bridge_dir, script_push(actor, prop, text), op="push_property")); return
        if self.path == "/select_branch":
            idx = data.get("index")
            if idx is None: self._json(400, {"ok": False, "error": "missing index"}); return
            self._json(200, run_script(self.bridge_dir, script_select_branch(int(idx)), op="select_branch")); return
        if self.path == "/select_and_refresh":
            label = data.get("label")
            if not label: self._json(400, {"ok": False, "error": "missing label"}); return
            self._json(200, run_script(self.bridge_dir, script_select_and_refresh(label), op="select_and_refresh")); return
        if self.path == "/refresh_euw":
            self._json(200, run_script(self.bridge_dir, script_refresh_euw(), op="refresh_euw")); return
        if self.path == "/rename_actor":
            key = data.get("key"); new_label = data.get("label")
            if not key or not new_label:
                self._json(400, {"ok": False, "error": "need key + label"}); return
            self._json(200, run_script(self.bridge_dir, script_rename_actor(key, new_label), op="rename_actor")); return
        if self.path == "/push":
            actor = data.get("actor"); text = data.get("text")
            if not actor or text is None:
                self._json(400, {"ok": False, "error": "need actor + text"}); return
            prop = data.get("property", "🌿 Dialogue 🌿")
            self._json(200, run_script(self.bridge_dir, script_push(actor, prop, text), op="push_branches")); return
        if self.path == "/dt_rows":
            path = data.get("path")
            if not path: self._json(400, {"ok": False, "error": "missing path"}); return
            self._json(200, run_script(self.bridge_dir, script_get_dt_rows(path), op="dt_rows")); return
        if self.path == "/dt_update":
            path = data.get("path"); row = data.get("row"); prop = data.get("prop"); val = data.get("value")
            if not path or not row or not prop:
                self._json(400, {"ok": False, "error": "need path + row + prop + value"}); return
            self._json(200, run_script(self.bridge_dir, script_set_dt_cell(path, row, prop, val), op="dt_update")); return
        if self.path == "/dt_bulk_update":
            path = data.get("path"); prop = data.get("prop"); val = data.get("value")
            if not path or not prop:
                self._json(400, {"ok": False, "error": "need path + prop + value"}); return
            self._json(200, run_script(self.bridge_dir, script_bulk_set_dt(path, prop, val), op="dt_bulk_update")); return
        self._json(404, {"ok": False, "error": "unknown path"})


class ThreadingServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bridge", default=DEFAULT_BRIDGE)
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--webdir", default=DEFAULT_WEBDIR,
                    help="Directory containing the HTML to serve")
    args = ap.parse_args()
    if not os.path.isdir(os.path.join(args.bridge, "pending")):
        print(f"[fatal] pending/ not found under {args.bridge}")
        sys.exit(1)
    Handler.bridge_dir = args.bridge
    server = ThreadingServer(("127.0.0.1", args.port), Handler)
    server.webdir = os.path.abspath(args.webdir)
    print(f"bridge server on http://127.0.0.1:{args.port}")
    print(f"  bridge dir: {args.bridge}")
    print(f"  web dir:    {server.webdir}")
    try: server.serve_forever()
    except KeyboardInterrupt: print("bye")

if __name__ == "__main__":
    main()
