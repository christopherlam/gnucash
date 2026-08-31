# CLAUDE.md — GnuCash Coding Standards (Discussion Draft)

> **Status: draft for discussion, not yet adopted.** The canonical reference has long been
> https://wiki.gnucash.org/wiki/CodingStandard, which predates C++11 and describes conventions
> for the original C codebase. GnuCash today is a mixed C / C++17 codebase (`CMAKE_CXX_STANDARD 17`
> in the top-level `CMakeLists.txt`) that already depends on Boost 1.67+, glib/GObject, and GTK.
> This file is a starting point for reconciling the old wiki page with current practice and
> proposing where modernization is actually worth it. Treat every "Proposed" item below as a
> discussion point, not a ruling — edit freely.

## 1. Scope and layout

- `libgnucash/` — the engine and business logic (C and C++, GObject-based types like `Account`,
  `Transaction`, `Split`).
- `gnucash/` — the GTK3 UI, importers/exporters, reports glue.
- `bindings/` — guile/Scheme and Python bindings.
- `libgnucash/engine/*.hpp` and newer modules (`gnc-numeric`, `gnc-datetime`, `gnc-optiondb`,
  `gnc-quotes`, CSV importer) are where most new C++ is being written; older code (`Account.c`,
  `Transaction.c`, `qofbook.c`, etc.) is still plain C using GObject conventions and is not being
  rewritten wholesale.
- New files should generally be C++ unless they are GObject class definitions (`G_DECLARE_*`,
  `GObjectClass`) that must interoperate with existing GObject/Scheme bindings, in which case a
  `.c`/`.h` pair or a thin C++ file with `extern "C"` boundary is still the norm.

## 2. Baseline: what the old wiki standard still gets right

Carry these forward unchanged — they're style, not technology, and the existing code is
consistent about them:

- **Indentation**: spaces, not tabs. 4-space indents in most `.cpp`/`.hpp`, though some legacy `.c`
  files use GNU-style (2-space, brace-on-own-line, space-before-paren-in-calls). Match the
  surrounding file rather than reformatting wholesale.
- **License header**: every new source file gets the GPLv2-or-later boilerplate header used
  throughout the tree (see any file in `libgnucash/engine/` for the exact text), with your name
  and year in a `Copyright` line if you're materially authoring the file.
- **Naming**: `gnc_` / `xacc` prefixes for public C engine functions that are part of the
  historical API surface; `GncFoo` / `gnc_foo_get_type()` for GObject types. Don't invent new
  prefixes for code that lives beside existing `gnc_*` APIs — consistency within a module beats a
  "better" scheme.
- **No trailing whitespace**, one blank line between top-level definitions, opening brace of a
  function on its own line (K&R-ish, matching the majority of the tree — check with `git blame`
  / neighboring functions if unsure).
- **Doxygen comments** (`/** ... */`, `@param`, `@return`) on public engine APIs in headers;
  `libgnucash/engine/qofbook.h` and friends are good examples. Keep using them — the Doxygen build
  (`doxygen.cfg.in`) still runs.

## 3. Where the wiki page is obsolete

- Any guidance assuming **C89/C90** (mid-block variable declaration bans, no `//` comments, no
  `<stdbool.h>`) no longer applies to `.cpp` files, and is largely moot even in `.c` files since
  the project builds with a modern C compiler.
- Guidance (if any) discouraging **STL/C++ features** as "too heavy" is out of date: the engine
  already uses `std::string`, `std::vector`, `std::optional`, `std::variant`, lambdas, and
  range-based `for` extensively (e.g. `gnc-optiondb.cpp`, `gnc-quotes.cpp`).
- References to **autotools** as the build system are obsolete — the project builds with CMake
  (`CMakeLists.txt`, `cmake/`).
- Manual reference counting / raw `new`/`delete` patterns described for old C++ helper classes
  should be considered superseded by RAII and smart pointers wherever the code isn't crossing a
  GObject boundary (see §5).

## 4. Current language/toolchain baseline

- **C++ standard: C++17**, enforced via `set(CMAKE_CXX_STANDARD 17)` /
  `CMAKE_CXX_STANDARD_REQUIRED ON`. Don't write C++20-only syntax (concepts, `<ranges>`,
  `std::format`, three-way comparison, modules) until the project's minimum standard actually
  moves — this needs a build-system decision, not a per-file one.
  - **Proposed discussion point**: is it time to evaluate C++20? Distros GnuCash targets
    (upcoming/future Debian/Fedora/openSUSE releases + the Windows/macOS CI toolchains) will
    mostly ship compilers capable of it soon, even where current stable releases don't yet. If
    the answer is "not yet," say so here explicitly so contributors stop proposing it file-by-file.
