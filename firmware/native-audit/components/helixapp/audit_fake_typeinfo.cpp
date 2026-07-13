// SPDX-License-Identifier: GPL-3.0-or-later
//
// Audit-grade fake typeinfo objects, isolated in a TU with NO app includes:
// GCC rejects an extern "C" variable named _ZTI<class> in any TU where the
// real class declaration is visible (conflicting implicit declaration).
//
// Rationale (see audit_stubs.cpp history): PanelWidgetManager's
// register_shared_resource<T>() keys a type_index map with typeid(T), which
// needs the typeinfo SYMBOL. Emitting the real one would drag each class's
// full vtable — 100+ virtuals on classes the slice never instantiates (the
// API/client pointers stay nullptr; dynamic_cast on a null pointer never
// consults typeinfo). The struct mimics the {vptr, name} ABI layout that
// libstdc++'s non-virtual type_info ops (hash_code/operator==/name) read.

namespace {
struct AuditFakeTypeinfo {
    const void* vptr;
    const char* name;
};
} // namespace

extern "C" AuditFakeTypeinfo _ZTI12MoonrakerAPI = {nullptr, "12MoonrakerAPI"};
extern "C" AuditFakeTypeinfo _ZTI19MoonrakerClientMock = {nullptr, "19MoonrakerClientMock"};
extern "C" AuditFakeTypeinfo _ZTIN5helix15MoonrakerClientE = {nullptr, "N5helix15MoonrakerClientE"};
