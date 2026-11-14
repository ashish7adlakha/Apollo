# Building Apollo on this PC (portable tools)

All build tools live inside `tools\build-env`. Nothing is installed on Windows:
no installer, no PATH change, no registry entries, no files in your user profile.
To remove everything, delete the folders listed at the bottom.

Run all commands from the repo folder (`d:\Projects\Apollo-master`) in PowerShell.

## 1. One-time setup

```powershell
git submodule update --init --recursive
powershell -ExecutionPolicy Bypass -File tools\build-env\setup.ps1
```

The first line downloads the libraries Apollo is built from (into `third-party\`).
The second line downloads the build tools. It needs internet and about 3 GB of disk space.
You can run it again at any time; finished steps are skipped.

What it downloads:

| Tool | Where it goes | What it is for |
|---|---|---|
| MSYS2 (UCRT64) | `tools\build-env\msys64` | C++ compiler (GCC), CMake, Ninja, Boost, OpenSSL and the other libraries |
| Node.js 20 | `tools\build-env\node` | builds the web UI |

## 2. Build

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-env\build.ps1
```

The first build takes a long time (it compiles everything). Later builds only compile what changed.

The result is in the `build` folder. The program is `build\sunshine.exe`.
The full output of the last build can be saved by adding `*> build.log` to the command.

Options:

| Command ending | What it does |
|---|---|
| `build.ps1 -Package` | also makes a ZIP package in `build\cpack_artifacts` |
| `build.ps1 -Clean` | deletes the `build` folder first (full rebuild) |
| `build.ps1 -BuildType Debug` | debug build |
| `build.ps1 -Shell` | opens a build shell, to run `cmake`, `ninja`, `pacman` yourself |

## 3. Trying the build

The build makes `build\sunshine.exe` and an `build\assets` folder (shaders and web UI).
`build.ps1 -Package` also makes `build\cpack_artifacts\Apollo.zip` with everything in it.

Apollo is installed on this PC in `C:\Program Files\Apollo` and runs as the service `ApolloService`.
Only one Apollo can run at a time, so the service must be stopped first.
Both ways below need an **administrator** PowerShell. Neither has been tested yet.

**Way 1: run from the build folder (does not touch the installed Apollo files)**

```powershell
Stop-Service ApolloService
cd d:\Projects\Apollo-master\build
.\sunshine.exe
```

- You must be inside the `build` folder, or it cannot find its `assets`.
- It uses its own settings in `build\config`, so your normal settings and paired devices are not there.
- When done: press Ctrl+C, then `Start-Service ApolloService` to get the installed Apollo back.

**Way 2: put the new build into the installed Apollo (keeps your settings and paired devices)**

1. `Stop-Service ApolloService`
2. Make a backup copy of `C:\Program Files\Apollo`.
3. Unpack `build\cpack_artifacts\Apollo.zip` over `C:\Program Files\Apollo` (do not delete the `config` folder).
4. `Start-Service ApolloService`

To go back, stop the service, restore the backup, start the service.

## How it stays portable

- MSYS2 is unpacked from an archive, not installed. Its home folder and temp folder are inside `msys64`.
- The build scripts start MSYS2 with its own PATH, so tools installed on Windows are not used.
- While the scripts run, the user profile and AppData folders point to `tools\build-env\sandbox`.
  The npm cache is there too.
- `git submodule update` and the build itself write only inside the repo folder.

## Removing everything

Delete these folders (all are ignored by git):

- `tools\build-env\msys64`
- `tools\build-env\node`
- `tools\build-env\sandbox`
- `tools\build-env\downloads`
- `build` and `node_modules` (build output)

## Updating the tools

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-env\setup.ps1
```

Running setup again updates the MSYS2 packages. To change the Node.js version, edit `$NodeVersion`
in `tools\build-env\env.ps1`, delete `tools\build-env\node`, and run setup again.
