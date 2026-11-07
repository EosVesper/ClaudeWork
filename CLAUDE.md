# Instructions for Claude

## Where generated files go (user preference — always follow)

Every file produced while working for the user must be stored on **GitHub or
Google Drive**, never left only on local/container storage. Choose the
destination **separately for each file**, based on what that file is:

**→ GitHub** (this repo, under `outputs/YYYY-MM-DD-<topic>/`, committed and pushed):
- Source code, scripts, notebooks, configs
- Plain-text / Markdown notes and technical docs
- Small structured data (CSV, JSON, YAML) that is used by or versioned with code
- Anything that benefits from version history or belongs with a project

**→ Google Drive** (upload with the Google Drive connector):
- Documents meant to be read, shared or edited: reports, letters, Word/PDF files,
  Google Docs, spreadsheets (xlsx / Google Sheets), presentations
- Media: images, videos, audio, design files
- Large files (over ~50 MB) or other binaries that don't belong in git

Rules:
- Decide per file. One task may send some files to GitHub and others to Drive.
- Upload/push as soon as each file is produced, without waiting to be asked,
  and tell the user where each file went (GitHub path or Drive link) and why.
- If a file is genuinely ambiguous, pick the better fit and say so briefly.
- Truly temporary intermediates (caches, partial renders) may stay in the
  scratchpad; any final or useful result must be stored.
- Never store secrets, credentials or tokens in either place.
