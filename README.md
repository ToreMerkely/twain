# twain

> *"…and never the twain shall meet."* — Rudyard Kipling

A simple diff tool. Side-by-side file diff with
intra-line highlighting, side-by-side folder tree compare with synced
expand/scroll/selection, and click-arrow-to-merge editing.

## Build

Requires Qt 6 (Widgets) and CMake.

```sh
make build
```

The binary lands at `build/twain`.

## Usage

```sh
twain                       # empty workspace; use File > Open to load
twain left right            # auto-detect: two files or two directories
twain -h                    # CLI help
twain --version
```

### Folder pair vs file pair

If both arguments are directories, twain opens the **folder-compare**
view (synced two-pane tree, blue for orphans, red for different).
Otherwise it opens the **file-diff** view (side-by-side text with
red change highlights).

### Tabs and shortcuts

Each opened pair is a tab. Drilling into a differing file from a folder
tab opens it in a new tab.

| Shortcut          | Action                                         |
|-------------------|------------------------------------------------|
| Ctrl+O            | Open file pair                                 |
| Ctrl+Shift+O      | Open folder pair                               |
| Ctrl+W            | Close current tab                              |
| Ctrl+S            | Save edits in the current diff tab             |
| F5                | Refresh                                        |
| F7 / Ctrl+N       | Next difference (or next differing file in a tree) |
| Shift+F7 / Ctrl+P | Previous difference                            |
| Ctrl+M            | Jump to the next differing file                |

In a diff tab, click a yellow arrow in the gutter to copy that hunk to
the other side. Then save (Ctrl+S) to write both files to disk.

## git integration

twain is designed to drop in as a `git difftool`. Print the config
commands and pipe them through `sh`:

```sh
build/twain --git-config | sh
```

That sets:

```sh
git config --global diff.tool twain
git config --global difftool.twain.cmd '/abs/path/to/twain "$LOCAL" "$REMOTE"'
git config --global difftool.prompt false
```

Drop `--global` to set per-repo only.

Then use it:

```sh
git difftool -d              # tree view: all changed files at once
git difftool -d HEAD~1       # tree view vs a revision
git difftool -d HEAD~1 HEAD  # what a commit changed (like 'git show')
git difftool                 # one file at a time (default)
```

The `-d` / `--dir-diff` flag is what gives you the side-by-side folder
tree. Without it, git invokes the difftool once per changed file.

A handy alias for the tree view:

```sh
git config --global alias.dd 'difftool --dir-diff'
```

The same info is available in-app under **Help → Configure as git
difftool…**.

## Editing

Direct typing works in either pane (Phase B): edit, Ctrl+S to save.
Undo/redo (Ctrl+Z / Ctrl+Y) works during a typing session and clears
on the next diff rebuild (after save or refresh).

Yellow merge arrows in the gutter copy a whole hunk from one side to
the other; the destination rows are marked with a yellow vertical bar
so you can see what was just changed.

## View options

Under the **View** menu:

- **Ignore Case** — fold case for diff matching
- **Ignore Whitespace** — collapse whitespace runs and strip leading/trailing
- **Ignore Blank Lines** — drop blank lines before diffing

These are persisted across sessions and apply to all open diff tabs.
