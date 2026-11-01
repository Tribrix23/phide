import os
import sys
import json
import urllib.request
import zipfile
import io

ext_ids = [
    'dsznajder.es7-react-js-snippets',
    'bradlc.vscode-tailwindcss',
    'redhat.vscode-yaml',
    'eamodio.gitlens'
]

dest_dir = 'assets/extensions'
os.makedirs(dest_dir, exist_ok=True)
os.makedirs('bin/assets/extensions', exist_ok=True)

url = 'https://marketplace.visualstudio.com/_apis/public/gallery/extensionquery'
headers = {
    'Content-Type': 'application/json',
    'Accept': 'application/json;api-version=3.0-preview.1'
}

body = {
    'filters': [{
        'criteria': [{'filterType': 7, 'value': eid} for eid in ext_ids]
    }],
    'flags': 0x1 | 0x2 | 0x4 | 0x10 | 0x20 | 0x40 | 0x80 | 0x100
}

req = urllib.request.Request(url, data=json.dumps(body).encode('utf-8'), headers=headers)
with urllib.request.urlopen(req, timeout=20) as resp:
    data = json.loads(resp.read().decode('utf-8'))
    extensions = data['results'][0]['extensions']

print(f"Fetched {len(extensions)} extensions from Marketplace.")

summary_list = []

