# Pre-flight Checklists

Use these checklists before starting common tasks to avoid frequent mistakes.

---

## ✅ Before Modifying XML

- [ ] **Read docs if unfamiliar:** Load `docs/LVGL9_XML_GUIDE.md` for syntax reference
- [ ] **No `flag_` prefix:** Use `hidden="true"` NOT `flag_hidden="true"`
- [ ] **Flex uses 3 properties:** `style_flex_main_place`, `style_flex_cross_place`, `style_flex_track_place` (NOT `flex_align`)
- [ ] **Conditional bindings = child elements:** Use `<lv_obj-bind_flag_if_eq>` NOT attributes
- [ ] **Component instantiations need names:** Always add `name="..."` explicitly
- [ ] **Using widget-maker agent?** XML work REQUIRES delegating to widget-maker

**Reference:** LVGL9_XML_GUIDE.md, CLAUDE.md "Common Gotchas"

---

## 🎨 Before Adding Colors/Dimensions

- [ ] **Defined in globals.xml:** All colors/dimensions must be XML constants
- [ ] **Light and dark variants:** Use `*_light` and `*_dark` suffix pattern
- [ ] **Read from C++:** Use `lv_xml_get_const()` + `ui_theme_parse_color()`
- [ ] **NO hardcoded values:** Never use `lv_color_hex(0x...)` in C++
- [ ] **Theme pattern reference:** See `ui_card.cpp:39-59` for complete example

**Why:** Theme changes without recompilation, consistent styling, dark/light mode support

**Reference:** CLAUDE.md "Critical Rules #1"

---

## 📝 Before Creating New File

- [ ] **GPL v3 copyright header:** Required in all `.c`, `.cpp`, `.h`, `.xml` files
- [ ] **Use correct template:** See `docs/COPYRIGHT_HEADERS.md` for variants
- [ ] **Follow naming conventions:** Study similar existing files first

**Reference:** COPYRIGHT_HEADERS.md

---

## 🔍 Before Committing

### Code Quality
- [ ] **Build succeeds:** Run `make` (or `make -j` for parallel build)
- [ ] **No printf/cout:** Only use `spdlog::info()`, `spdlog::debug()`, etc.
- [ ] **No private LVGL APIs:** Never use `_lv_*()`, `obj->coords`, `lv_obj_mark_dirty()`
- [ ] **Reference existing patterns:** Study similar code before implementing

### Documentation
- [ ] **Update affected docs:** If behavior or architecture changed, update the relevant `docs/devel/` doc

### Git
- [ ] **Meaningful commit message:** Use `.gitmessage` template format
- [ ] **Commit related changes together:** Don't mix unrelated fixes

**Reference:** DEVELOPMENT.md#contributing, CLAUDE.md "Critical Rules"

---

## 🧪 Before Running Tests

- [ ] **Test mode flag:** Use `--test` flag for mock backends
- [ ] **Real hardware testing:** Remove `--test` flag to test with actual printer/WiFi
- [ ] **Screenshot workflow:** Press 'S' in UI or use `scripts/screenshot.sh`
- [ ] **Check logs:** Use `-v` (info), `-vv` (debug), `-vvv` (trace) for verbosity

**Reference:** DEVELOPMENT.md "Screenshot Workflow", docs/UI_TESTING.md

---

## 🎯 Before Starting New Feature

- [ ] **Check ROADMAP.md:** Is this feature already planned?
- [ ] **Study existing patterns:** Find similar feature, review implementation
- [ ] **Use appropriate agent/skill:** Widget-maker (UI), moonraker skill (backend), etc.
- [ ] **Reference architecture:** Read ARCHITECTURE.md for system design

**Reference:** ARCHITECTURE.md, ROADMAP.md

---

## 💡 Usage Tips

**In chat:**
- "Show me the XML checklist"
- "What should I check before committing?"
- "Pre-flight checklist for colors"

**Quick reference:** Use `.claude/quickref/` cards for fast syntax lookups

**When in doubt:** Read the relevant documentation BEFORE coding (see CLAUDE.md "Lazy Documentation Loading")
