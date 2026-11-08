# Contributing to WHY?

Thank you for helping. WHY? is source-available, not open source: you can read the code, build it and run it for yourself, and every fix or improvement comes back here. See [LICENSE](LICENSE) for the exact terms.

## How to contribute

1. **Open an issue first** for anything larger than a small fix, so we agree on the approach before you spend time on it.
2. **Fork, change, open a pull request** against `main`. Forks exist only to prepare a pull request: publishing a modified version, even with a fix, is not allowed.
3. **Keep it focused.** One change per pull request, with a short description of the problem and how you checked the fix.
4. **Run the tests** before you submit:

   ```powershell
   pwsh scripts/build.ps1
   build/release/why_tests.exe
   ```

5. **Write in English**: code, comments, interface text and commit messages.

## What happens to your contribution

By opening a pull request you grant the owner the rights described in section 3 of the [LICENSE](LICENSE): your change may be used, modified, published or declined, and it does not give you the right to distribute WHY? or a modified version of it.

## Security problems

Do not open a public issue for a vulnerability. Follow [SECURITY.md](SECURITY.md).
