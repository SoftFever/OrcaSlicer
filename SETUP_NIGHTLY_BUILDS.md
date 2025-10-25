# Nightly Build Setup Instructions

This fork is configured to automatically build macOS ARM64 nightly builds of OrcaSlicer.

## Quick Setup

### 1. Create a Nightly Release

You need to create a GitHub release to which the nightly builds will be uploaded:

1. Go to https://github.com/cyberralf83/OrcaSlicer/releases
2. Click **"Create a new release"**
3. Fill in:
   - **Tag**: `nightly-builds`
   - **Release title**: `Nightly Builds`
   - **Description**: `Automated nightly builds of OrcaSlicer for macOS ARM64`
   - ✅ Check **"Set as a pre-release"**
4. Click **"Publish release"**

### 2. Get the Release ID

After creating the release, you need to find its ID:

**Option A: From the URL**
- Go to your new release page
- The URL will look like: `https://github.com/cyberralf83/OrcaSlicer/releases/tag/nightly-builds`
- Click "Edit" on the release
- The edit URL contains the release ID: `https://github.com/cyberralf83/OrcaSlicer/releases/edit/12345678`
- The number `12345678` is your RELEASE_ID

**Option B: Using GitHub CLI**
```bash
gh api repos/cyberralf83/OrcaSlicer/releases | jq '.[] | select(.tag_name=="nightly-builds") | .id'
```

**Option C: Using curl**
```bash
curl -s https://api.github.com/repos/cyberralf83/OrcaSlicer/releases | grep -A 5 '"tag_name": "nightly-builds"' | grep '"id"' | head -1 | sed 's/[^0-9]*//g'
```

### 3. Update the Workflow File

Open `.github/workflows/build_orca.yml` and replace **ALL** instances of `{{RELEASE_ID}}` with your actual release ID number.

**Find and replace:**
- Find: `{{RELEASE_ID}}`
- Replace with: `YOUR_ACTUAL_RELEASE_ID` (e.g., `12345678`)

There are 4 instances to replace (2 for main DMG, 2 for profile validator DMG).

### 4. Commit and Push

```bash
git add .github/workflows/build_orca.yml
git commit -m "Configure deployment to my fork's nightly release"
git push origin cyberralf83/orcanightly
```

## How It Works

### Automatic Upstream Sync
Every nightly build (7:35 AM UTC):
1. Syncs latest changes from `SoftFever/OrcaSlicer` nightly-builds tag
2. Merges into your `cyberralf83/orcanightly` branch
3. Builds macOS ARM64 version
4. Signs and notarizes the DMG
5. Uploads to your nightly release

### Manual Triggers
You can also trigger builds manually:
- Go to https://github.com/cyberralf83/OrcaSlicer/actions
- Select "Build Mac" workflow
- Click "Run workflow"

## Workflows

### Active Workflows
- ✅ `build_mac.yml` - Main nightly build workflow
- ✅ `build_orca.yml` - Build orchestration (called by build_mac)
- ✅ `build_check_cache.yml` - Dependency caching
- ✅ `build_deps.yml` - Build dependencies
- ✅ `test_signing.yml` - Quick signing/notarization test
- ✅ `claude.yml` - Claude AI integration (optional)

### Disabled Workflows
The following workflows are disabled (`.disabled` extension) as they're for upstream development:
- `assign.yml.disabled`
- `orca_bot.yml.disabled`
- `check_locale.yml.disabled`
- `check_profiles.yml.disabled`
- `claude-code-review.yml.disabled`
- `update-translation.yml.disabled`
- `validate-documentation.yml.disabled`
- `publish_docs_to_wiki.yml.disabled`
- `shellcheck.yml.disabled`

## Signing Configuration

Your code signing secrets are already configured:
- ✅ `BUILD_CERTIFICATE_BASE64` - Developer ID Application certificate
- ✅ `P12_PASSWORD` - Certificate password
- ✅ `KEYCHAIN_PASSWORD` - Keychain password
- ✅ `MACOS_CERTIFICATE_ID` - Certificate identity
- ✅ `APPLE_DEV_ACCOUNT` - Apple ID
- ✅ `TEAM_ID` - Apple Developer Team ID
- ✅ `APP_PWD` - App-specific password for notarization

## Downloads

After successful builds, you can download:
- **From Releases**: https://github.com/cyberralf83/OrcaSlicer/releases/tag/nightly-builds
  - `OrcaSlicer_Mac_arm64_nightly.dmg` - Main application
  - `OrcaSlicer_profile_validator_Mac_arm64_nightly.dmg` - Profile validator tool

- **From Actions** (all builds): https://github.com/cyberralf83/OrcaSlicer/actions/workflows/build_mac.yml

## Troubleshooting

### Build fails at deployment
- Verify you replaced `{{RELEASE_ID}}` with your actual release ID
- Check that the release exists and is accessible

### Merge conflicts during sync
- The workflow will fail and provide instructions
- Manually resolve conflicts and push
- Next nightly build will proceed normally

### Signing fails
- Run the `test_signing.yml` workflow to quickly test signing
- Verify all 7 signing secrets are set correctly
- Check certificate hasn't expired

## Need Help?

Check the workflow run logs at:
https://github.com/cyberralf83/OrcaSlicer/actions
