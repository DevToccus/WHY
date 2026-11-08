# WHY?

Current version: **WHY - Vigil** (2.0.0). Each version has a code name and its own release. [VERSIONS.md](VERSIONS.md) lists them and explains how to get a specific one.

WHY? watches a Windows PC, notices problems with drivers, hardware and software, and explains what caused them, with the evidence and a fix to try. It only observes: it never changes, fixes or blocks anything.

![Home: what needs a look, with an overview of the last month](docs/images/home.png)

Every statement carries one of three levels:

- **Verified:** the logs prove it.
- **Probable:** direct evidence supports it.
- **Only correlated:** it happened around the same time, which proves nothing.

The screenshots show the invented sample data the interface uses during development.

## What's new in Vigil

- **Always watching.** Live monitoring is now the first choice in setup and in Settings: a background service reads everything as administrator, keeps history and notifies you, even with WHY? closed. The alternative runs WHY? only while it is open.
- **Cases by severity.** Every case is coloured by how serious it is, with the Windows 11 status icons: red for serious, yellow for warnings, blue for minor ones. Cases that need a look also carry a coloured edge.
- **Cases you can filter.** Cases are split into Needs a look, Earlier and minor, and Marked as fixed, and can be filtered to Windows and hardware, Programs or Development.
- **What happened, in words.** A case opens with its key facts and tells its story: when it began, how often it came back, what changed before it, and each occurrence on one line of time.
- **Real error or forced closure.** Each case says whether the logs show a real failure, a forced closure (End task, the power button held down) or something they cannot tell apart, then lists concrete fixes for that problem.
- **Fixes that hold.** A case marked as fixed stays fixed when its details shift, for example a blue screen read without its dump. If it really comes back, WHY? says so: "Came back after your fix".
- **Quieter failed commands.** A build or test run that exits with code 1 to 255 is listed as minor and never asks for your attention. A Windows crash code, such as `0xC0000005`, is still a warning.
- **Opens reliably after sign-in.** When WHY? starts hidden at sign-in, its window is created when you first open it, so it no longer appears blank.

## Cases

![Cases grouped by what needs a look, coloured by severity](docs/images/cases.png)

A case groups every occurrence of one problem. Open one to see what happened, what WHY? found, and what to try.

![A case: key facts and what happened, with the changes that came before it](docs/images/case.png)

![What to try: whether it was a real error, then the fixes](docs/images/case-fix.png)

**Mark as fixed** tells WHY? you tried a fix. It changes nothing on your PC: WHY? keeps watching and tells you if the problem comes back.

## Development tools

WHY? also watches what you build. Node, Bun, Deno, Python, .NET, Java, Go, Cargo, Ruby and PHP processes that exit with an error become cases: a dev server that stops after running a while, or a command such as a build or a test run that fails. Ctrl+C is not counted, and a process ended from outside is reported as a forced closure.

WHY? cannot read a terminal it did not start, so to keep the error a command prints, start it through `why-run`:

```powershell
why-run npm run dev
why-run python -m uvicorn main:app --reload
```

The command runs as usual, with its colours and shortcuts. If it fails, the end of its error output is kept in `%LOCALAPPDATA%\WHY\runs` and shown in the case, whether or not WHY? was open. Put `why-run.exe` somewhere on your PATH to use it from any folder.

## Setup and Focus

![Setup: how WHY? should run](docs/images/setup.png)

The first time it opens, WHY? asks how it should run:

- **Always watching** (recommended) installs the background service. Windows asks for permission once.
- **Only while WHY? runs** installs nothing. You then choose whether WHY? runs as administrator, starts with Windows, and keeps watching from the notification area after you close the window.

Settings changes this later.

**Focus** narrows WHY? to one program, or to every program in a folder you develop in. It then shows and notifies only that program's crashes and hangs, not Windows itself.

`why notify-test` shows an example error and warning notification.

## Modes

| Mode | How to run it | What it does |
| --- | --- | --- |
| Investigation | Run `why.exe`, nothing to install | Reads what Windows has kept, in memory only, and watches while the window is open |
| Headless collection | `why collect --out report.whyreport.json` | Same reading, no window, writes a report |
| Report | `why report.whyreport.json`, or **Open report** | Reads a saved report, for example on a technician's PC |
| Live | **Always watching** in setup or Settings, or `why service install` as administrator | A background service keeps history and notifies you |

Without administrator rights WHY? still reads the System log and the driver inventory. Crash dumps, the file change journal and the activity recorder need elevation, and Settings says what is missing.

Because `why.exe` is a Windows-subsystem program, a command prompt does not wait for it. Use `start /wait why collect ...` in scripts.

## What it covers

- **Sources:** System and Application logs, crash dumps (read locally with the debug engine that ships with Windows, never downloading symbols), driver inventory, NTFS change journal for driver files, and an ETW recorder of process starts, stops and driver loads.
- **Cases:** blue screens, restarts without shutdown, graphics driver resets, WHEA hardware errors, disk and storage errors, file system corruption, drivers that fail to load, services that stop unexpectedly, programs that crash or stop responding, Microsoft Defender detections and real-time protection turned off, and dev servers and commands that fail.
- **What Changed:** driver snapshots (Live), Windows Update, driver service installs, driver store dates and the change journal.

Not covered yet: boot time, network, performance, disk health readings, CPU temperature.

## Building

Needs Visual Studio 2022 Build Tools with the C++ workload and the Windows SDK, plus Node.js. vcpkg (bundled with Visual Studio) fetches the C++ libraries.

```powershell
npm --prefix ui install
pwsh scripts/build.ps1                  # Release build in build/release
pwsh scripts/build.ps1 -Preset asan -Target why_tests   # Tests under AddressSanitizer
```

Tests:

```powershell
build/release/why_tests.exe   # Rules, parsers, investigation, storage, fuzzing
build/release/why_probe.exe   # Runs every Windows source on this machine. Run elevated for full coverage
```

To work on the interface alone, `npm --prefix ui run dev` serves it with invented sample data at `http://localhost:5199`. Add `?real` to read a report copied to `ui/public/dev-state.json`.

## Layout

```
src/core     Logic with no Windows dependency: event parsing, the signal table, the investigator, storage
src/collect  Windows data sources
src/engine   The engine that runs them, the local pipe and the service
src/app      Entry point, WebView2 window, tray notifier, performance monitor
src/run      why-run, the console wrapper for dev commands
ui           React interface, built into one HTML file embedded in why.exe
tests        Unit and fuzz tests, and the probe
docs/images  Screenshots for this file
```

The signal table in `src/core/signals.cpp` decides which Windows events matter. Anything not listed is ignored. An event ID only counts together with its provider, because different providers reuse the same numbers.

## Security notes

- The service runs from `%ProgramFiles%\WHY`, never from a folder a normal user can write to. Its history in `%ProgramData%\WHY` is readable only by SYSTEM and administrators.
- The interface has no privileges. It talks to the service through a named pipe with no network access, and refuses a server outside session 0.
- The WebView2 page is embedded, blocks every other request, and can only ask for a fixed set of commands.
- Reports remove account and computer names. Reports opened from elsewhere are validated first.
