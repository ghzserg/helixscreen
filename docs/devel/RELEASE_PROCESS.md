# HelixScreen Release Process

This document describes how to create and publish releases of HelixScreen.

---

## Table of Contents

- [Version Scheme](#version-scheme)
- [Automated Release Pipeline](#automated-release-pipeline)
- [Creating a Release](#creating-a-release)
- [Release Checklist](#release-checklist)
- [Hotfix Releases](#hotfix-releases)
- [Pre-release Versions](#pre-release-versions)

---

## Version Scheme

HelixScreen uses [Semantic Versioning](https://semver.org/):

```
MAJOR.MINOR.PATCH[-PRERELEASE]
```

| Component | When to Increment |
|-----------|-------------------|
| **MAJOR** | Breaking changes (config format, API, incompatible UI changes) |
| **MINOR** | New features, backwards-compatible |
| **PATCH** | Bug fixes, documentation, minor improvements |
| **PRERELEASE** | Optional: `-alpha`, `-beta`, `-rc.1` for testing |

### Examples

- `v1.0.0` - First stable release
- `v1.1.0` - New features added
- `v1.1.1` - Bug fix
- `v2.0.0` - Breaking changes
- `v1.2.0-beta` - Pre-release for testing

---

## Automated Release Pipeline

The release process is fully automated via GitHub Actions (`.github/workflows/release.yml`).

### Trigger

Pushing a tag matching `v*` triggers the release workflow:

```bash
git tag v1.2.0
git push origin v1.2.0
```

### Pipeline Stages

```text
┌─────────────────────────────────────────────────────────────┐
│                     Tag Push (v1.2.0)                       │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                         Build Matrix                        │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
│ build-pi │ │build-pi32│ │build-ad5m│ │ build-k1 │ │ build-k2 │
│ (45 min) │ │ (45 min) │ │ (45 min) │ │ (45 min) │ │ (45 min) │
│          │ │          │ │          │ │          │ │          │
│ • Docker │ │ • Docker │ │ • Docker │ │ • Docker │ │ • Docker │
│ • arm64  │ │ • armhf  │ │ • armv7l │ │ • mips32 │ │ • mips32 │
│ • Package│ │ • Package│ │ • Package│ │ • Package│ │ • Package│
└────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘
     │            │            │            │            │
     └────────────┼────────────┼────────────┼────────────┘
                               │
                               ▼
                     ┌──────────────────┐
                     │     release      │
                     │                  │
                     │ • Download all   │
                     │   artifacts      │
                     │ • Extract version│
                     │ • Generate notes │
                     │ • Create release │
                     └──────────────────┘
```

### Build Artifacts

| Platform | Artifact Name | Contents |
|----------|---------------|----------|
| Raspberry Pi (64-bit) | `helixscreen-pi-v{version}.tar.gz` | aarch64 binary, assets, configs |
| Raspberry Pi (32-bit) | `helixscreen-pi32-v{version}.tar.gz` | armhf binary, assets, configs |
| AD5M | `helixscreen-ad5m-v{version}.tar.gz` | armv7l binary (static), assets, configs |
| K1/Simple AF | `helixscreen-k1-v{version}.tar.gz` | MIPS32 binary (static, musl), assets, configs |
| K2/Simple AF | `helixscreen-k2-v{version}.tar.gz` | ARM binary (static, musl), assets, configs |
---

## Creating a Release

### Step 1: Prepare the Release

1. **Ensure main branch is stable:**
   ```bash
   git checkout main
   git pull origin main
   make test-run  # Run tests
   ```

2. **Update version references** (if any hardcoded versions exist):
   - Check `CLAUDE.md`, `README.md`, documentation for version strings
   - Usually not needed - version comes from git tag

3. **Test on actual hardware:**
   - MainsailOS / Raspberry Pi
   - AD5M with ForgeX (if applicable)
   - Run through verification checklist in `docs/user/TESTING_INSTALLATION.md`

### Step 2: Create the Tag

**With release notes (recommended):**

```bash
# Create annotated tag with release notes
git tag -a v1.2.0 -m "$(cat <<'EOF'
## What's New

### Features
- Added input shaper visualization
- Improved AMS panel responsiveness

### Bug Fixes
- Fixed crash when Moonraker disconnects during print
- Fixed touch calibration on rotated displays

### Other
- Updated documentation
- Performance improvements
EOF
)"

# Push the tag
git push origin v1.2.0
```

**Without release notes:**

```bash
git tag v1.2.0
git push origin v1.2.0
```

The workflow auto-generates basic release notes if no annotation is provided.

### Step 3: Monitor the Build

1. Go to **Actions** tab on GitHub
2. Watch the "Release" workflow
3. Build takes ~15-20 minutes typically

### Step 4: Verify the Release

1. Go to **Releases** on GitHub
2. Check both platform tarballs are attached
3. Verify checksums in release notes
4. Test installation:
   ```bash
   curl -sSL https://raw.githubusercontent.com/prestonbrown/helixscreen/main/scripts/install.sh | sh
   ```

---

## Release Checklist

### Before Tagging

- [ ] All tests pass (`make test-run`)
- [ ] No critical bugs in issue tracker
- [ ] Documentation updated for new features
- [ ] ROADMAP.md updated if needed
- [ ] Tested on real hardware (Pi and/or AD5M)

### After Release

- [ ] Release workflow completed successfully
- [ ] Both platform artifacts attached
- [ ] Release notes accurate
- [ ] Installation tested via curl|sh
- [ ] Update any external references (Discord, documentation sites)

---

## Hotfix Releases

For urgent bug fixes:

1. **Create hotfix branch** from the release tag:
   ```bash
   git checkout -b hotfix/v1.2.1 v1.2.0
   ```

2. **Apply minimal fix** - only the necessary changes

3. **Test thoroughly** - verify the fix, check for regressions

4. **Merge to main** (via PR if time permits):
   ```bash
   git checkout main
   git merge hotfix/v1.2.1
   ```

5. **Tag and release:**
   ```bash
   git tag -a v1.2.1 -m "Fix: [description of fix]"
   git push origin v1.2.1
   ```

---

## Pre-release Versions

For testing new features before stable release:

### Creating a Pre-release

```bash
# Beta version
git tag -a v1.3.0-beta -m "Beta release for testing new AMS features"
git push origin v1.3.0-beta

# Release candidate
git tag -a v1.3.0-rc.1 -m "Release candidate 1"
git push origin v1.3.0-rc.1
```

### Pre-release Behavior

- Tags containing `-` are automatically marked as **prerelease** on GitHub
- Not shown as "latest" release
- Users must explicitly choose to install:
  ```bash
  curl -sSL .../install.sh | sh -s -- --version v1.3.0-beta
  ```

### Graduating Pre-releases

After testing:

```bash
# When beta is ready for stable
git tag -a v1.3.0 -m "Stable release"
git push origin v1.3.0
```

---

## Manual Release (Emergency)

If GitHub Actions fails, you can build and release manually:

### Build Locally

```bash
# Build for Pi
make PLATFORM_TARGET=pi clean release-pi

# Build for AD5M
make PLATFORM_TARGET=ad5m clean release-ad5m
```

### Create Release Manually

1. Go to GitHub **Releases** → **Draft a new release**
2. Choose the tag
3. Write release notes
4. Upload the `.tar.gz` files from `releases/`
5. Publish

---

## Changelog Generation

Currently, changelogs are written manually in the tag annotation. Future options:

### Option A: Manual Changelog File

Maintain `CHANGELOG.md` in the repository, update before each release.

### Option B: Conventional Commits + Auto-generation

Use tools like `git-cliff` or `semantic-release`:

```bash
# Generate changelog from conventional commits
git cliff -o CHANGELOG.md
```

This requires commit messages follow [Conventional Commits](https://www.conventionalcommits.org/):
- `feat: add new feature`
- `fix: resolve bug`
- `docs: update documentation`

### Current Approach

We use annotated tags with manual release notes. This provides flexibility while keeping the process simple.

---

## Troubleshooting

### Build Fails

1. Check Actions logs for the specific error
2. Common issues:
   - Submodule not checked out
   - Docker cache issues (try re-running)
   - Toolchain image problems

### Wrong Version Released

1. **Delete the release** on GitHub
2. **Delete the tag:**
   ```bash
   git tag -d v1.2.0
   git push origin :refs/tags/v1.2.0
   ```
3. Fix the issue
4. Re-tag and push

### Missing Artifact

If one platform's build fails:

1. Fix the issue
2. Delete the partial release
3. Delete and re-push the tag to trigger fresh build

---

*Related: [CI/CD Guide](CI_CD_GUIDE.md) | [Testing Installation](user/TESTING_INSTALLATION.md)*
