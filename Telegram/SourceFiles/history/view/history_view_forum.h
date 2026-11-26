#pragma once

namespace HistoryView {

class Element;

[[nodiscard]] bool ForumRecord(const Element *view);
[[nodiscard]] int ForumPhotoLeft(const Element *view);
[[nodiscard]] int ForumPhotoSize(const Element *view);
[[nodiscard]] bool ForumInfoInHeader(const Element *view);

} // namespace HistoryView
