# Releasing VibeCheck

Friends should only ever need **one link**. This is how that works, and how to publish a version.

## How it fits together

```
you ── installer/release.sh 0.6.0 ──► git push (commit + tag v0.6.0)
                                         │
                                         ▼
                      GitHub Actions: build.yml
                        windows runner → Setup.exe + portable zip
                        macOS runner   → .dmg + .pkg
                        release job    → a Release with fixed file names + latest.json
                                         │
                ┌────────────────────────┴───────────────────────┐
                ▼                                                ▼
   download page (docs/index.html)                  running copies of the app read latest.json
   picks Windows or Mac for the visitor             and show "Update 0.6.0" in the sidebar
```

The Windows and Mac build machines are GitHub's, and they are free for a public repository. Your own
Gitea server (git.makebelievestudio.app) can stay the main home of the code: add the GitHub repo as a
**push mirror** and every push, tag included, reaches GitHub on its own.

## One-time setup

1. **GitHub.** Create an empty repository, e.g. `makebelievestudio/vibecheck`. Make it **public**:
   the free build machines, the Releases downloads and the download page all need that. (A private
   repository can build, but a friend would need a GitHub account added to it to download anything.)
2. **Push the code** to it:
   ```
   git remote add origin https://github.com/<owner>/<repo>.git
   git push -u origin main
   ```
3. **Turn on the download page.** Repository **Settings → Pages → Build and deployment → Deploy from a
   branch → `main` / `/docs`**. After a minute the page is at `https://<owner>.github.io/<repo>/`.
   That is the link to give people. It works out the repository from its own address.
4. **Optional, Gitea as the main home.** Create the repository on Gitea, push there as well, then in
   Gitea: repository **Settings → Repository → Mirror settings → Push mirror**, add the GitHub URL and a
   GitHub *personal access token* with the `repo` scope (GitHub → Settings → Developer settings). From
   then on, push to Gitea only.
   Do not point friends at Gitea: it asks anyone to sign in before they can see a thing.

## Publishing a version

```
installer/release.sh 0.6.0
```

That sets the version in `CMakeLists.txt`, commits, tags `v0.6.0` and pushes. About fifteen minutes
later the Releases page has:

| File | For |
|---|---|
| `VibeCheck-Windows-Setup.exe` | the Windows installer |
| `VibeCheck-Windows-portable.zip` | Windows without installing |
| `VibeCheck-macOS.dmg` | the Mac disk image (Apple silicon and Intel) |
| `VibeCheck-macOS.pkg` | the Mac guided installer |
| `SHA256SUMS.txt` | checksums |
| `latest.json` | what running copies of the app read |

and each also exists with the version in its name. The fixed names matter: a link such as
`https://github.com/<owner>/<repo>/releases/latest/download/VibeCheck-Windows-Setup.exe` always gives the
newest one, so nothing you have already sent goes stale.

If the build fails, open the **Actions** tab, click the red run, and read the failing step. A tag
whose version disagrees with `CMakeLists.txt` is refused on purpose; `release.sh` keeps them in step.

## What friends see

- **Download page:** a big button for their own system, and the first-open steps for it.
- **First open:** the installers are not signed with a paid certificate. Windows SmartScreen says
  "More info → Run anyway" once. On a Mac: right-click → Open once. The page and the release notes say so.
- **Updates:** a running app checks once at launch for `latest.json` (a few hundred bytes) and shows a
  button when there is a newer version. Nothing is sent about the user. `--no-update-check` turns it off.

## Removing the first-open warnings (optional, costs money)

| | What you need | Cost |
|---|---|---|
| Windows | a code-signing certificate; add `WINDOWS_SIGN_PFX_PATH` and `WINDOWS_SIGN_PASSWORD` secrets | about $100–300 a year |
| Mac | an Apple Developer account; signing and notarising (see the README, "Installing") | $99 a year |

Neither is needed for friends who are happy to click through the prompt once.
