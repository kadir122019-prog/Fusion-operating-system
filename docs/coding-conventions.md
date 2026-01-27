# Coding Conventions

- Language: C (freestanding) and x86_64 assembly only.
- Keep functions small and single-purpose.
- Avoid dynamic allocation in early boot.
- Prefer explicit types (`u8`, `u32`, `u64`) over plain `int`.
- Do not depend on host libc; provide needed primitives locally.
- Use `LOG_*` macros for serial diagnostics where practical.
