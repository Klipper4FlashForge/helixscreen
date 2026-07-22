# LVGL XML Licensing and Future Situation

**Date:** 2026-02-05 | **Updated:** 2026-02-18
**Status:** RESOLVED — XML extracted into `lib/helix-xml/`, LVGL upgraded to v9.5.0

## Summary

LVGL removed XML support from the core library on January 27, 2026. The XML functionality is now part of **LVGL Pro**, a paid subscription product. HelixScreen relies heavily on XML for its entire UI system.

**Resolution (2026-02-18):** We extracted the MIT-licensed XML engine into `lib/helix-xml/` as a standalone library and upgraded LVGL to v9.5.0. The extracted engine lives in `lib/helix-xml/`; its parsers are under `lib/helix-xml/src/xml/parsers/`.

## Timeline

| Date | Event |
|------|-------|
| 2024-11-22 | XML first added to LVGL (`fc5939dcf`) |
| 2024-02 | LVGL ends collaboration with SquareLine Studio |
| 2025-11-13 | Our current pinned commit (`c849861b2`) |
| 2026-01-26 | **Last safe commit with XML** (`a15dcbeb5`) |
| 2026-01-27 | XML removed from LVGL (`7c1e0684f`, PR #9565) |

## Why They Removed XML

From PR #9565:

1. **Editor coupling** - They want to version XML with the editor, not LVGL core
2. **Licensing** - XML engine had a "special licence making LVGL not pure MIT"
3. **Business** - They want XML under a "custom licence" (read: paid)

## Our Licensing Status

**The XML code in our current submodule IS MIT licensed.**

- LVGL's main `LICENCE.txt` is MIT
- No separate license file ever existed for the XML module
- What was released under MIT stays MIT - we can use it forever
- The expat XML parser bundled in `src/libs/expat/` is also MIT

## Safe Upgrade Window

We can advance **274 commits** from our current position before hitting the removal:

```
Current:    c849861b2  (2025-11-13, v9.3.0+640)
Last safe:  a15dcbeb5  (2026-01-26)
Removal:    7c1e0684f  (2026-01-27)
```

## Options Going Forward

### Option 1: Stay Pinned (Current Approach)
- Keep current commit or advance up to `a15dcbeb5`
- Miss out on future LVGL improvements
- XML stays MIT, no licensing concerns

### Option 2: Fork and Maintain XML
- Fork LVGL at `a15dcbeb5`
- Cherry-pick future commits that don't touch XML
- Maintain XML ourselves
- Significant maintenance burden

### Option 3: Pay for LVGL Pro
- Subscription-based pricing
- Get official XML support and editor
- Dependency on their business model
- See: https://pro.lvgl.io/

### Option 4: Migrate Away from XML
- Rewrite UI in pure C++ with LVGL API
- Massive undertaking given our XML-heavy architecture
- Loses the declarative benefits

## LVGL Pro Details

LVGL Pro is their new commercial offering:
- XML Editor (desktop app)
- Live Preview & Inspector
- Figma Integration
- CLI for CI/CD
- Subscription pricing (monthly/yearly)
- See: https://pro.lvgl.io/

## Background: LVGL + SquareLine Split

In February 2024, LVGL ended its collaboration with SquareLine Studio due to:
- Software stability concerns with SquareLine
- Lack of decision-making rights despite heavy promotion

LVGL then developed their own tooling (LVGL Pro) and is now monetizing it.

## References

- [LVGL ends collaboration with SquareLine](https://forum.lvgl.io/t/lvgl-ends-its-collaboration-with-squareline-studio/14638)
- [PR #9565: Remove XML](https://github.com/lvgl/lvgl/pull/9565)
- [LVGL Pro](https://pro.lvgl.io/)
- [LVGL License](https://github.com/lvgl/lvgl/blob/master/LICENCE.txt)

## Patches We Maintain

After the v9.5 upgrade, remaining LVGL patches in `patches/`:
- `lvgl_sdl_window.patch` - Window title, positioning, quit handling (regenerated for v9.5)
- `lvgl_theme_breakpoints.patch` - Theme breakpoints
- `lvgl_fbdev_stride_bpp.patch` - Framebuffer fixes
- `lvgl_observer_debug.patch` - Observer debugging
- `lvgl_slider_scroll_chain.patch` - Slider scroll fix
- `lvgl_strdup_null_guard.patch` - Null guard for strdup

XML patches were baked permanently into `lib/helix-xml/` and removed from `patches/`:
- ~~`lvgl_xml_const_silent.patch`~~ - XML const lookup (baked in)
- ~~`lvgl_image_parser_contain.patch`~~ - Image contain/cover (baked in)
- ~~`lvgl_translate_percent.patch`~~ - Translate percentages (baked in)

## What We're Missing (274 commits)

These are commits between our current position (`c849861b2`, 2025-11-13) and the last safe commit (`a15dcbeb5`, 2026-01-26).

### High Priority - Directly Relevant to HelixScreen

#### XML Improvements

The XML system got significant enhancements before removal. **Slot support** (`f38718108`) enables true component composition - you can define placeholder areas in components that parent XML can fill with content, similar to React children or Vue slots. This would let us build more reusable panel templates. **Imagebutton support** (`cfa983a5f`) adds the missing XML parser for image buttons with separate pressed/released states. **Local style selectors** (`c86b90772`) let you apply styles conditionally within XML without needing C++ code. **Animation improvements** include color percentage support (`5472a77c6`) and bg_image_opa animation (`9c18ca1ed`), expanding what we can animate declaratively.

- `f38718108` **feat(xml): add slot support** (#9193)
- `cfa983a5f` **feat(xml): add imagebutton support to XML** (#9381)
- `89790533d` **feat(test): add lv_xml_test_run_to** (#9189)
- `c86b90772` **feat(xml): support selector with local styles** (#9184)
- `5472a77c6` **feat(xml): add color percentage support to animations** (#9209)
- `9c18ca1ed` **fix(xml): allow animating bg_image_opa in XML timelines** (#9425)
- `7db7f149c` **fix(xml): pass raw # if there's no constant name** (#9418)
- `a79c798cb` **fix(xml): fix compile warning of uninitialized value** (#9368)

#### Display/Rendering (We use fbdev + SDL)

Display backend improvements that matter for our targets. The **DRM stride fix** (`d67aaab16`) corrects buffer stride calculation which could cause visual corruption on some displays. **SDL EGL support** (`aac49f630`) adds hardware-accelerated rendering option for desktop development. The **SDL null pointer fix** (`44e1a35d5`) prevents crashes when receiving invalid window IDs. Most exciting are the new visual effects: **blur support** (`e54d66863`) enables gaussian blur on any widget - great for modal overlays and depth effects. **Drop shadow support** (`bb3233b79`) adds proper shadow rendering without needing image hacks.

- `d67aaab16` **fix(drm): set stride for draw buffers** (#9609)
- `aac49f630` **feat(sdl): add EGL support** (#9462)
- `44e1a35d5` **fix(sdl): do not dereference nullptr for invalid window id** (#9299)
- `e54d66863` **feat(blur): add blur support** (#9223)
- `bb3233b79` **feat(dropshadow): add drop shadow support** (#9331)

#### Widgets We Use

Critical fixes for widgets HelixScreen uses heavily. **Slider min/max setter** (`935dfd17d`) adds proper API for setting slider range dynamically - we currently work around this. **Tabview sizing fix** (`4b657b16d`) corrects tab bar size recalculation when changing position, which we do for different screen orientations. **Textarea scroll fix** (`e23b7bebf`) ensures the cursor stays visible when styles change. **Dropdown symbol fix** (`f1cf3f65e`) corrects how dropdown arrows are handled. **Spinbox stack corruption** (`eb023d304`) is a serious bug that could cause crashes. **Roller click fix** (`2e3313622`) corrects touch position calculation on transformed rollers.

- `935dfd17d` **feat(slider): add min_value and max_value setter** (#9433)
- `4b657b16d` **fix(tabview): re-set tab bar size after changing position** (#9399)
- `e23b7bebf` **fix(textarea): scroll to position when style changes** (#9407)
- `f1cf3f65e` **fix(dropdown): symbol property behaves like IMGSRC** (#9608)
- `eb023d304` **fix(spinbox): fix stack corruption** (#9256)
- `2e3313622` **fix(roller): transformed roller click location** (#9212)

#### Layout/Positioning

Layout engine fixes that affect our responsive designs. The **flex_grow rounding fix** (`947084fc9`) addresses a long-standing issue where flex containers sometimes left 1-2px gaps due to integer rounding - this has bitten us. **SIZE_CONTENT with flex_grow** (`93cc9db56`) finally allows combining content-sized containers with flex grow, which was a painful limitation. **LV_PCT of SIZE_CONTENT** (`2e8b9d03b`) lets children use percentage sizing relative to content-sized parents. RTL fixes (`9f3918f66`, `20a1ce71a`) matter for future localization.

- `947084fc9` **fix(flex): fix rounding may leave unused space with flex_grow** (#9217)
- `93cc9db56` **feat(obj): support LV_SIZE_CONTENT and LV_PCT min width/height with flex grow** (#8685)
- `2e8b9d03b` **feat(obj): allow LV_PCT() of LV_SIZE_CONTENT parent** (#9243)
- `9f3918f66` **fix(grid): negative width with column span in RTL** (#9596)
- `20a1ce71a` **fix(obj_pos): alignment not reversed switching LTR to RTL** (#9616)

#### Input/Touch

Touch and input handling improvements. **Gesture threshold API** (`a15dcbeb5`) lets us tune swipe sensitivity per-device - critical for different touchscreen hardware quality. The **timer threshold fix** (`b06a460b5`) ensures input events fire at the right time, preventing missed touches. **KEY event fix** (`28b718256`) correctly routes keyboard events through the input device system.

- `a15dcbeb5` **feat(indev): add api to set gesture thresholds** (#9641)
- `b06a460b5` **fix(indev): ensure timer triggers when elapsed time meets threshold** (#9275)
- `28b718256` **fix(indev): send LV_EVENT_KEY as indev event** (#9208)

#### Theme System

The **theme API expansion** (`106d17377`) adds functions to create, copy, and delete themes at runtime. This enables true runtime theme switching without restart - useful for light/dark mode toggle or user-customizable themes.

- `106d17377` **feat(theme): add api to create, copy and delete themes** (#9167)

#### Memory/Performance

Memory leak and crash fixes. **lodepng leak** (`964b0220a`) fixes memory not being freed when image cache insertion fails. **Image cache leak** (`e7b561ef0`) fixes leaks when dumping cache contents. **Screen double-free** (`c87ae4411`) prevents crashes when loading screens rapidly - we've seen mysterious crashes that could be this.

- `964b0220a` **fix(lodepng): fix memory leak when cache addition fails** (#9569)
- `e7b561ef0` **fix(image_cache): fix image cache dump memory leaks** (#9448)
- `c87ae4411` **fix(screen): double free when loading screens** (#9276)

### Medium Priority - Nice to Have

#### New Features
- `721029ac2` **feat(obj): add LV_OBJ_FLAG_RADIO_BUTTON** (#9328) - easy radio buttons
- `3f5376b75` **feat(group): add user_data getter and setter** (#9466)
- `03c43fea7` **feat(obj): add scroll x and scroll y properties** (#9346)
- `dd42852d8` **feat(core): add external data and destructor feature** (#9112)
- `92bf15ac5` **feat(file_explorer): allow hiding the back button** (#9202)
- `e47ce852c` **feat(fs): support multiple littlefs filesystems** (#8868)
- `d0ce7258a` **feat(arclabel): support overflow mode** (#9484)

#### Animation/Drawing
- `8bc37d840` **fix(anim): ignore large apparent animation duration** (#9280)
- `d99d2a553` **feat(line): allow defining array of points in draw task** (#9269)
- `127a3fd22` **feat(scale): update needles when scale is transformed** (#9340)
- `eb86444ec` **fix(draw_sw_line): fix horizontal line incorrect dash length** (#9501)

#### Image/Media
- `e5f9926a2` **feat(jpeg): add support for orientation and CMYK** (#9296)
- `4bf24d994` **fix(gif): fix negative loop count handling** (#9241)
- `a745fc478` **fix(gif): add disposal handle to fix display error** (#9213)
- `130e8a43d` **feat(gstreamer): add support for sources with no audio** (#9551)

#### Property System (new widget API)
- `7801fd22e` **feat(chart): add property interface support**
- `8bf69520b` **feat(menu): add property interface support**
- `8a2b610f0` **feat(span): add property interface support**
- `847cc0c8d` **feat(buttonmatrix): add property interface support**
- `492770e77` **feat(tabview): add property interface support**
- ... and many more widgets

### Low Priority - Not Relevant to Us

- Wayland rewrites (we don't use Wayland)
- VG-Lite GPU stuff (we use software rendering)
- OpenGL/NanoVG backends
- glTF 3D support
- NuttX driver changes
- ESP/Arduino specific fixes
- RISC-V vector extensions

### Deprecations to Note

- `1eebf3679` **feat(fragment): deprecate lv_fragment** (#9460) - if we use fragments

### Calendar Update

- `6da0773e2` **feat(calendar): add 2026 to header** (#9630) - lol

## Resolution

**Chosen approach: Option 5 — Extract XML + Upgrade LVGL** (2026-02-18)

We extracted the MIT-licensed XML engine into `lib/helix-xml/` and upgraded LVGL to v9.5.0. This gives us:
- Full ownership of the XML engine with MIT license
- All 274 commits of LVGL improvements (blur, drop shadow, flex fixes, memory leak fixes, etc.)
- No dependency on LVGL Pro or its subscription model
- Collaboration path with lui-xml (Kevin/coevin) for future improvements

**Branch:** `feature/helix-xml` (merged) | **Engine:** `lib/helix-xml/`

### Previous Recommendation (obsolete)

~~**Short term:** Upgrade to `a15dcbeb5` to get 2.5 months of bug fixes and features while keeping XML.~~

~~**Long term:** Monitor LVGL Pro pricing and evaluate whether the subscription cost is acceptable, we want to fork and maintain XML ourselves, or a gradual migration to pure C++ makes sense.~~
