# Update Dialog Notes Style

Keep every Leapcast update easy to scan.

## Required format

- Start with one short summary sentence.
- Use 3–7 concise bullets for the important changes.
- Group longer releases under short headings such as `✨ New`, `🛠️ Fixed`, and `🎨 Improved`.
- Leave a blank line between headings or groups.
- Use one relevant emoji per heading or bullet; do not clutter every phrase.
- Put the user-visible result first. Avoid implementation details unless users must act on them.
- Keep each bullet to one or two short sentences.
- Clearly call out required actions such as reconnecting keys.
- Never paste a dense paragraph or an unedited commit list.

## Example

```markdown
Faster setup and cleaner chat controls.

### ✨ New

- Added Rumble follow and subscriber alerts.
- Added centered Kick and Rumble chat tabs.

### 🛠️ Fixed

- Blocked messages no longer appear in chat logs.

### 🔑 Action needed

- Reconnect any keys listed under **Keys** after installing.
```

The updater displays the first non-empty line as its short summary and keeps the complete Markdown notes in a spaced, scrollable panel.