- **Boost 1.67+** is already a hard dependency (`find_package(Boost 1.67.0 REQUIRED)`), with
  `date_time`, `filesystem`, `locale`, `program_options`, `regex`, `system` (and newer additions:
  `atomic`, `charconv`, `chrono`, `container`, `thread` for Boost ≥ 1.89). This means Boost is not
  an "avoid it" dependency here — it's foundational. Prefer it explicitly:
  - `boost::locale` for locale-aware formatting/parsing (already used for currency/number
    handling) — keep using it rather than hand-rolling locale logic.
  - `boost::date_time` / the project's own `gnc-datetime.hpp` wrapper for date handling — do not
    introduce a second date library.
  - `boost::regex` (with the ICU variant, `boost::regex/icu.hpp`) is used today for Unicode-aware
    regex in `gnc-numeric.cpp` and the CSV importer. This is a case where Boost is **definitely
    better than `std::regex`**: `std::regex` has no full-Unicode/locale-aware mode, and its
    real-world performance (particularly catastrophic backtracking on malformed input) is a known
    weak point. Keep using `boost::regex` for anything touching locale-sensitive or user-supplied
    patterns (import/export parsing, amount/date parsing).
  - **Proposed**: for new *pure-ASCII, hot-path* tokenizing/validation regexes (not the
    locale-aware cases above), consider [CTRE](https://github.com/hanickadot/compile-time-regular-expressions)
    (compile-time regex, header-only, C++17-compatible) instead of `std::regex`, since `std::regex`
    is the one to actively avoid — it's both slower and less capable than either alternative here.
    This would be a new third-party dependency (vendored header, similar to how `borrowed/`
    already vendors small libraries) — worth a real discussion before adopting, not a unilateral
    choice in one PR.
- **glib/GObject** remains required for the engine's type system, signals, and the C API surface
  consumed by Scheme/Python bindings. This is not going away — don't propose replacing
  `GHashTable`/`GList`/glib string helpers with STL equivalents in code that's part of the
  GObject-facing API. Inside pure-C++ internals with no GObject boundary, prefer STL containers
  (`std::vector`, `std::unordered_map`, `std::string`) over glib equivalents for new code.

## 5. Memory management and ownership

- **GObject-derived types** (`Account`, `Transaction`, `Split`, `Query`, etc.): keep using the
  existing `qof_instance`/GObject lifecycle (`xaccMallocAccount`, `xaccFreeAccount`-style
  create/destroy, or `g_object_new`/reference counting) — do not wrap these in `std::unique_ptr`
  or `std::shared_ptr`; their lifetime is owned by the book/collection machinery.
- **New pure-C++ types and helpers**: use RAII and standard smart pointers
  (`std::unique_ptr`/`std::shared_ptr`) instead of raw `new`/`delete`. This is already the pattern
  in `gnc-option-impl.hpp`, `gnc-datetime.hpp`, `SX-ttinfo.hpp`, `gnc-backend-prov.hpp` — continue
  it rather than introducing bespoke RAII wrappers or manual cleanup.
- **`std::optional`** for "value or absent" instead of sentinel values or output parameters plus
  a bool, in new C++ APIs that don't need to be called from C.
- Avoid introducing `boost::shared_ptr`/`boost::optional` in new code now that the C++17 standard
  library equivalents are always available — Boost is for what the standard library still lacks
  (regex-with-Unicode, date/time details, filesystem edge cases pre-adoption), not a general
  utility-belt to reach for by default.

## 6. Error handling

- The engine largely uses return codes / `QofBackendError` / GLib-style error propagation at the
  C boundary; keep that at the C/GObject API surface.
- Within pure C++ internals, prefer exceptions for genuinely exceptional conditions (as already
  done in `gnc-numeric.cpp`'s use of `std::invalid_argument`/`std::overflow_error`), but never let
  a C++ exception unwind across an `extern "C"` boundary into GObject/glib callback code — catch
  and convert at the boundary.

## 7. Formatting / tooling

- There is currently **no `.clang-format` file** in the repository, so formatting is "match the
  surrounding code" rather than tool-enforced.
  - **Proposed discussion point**: adopt a `.clang-format` (scoped to new/touched files only,
    not a mass reformat) so this stops being a per-PR debate. Needs agreement on base style
    (closest existing style is roughly GNU/K&R hybrid with 4-space indents in `.cpp`).
- Match existing per-directory conventions before applying anything in this document — a local
  `git log`/`git blame` on the file you're editing is a better guide than this document when the
  two disagree.

## 8. Tests

- Engine C++ code has unit tests under `libgnucash/engine/test/` (gtest-based, see
  `libgnucash/engine/test/gtest-gnc-numeric.cpp` and similar). New C++ engine logic should come
  with gtest coverage in the matching `test/` directory.
- Older C code uses the project's own `g-wrap`/`test-*.c` harness style — match whichever harness
  the module you're touching already uses rather than introducing a third.

## 9. Open questions for this discussion

1. C++20 upgrade — worth scoping now, or explicitly deferred, and until when?
2. Adopt CTRE as a vendored dependency for hot-path ASCII regex, or is `boost::regex` everywhere
   simpler to maintain even where Unicode/locale awareness isn't needed?
3. Add `.clang-format` — and if so, scoped to touched-lines-only via a pre-commit hook, or a
   one-time full reformat commit (which the project has historically avoided for `git blame`
   reasons)?
4. Should this document eventually replace https://wiki.gnucash.org/wiki/CodingStandard as the
   canonical reference linked from `HACKING`, or live alongside it?