for ext in extensions:
    pub = ext['publisher']['publisherName']
    name = ext['extensionName']
    eid = f"{pub}.{name}"
    display_name = ext.get('displayName', name)
    short_desc = ext.get('shortDescription', '')
    version_info = ext['versions'][0]
    ver = version_info['version']
    last_updated = version_info.get('lastUpdated', '')

    # Statistics
    stats = {s['statisticName']: s['value'] for s in ext.get('statistics', [])}
    install_count = int(stats.get('install', 0))
    rating = float(stats.get('averagerating', 5.0))
    rating_count = int(stats.get('ratingcount', 0))

    ext_folder = os.path.join(dest_dir, eid)
    os.makedirs(ext_folder, exist_ok=True)
    os.makedirs(os.path.join('bin/assets/extensions', eid), exist_ok=True)

    files_map = {f['assetType']: f['source'] for f in version_info['files']}

    # Download Icon
    icon_url = files_map.get('Microsoft.VisualStudio.Services.Icons.Default')
    icon_filename = "icon.png"
    if icon_url:
        try:
            req_icon = urllib.request.Request(icon_url, headers={'User-Agent': 'VSCode'})
            with urllib.request.urlopen(req_icon, timeout=15) as r:
                icon_bytes = r.read()
                with open(os.path.join(ext_folder, icon_filename), 'wb') as f:
                    f.write(icon_bytes)
                with open(os.path.join('bin/assets/extensions', eid, icon_filename), 'wb') as f:
                    f.write(icon_bytes)
                print(f"[{eid}] Saved icon ({len(icon_bytes)} bytes)")
        except Exception as e:
            print(f"[{eid}] Error downloading icon: {e}")

    # Download Details (README.md)
    details_url = files_map.get('Microsoft.VisualStudio.Services.Content.Details')
    readme_path = os.path.join(ext_folder, "README.md")
    readme_content = ""
    if details_url:
        try:
            req_details = urllib.request.Request(details_url, headers={'User-Agent': 'VSCode'})
            with urllib.request.urlopen(req_details, timeout=15) as r:
                readme_content = r.read().decode('utf-8', errors='replace')
                with open(readme_path, 'w', encoding='utf-8') as f:
                    f.write(readme_content)
                with open(os.path.join('bin/assets/extensions', eid, "README.md"), 'w', encoding='utf-8') as f:
                    f.write(readme_content)
                print(f"[{eid}] Saved README.md ({len(readme_content)} chars)")
        except Exception as e:
            print(f"[{eid}] Error downloading README: {e}")

    # Download Changelog
    changelog_url = files_map.get('Microsoft.VisualStudio.Services.Content.Changelog')
    if changelog_url:
        try:
            req_cl = urllib.request.Request(changelog_url, headers={'User-Agent': 'VSCode'})
            with urllib.request.urlopen(req_cl, timeout=15) as r:
                cl_content = r.read().decode('utf-8', errors='replace')
                with open(os.path.join(ext_folder, "CHANGELOG.md"), 'w', encoding='utf-8') as f:
                    f.write(cl_content)
                with open(os.path.join('bin/assets/extensions', eid, "CHANGELOG.md"), 'w', encoding='utf-8') as f:
                    f.write(cl_content)
        except Exception as e:
            pass

    # Download VSIX Package (or extract package.json and snippets)
    vsix_url = files_map.get('Microsoft.VisualStudio.Services.VSIXPackage')
    package_json = {}
    snippets_data = []

    if vsix_url:
        try:
            print(f"[{eid}] Downloading VSIX package for snippets/manifest...")
            req_vsix = urllib.request.Request(vsix_url, headers={'User-Agent': 'VSCode'})
            with urllib.request.urlopen(req_vsix, timeout=30) as r:
                vsix_bytes = r.read()
                print(f"[{eid}] VSIX downloaded: {len(vsix_bytes)} bytes. Extracting...")
                with zipfile.ZipFile(io.BytesIO(vsix_bytes)) as z:
                    for name_in_zip in z.namelist():
                        if name_in_zip.endswith('package.json') and 'extension/package.json' in name_in_zip:
                            pj_bytes = z.read(name_in_zip)
                            package_json = json.loads(pj_bytes.decode('utf-8', errors='replace'))
                            with open(os.path.join(ext_folder, "package.json"), 'wb') as f:
                                f.write(pj_bytes)
                            with open(os.path.join('bin/assets/extensions', eid, "package.json"), 'wb') as f:
                                f.write(pj_bytes)
                        # Extract snippets
                        if 'snippets' in name_in_zip.lower() and name_in_zip.endswith('.json'):
                            snip_bytes = z.read(name_in_zip)
                            base_snip_name = os.path.basename(name_in_zip)
                            snip_out = os.path.join(ext_folder, base_snip_name)
                            with open(snip_out, 'wb') as f:
                                f.write(snip_bytes)
                            with open(os.path.join('bin/assets/extensions', eid, base_snip_name), 'wb') as f:
                                f.write(snip_bytes)
                            print(f"[{eid}] Extracted snippet file: {base_snip_name}")
                            try:
                                snip_json = json.loads(snip_bytes.decode('utf-8', errors='replace'))
                                snippets_data.append((base_snip_name, snip_json))
                            except Exception as ex:
                                pass
        except Exception as e:
            print(f"[{eid}] Error processing VSIX: {e}")

    meta = {
        'id': eid,
        'displayName': display_name,
        'publisher': pub,
        'publisherDisplayName': ext['publisher'].get('displayName', pub),
        'version': ver,
        'description': short_desc,
        'installs': install_count,
        'rating': rating,
        'ratingCount': rating_count,
        'lastUpdated': last_updated,
        'icon': icon_filename,
        'categories': ext.get('categories', []),
        'tags': ext.get('tags', [])
    }
    with open(os.path.join(ext_folder, "meta.json"), 'w', encoding='utf-8') as f:
        json.dump(meta, f, indent=2)
    with open(os.path.join('bin/assets/extensions', eid, "meta.json"), 'w', encoding='utf-8') as f:
        json.dump(meta, f, indent=2)

    summary_list.append(meta)

# Save master extensions index
with open(os.path.join(dest_dir, "extensions.json"), 'w', encoding='utf-8') as f:
    json.dump(summary_list, f, indent=2)
with open(os.path.join('bin/assets/extensions', "extensions.json"), 'w', encoding='utf-8') as f:
    json.dump(summary_list, f, indent=2)

print("All extensions successfully downloaded and prepared!")
