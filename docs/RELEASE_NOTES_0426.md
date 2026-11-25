# FM1 Sampler 0.42.6

- Automatic current-session recovery, separate from 16 named projects. Checkpoint on app backgrounding/closing and periodic change checks, with atomic replacement and fsync.
- NEW PROJECT in PROJECT with confirmation; creates a blank workspace without deleting saved projects, sounds or packs.
- Preserves previous complete checkpoint if interrupted. In-flight recordings and changes before the last completed checkpoint may be lost during abrupt power loss; manual backups remain recommended.
- Same package and permanent signing certificate, versionCode 72. Manual FMP2/APP2 v8 project format retained; private recovery metadata v9.
