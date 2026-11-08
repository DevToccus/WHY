# Security policy

WHY? reads system logs and crash dumps, and its Always watching mode runs a service with administrator rights, so security problems matter.

## Reporting a vulnerability

Report it privately through GitHub: open the **Security** tab of this repository and choose **Report a vulnerability**. Please do not open a public issue or pull request for it.

Include what you found, the version (shown under the WHY? name in the sidebar), and the steps to reproduce it. You will get an answer as soon as possible, and the fix will be released as a new version. Credit is given in the release notes if you want it.

## Supported versions

Only the latest release receives security fixes.

| Version | Supported |
| --- | --- |
| 2.0.x (Vigil) | Yes |
| 0.1.x (Triage) | No |

## What is in scope

- The Always watching service, its named pipe and its data folder.
- The app window and the commands its interface can send.
- Reports: opening a report from someone else, and the names removed from a saved report.
- why-run and the output it keeps.
