# Instructions for Claude

## Where generated files go (user preference — always follow)

Every file produced while working for the user (reports, scripts, data exports,
images, videos, documents, build outputs, etc.) must end up stored on **GitHub**,
not left only on local/container storage.

- Save generated files under `outputs/` in this repository, in a subfolder named
  `YYYY-MM-DD-<short-topic>` (e.g. `outputs/2026-11-07-sales-report/`).
- Commit and push them to the session's working branch as soon as they are
  produced, without waiting to be asked. Tell the user the GitHub path.
- Truly temporary intermediates (caches, scratch files, partial renders) may use
  the scratchpad, but any final or useful result must be committed and pushed.
- Files over ~50 MB (GitHub rejects >100 MB) or that GitHub can't reasonably hold:
  upload to the user's **Google Drive** instead, and commit a small note in
  `outputs/` with the Drive link.
- Never commit secrets, credentials, or tokens.
