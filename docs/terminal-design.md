# Terminal design

Terminal is the native appearance direction for this personal VyGram fork.
The browser concept under `E:\dev\ayu-design` is a visual reference, not a
pixel-perfect specification or a screenshot of the compiled client.

## Enable

After the first successful password unlock, this build applies Terminal once
to new and existing profiles. The preset applies the bundled dark theme, sets message
corner radius to 2, removes message tails, sets avatar corners to 2, enables a
single corner radius and chooses JetBrains Mono for code. Accept the normal
night-mode confirmation when applying the preset manually, then restart when
prompted to refresh cached shapes. Automatic setup disconnects system dark mode
and the system accent so the bundled charcoal/amber palette remains intact.
Later manual theme choices are preserved by an encrypted migration marker.
The preset can also be reapplied from **AyuGram Settings → Appearance → Terminal
→ Apply Terminal design**.

Every cold-start password screen uses a public bundled Terminal palette in
memory, without reading private saved theme settings. After successful unlock,
protected settings and the saved background load before the one-time Terminal
setup. Theme/background writes are synchronized before the migration marker
is stored. Subsequent launches restore the user's saved theme.

A genuinely fresh profile keeps Terminal as its initial theme. Startup checks
for legacy authorization files and
requires `tdata` to be absent or empty apart from Telegram's `working` crash
marker and an empty `dumps` directory. Any other entry, an unreadable directory
or a symlink conservatively skips the default. The palette is loaded before
native widgets are constructed;
the chosen default is remembered only after password unlock and protected
settings restoration. After that first unlock, the fresh profile also receives
the preset's bubble radius, tail, avatar, single-radius and monospace-font
preferences. Existing profiles receive the same values during the one-time
Terminal setup after unlock.

Protected appearance settings refresh the runtime UI mirrors after unlock.
Material switches and bubble-radius readers use those updated values; existing
avatar images can retain their cached shapes. Width multipliers are evaluated
when styles initialize, and the monospace family can already be cached before
the password is entered. Updating their mirrors does not rebuild those caches:
the current process may retain default widths or its previous monospace font.
An ordinary restart also initializes styles before unlock, so it does not
guarantee these protected preferences take effect. Resolving that initialization
order remains outside this appearance patch.

The palette alone is also available in the ordinary chat theme chooser as
**Terminal**. Alternatively, open `Telegram/Resources/terminal.tdesktop-theme`
in an existing Telegram Desktop or AyuGram installation and use its normal theme
preview and confirmation. Installing the theme alone changes colors/background;
the native geometry, bundled font and preset require a build of this fork.

## What changes

- Neutral charcoal surfaces, cool readable text, muted amber controls and a solid chat
  background. Every color in the current application palette is defined.
- The 2026-11-18 refinement removes olive casts from panels, softens service
  previews/date labels and separates the chat canvas (#111316), incoming
  messages (#1b1f23) and outgoing messages (#24282c). Amber remains an accent.
  Design migration v2 refreshes profiles still using Terminal; a different
  manually selected theme and existing bubble/avatar preferences are preserved.
- Monospace timestamps and chat search; ordinary message text retains the
  application's proportional font and existing accessibility/font settings.
- More space in dialog rows and messages, wider maximum text bubbles and a
  rectangular search field. These compiled style changes are shared by themes
  in this fork; they are not dynamically rolled back when switching palettes.
- Thin row separators and an outlined compose field while Terminal is active,
  including theme previews. Existing message direction and sender grouping are
  retained; the linear transcript from the early mockup is not implemented here.
- An explicit appearance preset uses existing settings. Theme switching alone
  does not reset avatar/message settings; adjust them separately if desired.

The fresh-profile auto-lock default is 60 seconds; saved auto-lock preferences
remain unchanged. Locking hides the UI; it does not unload keys or erase media
already held in process memory. The password and encrypted archive changes have
separate validation and do not make every existing Telegram file password
protected.

This is the first appearance implementation, not a completed redesign of every
settings screen, message type, popup or platform title bar.

## Assets and validation

`terminal.tdesktop-palette` is the editable source. Regenerate the distributable:

```powershell
py -3 tools/themes/package_terminal_theme.py
py -3 -m unittest discover -s tools/tests -p test_terminal_theme.py -v
git diff --check
```

The packager uses only Python's standard library and writes a deterministic ZIP
with the current palette and a generated solid 32×32 PNG. JetBrains Mono Regular
is bundled locally with its SIL Open Font License in `Telegram/Resources/fonts`.
Font source: https://github.com/JetBrains/JetBrainsMono/tree/master/fonts/ttf

Asset checks cover palette completeness, contrast of primary text/control
states, archive consistency and resource paths. They do not compile C++ or run
the Qt application. The user's explicit build request overrides the repository's
default instruction to avoid builds. The first Debug client compiled on
2026-11-18; the follow-up startup fix has a native control-flow harness for
locked profiles, day/night migration, preserving manual choices, failed theme
application and write ordering. It substitutes Qt/storage boundaries and does
not verify the rendered client. Check 100/125/150/200%
UI scale, RTL, forum/tagged/narrow rows, drafts and replies, media captions,
keyboard focus, theme preview/revert and day/night switching in that build.
