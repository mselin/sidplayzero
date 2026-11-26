// SPDX-License-Identifier: GPL-3.0-or-later
// STUB: not built yet (Phase 5).
#include "spz/browser_model.h"

#include "spz/not_built.h"

namespace spz {

BrowserModel::BrowserModel(Collection*, Executor*, Executor*, Clock*) {}
void BrowserModel::set_listener(BrowserListener*) { SPZ_NOT_BUILT("SPEC-043"); }
void BrowserModel::show(const std::string&, const std::string&) { SPZ_NOT_BUILT("SPEC-043"); }
BrowserModel::Opened BrowserModel::open() { SPZ_NOT_BUILT("SPEC-044"); }
bool BrowserModel::parent() { SPZ_NOT_BUILT("SPEC-044"); }
std::string BrowserModel::folder() const { SPZ_NOT_BUILT("SPEC-044"); }
ListNote BrowserModel::note() const { SPZ_NOT_BUILT("SPEC-043"); }
int BrowserModel::count() const { SPZ_NOT_BUILT("SPEC-043"); }
const FolderEntry& BrowserModel::row(int) const { SPZ_NOT_BUILT("SPEC-043"); }
int BrowserModel::highlight() const { SPZ_NOT_BUILT("SPEC-044"); }
std::string BrowserModel::position() const { SPZ_NOT_BUILT("SPEC-045"); }
std::vector<FolderEntry> BrowserModel::tunes() const { SPZ_NOT_BUILT("SPEC-062"); }
void BrowserModel::set_rows_on_screen(int) { SPZ_NOT_BUILT("SPEC-045"); }
void BrowserModel::up() { SPZ_NOT_BUILT("SPEC-045"); }
void BrowserModel::down() { SPZ_NOT_BUILT("SPEC-045"); }
void BrowserModel::page_up() { SPZ_NOT_BUILT("SPEC-045"); }
void BrowserModel::page_down() { SPZ_NOT_BUILT("SPEC-045"); }
void BrowserModel::first() { SPZ_NOT_BUILT("SPEC-045"); }
void BrowserModel::last() { SPZ_NOT_BUILT("SPEC-045"); }
void BrowserModel::tune_changed(const std::string&) { SPZ_NOT_BUILT("SPEC-043"); }

}  // namespace spz
