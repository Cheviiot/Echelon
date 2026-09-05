# Central Generals: Arsenal Modification Repository

## Objective

Replace runtime dependency on third-party launcher catalogs and file providers
with one repository controlled by Generals: Arsenal. The launcher receives a
small immutable catalog model and never needs storage credentials.

## Delivery architecture

```text
repository maintainer
  -> repoctl validation and content addressing
  -> authenticated GitHub CLI publication
  -> immutable SHA-256 GitHub Release assets
  -> atomically selected latest catalog release
  -> Arsenal catalog cache and transactional installer
```

Packages and covers are stored under their SHA-256 digest in `objects-XX`
releases. A catalog snapshot is published only after every referenced object is
confirmed, and only that completed snapshot is marked as the latest release.
This ordering means a client can never observe a catalog entry before its
package is available. Hosting uses the public fork-owned GitHub repository and
requires no cloud subscription, payment method, database, or storage key.

## Trust boundaries

- Normal launcher sessions use exactly one built-in Arsenal source.
- `Repositories.ini` cannot add a foreign network catalog.
- Package and cover URLs must use HTTPS and the same origin as the catalog.
- Each package publishes an exact size and SHA-256 digest.
- GitHub Release assets provide `GET`, `HEAD`, byte ranges, strong ETags, and
  `If-Match` preconditions after an HTTPS redirect to the asset service.
- Publishing uses the maintainer's local GitHub CLI authentication and no
  credential is present in the launcher or catalog.
- Third-party modifications are published only with permission from their
  authors; the repository does not mirror arbitrary GenLauncher downloads.

## Repository project

The independent working tree is
`/home/cheviiot/Project/Game/GeneralsArsenalRepository`. It contains the
dependency-free catalog publisher, local and public protocol tests, and GitHub
Actions verification. The public project is
`https://github.com/Cheviiot/GeneralsArsenalRepository`.

## Publication state

The public repository and first empty catalog release are live. The final
remaining content task is to add only author-approved modification archives,
publish their immutable assets with `repoctl.py publish-github`, and verify each
real package through the launcher's transactional downloader. An installed
Flatpak has already consumed a temporary three-item mod -> patch -> add-on
catalog and its package asset without rebuilding the launcher, proving the
catalog is independent of the application release. The temporary QA releases
were removed after the production empty catalog had been restored. GitHub
limits an individual Release asset to 2 GiB; larger distributions require a
documented multi-archive manifest extension before they can be accepted.

## Rise of the Reds candidate graph

The first real onboarding target is Rise of the Reds for Zero Hour. Its
repository entry is intentionally maintained as metadata-only candidate data
until redistribution permission and an approved package source are available.
The candidate validator covers the base 1.87 Public Build 2.0 release,
independent HanPatch, AntiThesis, and ROTR Navy patch branches, and their exact
add-on dependencies. Patch branches conflict with one another, and a
GenTool-dependent add-on remains runtime-blocked because Arsenal never loads
third-party executables.

Candidate validation is part of normal repository verification, but candidate
records cannot enter `catalog.json` and cannot reference package URLs, hashes,
or objects. Promotion therefore remains a deliberate transaction: obtain
permission, import the approved files, audit the archive, add immutable
content-addressed objects, then create the live catalog entry with the same
reviewed requirement/conflict graph.
