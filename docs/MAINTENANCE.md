# Maintenance Workflow

This project has two separate git remotes with different purposes:

- `origin` (`github.com/Lyssten/retypex`) is the source code repository.
- `aur` (`aur.archlinux.org/retypex-git.git`) is the packaging repository.

Do not mix their histories in one branch.

## Branch Model

- `master` tracks `origin/master` and contains the application source.
- `aur/master` (remote-tracking) contains only AUR package files (`PKGBUILD`, `.SRCINFO`).

Recommended local workflow:

1. Keep normal development on `master`.
2. Update AUR from a dedicated branch created from `aur/master`.

## Release Steps

### 1) Push code to GitHub

```bash
git checkout master
git pull --ff-only origin master
git push origin master
```

### 2) Update AUR package metadata

```bash
git fetch aur master
git checkout -B aur-update aur/master
```

Edit only:

- `PKGBUILD`
- `.SRCINFO` (regenerate with `makepkg --printsrcinfo > .SRCINFO`)

Then:

```bash
git add PKGBUILD .SRCINFO
git commit -m "pkgrel bump for upstream updates"
git push aur aur-update:master
```

### 3) Return to code branch

```bash
git checkout master
```

## Notes

- For `retypex-git`, source code is fetched from GitHub (`source=("retypex-git::git+$url.git")`), so most code updates do not need PKGBUILD changes.
- Bump `pkgrel` when package metadata/packaging behavior changes and you want AUR users to pick up a new package release.
