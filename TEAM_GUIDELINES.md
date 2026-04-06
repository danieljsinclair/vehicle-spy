# Team Guidelines - vehicle-sim

## Core Principles

### Version Control Best Practices

**NO .BAK FILES PRINCIPLE:**
- Use git for all versioning (git rm, git revert, git checkout)
- Never create .bak files in repository
- Let git handle backups and history
- Maintain clean repository without manual backup clutter

**GIT COMMANDS TO USE:**
- `git restore <file>` - revert to last committed version
- `git checkout <file>` - restore from history
- `git rm <file>` - remove untracked files
- `git clean -fdx` - clean untracked files

**COMMANDS TO AVOID:**
- Never create .bak files
- Never create manual backups outside git
- Never rename files with .bak extensions

### Code Quality Standards

- Follow SOLID principles
- Use TDD methodology
- Maintain clean git history
- Write clear commit messages
- Keep branches focused on single features

### Project Scope

**CURRENT SCOPE: Phase 0 MVP - Standalone Data Acquisition**
- Read Tesla CAN bus data from Bluetooth OBD2 module
- Translate raw CAN signals into standard normalized telemetry values
- Display live telemetry values on iPhone SwiftUI dashboard

**OUT OF SCOPE:**
- No physics simulation
- No engine model integration
- No external dependencies beyond required libraries

### Communication Guidelines

- Work from assigned tickets only
- Report progress on ticket completion
- Wait for PO direction before starting new work
- No self-assigned workstreams

### Testing Standards

- All code must compile (even in RED phase)
- Tests must pass before marking tasks complete
- Focus on business value over vanity coverage
- Test real production code, not mocks or external code
- Honor SRP in tests

---

*Last updated: 2026-04-06*