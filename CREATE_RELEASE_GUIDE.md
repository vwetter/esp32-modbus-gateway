# GitHub Release Creation Guide for v1.2.3

## Quick Steps to Create GitHub Release

1. **Go to GitHub Repository**:
   https://github.com/vwetter/esp32-modbus-gateway

2. **Navigate to Releases**:
   - Click "Releases" (right side of repository page)
   - OR go directly to: https://github.com/vwetter/esp32-modbus-gateway/releases

3. **Create New Release**:
   - Click "Create a new release"
   - **Tag**: Select existing tag `v1.2.3` 
   - **Title**: `v1.2.3 - Arduino IDE Pure Project`

4. **Release Description**:
   Copy the content from `RELEASE_NOTES_v1.2.3.md` into the description field

5. **Additional Settings**:
   - ✅ Set as latest release
   - ✅ Create discussion for this release (optional)
   - ❌ This is a pre-release (leave unchecked)

6. **Publish**:
   - Click "Publish release"

## Alternative: Command Line (if GitHub CLI installed)

```bash
gh release create v1.2.3 \
  --title "v1.2.3 - Arduino IDE Pure Project" \
  --notes-file RELEASE_NOTES_v1.2.3.md
```

## What This Release Contains

- **Tag**: `v1.2.3` (already pushed)
- **Source Code**: Automatic zip/tar.gz downloads
- **Release Notes**: Comprehensive changelog and setup instructions
- **Assets**: Source code archives (automatic)

The release will be visible at:
https://github.com/vwetter/esp32-modbus-gateway/releases/tag/v1.2.3